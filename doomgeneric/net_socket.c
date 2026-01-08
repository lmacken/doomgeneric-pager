//
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// POSIX socket-based network module (replaces SDL_net)
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "doomtype.h"
#include "net_defs.h"
#include "net_packet.h"
#include "net_socket.h"

#define DEFAULT_PORT 2342  // Chocolate Doom default port

// Socket address wrapper
typedef struct
{
    struct sockaddr_in addr;
    int refcount;
} socket_addr_t;

static int client_socket = -1;
static int server_socket = -1;

// Set socket to non-blocking mode
static boolean SetNonBlocking(int sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0)
        return false;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static boolean NET_Socket_InitClient(void)
{
    client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_socket < 0)
    {
        fprintf(stderr, "NET_Socket_InitClient: Failed to create socket\n");
        return false;
    }

    if (!SetNonBlocking(client_socket))
    {
        fprintf(stderr, "NET_Socket_InitClient: Failed to set non-blocking\n");
        close(client_socket);
        client_socket = -1;
        return false;
    }

    return true;
}

static boolean NET_Socket_InitServer(void)
{
    struct sockaddr_in addr;

    server_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (server_socket < 0)
    {
        fprintf(stderr, "NET_Socket_InitServer: Failed to create socket\n");
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(DEFAULT_PORT);

    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        fprintf(stderr, "NET_Socket_InitServer: Failed to bind to port %d\n", DEFAULT_PORT);
        close(server_socket);
        server_socket = -1;
        return false;
    }

    if (!SetNonBlocking(server_socket))
    {
        fprintf(stderr, "NET_Socket_InitServer: Failed to set non-blocking\n");
        close(server_socket);
        server_socket = -1;
        return false;
    }

    return true;
}

static void NET_Socket_SendPacket(net_addr_t *addr, net_packet_t *packet)
{
    socket_addr_t *sockaddr;
    int sock;
    ssize_t result;

    sockaddr = (socket_addr_t *)addr->handle;

    // Use server socket if available, otherwise client socket
    sock = (server_socket >= 0) ? server_socket : client_socket;
    if (sock < 0)
    {
        fprintf(stderr, "NET_Socket_SendPacket: No socket available\n");
        return;
    }

    result = sendto(sock, packet->data, packet->len, 0,
           (struct sockaddr *)&sockaddr->addr, sizeof(sockaddr->addr));
    
    (void)result;  // Suppress unused warning - logging removed for performance
}

static boolean NET_Socket_RecvPacket(net_addr_t **addr, net_packet_t **packet)
{
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
    ssize_t len;
    int sock;
    byte buffer[2048];
    socket_addr_t *sockaddr;
    net_addr_t *netaddr;

    // Use server socket if available, otherwise client socket
    sock = (server_socket >= 0) ? server_socket : client_socket;
    if (sock < 0)
    {
        // No socket - single-player mode, silently return
        return false;
    }

    len = recvfrom(sock, buffer, sizeof(buffer), 0,
                   (struct sockaddr *)&from_addr, &from_len);

    if (len < 0)
    {
        // No data available (would block)
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return false;
        fprintf(stderr, "NET_Socket_RecvPacket: recvfrom error: %s\n", strerror(errno));
        return false;
    }

    if (len == 0)
        return false;

    // Create packet
    *packet = NET_NewPacket(len);
    if (*packet == NULL)
        return false;

    memcpy((*packet)->data, buffer, len);
    (*packet)->len = len;

    // Create address
    sockaddr = malloc(sizeof(socket_addr_t));
    if (sockaddr == NULL)
    {
        NET_FreePacket(*packet);
        return false;
    }

    memcpy(&sockaddr->addr, &from_addr, sizeof(from_addr));
    sockaddr->refcount = 1;

    netaddr = malloc(sizeof(net_addr_t));
    if (netaddr == NULL)
    {
        free(sockaddr);
        NET_FreePacket(*packet);
        return false;
    }

    netaddr->module = &net_socket_module;
    netaddr->handle = sockaddr;
    *addr = netaddr;

    return true;
}

static void NET_Socket_AddrToString(net_addr_t *addr, char *buffer, int buffer_len)
{
    socket_addr_t *sockaddr = (socket_addr_t *)addr->handle;

    snprintf(buffer, buffer_len, "%s:%d",
             inet_ntoa(sockaddr->addr.sin_addr),
             ntohs(sockaddr->addr.sin_port));
}

static void NET_Socket_FreeAddress(net_addr_t *addr)
{
    socket_addr_t *sockaddr = (socket_addr_t *)addr->handle;

    sockaddr->refcount--;
    if (sockaddr->refcount <= 0)
    {
        free(sockaddr);
    }
    free(addr);
}

static net_addr_t *NET_Socket_ResolveAddress(char *address)
{
    socket_addr_t *sockaddr;
    net_addr_t *netaddr;
    struct hostent *host;
    char *addr_str;
    char *port_str;
    char addr_copy[256];
    int port = DEFAULT_PORT;

    // Parse address:port
    strncpy(addr_copy, address, sizeof(addr_copy) - 1);
    addr_copy[sizeof(addr_copy) - 1] = '\0';

    addr_str = addr_copy;
    port_str = strchr(addr_copy, ':');
    if (port_str != NULL)
    {
        *port_str = '\0';
        port_str++;
        port = atoi(port_str);
    }

    sockaddr = malloc(sizeof(socket_addr_t));
    if (sockaddr == NULL)
        return NULL;

    memset(&sockaddr->addr, 0, sizeof(sockaddr->addr));
    sockaddr->addr.sin_family = AF_INET;
    sockaddr->addr.sin_port = htons(port);
    sockaddr->refcount = 1;

    // Try as IP address first
    if (inet_aton(addr_str, &sockaddr->addr.sin_addr) == 0)
    {
        // Try DNS lookup
        host = gethostbyname(addr_str);
        if (host == NULL)
        {
            fprintf(stderr, "NET_Socket_ResolveAddress: Failed to resolve '%s'\n", addr_str);
            free(sockaddr);
            return NULL;
        }
        memcpy(&sockaddr->addr.sin_addr, host->h_addr_list[0], host->h_length);
    }

    netaddr = malloc(sizeof(net_addr_t));
    if (netaddr == NULL)
    {
        free(sockaddr);
        return NULL;
    }

    netaddr->module = &net_socket_module;
    netaddr->handle = sockaddr;

    return netaddr;
}

void NET_Socket_Shutdown(void)
{
    if (client_socket >= 0)
    {
        close(client_socket);
        client_socket = -1;
    }
    if (server_socket >= 0)
    {
        close(server_socket);
        server_socket = -1;
    }
}

net_module_t net_socket_module =
{
    NET_Socket_InitClient,
    NET_Socket_InitServer,
    NET_Socket_SendPacket,
    NET_Socket_RecvPacket,
    NET_Socket_AddrToString,
    NET_Socket_FreeAddress,
    NET_Socket_ResolveAddress,
};

