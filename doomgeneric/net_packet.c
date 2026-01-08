//
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Network packet buffer management
//

#include <stdlib.h>
#include <string.h>

#include "net_packet.h"

net_packet_t *NET_NewPacket(int initial_size)
{
    net_packet_t *packet;

    packet = malloc(sizeof(net_packet_t));
    if (packet == NULL)
        return NULL;

    packet->alloced = initial_size;
    packet->data = malloc(initial_size);
    if (packet->data == NULL)
    {
        free(packet);
        return NULL;
    }

    packet->len = 0;
    packet->pos = 0;

    return packet;
}

net_packet_t *NET_PacketDup(net_packet_t *packet)
{
    net_packet_t *newpacket;

    newpacket = NET_NewPacket(packet->len);
    if (newpacket == NULL)
        return NULL;

    memcpy(newpacket->data, packet->data, packet->len);
    newpacket->len = packet->len;

    return newpacket;
}

void NET_FreePacket(net_packet_t *packet)
{
    free(packet->data);
    free(packet);
}

// Ensure packet has enough space for 'size' more bytes
static boolean NET_EnsureSpace(net_packet_t *packet, size_t size)
{
    size_t newsize;
    byte *newdata;

    if (packet->len + size <= packet->alloced)
        return true;

    // Need to grow the buffer
    newsize = packet->alloced * 2;
    while (newsize < packet->len + size)
        newsize *= 2;

    newdata = realloc(packet->data, newsize);
    if (newdata == NULL)
        return false;

    packet->data = newdata;
    packet->alloced = newsize;
    return true;
}

// Read functions
boolean NET_ReadInt8(net_packet_t *packet, unsigned int *data)
{
    if (packet->pos + 1 > packet->len)
        return false;

    *data = packet->data[packet->pos];
    packet->pos += 1;
    return true;
}

boolean NET_ReadInt16(net_packet_t *packet, unsigned int *data)
{
    if (packet->pos + 2 > packet->len)
        return false;

    // Big-endian (network byte order) - high byte first
    *data = (packet->data[packet->pos] << 8)
          | packet->data[packet->pos + 1];
    packet->pos += 2;
    return true;
}

boolean NET_ReadInt32(net_packet_t *packet, unsigned int *data)
{
    if (packet->pos + 4 > packet->len)
        return false;

    // Big-endian (network byte order) - highest byte first
    *data = (packet->data[packet->pos] << 24)
          | (packet->data[packet->pos + 1] << 16)
          | (packet->data[packet->pos + 2] << 8)
          | packet->data[packet->pos + 3];
    packet->pos += 4;
    return true;
}

boolean NET_ReadSInt8(net_packet_t *packet, signed int *data)
{
    unsigned int udata;
    if (!NET_ReadInt8(packet, &udata))
        return false;
    *data = (signed char)udata;
    return true;
}

boolean NET_ReadSInt16(net_packet_t *packet, signed int *data)
{
    unsigned int udata;
    if (!NET_ReadInt16(packet, &udata))
        return false;
    *data = (signed short)udata;
    return true;
}

boolean NET_ReadSInt32(net_packet_t *packet, signed int *data)
{
    if (!NET_ReadInt32(packet, (unsigned int *)data))
        return false;
    return true;
}

char *NET_ReadString(net_packet_t *packet)
{
    char *start;

    start = (char *)packet->data + packet->pos;

    // Search for NUL terminator
    while (packet->pos < packet->len && packet->data[packet->pos] != '\0')
        packet->pos++;

    if (packet->pos >= packet->len)
        return NULL;  // No terminator found

    packet->pos++;  // Skip the NUL
    return start;
}

// Write functions
void NET_WriteInt8(net_packet_t *packet, unsigned int i)
{
    if (!NET_EnsureSpace(packet, 1))
        return;

    packet->data[packet->len] = i & 0xff;
    packet->len += 1;
}

void NET_WriteInt16(net_packet_t *packet, unsigned int i)
{
    if (!NET_EnsureSpace(packet, 2))
        return;

    // Big-endian (network byte order) - high byte first
    packet->data[packet->len] = (i >> 8) & 0xff;
    packet->data[packet->len + 1] = i & 0xff;
    packet->len += 2;
}

void NET_WriteInt32(net_packet_t *packet, unsigned int i)
{
    if (!NET_EnsureSpace(packet, 4))
        return;

    // Big-endian (network byte order) - highest byte first
    packet->data[packet->len] = (i >> 24) & 0xff;
    packet->data[packet->len + 1] = (i >> 16) & 0xff;
    packet->data[packet->len + 2] = (i >> 8) & 0xff;
    packet->data[packet->len + 3] = i & 0xff;
    packet->len += 4;
}

void NET_WriteString(net_packet_t *packet, char *string)
{
    size_t len = strlen(string) + 1;

    if (!NET_EnsureSpace(packet, len))
        return;

    memcpy(packet->data + packet->len, string, len);
    packet->len += len;
}



