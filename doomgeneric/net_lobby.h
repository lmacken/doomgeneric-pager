//
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Network lobby and server browser UI
//

#ifndef NET_LOBBY_H
#define NET_LOBBY_H

#include "doomtype.h"
#include "net_defs.h"

//=============================================================================
// Lobby Drawing (waiting room before game starts)
//=============================================================================

// Draw the multiplayer lobby screen
void DG_DrawLobby(int num_players, int max_players, int is_controller, 
                  const char player_names[NET_MAXPLAYERS][MAXPLAYERNAME],
                  const char player_addrs[NET_MAXPLAYERS][MAXPLAYERNAME],
                  int consoleplayer,
                  const char *server_addr);

//=============================================================================
// Server Browser UI
//=============================================================================

// Initialize the browser
void DG_Browser_Init(void);

// Refresh server list (start querying)
void DG_Browser_Refresh(void);

// Update browser state (call every frame)
void DG_Browser_Update(void);

// Navigation
void DG_Browser_SelectUp(void);
void DG_Browser_SelectDown(void);

// Get selected server address
const char *DG_Browser_GetSelectedAddress(void);

// Get auto-matched server address (smart matching)
const char *DG_Browser_GetAutoMatchAddress(void);

// Draw browser screen
void DG_DrawBrowser(void);

// Draw connection status screens
void DG_DrawConnecting(const char *server_addr);
void DG_DrawAutoMatch(void);
void DG_DrawNoServers(void);
void DG_DrawExiting(void);

// Loading screens with DOOM skull sprite
void DG_DrawLoadingBrowser(const char *message);
void DG_DrawLoadingAutoMatch(int servers_found, int ports_scanned);

//=============================================================================
// Input (defined in doomgeneric_linuxvt.c)
//=============================================================================

// Check for lobby/browser input:
//   1  = GREEN (select/join)
//  -1  = RED (back/quit)
//   2  = UP (navigate up)
//   3  = DOWN (navigate down)
//   4  = LEFT
//   5  = RIGHT
//   0  = nothing
int DG_CheckLobbyInput(void);

#endif /* #ifndef NET_LOBBY_H */

