//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Loopback network module - for local server in same process
//

#include <stdlib.h>
#include <string.h>

#include "doomtype.h"
#include "net_defs.h"
#include "net_loop.h"
#include "net_packet.h"

#define MAX_QUEUE_SIZE 16

typedef struct
{
    net_packet_t *packets[MAX_QUEUE_SIZE];
    int head;
    int tail;
} packet_queue_t;

static packet_queue_t client_queue;
static packet_queue_t server_queue;
static boolean client_initialized = false;
static boolean server_initialized = false;

// Loopback address
static net_addr_t loopback_addr;

static void QueueInit(packet_queue_t *queue)
{
    queue->head = 0;
    queue->tail = 0;
}

static void QueuePush(packet_queue_t *queue, net_packet_t *packet)
{
    int next = (queue->head + 1) % MAX_QUEUE_SIZE;

    if (next == queue->tail)
    {
        // Queue full, drop oldest
        NET_FreePacket(queue->packets[queue->tail]);
        queue->tail = (queue->tail + 1) % MAX_QUEUE_SIZE;
    }

    queue->packets[queue->head] = NET_PacketDup(packet);
    queue->head = next;
}

static net_packet_t *QueuePop(packet_queue_t *queue)
{
    net_packet_t *packet;

    if (queue->head == queue->tail)
        return NULL;

    packet = queue->packets[queue->tail];
    queue->tail = (queue->tail + 1) % MAX_QUEUE_SIZE;

    return packet;
}

// Client module functions

static boolean NET_Loop_Client_InitClient(void)
{
    QueueInit(&client_queue);
    client_initialized = true;
    return true;
}

static boolean NET_Loop_Client_InitServer(void)
{
    return false;  // Client module can't be server
}

static void NET_Loop_Client_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    if (server_initialized)
    {
        QueuePush(&server_queue, packet);
    }
}

static boolean NET_Loop_Client_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    *packet = QueuePop(&client_queue);

    if (*packet == NULL)
        return false;

    *addr = &loopback_addr;
    return true;
}

static void NET_Loop_Client_AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    snprintf(buffer, buffer_len, "loopback");
}

static void NET_Loop_Client_FreeAddress(net_addr_t *addr)
{
    // Loopback address is static
}

static net_addr_t *NET_Loop_Client_ResolveAddress(char *addr)
{
    if (addr == NULL)
    {
        loopback_addr.module = &net_loop_client_module;
        loopback_addr.handle = NULL;
        return &loopback_addr;
    }
    return NULL;
}

// Server module functions

static boolean NET_Loop_Server_InitClient(void)
{
    return false;  // Server module can't be client
}

static boolean NET_Loop_Server_InitServer(void)
{
    QueueInit(&server_queue);
    server_initialized = true;
    return true;
}

static void NET_Loop_Server_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    if (client_initialized)
    {
        QueuePush(&client_queue, packet);
    }
}

static boolean NET_Loop_Server_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    *packet = QueuePop(&server_queue);

    if (*packet == NULL)
        return false;

    *addr = &loopback_addr;
    return true;
}

static void NET_Loop_Server_AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    snprintf(buffer, buffer_len, "loopback");
}

static void NET_Loop_Server_FreeAddress(net_addr_t *addr)
{
    // Loopback address is static
}

static net_addr_t *NET_Loop_Server_ResolveAddress(char *addr)
{
    return NULL;
}

net_module_t net_loop_client_module =
{
    NET_Loop_Client_InitClient,
    NET_Loop_Client_InitServer,
    NET_Loop_Client_SendPacket,
    NET_Loop_Client_RecvPacket,
    NET_Loop_Client_AddrToString,
    NET_Loop_Client_FreeAddress,
    NET_Loop_Client_ResolveAddress,
};

net_module_t net_loop_server_module =
{
    NET_Loop_Server_InitClient,
    NET_Loop_Server_InitServer,
    NET_Loop_Server_SendPacket,
    NET_Loop_Server_RecvPacket,
    NET_Loop_Server_AddrToString,
    NET_Loop_Server_FreeAddress,
    NET_Loop_Server_ResolveAddress,
};



