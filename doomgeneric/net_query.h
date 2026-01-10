//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Server query and lobby browser functions
//

#ifndef NET_QUERY_H
#define NET_QUERY_H

#include "doomtype.h"
#include "net_defs.h"

// Default server configuration
#define DEFAULT_SERVER_IP "64.227.99.100"
#define DEFAULT_BASE_PORT 2342

// Initialize/shutdown query system
boolean NET_Query_Init(void);
void NET_Query_Shutdown(void);

// Server list management
boolean NET_Query_AddServer(const char *address);
void NET_Query_ClearServers(void);
int NET_Query_GetServerCount(void);

// Server discovery (scans sequential ports to find active servers)
void NET_Query_DiscoverServers(const char *server_ip, int base_port);
void NET_Query_DiscoverDefaultServers(void);

// Query operations
void NET_Query_StartAll(void);      // Start async query of all servers
boolean NET_Query_Poll(void);       // Poll for responses, returns true when complete
void NET_Query_RunAll(void);        // Blocking query of all servers

// Get server info
boolean NET_Query_GetServerInfo(int index, net_querydata_t *data,
                                 char *address, int address_len,
                                 unsigned int *ping);
const char *NET_Query_GetServerAddress(int index);

// Auto-matchmaking
int NET_Query_FindBestServer(void);  // Returns index of best server, -1 if none

// Legacy compatibility stubs
void NET_QueryAddress(char *addr);
void NET_LANQuery(void);
void NET_MasterQuery(void);

#endif /* #ifndef NET_QUERY_H */
