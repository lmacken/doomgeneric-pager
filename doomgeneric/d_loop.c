//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     Main loop code.
//

#include <stdlib.h>
#include <string.h>

#include "doomfeatures.h"

#include "d_event.h"
#include "d_loop.h"
#include "d_ticcmd.h"

#include "i_system.h"
#include "i_timer.h"
#include "i_video.h"

#include "m_argv.h"
#include "m_fixed.h"

#include "net_client.h"
#include "net_gui.h"
#include "net_io.h"
#include "net_query.h"
#include "net_server.h"
#include "net_socket.h"  // Use POSIX sockets instead of SDL_net
#include "net_loop.h"

// External declarations for network settings received from server
extern net_gamesettings_t received_settings;
extern boolean received_settings_valid;

// Server browser and lobby UI
#include "net_lobby.h"

// The complete set of data for a particular tic.

typedef struct
{
    ticcmd_t cmds[NET_MAXPLAYERS];
    boolean ingame[NET_MAXPLAYERS];
} ticcmd_set_t;

//
// gametic is the tic about to (or currently being) run
// maketic is the tic that hasn't had control made for it yet
// recvtic is the latest tic received from the server.
//
// a gametic cannot be run until ticcmds are received for it
// from all players.
//

static ticcmd_set_t ticdata[BACKUPTICS];

// The index of the next tic to be made (with a call to BuildTiccmd).

static int maketic;

// The number of complete tics received from the server so far.

static int recvtic;

// The number of tics that have been run (using RunTic) so far.

int gametic;

// When set to true, a single tic is run each time TryRunTics() is called.
// This is used for -timedemo mode.

boolean singletics = false;

// Index of the local player.

static int localplayer;

// Used for original sync code.

static int      skiptics = 0;

// Reduce the bandwidth needed by sampling game input less and transmitting
// less.  If ticdup is 2, sample half normal, 3 = one third normal, etc.

int		ticdup;

// Amount to offset the timer for game sync.

fixed_t         offsetms;

// Use new client syncronisation code

static boolean  new_sync = true;

// Callback functions for loop code.

static loop_interface_t *loop_interface = NULL;

// Current players in the multiplayer game.
// This is distinct from playeringame[] used by the game code, which may
// modify playeringame[] when playing back multiplayer demos.

static boolean local_playeringame[NET_MAXPLAYERS];

// Requested player class "sent" to the server on connect.
// If we are only doing a single player game then this needs to be remembered
// and saved in the game settings.

static int player_class;


// 35 fps clock adjusted by offsetms milliseconds

static int GetAdjustedTime(void)
{
    int time_ms;

    time_ms = I_GetTimeMS();

    if (new_sync)
    {
	// Use the adjustments from net_client.c only if we are
	// using the new sync mode.

        time_ms += (offsetms / FRACUNIT);
    }

    return (time_ms * TICRATE) / 1000;
}

static boolean BuildNewTic(void)
{
    int	gameticdiv;
    ticcmd_t cmd;

    gameticdiv = gametic/ticdup;

    I_StartTic ();
    loop_interface->ProcessEvents();

    // Always run the menu

    loop_interface->RunMenu();

    if (drone)
    {
        // In drone mode, do not generate any ticcmds.

        return false;
    }

    if (new_sync)
    {
       // If playing single player, do not allow tics to buffer
       // up very far

       if (!net_client_connected && maketic - gameticdiv > 2)
           return false;

       // Never go more than ~200ms ahead

       if (maketic - gameticdiv > 8)
           return false;
    }
    else
    {
       if (maketic - gameticdiv >= 5)
           return false;
    }

    //printf ("mk:%i ",maketic);
    memset(&cmd, 0, sizeof(ticcmd_t));
    loop_interface->BuildTiccmd(&cmd, maketic);

#ifdef FEATURE_MULTIPLAYER

    if (net_client_connected)
    {
        NET_CL_SendTiccmd(&cmd, maketic);
    }

#endif
    ticdata[maketic % BACKUPTICS].cmds[localplayer] = cmd;
    ticdata[maketic % BACKUPTICS].ingame[localplayer] = true;

    ++maketic;

    return true;
}

//
// NetUpdate
// Builds ticcmds for console player,
// sends out a packet
//
int      lasttime;

void NetUpdate (void)
{
    int nowtime;
    int newtics;
    int	i;

    // If we are running with singletics (timing a demo), this
    // is all done separately.

    if (singletics)
        return;

#ifdef FEATURE_MULTIPLAYER

    // Run network subsystems

    NET_CL_Run();
    NET_SV_Run();

#endif

    // check time
    nowtime = GetAdjustedTime() / ticdup;
    newtics = nowtime - lasttime;

    lasttime = nowtime;

    if (skiptics <= newtics)
    {
        newtics -= skiptics;
        skiptics = 0;
    }
    else
    {
        skiptics -= newtics;
        newtics = 0;
    }

    // build new ticcmds for console player

    for (i=0 ; i<newtics ; i++)
    {
        if (!BuildNewTic())
        {
            break;
        }
    }
}

static void D_Disconnected(void)
{
    // In drone mode, the game cannot continue once disconnected.

    if (drone)
    {
        I_Error("Disconnected from server in drone mode.");
    }

    // disconnected from server

    printf("Disconnected from server.\n");
}

//
// Invoked by the network engine when a complete set of ticcmds is
// available.
//

void D_ReceiveTic(ticcmd_t *ticcmds, boolean *players_mask)
{
    int i;

    // Disconnected from server?

    if (ticcmds == NULL && players_mask == NULL)
    {
        D_Disconnected();
        return;
    }

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        if (!drone && i == localplayer)
        {
            // This is us.  Don't overwrite it.
        }
        else
        {
            ticdata[recvtic % BACKUPTICS].cmds[i] = ticcmds[i];
            ticdata[recvtic % BACKUPTICS].ingame[i] = players_mask[i];
        }
    }

    ++recvtic;
}

//
// Start game loop
//
// Called after the screen is set but before the game starts running.
//

void D_StartGameLoop(void)
{
    lasttime = GetAdjustedTime() / ticdup;
}

#if ORIGCODE
//
// Block until the game start message is received from the server.
//

static void BlockUntilStart(net_gamesettings_t *settings,
                            netgame_startup_callback_t callback)
{
    while (!NET_CL_GetSettings(settings))
    {
        NET_CL_Run();
        NET_SV_Run();

        if (!net_client_connected)
        {
            I_Error("Lost connection to server");
        }

        if (callback != NULL && !callback(net_client_wait_data.ready_players,
                                          net_client_wait_data.num_players))
        {
            I_Error("Netgame startup aborted.");
        }

        I_Sleep(100);
    }
}

#endif

void D_StartNetGame(net_gamesettings_t *settings,
                    netgame_startup_callback_t callback)
{
#if ORIGCODE
    int i;

    offsetms = 0;
    recvtic = 0;

    settings->consoleplayer = 0;
    settings->num_players = 1;
    settings->player_classes[0] = player_class;

    //!
    // @category net
    //
    // Use new network client sync code rather than the classic
    // sync code. This is currently disabled by default because it
    // has some bugs.
    //
    if (M_CheckParm("-newsync") > 0)
        settings->new_sync = 1;
    else
        settings->new_sync = 0;

    // TODO: New sync code is not enabled by default because it's
    // currently broken. 
    //if (M_CheckParm("-oldsync") > 0)
    //    settings->new_sync = 0;
    //else
    //    settings->new_sync = 1;

    //!
    // @category net
    // @arg <n>
    //
    // Send n extra tics in every packet as insurance against dropped
    // packets.
    //

    i = M_CheckParmWithArgs("-extratics", 1);

    if (i > 0)
        settings->extratics = atoi(myargv[i+1]);
    else
        settings->extratics = 1;

    //!
    // @category net
    // @arg <n>
    //
    // Reduce the resolution of the game by a factor of n, reducing
    // the amount of network bandwidth needed.
    //

    i = M_CheckParmWithArgs("-dup", 1);

    if (i > 0)
        settings->ticdup = atoi(myargv[i+1]);
    else
        settings->ticdup = 1;

    if (net_client_connected)
    {
        // Send our game settings and block until game start is received
        // from the server.

        NET_CL_StartGame(settings);
        BlockUntilStart(settings, callback);

        // Read the game settings that were received.

        NET_CL_GetSettings(settings);
    }

    if (drone)
    {
        settings->consoleplayer = 0;
    }

    // Set the local player and playeringame[] values.

    localplayer = settings->consoleplayer;

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        local_playeringame[i] = i < settings->num_players;
    }

    // Copy settings to global variables.

    ticdup = settings->ticdup;
    new_sync = settings->new_sync;

    // TODO: Message disabled until we fix new_sync.
    //if (!new_sync)
    //{
    //    printf("Syncing netgames like Vanilla Doom.\n");
    //}
#else
#ifdef FEATURE_MULTIPLAYER
    // If we're connected to a network game, send GAMESTART and wait for response
    if (net_client_connected)
    {
        // Use command-line values (from d_main.c) for game settings
        extern int startepisode;
        extern int startmap;
        extern int startskill;
        extern int nomonsters;
        extern int fastparm;
        extern int respawnparm;
        extern int timelimit;
        
        // Fill in settings to send to server
        settings->ticdup = 1;
        settings->extratics = 1;
        settings->deathmatch = 1;  // Deathmatch mode
        settings->episode = startepisode;
        settings->map = startmap;
        settings->skill = startskill;
        settings->nomonsters = nomonsters;
        settings->fast_monsters = fastparm;
        settings->respawn_monsters = respawnparm;
        settings->timelimit = timelimit;
        settings->loadgame = -1;
        settings->lowres_turn = 0;
        settings->new_sync = 0;
        settings->gameversion = 0;
        settings->player_classes[0] = player_class;
        
        // Send GAMESTART and wait for server's response
        extern void NET_CL_SendStartAndWait(net_gamesettings_t *settings);
        NET_CL_SendStartAndWait(settings);
        
        // Now use the received settings from server
        if (received_settings_valid)
        {
            // Copy all settings from what the server sent us
            settings->consoleplayer = received_settings.consoleplayer;
            settings->num_players = received_settings.num_players;
            settings->deathmatch = received_settings.deathmatch;
            settings->episode = received_settings.episode;
            settings->map = received_settings.map;
            settings->skill = received_settings.skill;
            settings->nomonsters = received_settings.nomonsters;
            settings->fast_monsters = received_settings.fast_monsters;
            settings->respawn_monsters = received_settings.respawn_monsters;
            settings->timelimit = received_settings.timelimit;
            settings->loadgame = received_settings.loadgame;
            settings->lowres_turn = received_settings.lowres_turn;
            settings->new_sync = received_settings.new_sync;
            settings->extratics = received_settings.extratics;
            settings->ticdup = received_settings.ticdup;
            
            for (int i = 0; i < NET_MAXPLAYERS; i++)
                settings->player_classes[i] = received_settings.player_classes[i];
            
            printf("D_StartNetGame: Using network settings - player %d of %d\n",
                   settings->consoleplayer + 1, settings->num_players);
            
            // CRITICAL: Set localplayer so we control our own character!
            localplayer = settings->consoleplayer;
            
            // Set playeringame for all players
            for (int i = 0; i < NET_MAXPLAYERS; i++)
                local_playeringame[i] = i < settings->num_players;
        }
        else
        {
            // Server didn't send game settings - likely a game is already in progress
            // or there was a connection issue. Exit cleanly with error message.
            I_Error("Failed to start network game!\n\n"
                    "The server did not send game settings.\n"
                    "Another game may already be in progress.\n"
                    "Try again or connect to a different server.");
        }
    }
    else
#endif
    {
        // Single player defaults
        settings->consoleplayer = 0;
        settings->num_players = 1;
        settings->player_classes[0] = player_class;
        settings->new_sync = 0;
        settings->extratics = 1;
        settings->ticdup = 1;
        localplayer = 0;
        local_playeringame[0] = true;
    }

    ticdup = settings->ticdup;
    new_sync = settings->new_sync;
#endif
}

boolean D_InitNetGame(net_connect_data_t *connect_data)
{
    boolean result = false;
#ifdef FEATURE_MULTIPLAYER
    net_addr_t *addr = NULL;
    int i;
#endif

    // Call D_QuitNetGame on exit:

    I_AtExit(D_QuitNetGame, true);

    player_class = connect_data->player_class;

#ifdef FEATURE_MULTIPLAYER

    //!
    // @category net
    //
    // Start a multiplayer server, listening for connections.
    //

    if (M_CheckParm("-server") > 0
     || M_CheckParm("-privateserver") > 0)
    {
        NET_SV_Init();
        NET_SV_AddModule(&net_loop_server_module);
        NET_SV_AddModule(&net_socket_module);
        NET_SV_RegisterWithMaster();

        net_loop_client_module.InitClient();
        addr = net_loop_client_module.ResolveAddress(NULL);
    }
    else
    {
        //!
        // @arg <name>
        // @category net
        //
        // Set the player name for multiplayer games.
        // Parse this early so it applies to all connection modes.
        //

        i = M_CheckParmWithArgs("-name", 1);

        if (i > 0)
        {
            net_player_name = myargv[i+1];
        }

        //!
        // @category net
        //
        // Automatically search the local LAN for a multiplayer
        // server and join it.
        //

        i = M_CheckParm("-autojoin");

        if (i > 0)
        {
            addr = NET_FindLANServer();

            if (addr == NULL)
            {
                I_Error("No server found on local LAN");
            }
        }

        //!
        // @category net
        //
        // Show server browser UI to select a server to join.
        //

        i = M_CheckParm("-browse");

        if (i > 0)
        {
            const char *selected_addr = NULL;
            int input;
            boolean stay_in_browser = true;
            
            printf("Starting server browser...\n");
            
            // Initialize browser once
            DG_Browser_Init();
            
            // Main browser loop - returns here if user quits lobby
            while (stay_in_browser)
            {
                selected_addr = NULL;
                addr = NULL;
                
                DG_Browser_Refresh();
                
                // Browser selection loop
                while (1)
                {
                    DG_Browser_Update();
                    DG_DrawBrowser();
                    
                    input = DG_CheckLobbyInput();
                    
                    if (input == 1)  // Green button - select/join
                    {
                        selected_addr = DG_Browser_GetSelectedAddress();
                        if (selected_addr != NULL)
                        {
                            printf("Selected server: %s\n", selected_addr);
                            DG_DrawConnecting(selected_addr);
                            break;
                        }
                    }
                    else if (input == -1)  // Red button - exit game
                    {
                        printf("Browser cancelled\n");
                        DG_DrawExiting();
                        I_Quit();
                    }
                    else if (input == 2)  // Up
                    {
                        DG_Browser_SelectUp();
                    }
                    else if (input == 3)  // Down
                    {
                        DG_Browser_SelectDown();
                    }
                    
                    I_Sleep(16);  // ~60fps
                }
                
                if (selected_addr != NULL)
                {
                    net_socket_module.InitClient();
                    addr = net_socket_module.ResolveAddress((char *)selected_addr);
                    
                    if (addr == NULL)
                    {
                        printf("Unable to resolve '%s', returning to browser\n", selected_addr);
                        continue;  // Back to browser
                    }
                    
                    // Try to connect
                    if (!NET_CL_Connect(addr, connect_data))
                    {
                        printf("Failed to connect to %s, returning to browser\n", selected_addr);
                        continue;  // Back to browser
                    }
                    
                    printf("Connected to %s, entering lobby...\n", NET_AddrToString(addr));
                    
                    // Wait in lobby - returns false if user presses RED to quit
                    if (NET_WaitForLaunch())
                    {
                        // Game is starting!
                        stay_in_browser = false;
                        result = true;
                    }
                    else
                    {
                        // User quit lobby - go back to browser
                        printf("Returning to server browser...\n");
                        // Connection already closed by NET_WaitForLaunch
                        // Reset query system so it reinitializes the socket
                        NET_Query_Shutdown();
                    }
                }
            }
            
            // Skip the normal connect/wait flow below since we handled it
            return result;
        }

        //!
        // @category net
        //
        // Automatically find and join the best available server.
        // Uses smart matching to prefer servers with 1-2 players waiting.
        //

        i = M_CheckParm("-automatch");

        if (i > 0)
        {
            const char *best_addr = NULL;
            int retry;
            boolean stay_in_automatch = true;
            
            while (stay_in_automatch)
            {
                best_addr = NULL;
                
                printf("Starting auto-matchmaking...\n");
                DG_DrawAutoMatch();
                
                // Try up to 3 times to find a server
                for (retry = 0; retry < 3 && best_addr == NULL; retry++)
                {
                    if (retry > 0)
                    {
                        printf("Retry %d/3...\n", retry + 1);
                        I_Sleep(500);  // Brief pause before retry
                    }
                    
                    // Initialize and query servers
                    NET_Query_Init();
                    NET_Query_RunAll();  // Blocking query
                    
                    // Find best server
                    best_addr = DG_Browser_GetAutoMatchAddress();
                }
                
                if (best_addr == NULL)
                {
                    printf("No suitable server found after 3 attempts!\n");
                    DG_DrawNoServers();
                    
                    // Wait for input then exit
                    while (DG_CheckLobbyInput() == 0)
                    {
                        I_Sleep(50);
                    }
                    I_Quit();
                }
                
                printf("Auto-matched to: %s\n", best_addr);
                DG_DrawConnecting(best_addr);
                
                net_socket_module.InitClient();
                addr = net_socket_module.ResolveAddress((char *)best_addr);
                
                if (addr == NULL)
                {
                    I_Error("Unable to resolve '%s'\n", best_addr);
                }
                
                // Connect and wait in lobby
                if (!NET_CL_Connect(addr, connect_data))
                {
                    printf("Failed to connect, retrying...\n");
                    NET_Query_Shutdown();
                    continue;
                }
                
                printf("Connected to %s, entering lobby...\n", NET_AddrToString(addr));
                
                // Wait in lobby - returns false if user quits
                if (NET_WaitForLaunch())
                {
                    // Game is starting!
                    stay_in_automatch = false;
                    result = true;
                }
                else
                {
                    // User quit lobby - search for another server
                    printf("Returning to automatch...\n");
                    NET_Query_Shutdown();
                }
            }
            
            // Skip the normal connect/wait flow below
            return result;
        }

        //!
        // @arg <address>
        // @category net
        //
        // Connect to a multiplayer server running on the given
        // address.
        //

        i = M_CheckParmWithArgs("-connect", 1);

        if (i > 0)
        {
            net_socket_module.InitClient();
            addr = net_socket_module.ResolveAddress(myargv[i+1]);

            if (addr == NULL)
            {
                I_Error("Unable to resolve '%s'\n", myargv[i+1]);
            }
        }
    }

    if (addr != NULL)
    {
        if (M_CheckParm("-drone") > 0)
        {
            connect_data->drone = true;
        }

        if (!NET_CL_Connect(addr, connect_data))
        {
            I_Error("D_InitNetGame: Failed to connect to %s\n",
                    NET_AddrToString(addr));
        }

        printf("D_InitNetGame: Connected to %s\n", NET_AddrToString(addr));

        // Wait for launch message received from server.
        // Returns false if user quit the lobby with RED button.

        if (NET_WaitForLaunch())
        {
            result = true;
        }
        else
        {
            printf("D_InitNetGame: User quit lobby, exiting.\n");
            I_Quit();
        }
    }
#endif

    return result;
}


//
// D_QuitNetGame
// Called before quitting to leave a net game
// without hanging the other players
//
void D_QuitNetGame (void)
{
#ifdef FEATURE_MULTIPLAYER
    NET_SV_Shutdown();
    NET_CL_Disconnect();
#endif
}

static int GetLowTic(void)
{
    int lowtic;

    lowtic = maketic;

#ifdef FEATURE_MULTIPLAYER
    if (net_client_connected)
    {
        if (drone || recvtic < lowtic)
        {
            lowtic = recvtic;
        }
    }
#endif

    return lowtic;
}

static int frameon;
static int frameskip[4];
static int oldnettics;

static void OldNetSync(void)
{
    unsigned int i;
    int keyplayer = -1;

    frameon++;

    // ideally maketic should be 1 - 3 tics above lowtic
    // if we are consistantly slower, speed up time

    for (i=0 ; i<NET_MAXPLAYERS ; i++)
    {
        if (local_playeringame[i])
        {
            keyplayer = i;
            break;
        }
    }

    if (keyplayer < 0)
    {
        // If there are no players, we can never advance anyway

        return;
    }

    if (localplayer == keyplayer)
    {
        // the key player does not adapt
    }
    else
    {
        if (maketic <= recvtic)
        {
            lasttime--;
            // printf ("-");
        }

        frameskip[frameon & 3] = oldnettics > recvtic;
        oldnettics = maketic;

        if (frameskip[0] && frameskip[1] && frameskip[2] && frameskip[3])
        {
            skiptics = 1;
            // printf ("+");
        }
    }
}

// Returns true if there are players in the game:

static boolean PlayersInGame(void)
{
    boolean result = false;
    unsigned int i;

    // If we are connected to a server, check if there are any players
    // in the game.

    if (net_client_connected)
    {
        for (i = 0; i < NET_MAXPLAYERS; ++i)
        {
            result = result || local_playeringame[i];
        }
    }

    // Whether single or multi-player, unless we are running as a drone,
    // we are in the game.

    if (!drone)
    {
        result = true;
    }

    return result;
}

// When using ticdup, certain values must be cleared out when running
// the duplicate ticcmds.

static void TicdupSquash(ticcmd_set_t *set)
{
    ticcmd_t *cmd;
    unsigned int i;

    for (i = 0; i < NET_MAXPLAYERS ; ++i)
    {
        cmd = &set->cmds[i];
        cmd->chatchar = 0;
        if (cmd->buttons & BT_SPECIAL)
            cmd->buttons = 0;
    }
}

// When running in single player mode, clear all the ingame[] array
// except the local player.

static void SinglePlayerClear(ticcmd_set_t *set)
{
    unsigned int i;

    for (i = 0; i < NET_MAXPLAYERS; ++i)
    {
        if (i != localplayer)
        {
            set->ingame[i] = false;
        }
    }
}

//
// TryRunTics
//

void TryRunTics (void)
{
    int	i;
    int	lowtic;
    int	entertic;
    static int oldentertics;
    int realtics;
    int	availabletics;
    int	counts;

    // get real tics
    entertic = I_GetTime() / ticdup;
    realtics = entertic - oldentertics;
    oldentertics = entertic;

    // in singletics mode, run a single tic every time this function
    // is called.

    if (singletics)
    {
        BuildNewTic();
    }
    else
    {
        NetUpdate ();
    }

    lowtic = GetLowTic();

    availabletics = lowtic - gametic/ticdup;

    // decide how many tics to run

    if (new_sync)
    {
	counts = availabletics;
    }
    else
    {
        // decide how many tics to run
        if (realtics < availabletics-1)
            counts = realtics+1;
        else if (realtics < availabletics)
            counts = realtics;
        else
            counts = availabletics;

        if (counts < 1)
            counts = 1;

        if (net_client_connected)
        {
            OldNetSync();
        }
    }

    if (counts < 1)
	counts = 1;

    // wait for new tics if needed

    while (!PlayersInGame() || lowtic < gametic/ticdup + counts)
    {
	NetUpdate ();

        lowtic = GetLowTic();

	if (lowtic < gametic/ticdup)
	    I_Error ("TryRunTics: lowtic < gametic");

        // Don't stay in this loop forever.  The menu is still running,
        // so return to update the screen

	if (I_GetTime() / ticdup - entertic > 0)
	{
	    return;
	}

        I_Sleep(1);
    }

    // run the count * ticdup dics
    while (counts--)
    {
        ticcmd_set_t *set;

        if (!PlayersInGame())
        {
            return;
        }

        set = &ticdata[(gametic / ticdup) % BACKUPTICS];

        if (!net_client_connected)
        {
            SinglePlayerClear(set);
        }

	for (i=0 ; i<ticdup ; i++)
	{
            if (gametic/ticdup > lowtic)
                I_Error ("gametic>lowtic");

            memcpy(local_playeringame, set->ingame, sizeof(local_playeringame));

            loop_interface->RunTic(set->cmds, set->ingame);
	    gametic++;

	    // modify command for duplicated tics

            TicdupSquash(set);
	}

	NetUpdate ();	// check for new console commands
    }
}

void D_RegisterLoopCallbacks(loop_interface_t *i)
{
    loop_interface = i;
}
