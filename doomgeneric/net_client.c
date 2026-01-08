//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Network client code
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "doomtype.h"
#include "d_loop.h"
#include "d_ticcmd.h"
#include "i_system.h"
#include "i_timer.h"
#include "m_argv.h"
#include "m_misc.h"
#include "net_client.h"
#include "net_defs.h"
#include "net_io.h"
#include "net_packet.h"
#include "net_socket.h"
#include "w_wad.h"

// Connection state
typedef enum
{
    CLIENT_STATE_DISCONNECTED,
    CLIENT_STATE_CONNECTING,
    CLIENT_STATE_CONNECTED,
    CLIENT_STATE_WAITING_START,
    CLIENT_STATE_IN_GAME,
} client_state_t;

static client_state_t client_state = CLIENT_STATE_DISCONNECTED;
static net_addr_t *server_addr = NULL;
static net_context_t *client_context = NULL;

// Global variables exported to other modules
boolean net_client_connected = false;
boolean net_client_received_wait_data = false;
net_waitdata_t net_client_wait_data;
boolean net_waiting_for_launch = false;
char *net_player_name = "Pager";

sha1_digest_t net_server_wad_sha1sum;
sha1_digest_t net_server_deh_sha1sum;
unsigned int net_server_is_freedoom = 0;
sha1_digest_t net_local_wad_sha1sum;
sha1_digest_t net_local_deh_sha1sum;
unsigned int net_local_is_freedoom = 0;

boolean drone = false;

// Game settings received from server
// Received game settings from server (non-static for d_loop.c access)
net_gamesettings_t received_settings;
boolean received_settings_valid = false;

// Send queue - stores ticcmd diffs for retransmission
typedef struct
{
    boolean active;
    unsigned int seq;
    unsigned int time;  // Time command was generated
    unsigned int diff;  // Diff bitfield
    ticcmd_t cmd;       // The full ticcmd
} net_send_queue_t;

#define BACKUPTICS 128
static net_send_queue_t send_queue[BACKUPTICS];
static ticcmd_t last_ticcmd;  // For calculating diffs

// Receive window - baseline ticcmds for patching diffs (per-player)
static ticcmd_t recvwindow_cmd_base[NET_MAXPLAYERS];

// Last time we sent a packet
static unsigned int last_send_time = 0;
static unsigned int last_gamedata_time = 0;
static unsigned int last_packet_send_time = 0;  // For keepalive

// Connection sequence number
static unsigned int connect_seq = 0;

// Local player number
static int local_player = 0;

// Receive window - tracks which tic we're expecting
static unsigned int recvwindow_start = 0;

// Track the next expected reliable sequence number
static unsigned int reliable_recv_seq = 0;

// Expand a low byte tic number to a full tic number relative to recvwindow_start
static unsigned int NET_CL_ExpandTicNum(unsigned int b)
{
    unsigned int l, h;
    unsigned int result;

    h = recvwindow_start & ~0xff;
    l = recvwindow_start & 0xff;

    result = h | b;

    if (l < 0x40 && b > 0xb0)
        result -= 0x100;
    if (l > 0xb0 && b < 0x40)
        result += 0x100;
    
    return result;
}

static void NET_CL_SendSyn(net_connect_data_t *data);
static void NET_CL_ParsePacket(net_packet_t *packet);
static void NET_CL_SendReliableACK(unsigned int seq);
static void NET_CL_SendGameDataACK(void);
static void NET_CL_SendTics(int starttic, int endtic);

// Track last reliable sequence we ACKed to avoid duplicate ACKs
static unsigned int last_acked_seq = 0xFF;

void NET_CL_Init(void)
{
    client_context = NET_NewContext();
    NET_AddModule(client_context, &net_socket_module);
}

boolean NET_CL_Connect(net_addr_t *addr, net_connect_data_t *data)
{
    int start_time;
    int attempts;

    if (client_state != CLIENT_STATE_DISCONNECTED)
    {
        fprintf(stderr, "NET_CL_Connect: Already connected\n");
        return false;
    }

    // Initialize socket module
    if (!net_socket_module.InitClient())
    {
        fprintf(stderr, "NET_CL_Connect: Failed to init socket\n");
        return false;
    }

    server_addr = addr;
    client_state = CLIENT_STATE_CONNECTING;
    connect_seq = 0;
    last_packet_send_time = I_GetTimeMS();  // Initialize keepalive timer

    printf("NET_CL_Connect: Connecting to %s...\n", NET_AddrToString(addr));

    // Try to connect with timeout
    start_time = I_GetTimeMS();
    attempts = 0;

    while (client_state == CLIENT_STATE_CONNECTING)
    {
        // Send SYN packet
        if (I_GetTimeMS() - last_send_time > 500)
        {
            NET_CL_SendSyn(data);
            last_send_time = I_GetTimeMS();
            attempts++;
            printf("NET_CL_Connect: Attempt %d...\n", attempts);
        }

        // Check for response
        NET_CL_Run();

        // Timeout after 10 seconds
        if (I_GetTimeMS() - start_time > 10000)
        {
            fprintf(stderr, "NET_CL_Connect: Connection timed out\n");
            client_state = CLIENT_STATE_DISCONNECTED;
            return false;
        }

        I_Sleep(10);
    }

    if (client_state == CLIENT_STATE_CONNECTED ||
        client_state == CLIENT_STATE_WAITING_START)
    {
        printf("NET_CL_Connect: Connected!\n");
        net_client_connected = true;
        return true;
    }

    return false;
}

static void NET_CL_SendSyn(net_connect_data_t *data)
{
    net_packet_t *packet;

    packet = NET_NewPacket(256);
    if (packet == NULL)
        return;

    // Packet type
    NET_WriteInt16(packet, NET_PACKET_TYPE_SYN);
    
    // Magic number
    NET_WriteInt32(packet, NET_MAGIC_NUMBER);
    
    // Version string (must match format server expects)
    NET_WriteString(packet, "Chocolate Doom 3.1.1");
    
    // Protocol list: count followed by protocol name strings
    NET_WriteInt8(packet, 1);  // We support 1 protocol
    NET_WriteString(packet, "CHOCOLATE_DOOM_0");
    
    // Connect data
    NET_WriteInt8(packet, data->gamemode);
    NET_WriteInt8(packet, data->gamemission);
    NET_WriteInt8(packet, data->lowres_turn);
    NET_WriteInt8(packet, data->drone);
    NET_WriteInt8(packet, data->max_players);
    NET_WriteInt8(packet, data->is_freedoom);

    // WAD SHA1
    for (int i = 0; i < 20; i++)
        NET_WriteInt8(packet, data->wad_sha1sum[i]);

    // DEH SHA1
    for (int i = 0; i < 20; i++)
        NET_WriteInt8(packet, data->deh_sha1sum[i]);

    NET_WriteInt8(packet, data->player_class);
    
    // Player name
    NET_WriteString(packet, net_player_name);

    NET_SendPacket(server_addr, packet);
    NET_FreePacket(packet);
}

static void NET_CL_SendReliableACK(unsigned int seq)
{
    net_packet_t *packet;
    
    // Check if this is the sequence we expected
    if (seq == (reliable_recv_seq & 0xff))
    {
        // Accept this packet and increment our expected sequence
        reliable_recv_seq = (reliable_recv_seq + 1) & 0xff;
    }
    // If not the expected sequence, still ACK but don't increment
    // (this handles retries and out-of-order packets)
    
    packet = NET_NewPacket(16);
    if (packet == NULL)
        return;
    
    // ACK contains the NEXT expected sequence number
    NET_WriteInt16(packet, NET_PACKET_TYPE_RELIABLE_ACK);
    NET_WriteInt8(packet, reliable_recv_seq & 0xFF);
    
    NET_SendPacket(server_addr, packet);
    NET_FreePacket(packet);
}

// Send acknowledgment for received game data
static void NET_CL_SendGameDataACK(void)
{
    net_packet_t *packet;
    
    packet = NET_NewPacket(10);
    if (packet == NULL)
        return;
    
    NET_WriteInt16(packet, NET_PACKET_TYPE_GAMEDATA_ACK);
    NET_WriteInt8(packet, recvwindow_start & 0xFF);
    
    NET_SendPacket(server_addr, packet);
    NET_FreePacket(packet);
}

void NET_CL_Disconnect(void)
{
    net_packet_t *packet;

    if (!net_client_connected)
        return;

    // Send disconnect packet
    packet = NET_NewPacket(16);
    if (packet != NULL)
    {
        NET_WriteInt16(packet, NET_PACKET_TYPE_DISCONNECT);
        NET_SendPacket(server_addr, packet);
        NET_FreePacket(packet);
    }

    NET_Socket_Shutdown();
    client_state = CLIENT_STATE_DISCONNECTED;
    net_client_connected = false;
}

void NET_CL_Run(void)
{
    net_addr_t *addr;
    net_packet_t *packet;
    unsigned int nowtime;

    if (client_context == NULL)
        return;

    // Receive and process packets
    while (NET_RecvPacket(client_context, &addr, &packet))
    {
        NET_CL_ParsePacket(packet);
        NET_FreePacket(packet);
        NET_FreeAddress(addr);
    }
    
    // Send keepalive if we haven't sent anything recently (every 1 second)
    // Server disconnects clients after 30 seconds of silence!
    nowtime = I_GetTimeMS();
    if (net_client_connected && (nowtime - last_packet_send_time > 1000))
    {
        packet = NET_NewPacket(16);
        if (packet != NULL)
        {
            NET_WriteInt16(packet, NET_PACKET_TYPE_KEEPALIVE);
            NET_SendPacket(server_addr, packet);
            NET_FreePacket(packet);
            last_packet_send_time = nowtime;
        }
    }
}

static void NET_CL_ParsePacket(net_packet_t *packet)
{
    unsigned int packet_type;
    unsigned int is_reliable;
    unsigned int reliable_seq = 0;

    if (!NET_ReadInt16(packet, &packet_type))
        return;

    // Check for reliable packet flag (bit 15)
    is_reliable = packet_type & NET_RELIABLE_PACKET;
    packet_type = packet_type & ~NET_RELIABLE_PACKET;
    
    // Read reliable sequence number and send ACK
    if (is_reliable)
    {
        if (!NET_ReadInt8(packet, &reliable_seq))
            return;
        NET_CL_SendReliableACK(reliable_seq);
    }
    
    // Debug output - only for non-GAMEDATA packets to reduce spam
    if (packet_type != NET_PACKET_TYPE_GAMEDATA && packet_type != NET_PACKET_TYPE_GAMEDATA_RESEND)
    {
        fprintf(stderr, "NET_CL_ParsePacket: type=%d reliable=%d seq=%d\n", 
                packet_type, is_reliable ? 1 : 0, reliable_seq);
    }

    switch (packet_type)
    {
        case NET_PACKET_TYPE_SYN:
        {
            // Server's SYN response - connection accepted!
            // Read server version string
            char *server_version = NET_ReadString(packet);
            if (server_version != NULL)
            {
                fprintf(stderr, "NET_CL_ParsePacket: Server version: %s\n", server_version);
            }
            
            // Read server's protocol
            char *protocol = NET_ReadString(packet);
            if (protocol != NULL)
            {
                fprintf(stderr, "NET_CL_ParsePacket: Protocol: %s\n", protocol);
            }
            
            // Connection successful!
            printf("NET_CL_ParsePacket: Connected to server!\n");
            client_state = CLIENT_STATE_WAITING_START;
            break;
        }
        
        case NET_PACKET_TYPE_ACK:
        {
            // Legacy: Connection accepted (deprecated but handle anyway)
            unsigned int player;
            if (NET_ReadInt8(packet, &player))
            {
                local_player = player;
                printf("NET_CL_ParsePacket: Accepted as player %d\n", local_player);
                client_state = CLIENT_STATE_WAITING_START;
            }
            break;
        }

        case NET_PACKET_TYPE_REJECTED:
        {
            char *reason = NET_ReadString(packet);
            if (reason != NULL)
                fprintf(stderr, "Connection rejected: %s\n", reason);
            client_state = CLIENT_STATE_DISCONNECTED;
            break;
        }

        case NET_PACKET_TYPE_WAITING_DATA:
        {
            // Server is waiting for more players - read full wait data
            char *s;
            int i;
            
            NET_ReadInt8(packet, (unsigned int *)&net_client_wait_data.num_players);
            NET_ReadInt8(packet, (unsigned int *)&net_client_wait_data.num_drones);
            NET_ReadInt8(packet, (unsigned int *)&net_client_wait_data.ready_players);
            NET_ReadInt8(packet, (unsigned int *)&net_client_wait_data.max_players);
            NET_ReadInt8(packet, (unsigned int *)&net_client_wait_data.is_controller);
            NET_ReadInt8(packet, (unsigned int *)&net_client_wait_data.consoleplayer);
            
            // Read player names and addresses
            for (i = 0; i < net_client_wait_data.num_players && i < NET_MAXPLAYERS; i++) {
                s = NET_ReadString(packet);
                if (s != NULL) {
                    strncpy(net_client_wait_data.player_names[i], s, MAXPLAYERNAME - 1);
                    net_client_wait_data.player_names[i][MAXPLAYERNAME - 1] = '\0';
                }
                
                s = NET_ReadString(packet);
                if (s != NULL) {
                    strncpy(net_client_wait_data.player_addrs[i], s, MAXPLAYERNAME - 1);
                    net_client_wait_data.player_addrs[i][MAXPLAYERNAME - 1] = '\0';
                }
            }
            
            // Skip SHA1 sums and freedoom flag (we don't validate them)
            // wad_sha1sum (20 bytes), deh_sha1sum (20 bytes), is_freedoom (1 byte)
            
            net_client_received_wait_data = true;
            break;
        }

        case NET_PACKET_TYPE_GAMESTART:
        {
            unsigned int random_val;
            extern int prndindex;  // From m_random.c - P_Random seed
            extern int rndindex;   // From m_random.c - M_Random seed (used in consistency)
            
            fprintf(stderr, "NET_CL_ParsePacket: Received GAMESTART!\n");
            // Game is starting - read settings in CORRECT ORDER per chocolate-doom
            // Order: ticdup, extratics, deathmatch, nomonsters, fast_monsters, respawn_monsters,
            //        episode, map, skill, gameversion, lowres_turn, new_sync, timelimit(32!),
            //        loadgame, random, num_players, consoleplayer, player_classes[]
            NET_ReadInt8(packet, (unsigned int *)&received_settings.ticdup);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.extratics);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.deathmatch);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.nomonsters);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.fast_monsters);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.respawn_monsters);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.episode);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.map);
            NET_ReadSInt8(packet, &received_settings.skill);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.gameversion);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.lowres_turn);
            NET_ReadInt8(packet, (unsigned int *)&received_settings.new_sync);
            NET_ReadInt32(packet, (unsigned int *)&received_settings.timelimit);  // INT32!
            NET_ReadSInt8(packet, &received_settings.loadgame);
            NET_ReadInt8(packet, &random_val);  // random field - CRITICAL for sync!
            NET_ReadInt8(packet, (unsigned int *)&received_settings.num_players);
            NET_ReadSInt8(packet, &received_settings.consoleplayer);
            
            // Set the random seed to match server - CRITICAL for game sync!
            prndindex = random_val;
            rndindex = random_val;
            fprintf(stderr, "NET_CL_ParsePacket: Random seed set to %d\n", random_val);

            fprintf(stderr, "NET_CL_ParsePacket: GAMESTART parsed: ep=%d map=%d players=%d console=%d\n",
                    received_settings.episode, received_settings.map,
                    received_settings.num_players, received_settings.consoleplayer);

            for (int i = 0; i < received_settings.num_players && i < NET_MAXPLAYERS; i++)
                NET_ReadInt8(packet, (unsigned int *)&received_settings.player_classes[i]);

            received_settings_valid = true;
            client_state = CLIENT_STATE_IN_GAME;
            net_waiting_for_launch = false;
            
            // Initialize send queue, last_ticcmd, and receive window baseline
            memset(send_queue, 0, sizeof(send_queue));
            memset(&last_ticcmd, 0, sizeof(last_ticcmd));
            memset(recvwindow_cmd_base, 0, sizeof(recvwindow_cmd_base));
            recvwindow_start = 0;
            
            printf("NET_CL_ParsePacket: Game starting! We are player %d (consoleplayer=%d) of %d\n",
                   received_settings.consoleplayer + 1,
                   received_settings.consoleplayer,
                   received_settings.num_players);
            fprintf(stderr, "DEBUG: consoleplayer=%d, num_players=%d\n",
                    received_settings.consoleplayer, received_settings.num_players);
            break;
        }

        case NET_PACKET_TYPE_GAMEDATA:
        {
            // Game data from server - CORRECT FORMAT:
            // int8: start_tic (low byte)
            // int8: num_tics
            // For each tic:
            //   int16: latency
            //   int8: playeringame bitfield (bit 0 = player 0, etc)
            //   For each player in bitfield: ticcmd diff
            
            unsigned int start_tic;
            unsigned int num_tics;
            unsigned int tic_idx;
            
            if (!NET_ReadInt8(packet, &start_tic))
                break;
            if (!NET_ReadInt8(packet, &num_tics))
                break;
            
            // Expand start_tic from low byte to full value
            // (server only sends low byte, we reconstruct full value)
            start_tic = NET_CL_ExpandTicNum(start_tic);
            
            // Process each tic in the packet
            for (tic_idx = 0; tic_idx < num_tics; tic_idx++)
            {
                unsigned int current_tic = start_tic + tic_idx;
                signed int latency;
                unsigned int bitfield;
                ticcmd_t ticcmds[NET_MAXPLAYERS];
                boolean playeringame[NET_MAXPLAYERS];
                
                // Start from baseline (previous state), not zero!
                memcpy(ticcmds, recvwindow_cmd_base, sizeof(ticcmds));
                memset(playeringame, 0, sizeof(playeringame));
                
                // Read latency
                if (!NET_ReadSInt16(packet, &latency))
                    break;
                
                // Read playeringame BITFIELD (single byte, not 8 separate bytes!)
                if (!NET_ReadInt8(packet, &bitfield))
                    break;
                
                // Convert bitfield to array
                for (int i = 0; i < NET_MAXPLAYERS; i++)
                    playeringame[i] = (bitfield & (1 << i)) != 0;
                
                // Read ticcmd for each active player
                for (int i = 0; i < NET_MAXPLAYERS; i++)
                {
                    if (playeringame[i])
                    {
                        unsigned int diff;
                        signed int sval;
                        unsigned int uval;
                        
                        if (!NET_ReadInt8(packet, &diff))
                            break;
                        
                        // Skip our own player - we use our local ticcmd, not the relayed one
                        // But we still need to read the diff data to advance the packet position
                        if (i == received_settings.consoleplayer && !drone)
                        {
                            // Just skip the diff data - don't apply it
                            if (diff & NET_TICDIFF_FORWARD)
                                NET_ReadSInt8(packet, &sval);
                            if (diff & NET_TICDIFF_SIDE)
                                NET_ReadSInt8(packet, &sval);
                            if (diff & NET_TICDIFF_TURN)
                            {
                                if (received_settings.lowres_turn)
                                    NET_ReadSInt8(packet, &sval);
                                else
                                    NET_ReadSInt16(packet, &sval);
                            }
                            if (diff & NET_TICDIFF_BUTTONS)
                                NET_ReadInt8(packet, &uval);
                            if (diff & NET_TICDIFF_CONSISTANCY)
                                NET_ReadInt8(packet, &uval);
                            if (diff & NET_TICDIFF_CHATCHAR)
                                NET_ReadInt8(packet, &uval);
                            if (diff & NET_TICDIFF_RAVEN)
                            {
                                NET_ReadInt8(packet, &uval);
                                NET_ReadInt8(packet, &uval);
                            }
                            if (diff & NET_TICDIFF_STRIFE)
                            {
                                NET_ReadInt8(packet, &uval);
                                NET_ReadInt16(packet, &uval);
                            }
                            continue;  // Don't update baseline for our own player
                        }
                        
                        // Now process other players' ticcmds normally
                        if (diff & NET_TICDIFF_FORWARD)
                        {
                            NET_ReadSInt8(packet, &sval);
                            ticcmds[i].forwardmove = sval;
                        }
                        if (diff & NET_TICDIFF_SIDE)
                        {
                            NET_ReadSInt8(packet, &sval);
                            ticcmds[i].sidemove = sval;
                        }
                        if (diff & NET_TICDIFF_TURN)
                        {
                            // Check for lowres_turn mode
                            if (received_settings.lowres_turn)
                            {
                                NET_ReadSInt8(packet, &sval);
                                ticcmds[i].angleturn = sval * 256;
                            }
                            else
                            {
                                NET_ReadSInt16(packet, &sval);
                                ticcmds[i].angleturn = sval;
                            }
                        }
                        if (diff & NET_TICDIFF_BUTTONS)
                        {
                            NET_ReadInt8(packet, &uval);
                            ticcmds[i].buttons = uval;
                        }
                        if (diff & NET_TICDIFF_CONSISTANCY)
                        {
                            NET_ReadInt8(packet, &uval);
                            ticcmds[i].consistancy = uval;
                        }
                        // else: Consistency not in diff - using baseline value
                        // Handle optional chatchar - MUST zero if not in diff (per Chocolate Doom)
                        if (diff & NET_TICDIFF_CHATCHAR)
                        {
                            NET_ReadInt8(packet, &uval);
                            ticcmds[i].chatchar = uval;
                        }
                        else
                        {
                            ticcmds[i].chatchar = 0;
                        }
                        
                        // Raven (Heretic/Hexen) - not used in Doom but parse anyway
                        if (diff & NET_TICDIFF_RAVEN)
                        {
                            NET_ReadInt8(packet, &uval);  // lookfly
                            NET_ReadInt8(packet, &uval);  // arti
                        }
                        
                        // Strife - not used in Doom but parse anyway
                        if (diff & NET_TICDIFF_STRIFE)
                        {
                            NET_ReadInt8(packet, &uval);   // buttons2
                            NET_ReadInt16(packet, &uval);  // inventory
                        }
                        
                        // Update baseline for this player for next diff
                        recvwindow_cmd_base[i] = ticcmds[i];
                    }
                }
                
                // Only deliver if this is the tic we're expecting
                // This ensures sequential processing even if packets arrive out of order
                if (current_tic == recvwindow_start)
                {
                    // Deliver to game loop
                    D_ReceiveTic(ticcmds, playeringame);
                    
                    // Advance receive window
                    recvwindow_start++;
                }
                // else if current_tic > recvwindow_start: out of order - skip
                // else if current_tic < recvwindow_start: duplicate - ignore
            }
            
            last_gamedata_time = I_GetTimeMS();
            
            // Send ACK after processing all tics
            NET_CL_SendGameDataACK();
            break;
        }

        case NET_PACKET_TYPE_GAMEDATA_RESEND:
        {
            // Server is asking us to resend our GAMEDATA
            // The packet contains: start tic (int32) + num_tics (int8)
            unsigned int start_tic;
            unsigned int num_tics;
            unsigned int end_tic;
            
            if (!NET_ReadInt32(packet, &start_tic))
                break;
            if (!NET_ReadInt8(packet, &num_tics))
                break;
            
            end_tic = start_tic + num_tics - 1;
            
            // Trim range to what we actually have in send queue
            while (start_tic <= end_tic
                && (!send_queue[start_tic % BACKUPTICS].active
                 || send_queue[start_tic % BACKUPTICS].seq != start_tic))
            {
                start_tic++;
            }
            
            while (start_tic <= end_tic
                && (!send_queue[end_tic % BACKUPTICS].active
                 || send_queue[end_tic % BACKUPTICS].seq != end_tic))
            {
                end_tic--;
            }
            
            // Resend the tics we have
            if (start_tic <= end_tic)
            {
                NET_CL_SendTics(start_tic, end_tic);
            }
            break;
        }

        case NET_PACKET_TYPE_LAUNCH:
        {
            fprintf(stderr, "NET_CL_ParsePacket: Received LAUNCH signal!\n");
            net_waiting_for_launch = false;
            fprintf(stderr, "NET_CL_ParsePacket: net_waiting_for_launch = %d\n", net_waiting_for_launch);
            break;
        }

        case NET_PACKET_TYPE_DISCONNECT:
        case NET_PACKET_TYPE_DISCONNECT_ACK:
        {
            printf("NET_CL_ParsePacket: Server disconnected\n");
            client_state = CLIENT_STATE_DISCONNECTED;
            net_client_connected = false;
            break;
        }

        default:
            break;
    }
}

static unsigned int reliable_send_seq = 0;

void NET_CL_LaunchGame(void)
{
    net_packet_t *packet;

    if (!net_client_connected)
        return;

    fprintf(stderr, "NET_CL_LaunchGame: Sending LAUNCH as reliable packet\n");

    packet = NET_NewPacket(16);
    if (packet != NULL)
    {
        // Send as reliable packet (like Chocolate Doom does)
        NET_WriteInt16(packet, NET_PACKET_TYPE_LAUNCH | NET_RELIABLE_PACKET);
        NET_WriteInt8(packet, reliable_send_seq & 0xFF);
        reliable_send_seq++;
        NET_SendPacket(server_addr, packet);
        NET_FreePacket(packet);
    }
}

void NET_CL_StartGame(net_gamesettings_t *settings)
{
    // Send game start request - MUST match chocolate-doom NET_WriteSettings order
    net_packet_t *packet;
    int i;

    if (!net_client_connected)
        return;

    fprintf(stderr, "NET_CL_StartGame: Sending GAMESTART request (reliable)\n");

    packet = NET_NewPacket(128);
    if (packet != NULL)
    {
        // Send as reliable packet: set NET_RELIABLE_PACKET flag + sequence
        NET_WriteInt16(packet, NET_PACKET_TYPE_GAMESTART | NET_RELIABLE_PACKET);
        NET_WriteInt8(packet, reliable_send_seq & 0xFF);
        reliable_send_seq++;
        
        // Game settings - order MUST match NET_WriteSettings/NET_ReadSettings
        NET_WriteInt8(packet, settings->ticdup);
        NET_WriteInt8(packet, settings->extratics);
        NET_WriteInt8(packet, settings->deathmatch);
        NET_WriteInt8(packet, settings->nomonsters);
        NET_WriteInt8(packet, settings->fast_monsters);
        NET_WriteInt8(packet, settings->respawn_monsters);
        NET_WriteInt8(packet, settings->episode);
        NET_WriteInt8(packet, settings->map);
        NET_WriteInt8(packet, settings->skill);
        NET_WriteInt8(packet, settings->gameversion);
        NET_WriteInt8(packet, settings->lowres_turn);
        NET_WriteInt8(packet, settings->new_sync);
        NET_WriteInt32(packet, settings->timelimit);  // INT32!
        NET_WriteInt8(packet, settings->loadgame);
        NET_WriteInt8(packet, 0);  // random
        NET_WriteInt8(packet, settings->num_players > 0 ? settings->num_players : 2);  // num_players
        NET_WriteInt8(packet, settings->consoleplayer);  // consoleplayer
        
        // Player classes for each player
        for (i = 0; i < NET_MAXPLAYERS; i++)
        {
            NET_WriteInt8(packet, settings->player_classes[i]);
        }

        NET_SendPacket(server_addr, packet);
        NET_FreePacket(packet);
    }
}

// Write a single ticcmd diff to the packet
static void WriteTiccmdDiff(net_packet_t *packet, net_send_queue_t *sendobj)
{
    unsigned int diff = sendobj->diff;
    ticcmd_t *cmd = &sendobj->cmd;
    
    NET_WriteInt8(packet, diff);
    
    if (diff & NET_TICDIFF_FORWARD)
        NET_WriteInt8(packet, (unsigned int)cmd->forwardmove);
    if (diff & NET_TICDIFF_SIDE)
        NET_WriteInt8(packet, (unsigned int)cmd->sidemove);
    if (diff & NET_TICDIFF_TURN)
    {
        if (received_settings.lowres_turn)
            NET_WriteInt8(packet, cmd->angleturn >> 8);
        else
            NET_WriteInt16(packet, cmd->angleturn);
    }
    if (diff & NET_TICDIFF_BUTTONS)
        NET_WriteInt8(packet, cmd->buttons);
    if (diff & NET_TICDIFF_CONSISTANCY)
        NET_WriteInt8(packet, cmd->consistancy);
}

// Send ticcmds from starttic to endtic
static void NET_CL_SendTics(int starttic, int endtic)
{
    net_packet_t *packet;
    int i;
    
    if (!net_client_connected || client_state != CLIENT_STATE_IN_GAME)
        return;
    
    if (starttic < 0)
        starttic = 0;
    
    packet = NET_NewPacket(512);
    if (packet == NULL)
        return;
    
    // Header: type, ack, start, num_tics
    NET_WriteInt16(packet, NET_PACKET_TYPE_GAMEDATA);
    NET_WriteInt8(packet, recvwindow_start & 0xFF);
    NET_WriteInt8(packet, starttic & 0xFF);
    NET_WriteInt8(packet, endtic - starttic + 1);
    
    // Write each tic - but first count how many we actually have
    int actual_tics = 0;
    for (i = starttic; i <= endtic; i++)
    {
        net_send_queue_t *sendobj = &send_queue[i % BACKUPTICS];
        if (sendobj->active && sendobj->seq == (unsigned int)i)
            actual_tics++;
        else
            break;  // Stop at first missing tic
    }
    
    if (actual_tics == 0)
    {
        NET_FreePacket(packet);
        return;
    }
    
    // Update num_tics in packet (position 4, after type(2)+ack(1)+start(1))
    packet->data[4] = actual_tics;
    
    // Write each tic
    for (i = starttic; i < starttic + actual_tics; i++)
    {
        net_send_queue_t *sendobj = &send_queue[i % BACKUPTICS];
        
        // Latency - time since we created this tic
        int latency = I_GetTimeMS() - sendobj->time;
        if (latency < 0) latency = 0;
        NET_WriteInt16(packet, latency);
        
        // Ticcmd diff
        WriteTiccmdDiff(packet, sendobj);
    }
    
    NET_SendPacket(server_addr, packet);
    NET_FreePacket(packet);
}

void NET_CL_SendTiccmd(ticcmd_t *ticcmd, int maketic)
{
    net_send_queue_t *sendobj;
    unsigned int diff = 0;
    int starttic, endtic;
    
    if (!net_client_connected || client_state != CLIENT_STATE_IN_GAME)
        return;
    
    // Calculate diff from last command
    if (ticcmd->forwardmove != last_ticcmd.forwardmove)
        diff |= NET_TICDIFF_FORWARD;
    if (ticcmd->sidemove != last_ticcmd.sidemove)
        diff |= NET_TICDIFF_SIDE;
    if (ticcmd->angleturn != last_ticcmd.angleturn)
        diff |= NET_TICDIFF_TURN;
    if (ticcmd->buttons != last_ticcmd.buttons)
        diff |= NET_TICDIFF_BUTTONS;
    // ALWAYS include consistency - critical for game sync!
    diff |= NET_TICDIFF_CONSISTANCY;
    
    // Store in send queue
    sendobj = &send_queue[maketic % BACKUPTICS];
    sendobj->active = true;
    sendobj->seq = maketic;
    sendobj->time = I_GetTimeMS();
    sendobj->diff = diff;
    sendobj->cmd = *ticcmd;
    
    // Update last ticcmd for future diffs
    last_ticcmd = *ticcmd;
    
    // Send just the current tic for now (simpler - avoid extratics complexity)
    starttic = maketic;
    endtic = maketic;
    
    NET_CL_SendTics(starttic, endtic);
}

boolean NET_CL_GetSettings(net_gamesettings_t *settings)
{
    if (!received_settings_valid)
        return false;

    memcpy(settings, &received_settings, sizeof(net_gamesettings_t));
    return true;
}

void NET_Init(void)
{
    NET_CL_Init();
}

void NET_BindVariables(void)
{
    // Could bind config variables here
}

// External lobby functions (from net_lobby.c and doomgeneric_linuxvt.c)
extern void DG_DrawLobby(int num_players, int max_players, int is_controller,
                         const char player_names[NET_MAXPLAYERS][MAXPLAYERNAME],
                         const char player_addrs[NET_MAXPLAYERS][MAXPLAYERNAME],
                         int consoleplayer);
extern int DG_CheckLobbyInput(void);  // Returns 1=start, -1=quit, 0=nothing

// NET_WaitForLaunch - Wait for LAUNCH signal in the lobby
// Does NOT send GAMESTART - that happens in D_StartNetGame
void NET_WaitForLaunch(void)
{
    int last_num_players = -1;
    int lobby_update_timer = 0;
    int input;
    
    net_waiting_for_launch = true;

    fprintf(stderr, "NET_WaitForLaunch: Waiting for game launch...\n");

    while (net_waiting_for_launch && net_client_connected)
    {
        NET_CL_Run();
        
        // Update lobby display periodically or when player count changes
        if (net_client_received_wait_data) {
            if (net_client_wait_data.num_players != last_num_players || lobby_update_timer <= 0) {
                DG_DrawLobby(net_client_wait_data.num_players,
                            net_client_wait_data.max_players,
                            net_client_wait_data.is_controller,
                            net_client_wait_data.player_names,
                            net_client_wait_data.player_addrs,
                            net_client_wait_data.consoleplayer);
                last_num_players = net_client_wait_data.num_players;
                lobby_update_timer = 20;  // Update every ~1 second
            }
            lobby_update_timer--;
            
            // Check for input (Green button to start if controller)
            input = DG_CheckLobbyInput();
            if (input == 1 && net_client_wait_data.is_controller) {
                // Controller pressed start (Green button)
                fprintf(stderr, "NET_WaitForLaunch: Controller starting game!\n");
                NET_CL_LaunchGame();
            }
        }
        
        I_Sleep(50);
    }
    
    fprintf(stderr, "NET_WaitForLaunch: Got LAUNCH signal, returning to game init...\n");
}

// NET_CL_SendStartAndWait - Send GAMESTART and wait for server response
// Called from D_StartNetGame after NET_WaitForLaunch returns
void NET_CL_SendStartAndWait(net_gamesettings_t *settings)
{
    int timeout;
    
    if (!net_client_connected) {
        fprintf(stderr, "NET_CL_SendStartAndWait: Not connected!\n");
        return;
    }
    
    fprintf(stderr, "NET_CL_SendStartAndWait: Sending GAMESTART request...\n");
    
    NET_CL_StartGame(settings);
    
    fprintf(stderr, "NET_CL_SendStartAndWait: Waiting for GAMESTART response...\n");
    
    // Wait for GAMESTART response with final settings from server
    timeout = 0;
    while (!received_settings_valid && net_client_connected && timeout < 200)
    {
        NET_CL_Run();
        I_Sleep(50);
        timeout++;
    }
    
    fprintf(stderr, "NET_CL_SendStartAndWait: Done! settings_valid=%d connected=%d timeout=%d\n", 
            received_settings_valid, net_client_connected, timeout);
}

net_addr_t *NET_FindLANServer(void)
{
    // LAN discovery not implemented
    return NULL;
}



