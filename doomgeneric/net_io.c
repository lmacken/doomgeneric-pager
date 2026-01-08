//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Network I/O layer - manages network contexts and modules
//

#include <stdlib.h>
#include <string.h>

#include "net_io.h"
#include "net_packet.h"

#define MAX_MODULES 16

struct _net_context_s
{
    net_module_t *modules[MAX_MODULES];
    int num_modules;
};

// Broadcast address placeholder
net_addr_t net_broadcast_addr;

net_context_t *NET_NewContext(void)
{
    net_context_t *context;

    context = malloc(sizeof(net_context_t));
    if (context == NULL)
        return NULL;

    context->num_modules = 0;
    return context;
}

void NET_AddModule(net_context_t *context, net_module_t *module)
{
    if (context->num_modules >= MAX_MODULES)
        return;

    context->modules[context->num_modules] = module;
    context->num_modules++;
}

void NET_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    if (addr == NULL || addr->module == NULL)
        return;

    addr->module->SendPacket(addr, packet);
}

void NET_SendBroadcast(net_context_t *context, net_packet_t *packet)
{
    // For now, broadcast is not implemented
    // Would need to send to 255.255.255.255 on each module
}

boolean NET_RecvPacket(net_context_t *context, net_addr_t **addr,
                       net_packet_t **packet)
{
    int i;

    for (i = 0; i < context->num_modules; i++)
    {
        if (context->modules[i]->RecvPacket(addr, packet))
            return true;
    }

    return false;
}

char *NET_AddrToString(net_addr_t *addr)
{
    static char buffer[128];

    if (addr == NULL || addr->module == NULL)
    {
        strcpy(buffer, "(null)");
        return buffer;
    }

    addr->module->AddrToString(addr, buffer, sizeof(buffer));
    return buffer;
}

void NET_FreeAddress(net_addr_t *addr)
{
    if (addr == NULL || addr->module == NULL)
        return;

    addr->module->FreeAddress(addr);
}

net_addr_t *NET_ResolveAddress(net_context_t *context, char *address)
{
    net_addr_t *result;
    int i;

    for (i = 0; i < context->num_modules; i++)
    {
        result = context->modules[i]->ResolveAddress(address);
        if (result != NULL)
            return result;
    }

    return NULL;
}



