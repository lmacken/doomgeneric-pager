//
// Copyright(C) 2026 WiFi Pineapple Pager port
//
// Simple lobby screen for multiplayer waiting room
//

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>

#include "doomtype.h"
#include "net_defs.h"
#include "net_query.h"

// Embedded 8x8 font (basic ASCII 32-127)
// Simple monospace bitmap font for lobby text
static const unsigned char font8x8[][8] = {
    // Space (32)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (33)
    {0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00},
    // " (34)
    {0x6C, 0x6C, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00},
    // # (35)
    {0x6C, 0xFE, 0x6C, 0x6C, 0xFE, 0x6C, 0x00, 0x00},
    // $ (36)
    {0x18, 0x7E, 0xC0, 0x7C, 0x06, 0xFC, 0x18, 0x00},
    // % (37)
    {0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00, 0x00},
    // & (38)
    {0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00},
    // ' (39)
    {0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ( (40)
    {0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00},
    // ) (41)
    {0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00},
    // * (42)
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    // + (43)
    {0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00},
    // , (44)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30},
    // - (45)
    {0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00},
    // . (46)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00},
    // / (47)
    {0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x00, 0x00},
    // 0 (48)
    {0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C, 0x00},
    // 1 (49)
    {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
    // 2 (50)
    {0x7C, 0xC6, 0x06, 0x1C, 0x30, 0x66, 0xFE, 0x00},
    // 3 (51)
    {0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00},
    // 4 (52)
    {0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x1E, 0x00},
    // 5 (53)
    {0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00},
    // 6 (54)
    {0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00},
    // 7 (55)
    {0xFE, 0xC6, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00},
    // 8 (56)
    {0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00},
    // 9 (57)
    {0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00},
    // : (58)
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00},
    // ; (59)
    {0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30},
    // < (60)
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    // = (61)
    {0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00},
    // > (62)
    {0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00},
    // ? (63)
    {0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00},
    // @ (64)
    {0x7C, 0xC6, 0xDE, 0xDE, 0xDE, 0xC0, 0x78, 0x00},
    // A-Z (65-90)
    {0x38, 0x6C, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, // A
    {0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00}, // B
    {0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00}, // C
    {0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00}, // D
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00}, // E
    {0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00}, // F
    {0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3E, 0x00}, // G
    {0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0x00}, // H
    {0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // I
    {0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00}, // J
    {0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00}, // K
    {0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00}, // L
    {0xC6, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0x00}, // M
    {0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00}, // N
    {0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // O
    {0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00}, // P
    {0x7C, 0xC6, 0xC6, 0xC6, 0xD6, 0xDE, 0x7C, 0x06}, // Q
    {0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00}, // R
    {0x7C, 0xC6, 0x60, 0x38, 0x0C, 0xC6, 0x7C, 0x00}, // S
    {0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // T
    {0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // U
    {0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00}, // V
    {0xC6, 0xC6, 0xD6, 0xFE, 0xFE, 0xEE, 0xC6, 0x00}, // W
    {0xC6, 0x6C, 0x38, 0x38, 0x38, 0x6C, 0xC6, 0x00}, // X
    {0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x3C, 0x00}, // Y
    {0xFE, 0xC6, 0x8C, 0x18, 0x32, 0x66, 0xFE, 0x00}, // Z
    // [ (91)
    {0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00},
    // \ (92)
    {0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00},
    // ] (93)
    {0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00},
    // ^ (94)
    {0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00},
    // _ (95)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    // ` (96)
    {0x30, 0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00},
    // a-z (97-122) - lowercase
    {0x00, 0x00, 0x78, 0x0C, 0x7C, 0xCC, 0x76, 0x00}, // a
    {0xE0, 0x60, 0x7C, 0x66, 0x66, 0x66, 0xDC, 0x00}, // b
    {0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC6, 0x7C, 0x00}, // c
    {0x1C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00}, // d
    {0x00, 0x00, 0x7C, 0xC6, 0xFE, 0xC0, 0x7C, 0x00}, // e
    {0x38, 0x6C, 0x60, 0xF0, 0x60, 0x60, 0xF0, 0x00}, // f
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x78}, // g
    {0xE0, 0x60, 0x6C, 0x76, 0x66, 0x66, 0xE6, 0x00}, // h
    {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00}, // i
    {0x06, 0x00, 0x0E, 0x06, 0x06, 0x66, 0x66, 0x3C}, // j
    {0xE0, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0xE6, 0x00}, // k
    {0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00}, // l
    {0x00, 0x00, 0xCC, 0xFE, 0xFE, 0xD6, 0xC6, 0x00}, // m
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00}, // n
    {0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x7C, 0x00}, // o
    {0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0}, // p
    {0x00, 0x00, 0x76, 0xCC, 0xCC, 0x7C, 0x0C, 0x1E}, // q
    {0x00, 0x00, 0xDC, 0x76, 0x66, 0x60, 0xF0, 0x00}, // r
    {0x00, 0x00, 0x7C, 0xC0, 0x7C, 0x06, 0xFC, 0x00}, // s
    {0x30, 0x30, 0xFC, 0x30, 0x30, 0x36, 0x1C, 0x00}, // t
    {0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00}, // u
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00}, // v
    {0x00, 0x00, 0xC6, 0xD6, 0xFE, 0xFE, 0x6C, 0x00}, // w
    {0x00, 0x00, 0xC6, 0x6C, 0x38, 0x6C, 0xC6, 0x00}, // x
    {0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x7C}, // y
    {0x00, 0x00, 0xFE, 0x8C, 0x18, 0x32, 0xFE, 0x00}, // z
};

// Framebuffer access (defined in doomgeneric_linuxvt.c)
extern int fbFd;
extern unsigned int fbWidth, fbHeight;
extern uint16_t *renderBuffer;
extern size_t renderBufferSize;

// Virtual screen dimensions after 90° CCW rotation
// Physical: 222x480, Virtual (for content): 480x222
#define LOBBY_WIDTH  480
#define LOBBY_HEIGHT 222

// RGB565 color definitions
#define RGB565_BLACK    0x0000
#define RGB565_WHITE    0xFFFF
#define RGB565_RED      0xF800
#define RGB565_GREEN    0x07E0
#define RGB565_BLUE     0x001F
#define RGB565_YELLOW   0xFFE0
#define RGB565_CYAN     0x07FF
#define RGB565_MAGENTA  0xF81F
#define RGB565_ORANGE   0xFD20
#define RGB565_DOOM_RED 0xA800  // Dark red for DOOM theme

// Set a pixel with 90° CW rotation (to match game orientation)
// Virtual (x,y) in 480x222 space -> Physical in 222x480 framebuffer
static inline void lobby_set_pixel(int vx, int vy, uint16_t color)
{
    // 90° CW rotation: (vx, vy) -> (LOBBY_HEIGHT-1-vy, vx)
    int px = LOBBY_HEIGHT - 1 - vy;
    int py = vx;
    
    if (px >= 0 && px < (int)fbWidth && py >= 0 && py < (int)fbHeight) {
        renderBuffer[py * fbWidth + px] = color;
    }
}

// Draw a single character at virtual (x,y)
static void lobby_draw_char(int x, int y, char c, uint16_t color)
{
    if (c < 32 || c > 122) c = '?';
    int idx = c - 32;
    if (idx < 0 || idx >= (int)(sizeof(font8x8)/sizeof(font8x8[0])))
        return;
    
    const unsigned char *glyph = font8x8[idx];
    
    // Draw each pixel of the 8x8 glyph
    for (int gy = 0; gy < 8; gy++) {
        unsigned char row = glyph[gy];
        for (int gx = 0; gx < 8; gx++) {
            if (row & (0x80 >> gx)) {
                lobby_set_pixel(x + gx, y + gy, color);
            }
        }
    }
}

// Draw a string at virtual (x,y)
static void lobby_draw_string(int x, int y, const char *str, uint16_t color)
{
    while (*str) {
        lobby_draw_char(x, y, *str, color);
        x += 8;  // 8 pixels per character
        str++;
    }
}

// Draw a centered string (centered in virtual 480-wide space)
static void lobby_draw_centered(int y, const char *str, uint16_t color)
{
    int len = strlen(str);
    int x = (LOBBY_WIDTH - len * 8) / 2;
    lobby_draw_string(x, y, str, color);
}

// Draw a filled rectangle in virtual coordinates
static void lobby_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int vy = y; vy < y + h && vy < LOBBY_HEIGHT; vy++) {
        for (int vx = x; vx < x + w && vx < LOBBY_WIDTH; vx++) {
            if (vx >= 0 && vy >= 0) {
                lobby_set_pixel(vx, vy, color);
            }
        }
    }
}

// Clear the screen
static void lobby_clear(uint16_t color)
{
    for (size_t i = 0; i < fbWidth * fbHeight; i++) {
        renderBuffer[i] = color;
    }
}

// Flush the render buffer to screen
static void lobby_flush(void)
{
    if (fbFd >= 0 && renderBuffer) {
        lseek(fbFd, 0, SEEK_SET);
        write(fbFd, renderBuffer, renderBufferSize);
    }
}

// Draw loading screen with real-time info
void DG_DrawLoadingBrowser(const char *message)
{
    lobby_clear(0x0841);  // Very dark gray background
    
    // Title bar
    lobby_fill_rect(0, 0, LOBBY_WIDTH, 24, RGB565_DOOM_RED);
    lobby_draw_centered(8, "DOOM DEATHMATCH", RGB565_WHITE);
    
    // Message centered on screen
    if (message) {
        lobby_draw_centered(100, message, RGB565_YELLOW);
    }
    
    lobby_flush();
}

// Draw loading screen for auto-match with real-time progress
void DG_DrawLoadingAutoMatch(int servers_found, int ports_scanned)
{
    char buf[64];
    
    lobby_clear(0x0841);  // Very dark gray
    
    // Title bar
    lobby_fill_rect(0, 0, LOBBY_WIDTH, 24, RGB565_DOOM_RED);
    lobby_draw_centered(8, "DOOM DEATHMATCH", RGB565_WHITE);
    
    // Scanning status
    lobby_draw_centered(70, "SCANNING FOR SERVERS", RGB565_YELLOW);
    
    // Real-time stats
    snprintf(buf, sizeof(buf), "Port: %d", DEFAULT_BASE_PORT + ports_scanned - 1);
    lobby_draw_centered(100, buf, RGB565_CYAN);
    
    snprintf(buf, sizeof(buf), "Found: %d server%s", 
             servers_found, servers_found == 1 ? "" : "s");
    lobby_draw_centered(130, buf, servers_found > 0 ? RGB565_GREEN : RGB565_WHITE);
    
    lobby_flush();
}

// Main lobby drawing function - called from NET_WaitForLaunch
// Uses virtual 480x222 coordinate system (rotated 90° CW to physical 222x480)
void DG_DrawLobby(int num_players, int max_players, int is_controller, 
                  const char player_names[NET_MAXPLAYERS][MAXPLAYERNAME],
                  const char player_addrs[NET_MAXPLAYERS][MAXPLAYERNAME],
                  int consoleplayer,
                  const char *server_addr)
{
    char buf[80];
    int y = 15;
    
    // Clear to dark background
    lobby_clear(0x1082);  // Dark gray-blue
    
    // Title with red accent box (full width of virtual screen)
    lobby_fill_rect(0, 5, LOBBY_WIDTH, 35, RGB565_DOOM_RED);
    lobby_draw_centered(10, "DOOM DEATHMATCH LOBBY", RGB565_WHITE);
    
    // Show server address (so users can share with friends)
    if (server_addr && server_addr[0]) {
        snprintf(buf, sizeof(buf), "%s", server_addr);
        lobby_draw_centered(22, buf, RGB565_CYAN);
    }
    
    y = 48;
    
    // Player count
    snprintf(buf, sizeof(buf), "Players: %d / %d", num_players, max_players);
    lobby_draw_centered(y, buf, RGB565_WHITE);
    y += 14;
    
    // Separator line
    lobby_fill_rect(30, y, LOBBY_WIDTH - 60, 2, RGB565_CYAN);
    y += 8;
    
    // Player list (max 4 players in DOOM deathmatch)
    for (int i = 0; i < max_players && i < 4; i++) {
        if (i < num_players) {
            uint16_t color = (i == consoleplayer) ? RGB565_YELLOW : RGB565_GREEN;
            const char *name = player_names[i][0] ? player_names[i] : "Player";
            
            if (i == consoleplayer) {
                snprintf(buf, sizeof(buf), "%d. %s (YOU)", i + 1, name);
            } else {
                snprintf(buf, sizeof(buf), "%d. %s", i + 1, name);
            }
            lobby_draw_centered(y, buf, color);
        } else {
            snprintf(buf, sizeof(buf), "%d. ---", i + 1);
            lobby_draw_centered(y, buf, 0x4208);  // Gray
        }
        y += 20;
    }
    
    // Instructions at bottom
    y = LOBBY_HEIGHT - 40;
    
    if (is_controller) {
        lobby_draw_centered(y, "GREEN = START GAME", RGB565_GREEN);
    } else {
        lobby_draw_centered(y, "Waiting for host...", RGB565_YELLOW);
    }
    y += 12;
    lobby_draw_centered(y, "RED = Quit", RGB565_RED);
    
    // Flush to screen
    lobby_flush();
}

// DG_CheckLobbyInput is defined in doomgeneric_linuxvt.c
// (has access to input file descriptors)

//=============================================================================
// SERVER BROWSER UI
//=============================================================================

#include "net_query.h"
#include "i_timer.h"

// Browser state
static int browser_selection = 0;
static int browser_scroll = 0;
static boolean browser_querying = false;
static unsigned int browser_last_query = 0;
static unsigned int browser_last_refresh = 0;

// Number of servers visible at once
#define BROWSER_VISIBLE_SERVERS 4

// Auto-refresh interval (milliseconds)
#define BROWSER_AUTO_REFRESH_MS 10000

// Browser state enum
typedef enum {
    BROWSER_STATE_IDLE,
    BROWSER_STATE_QUERYING,
    BROWSER_STATE_READY,
    BROWSER_STATE_CONNECTING,
} browser_state_t;

static browser_state_t browser_state = BROWSER_STATE_IDLE;

// Initialize browser
void DG_Browser_Init(void)
{
    browser_selection = 0;
    browser_scroll = 0;
    browser_state = BROWSER_STATE_IDLE;
    browser_last_refresh = 0;
    
    // Show loading screen
    DG_DrawLoadingBrowser("INITIALIZING...");
    
    // Initialize query system
    NET_Query_Init();
}

// Start querying servers (includes auto-discovery)
void DG_Browser_Refresh(void)
{
    browser_state = BROWSER_STATE_QUERYING;
    browser_querying = true;
    browser_last_query = I_GetTimeMS();
    
    // Show loading screen before discovery
    DG_DrawLoadingBrowser("SCANNING FOR SERVERS...");
    
    // NET_Query_StartAll will auto-discover servers if none are configured
    NET_Query_StartAll();
}

// Update browser state (call every frame)
void DG_Browser_Update(void)
{
    unsigned int now = I_GetTimeMS();
    
    if (browser_querying)
    {
        if (NET_Query_Poll())
        {
            browser_querying = false;
            browser_state = BROWSER_STATE_READY;
            browser_last_refresh = now;
        }
    }
    else if (browser_state == BROWSER_STATE_READY)
    {
        // Auto-refresh every 10 seconds
        if (now - browser_last_refresh >= BROWSER_AUTO_REFRESH_MS)
        {
            DG_Browser_Refresh();
        }
    }
}

// Move selection up
void DG_Browser_SelectUp(void)
{
    if (browser_selection > 0)
    {
        browser_selection--;
        if (browser_selection < browser_scroll)
            browser_scroll = browser_selection;
    }
}

// Move selection down
void DG_Browser_SelectDown(void)
{
    int count = NET_Query_GetServerCount();
    if (browser_selection < count - 1)
    {
        browser_selection++;
        if (browser_selection >= browser_scroll + BROWSER_VISIBLE_SERVERS)
            browser_scroll = browser_selection - BROWSER_VISIBLE_SERVERS + 1;
    }
}

// Get selected server address
const char *DG_Browser_GetSelectedAddress(void)
{
    return NET_Query_GetServerAddress(browser_selection);
}

// Get auto-matched server address
const char *DG_Browser_GetAutoMatchAddress(void)
{
    int best = NET_Query_FindBestServer();
    if (best >= 0)
        return NET_Query_GetServerAddress(best);
    return NULL;
}

// Draw the server browser screen
void DG_DrawBrowser(void)
{
    char buf[80];
    int y = 5;
    int server_count = NET_Query_GetServerCount();
    
    // Clear to dark background
    lobby_clear(0x1082);  // Dark gray-blue
    
    // Title bar
    lobby_fill_rect(0, 0, LOBBY_WIDTH, 28, RGB565_DOOM_RED);
    lobby_draw_centered(5, "DOOM SERVER BROWSER", RGB565_WHITE);
    lobby_draw_centered(16, "Select a server to join", RGB565_YELLOW);
    
    y = 35;
    
    // Status line
    if (browser_state == BROWSER_STATE_QUERYING)
    {
        if (server_count == 0)
        {
            lobby_draw_centered(y, "Discovering servers...", RGB565_CYAN);
        }
        else
        {
            lobby_draw_centered(y, "Refreshing...", RGB565_CYAN);
        }
    }
    else
    {
        snprintf(buf, sizeof(buf), "%d servers found", server_count);
        lobby_draw_centered(y, buf, RGB565_WHITE);
    }
    y += 14;
    
    // Separator
    lobby_fill_rect(20, y, LOBBY_WIDTH - 40, 1, RGB565_CYAN);
    y += 6;
    
    // Server list
    int list_start_y = y;
    
    for (int i = 0; i < BROWSER_VISIBLE_SERVERS && (i + browser_scroll) < server_count; i++)
    {
        int server_idx = i + browser_scroll;
        boolean is_selected = (server_idx == browser_selection);
        
        net_querydata_t data;
        char address[64];
        unsigned int ping;
        
        // Draw selection highlight
        if (is_selected)
        {
            lobby_fill_rect(5, y - 2, LOBBY_WIDTH - 10, 36, 0x3186);  // Brighter highlight
            lobby_draw_string(8, y, ">", RGB565_YELLOW);  // Selection arrow
        }
        
        // Get server info
        if (NET_Query_GetServerInfo(server_idx, &data, address, sizeof(address), &ping))
        {
            // Server name (or address if no name)
            const char *name = data.description[0] ? data.description : address;
            uint16_t name_color = is_selected ? RGB565_YELLOW : RGB565_WHITE;
            snprintf(buf, sizeof(buf), "%d. %s", server_idx + 1, name);
            lobby_draw_string(20, y, buf, name_color);
            y += 10;
            
            // Status line: players, state, ping
            const char *state_str;
            uint16_t state_color;
            
            if (data.server_state == 0)  // Waiting
            {
                if (data.num_players >= data.max_players)
                {
                    state_str = "FULL";
                    state_color = RGB565_RED;
                }
                else if (data.num_players > 0)
                {
                    state_str = "WAITING";
                    state_color = RGB565_GREEN;
                }
                else
                {
                    state_str = "EMPTY";
                    state_color = RGB565_CYAN;
                }
            }
            else  // In game
            {
                state_str = "IN GAME";
                state_color = RGB565_ORANGE;
            }
            
            snprintf(buf, sizeof(buf), "   %d/%d players - %s - %dms",
                     data.num_players, data.max_players, state_str, ping);
            lobby_draw_string(20, y, buf, state_color);
            y += 10;
            
            // Server address (so users can share with PC friends)
            snprintf(buf, sizeof(buf), "   %s", address);
            lobby_draw_string(20, y, buf, 0x6B4D);  // Gray
            y += 16;
        }
        else
        {
            // No response from this server
            uint16_t color = is_selected ? RGB565_YELLOW : 0x8410;
            snprintf(buf, sizeof(buf), "%d. %s", server_idx + 1, 
                     NET_Query_GetServerAddress(server_idx));
            lobby_draw_string(20, y, buf, color);
            y += 10;
            
            lobby_draw_string(20, y, "   No response (offline?)", RGB565_RED);
            y += 26;
        }
    }
    
    // Scroll indicators
    if (browser_scroll > 0)
    {
        lobby_draw_string(LOBBY_WIDTH - 30, list_start_y, "^", RGB565_WHITE);
    }
    if (browser_scroll + BROWSER_VISIBLE_SERVERS < server_count)
    {
        lobby_draw_string(LOBBY_WIDTH - 30, list_start_y + (BROWSER_VISIBLE_SERVERS * 36) - 10, 
                          "v", RGB565_WHITE);
    }
    
    // Instructions at bottom
    y = LOBBY_HEIGHT - 32;
    lobby_fill_rect(0, y - 4, LOBBY_WIDTH, 40, 0x1082);
    // Draw instructions with RED colored red
    lobby_draw_string(60, y, "UP/DOWN: Select  GREEN: Join  ", RGB565_GREEN);
    lobby_draw_string(300, y, "RED", RGB565_RED);
    lobby_draw_string(324, y, ": Quit", RGB565_GREEN);
    y += 12;
    
    // Show auto-refresh countdown
    if (browser_state == BROWSER_STATE_READY && browser_last_refresh > 0)
    {
        unsigned int elapsed = I_GetTimeMS() - browser_last_refresh;
        unsigned int remaining = (BROWSER_AUTO_REFRESH_MS - elapsed) / 1000;
        if (remaining > 10) remaining = 10;
        char refresh_buf[32];
        snprintf(refresh_buf, sizeof(refresh_buf), "Auto-refresh in %ds", remaining);
        lobby_draw_centered(y, refresh_buf, 0x8410);
    }
    else
    {
        lobby_draw_centered(y, "Auto-refresh: 10s", 0x8410);
    }
    
    // Flush to screen
    lobby_flush();
}

// Draw "Connecting..." screen
void DG_DrawConnecting(const char *server_addr)
{
    char buf[80];
    
    lobby_clear(0x1082);
    lobby_fill_rect(0, 80, LOBBY_WIDTH, 60, RGB565_DOOM_RED);
    lobby_draw_centered(90, "CONNECTING...", RGB565_WHITE);
    snprintf(buf, sizeof(buf), "%s", server_addr ? server_addr : "...");
    lobby_draw_centered(110, buf, RGB565_YELLOW);
    lobby_flush();
}

// Draw "Auto-matching..." screen  
void DG_DrawAutoMatch(void)
{
    lobby_clear(0x1082);
    
    lobby_fill_rect(0, 50, LOBBY_WIDTH, 120, RGB565_DOOM_RED);
    lobby_draw_centered(55, "AUTO-MATCHMAKING", RGB565_WHITE);
    lobby_draw_centered(75, "Discovering servers...", RGB565_YELLOW);
    
    lobby_draw_centered(100, "Scanning ports for active servers", RGB565_CYAN);
    
    lobby_flush();
}

// Draw "No servers available" screen
void DG_DrawNoServers(void)
{
    lobby_clear(0x1082);
    
    lobby_fill_rect(0, 70, LOBBY_WIDTH, 80, RGB565_DOOM_RED);
    lobby_draw_centered(80, "NO SERVERS AVAILABLE", RGB565_WHITE);
    lobby_draw_centered(100, "All servers are offline,", RGB565_YELLOW);
    lobby_draw_centered(115, "full, or in-game", RGB565_YELLOW);
    
    lobby_draw_centered(160, "GREEN: Try again   RED: Exit", RGB565_CYAN);
    
    lobby_flush();
}

// Draw "Exiting..." screen
void DG_DrawExiting(void)
{
    lobby_clear(0x0000);  // Black background
    
    lobby_fill_rect(0, 90, LOBBY_WIDTH, 40, RGB565_DOOM_RED);
    lobby_draw_centered(100, "EXITING...", RGB565_WHITE);
    
    lobby_flush();
}

