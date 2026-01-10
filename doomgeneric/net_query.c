//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Server query and lobby browser functions
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "i_timer.h"
#include "net_defs.h"
#include "net_io.h"
#include "net_lobby.h"
#include "net_packet.h"
#include "net_query.h"
#include "net_socket.h"

// Query timeout in milliseconds
#define QUERY_TIMEOUT_MS 2000
#define QUERY_MAX_ATTEMPTS 3
#define QUERY_RETRY_INTERVAL_MS 500

// Maximum servers we can track
#define MAX_SERVERS 16

// Server states
typedef enum
{
    SERVER_STATE_WAITING,    // Lobby, waiting for players
    SERVER_STATE_IN_GAME,    // Game in progress
} server_state_t;

// Query target state
typedef enum
{
    QUERY_STATE_IDLE,
    QUERY_STATE_QUERYING,
    QUERY_STATE_RESPONDED,
    QUERY_STATE_TIMEOUT,
} query_state_t;

// Server entry for browser
typedef struct
{
    char address[64];           // "ip:port" string
    int port;
    query_state_t query_state;
    unsigned int query_time;    // When query was sent (ms)
    unsigned int ping_time;     // RTT in ms
    int query_attempts;
    
    // Query response data
    boolean has_response;
    char version[32];
    int server_state;           // 0=waiting, 1=in_game
    int num_players;
    int max_players;
    int gamemode;
    int gamemission;
    char description[64];
    
    // Cached net_addr for sending queries
    net_addr_t *addr;
} server_entry_t;

// Server list
static server_entry_t servers[MAX_SERVERS];
static int num_servers = 0;

// Query context
static net_context_t *query_context = NULL;
static boolean query_initialized = false;

// Default server configuration is in net_query.h
#define MAX_PORT_SCAN 16  // Maximum ports to scan from base

// Discovery timeout (fast scanning - servers should respond quickly)
#define DISCOVERY_TIMEOUT_MS 500
#define DISCOVERY_MAX_ATTEMPTS 1

// Forward declarations for internal functions
static void NET_Query_SendQueryPacket(server_entry_t *server);
static void NET_Query_ProcessResponses(void);
void NET_Query_SortByPort(void);

// Initialize the query system
boolean NET_Query_Init(void)
{
    if (query_initialized)
        return true;

    // Create network context
    query_context = NET_NewContext();
    if (query_context == NULL)
    {
        fprintf(stderr, "NET_Query_Init: Failed to create context\n");
        return false;
    }

    // Initialize socket module
    if (!net_socket_module.InitClient())
    {
        fprintf(stderr, "NET_Query_Init: Failed to init socket\n");
        return false;
    }

    NET_AddModule(query_context, &net_socket_module);
    
    // Initialize empty server list - will be populated by discovery
    num_servers = 0;
    memset(servers, 0, sizeof(servers));
    
    // Small delay to let socket settle before first query
    I_Sleep(100);
    
    query_initialized = true;
    printf("NET_Query_Init: Initialized (discovery pending)\n");
    return true;
}

// Discover servers by scanning sequential ports
// Starts at base_port and scans until it finds a non-responding port
void NET_Query_DiscoverServers(const char *server_ip, int base_port)
{
    char addr_buf[64];
    int port;
    int consecutive_failures = 0;
    int discovered = 0;
    int ports_scanned = 0;
    
    printf("NET_Query_DiscoverServers: Scanning %s starting at port %d...\n", 
           server_ip, base_port);
    
    // Show initial loading screen
    DG_DrawLoadingAutoMatch(0, 0);
    
    // Clear existing server list
    NET_Query_ClearServers();
    
    // Scan ports sequentially
    for (port = base_port; port < base_port + MAX_PORT_SCAN && num_servers < MAX_SERVERS; port++)
    {
        ports_scanned++;
        
        // Update loading screen with progress
        DG_DrawLoadingAutoMatch(discovered, ports_scanned);
        
        snprintf(addr_buf, sizeof(addr_buf), "%s:%d", server_ip, port);
        
        // Add server for querying
        if (!NET_Query_AddServer(addr_buf))
            break;
        
        server_entry_t *server = &servers[num_servers - 1];
        
        // Send query with shorter timeout for discovery
        int attempts;
        boolean got_response = false;
        
        for (attempts = 0; attempts < DISCOVERY_MAX_ATTEMPTS && !got_response; attempts++)
        {
            NET_Query_SendQueryPacket(server);
            
            // Wait for response with timeout
            unsigned int start = I_GetTimeMS();
            while (I_GetTimeMS() - start < DISCOVERY_TIMEOUT_MS)
            {
                NET_Query_ProcessResponses();
                
                if (server->has_response)
                {
                    got_response = true;
                    // Update screen immediately when we find one
                    DG_DrawLoadingAutoMatch(discovered + 1, ports_scanned);
                    break;
                }
                I_Sleep(10);
            }
        }
        
        if (got_response)
        {
            printf("  Port %d: %s (%d/%d players)\n", 
                   port, server->description, server->num_players, server->max_players);
            consecutive_failures = 0;
            discovered++;
        }
        else
        {
            // No response - remove this server from list
            printf("  Port %d: No response\n", port);
            if (server->addr != NULL)
            {
                NET_FreeAddress(server->addr);
                server->addr = NULL;
            }
            num_servers--;
            
            consecutive_failures++;
            
            // Stop after 1 failure (first non-responding port = end of server list)
            printf("NET_Query_DiscoverServers: Stopping scan (no response)\n");
            break;
        }
    }
    
    printf("NET_Query_DiscoverServers: Found %d active servers\n", discovered);
    
    // Sort servers by port number for consistent ordering
    NET_Query_SortByPort();
}

// Discover servers using default IP and base port
void NET_Query_DiscoverDefaultServers(void)
{
    NET_Query_DiscoverServers(DEFAULT_SERVER_IP, DEFAULT_BASE_PORT);
}

// Shutdown query system
void NET_Query_Shutdown(void)
{
    for (int i = 0; i < num_servers; i++)
    {
        if (servers[i].addr != NULL)
        {
            NET_FreeAddress(servers[i].addr);
            servers[i].addr = NULL;
        }
    }
    num_servers = 0;
    query_initialized = false;
}

// Add a server to the list
boolean NET_Query_AddServer(const char *address)
{
    if (num_servers >= MAX_SERVERS)
        return false;
    
    server_entry_t *server = &servers[num_servers];
    memset(server, 0, sizeof(server_entry_t));
    
    strncpy(server->address, address, sizeof(server->address) - 1);
    server->query_state = QUERY_STATE_IDLE;
    server->has_response = false;
    server->addr = NULL;
    
    // Parse port from address string (format: "ip:port")
    const char *colon = strrchr(address, ':');
    if (colon != NULL)
    {
        server->port = atoi(colon + 1);
    }
    
    num_servers++;
    return true;
}

// Compare function for sorting servers by port
static int compare_servers_by_port(const void *a, const void *b)
{
    const server_entry_t *sa = (const server_entry_t *)a;
    const server_entry_t *sb = (const server_entry_t *)b;
    return sa->port - sb->port;
}

// Sort servers by port number
void NET_Query_SortByPort(void)
{
    if (num_servers > 1)
    {
        qsort(servers, num_servers, sizeof(server_entry_t), compare_servers_by_port);
        printf("NET_Query_SortByPort: Sorted %d servers by port\n", num_servers);
    }
}

// Clear server list
void NET_Query_ClearServers(void)
{
    for (int i = 0; i < num_servers; i++)
    {
        if (servers[i].addr != NULL)
        {
            NET_FreeAddress(servers[i].addr);
            servers[i].addr = NULL;
        }
    }
    num_servers = 0;
}

// Send a query packet to a server
static void NET_Query_SendQueryPacket(server_entry_t *server)
{
    net_packet_t *packet;
    
    // Resolve address if needed
    if (server->addr == NULL)
    {
        server->addr = net_socket_module.ResolveAddress(server->address);
        if (server->addr == NULL)
        {
            fprintf(stderr, "NET_Query_SendQueryPacket: Failed to resolve %s\n", server->address);
            server->query_state = QUERY_STATE_TIMEOUT;
            return;
        }
    }
    
    packet = NET_NewPacket(16);
    if (packet == NULL)
        return;
    
    // Write query packet type
    NET_WriteInt16(packet, NET_PACKET_TYPE_QUERY);
    
    // Send it
    NET_SendPacket(server->addr, packet);
    NET_FreePacket(packet);
    
    server->query_time = I_GetTimeMS();
    server->query_state = QUERY_STATE_QUERYING;
    server->query_attempts++;
}

// Parse a query response packet
static boolean NET_Query_ParseResponse(net_packet_t *packet, server_entry_t *server)
{
    unsigned int packet_type;
    char *str;
    
    // Read packet type
    if (!NET_ReadInt16(packet, &packet_type))
        return false;
    
    if (packet_type != NET_PACKET_TYPE_QUERY_RESPONSE)
        return false;
    
    // Read version string
    str = NET_ReadString(packet);
    if (str == NULL)
        return false;
    strncpy(server->version, str, sizeof(server->version) - 1);
    
    // Read server state
    if (!NET_ReadInt8(packet, (unsigned int *)&server->server_state))
        return false;
    
    // Read num_players
    if (!NET_ReadInt8(packet, (unsigned int *)&server->num_players))
        return false;
    
    // Read max_players
    if (!NET_ReadInt8(packet, (unsigned int *)&server->max_players))
        return false;
    
    // Read gamemode
    if (!NET_ReadInt8(packet, (unsigned int *)&server->gamemode))
        return false;
    
    // Read gamemission
    if (!NET_ReadInt8(packet, (unsigned int *)&server->gamemission))
        return false;
    
    // Read description
    str = NET_ReadString(packet);
    if (str == NULL)
        return false;
    strncpy(server->description, str, sizeof(server->description) - 1);
    
    // Protocol list is optional, ignore if not present
    
    return true;
}

// Process incoming query responses
static void NET_Query_ProcessResponses(void)
{
    net_addr_t *addr;
    net_packet_t *packet;
    char addr_str[128];
    
    while (NET_RecvPacket(query_context, &addr, &packet))
    {
        // Get address string for matching
        addr->module->AddrToString(addr, addr_str, sizeof(addr_str));
        
        // Find matching server BY ADDRESS
        for (int i = 0; i < num_servers; i++)
        {
            // Match by address string (must match the server we queried)
            if (strcmp(servers[i].address, addr_str) == 0 &&
                servers[i].query_state == QUERY_STATE_QUERYING)
            {
                // Try to parse response
                if (NET_Query_ParseResponse(packet, &servers[i]))
                {
                    servers[i].has_response = true;
                    servers[i].query_state = QUERY_STATE_RESPONDED;
                    servers[i].ping_time = I_GetTimeMS() - servers[i].query_time;
                    printf("Query response from %s: %s (%d/%d players, ping %dms)\n",
                           servers[i].address,
                           servers[i].description,
                           servers[i].num_players,
                           servers[i].max_players,
                           servers[i].ping_time);
                    break;
                }
            }
        }
        
        NET_FreePacket(packet);
        NET_FreeAddress(addr);
    }
}

// Check for query timeouts and retries
static void NET_Query_CheckTimeouts(void)
{
    unsigned int now = I_GetTimeMS();
    
    for (int i = 0; i < num_servers; i++)
    {
        if (servers[i].query_state == QUERY_STATE_QUERYING)
        {
            if (now - servers[i].query_time > QUERY_TIMEOUT_MS)
            {
                if (servers[i].query_attempts < QUERY_MAX_ATTEMPTS)
                {
                    // Retry
                    printf("Query timeout for %s, retrying (%d/%d)\n",
                           servers[i].address,
                           servers[i].query_attempts + 1,
                           QUERY_MAX_ATTEMPTS);
                    NET_Query_SendQueryPacket(&servers[i]);
                }
                else
                {
                    // Give up
                    servers[i].query_state = QUERY_STATE_TIMEOUT;
                    printf("Query failed for %s (timeout)\n", servers[i].address);
                }
            }
        }
    }
}

// Start querying all servers
void NET_Query_StartAll(void)
{
    if (!query_initialized)
    {
        if (!NET_Query_Init())
            return;
    }
    
    // If no servers in list, run discovery first
    if (num_servers == 0)
    {
        printf("NET_Query_StartAll: No servers configured, running discovery...\n");
        NET_Query_DiscoverDefaultServers();
    }
    
    printf("NET_Query_StartAll: Querying %d servers...\n", num_servers);
    
    for (int i = 0; i < num_servers; i++)
    {
        servers[i].query_state = QUERY_STATE_IDLE;
        servers[i].has_response = false;
        servers[i].query_attempts = 0;
        servers[i].ping_time = 0;
        NET_Query_SendQueryPacket(&servers[i]);
    }
}

// Poll for query results - returns true when all queries complete
boolean NET_Query_Poll(void)
{
    if (!query_initialized)
        return true;
    
    NET_Query_ProcessResponses();
    NET_Query_CheckTimeouts();
    
    // Check if all queries are complete
    for (int i = 0; i < num_servers; i++)
    {
        if (servers[i].query_state == QUERY_STATE_QUERYING)
            return false;  // Still waiting
    }
    
    return true;  // All complete
}

// Query all servers synchronously (blocking)
void NET_Query_RunAll(void)
{
    unsigned int start_time;
    
    NET_Query_StartAll();
    
    start_time = I_GetTimeMS();
    
    // Wait for all responses (with global timeout)
    while (!NET_Query_Poll())
    {
        if (I_GetTimeMS() - start_time > QUERY_TIMEOUT_MS * QUERY_MAX_ATTEMPTS + 500)
        {
            printf("NET_Query_RunAll: Global timeout\n");
            break;
        }
        I_Sleep(10);
    }
    
    printf("NET_Query_RunAll: Complete\n");
}

// Get number of servers
int NET_Query_GetServerCount(void)
{
    return num_servers;
}

// Get server info by index
boolean NET_Query_GetServerInfo(int index, net_querydata_t *data, 
                                 char *address, int address_len,
                                 unsigned int *ping)
{
    if (index < 0 || index >= num_servers)
        return false;
    
    server_entry_t *server = &servers[index];
    
    if (!server->has_response)
        return false;
    
    if (data != NULL)
    {
        data->version = server->version;
        data->server_state = server->server_state;
        data->num_players = server->num_players;
        data->max_players = server->max_players;
        data->gamemode = server->gamemode;
        data->gamemission = server->gamemission;
        data->description = server->description;
    }
    
    if (address != NULL)
    {
        strncpy(address, server->address, address_len - 1);
        address[address_len - 1] = '\0';
    }
    
    if (ping != NULL)
    {
        *ping = server->ping_time;
    }
    
    return true;
}

// Get raw server entry (for browser)
server_entry_t *NET_Query_GetServerEntry(int index)
{
    if (index < 0 || index >= num_servers)
        return NULL;
    return &servers[index];
}

// Calculate matchmaking score for a server
// Higher score = better match
// Returns -1 if server should be skipped
static int NET_Query_CalcScore(server_entry_t *server)
{
    // No response = skip
    if (!server->has_response)
        return -1;
    
    // In-game = skip
    if (server->server_state != SERVER_STATE_WAITING)
        return -1;
    
    // Full = skip
    if (server->num_players >= server->max_players)
        return -1;
    
    // Scoring:
    // Prefer 1-2 players (ideal for quick game start)
    // Then 0 players (empty server)
    // Then 3+ players (nearly full)
    
    int score = 100;
    
    // Bonus for 1-2 players (someone waiting!)
    if (server->num_players >= 1 && server->num_players <= 2)
        score += 50;
    
    // Slight bonus for having any players
    if (server->num_players > 0)
        score += 10 * server->num_players;
    
    // Penalty for low ping (actually bonus for low ping)
    if (server->ping_time < 50)
        score += 30;
    else if (server->ping_time < 100)
        score += 20;
    else if (server->ping_time < 200)
        score += 10;
    
    // Penalty for nearly full
    if (server->num_players >= server->max_players - 1)
        score -= 20;
    
    return score;
}

// Find best server for auto-matchmaking
// Returns server index, or -1 if no suitable server found
int NET_Query_FindBestServer(void)
{
    int best_index = -1;
    int best_score = -1;
    
    for (int i = 0; i < num_servers; i++)
    {
        int score = NET_Query_CalcScore(&servers[i]);
        if (score > best_score)
        {
            best_score = score;
            best_index = i;
        }
    }
    
    if (best_index >= 0)
    {
        printf("NET_Query_FindBestServer: Selected %s (score %d)\n",
               servers[best_index].address, best_score);
    }
    else
    {
        printf("NET_Query_FindBestServer: No suitable server found\n");
    }
    
    return best_index;
}

// Get address string for server index
const char *NET_Query_GetServerAddress(int index)
{
    if (index < 0 || index >= num_servers)
        return NULL;
    return servers[index].address;
}

// Legacy stub functions for compatibility
void NET_QueryAddress(char *addr)
{
    printf("NET_QueryAddress: %s\n", addr);
    NET_Query_Init();
    NET_Query_ClearServers();
    NET_Query_AddServer(addr);
    NET_Query_RunAll();
}

void NET_LANQuery(void)
{
    printf("NET_LANQuery: LAN broadcast not implemented\n");
}

void NET_MasterQuery(void)
{
    printf("NET_MasterQuery: Master server not implemented\n");
}

