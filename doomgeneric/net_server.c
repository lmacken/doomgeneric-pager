//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Network server code - minimal implementation for client-only mode
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "net_defs.h"
#include "net_server.h"

static net_module_t *server_modules[16];
static int num_server_modules = 0;
static boolean server_initialized = false;

void NET_SV_Init(void)
{
    if (server_initialized)
        return;

    printf("NET_SV_Init: Server mode not fully implemented\n");
    server_initialized = true;
}

void NET_SV_Run(void)
{
    // Server tick - not implemented for client-only mode
}

void NET_SV_Shutdown(void)
{
    server_initialized = false;
}

void NET_SV_AddModule(net_module_t *module)
{
    if (num_server_modules >= 16)
        return;

    server_modules[num_server_modules++] = module;
}

void NET_SV_RegisterWithMaster(void)
{
    // Master server registration - not implemented
    printf("NET_SV_RegisterWithMaster: Not implemented\n");
}



