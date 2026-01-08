//doomgeneric for a bare Linux VirtualTerminal
// Copyright (C) 2025 Techflash
// Based on doomgeneric_sdl.c

#include "doomkeys.h"
#include "m_argv.h"
#include "doomgeneric.h"
#include "i_system.h"
#include "doomdef.h"   // For GS_LEVEL constant
#include "doomstat.h"  // For gamestate variable

// XXX: HACK
// Linux's input-event-codes.h and doomkeys.h have many collisions.
// Redefine some of doomkeys.h's names here to work around this.
// I could try to redefine Linux's... but that sounds incredibly
// fragile, and is very likely not a good idea.
#undef KEY_TAB
#undef KEY_ENTER
#undef KEY_BACKSPACE
#undef KEY_MINUS
#undef KEY_F1
#undef KEY_F2
#undef KEY_F3
#undef KEY_F4
#undef KEY_F5
#undef KEY_F6
#undef KEY_F7
#undef KEY_F8
#undef KEY_F9
#undef KEY_F10
#undef KEY_F11
#define DOOM_KEY_TAB		9
#define DOOM_KEY_ENTER		13
#define DOOM_KEY_MINUS		0x2d
#define DOOM_KEY_BACKSPACE	0x7f
#define DOOM_KEY_F1		(0x80+0x3b)
#define DOOM_KEY_F2		(0x80+0x3c)
#define DOOM_KEY_F3		(0x80+0x3d)
#define DOOM_KEY_F4		(0x80+0x3e)
#define DOOM_KEY_F5		(0x80+0x3f)
#define DOOM_KEY_F6		(0x80+0x40)
#define DOOM_KEY_F7		(0x80+0x41)
#define DOOM_KEY_F8		(0x80+0x42)
#define DOOM_KEY_F9		(0x80+0x43)
#define DOOM_KEY_F10		(0x80+0x44)
#define DOOM_KEY_F11		(0x80+0x57)
#define DOOM_KEY_F12		(0x80+0x58)


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <signal.h>

#include <stdbool.h>

// External references to video buffers from i_video.c for optimized rendering
// I_VideoBuffer: 8-bit indexed palette buffer (320x200)
// rgb565_palette: Precomputed RGB565 lookup table (256 entries)
extern byte *I_VideoBuffer;
extern uint16_t rgb565_palette[256];

// MIPS 24KEc has 32-byte cache lines - align hot buffers for optimal performance
#define CACHE_LINE_SIZE 32

// Helper for cache-aligned allocation (reduces cache line splits)
static inline void *aligned_alloc_cached(size_t size) {
    void *ptr;
    // posix_memalign returns memory aligned to CACHE_LINE_SIZE boundary
    if (posix_memalign(&ptr, CACHE_LINE_SIZE, size) != 0)
        return NULL;
    return ptr;
}

#define KEYQUEUE_SIZE 16

#define MAX_INPUT_DEVS 16
#define INPUT_TYPE_KEYBOARD 0
#define INPUT_TYPE_JOYSTICK 1
#define INPUT_TYPE_MOUSE    2
#define INPUT_TYPE_TOUCH    3

// timing stuff
static struct timeval startTime;

// FPS and timing tracking (writes to file, not stderr - stderr crashes SIGIL!)
static uint32_t frameCount = 0;
static uint32_t lastFpsTime = 0;
static uint32_t currentFps = 0;
static uint32_t totalWriteTimeMs = 0;  // Accumulated write() time
static uint32_t avgWriteTimeMs = 0;    // Average write time per frame
static int useVsync = 0;               // If 1, fsync after write (no tearing but ~10 FPS)
static int fpsFd = -1;                 // File descriptor for FPS logging
#define FPS_UPDATE_INTERVAL_MS 1000  // Update FPS every second

// framebuffer stuff 
static uint8_t *fbPtr;
// These are non-static so net_lobby.c can access them for lobby drawing
uint16_t *renderBuffer = NULL;  // Local render buffer for 16-bit mode
size_t renderBufferSize = 0;
int fbFd;
static int ttyFd = -1;  // TTY for graphics mode switching
unsigned int fbWidth, fbHeight;  // Non-static for net_lobby.c access
static unsigned int fbStride, fbBytesPerPixel, fbOffsetX, fbOffsetY;
static int fbIs16Bit = 0; // 1 if framebuffer is 16-bit RGB565

// Precomputed lookup tables for scaling (avoids per-pixel division)
// Full-screen stretched (for gameplay with FOV correction)
static unsigned int *srcXLookup = NULL;  // For each dest Y, source X
static unsigned int *srcYLookup = NULL;  // For each dest X, source Y
static unsigned int scaledOutW = 0;
static unsigned int scaledOutH = 0;
static unsigned int scaledOffY = 0;

// Aspect-correct (for title screens/menus - uses black bars)
static unsigned int *srcXLookupAspect = NULL;
static unsigned int *srcYLookupAspect = NULL;
static unsigned int aspectOutW = 0;
static unsigned int aspectOutH = 0;
static unsigned int aspectOffY = 0;  // Vertical offset for centering (in display rows)

// Cleanup function for signal handling
static void cleanup_and_exit(int sig) {
	// Restore text mode if we switched to graphics mode
	if (ttyFd >= 0) {
		ioctl(ttyFd, KDSETMODE, KD_TEXT);
		close(ttyFd);
	}
	if (renderBuffer) free(renderBuffer);
	if (srcXLookup) free(srcXLookup);
	if (srcYLookup) free(srcYLookup);
	if (srcXLookupAspect) free(srcXLookupAspect);
	if (srcYLookupAspect) free(srcYLookupAspect);
	if (fpsFd >= 0) close(fpsFd);
	if (fbFd >= 0) close(fbFd);
	_exit(sig ? 128 + sig : 0);
}

// input stuff
static int numInputFds = 0;
static int inputFds[MAX_INPUT_DEVS];
static bool shiftPressed = false;
static struct pollfd pollfds[MAX_INPUT_DEVS];

static unsigned short s_KeyQueue[KEYQUEUE_SIZE];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

// Track button states for combo detection
static int redButtonPressed = 0;   // BTN_SOUTH (0x130)
static int greenButtonPressed = 0; // BTN_EAST (0x131)

// Track D-pad states for green+direction combos
static int dpadUpPressed = 0;
static int dpadDownPressed = 0;
static int dpadLeftPressed = 0;
static int dpadRightPressed = 0;

// XXX: HACK
// Linux's evdev system doesn't make it feasible to just use
// tolower(key) like the existing conversions did, so we
// use a few ranges of maps here to avoid an obscenely long switch/case
static char evdevKeysToASCII1[10] = {
	'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'
};
static char evdevKeysToASCII2[12] = {
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']'
};
static char evdevKeysToASCII3[12] = {
	'a', 's', 'd', 'f', 'g', 'h', 'i', 'j', 'k', 'l', ';', '\''
};
static char evdevKeysToASCII4[11] = {
	'\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/'
};


static char evdevShiftKeysToASCII1[12] = {
	'!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '{', '}'
};
static char evdevShiftKeysToASCII2[12] = {
	'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '{', '}'
};
static char evdevShiftKeysToASCII3[12] = {
	'a', 's', 'd', 'f', 'g', 'h', 'i', 'j', 'k', 'l', ':', '"'
};
static char evdevShiftKeysToASCII4[11] = {
	'|', 'z', 'x', 'c', 'v', 'b', 'n', 'm', '<', '>', '?'
};


static unsigned char convertToDoomKey(unsigned int key){
	switch (key) {
		case KEY_ENTER:
			key = DOOM_KEY_ENTER;
			break;
		case KEY_ESC:
			key = KEY_ESCAPE;
			break;
		case KEY_LEFT:
			key = KEY_LEFTARROW;
			break;
		case KEY_RIGHT:
			key = KEY_RIGHTARROW;
			break;
		case KEY_UP:
			key = KEY_UPARROW;
			break;
		case KEY_DOWN:
			key = KEY_DOWNARROW;
			break;
		case KEY_LEFTCTRL:
		case KEY_RIGHTCTRL:
			key = KEY_FIRE;
			break;
		case KEY_SPACE:
			key = KEY_USE;
			break;
		case KEY_LEFTSHIFT:
		case KEY_RIGHTSHIFT:
			key = KEY_RSHIFT;
			break;
		case KEY_LEFTALT:
		case KEY_RIGHTALT:
			key = KEY_LALT;
			break;
		case KEY_F2:
			key = DOOM_KEY_F2;
			break;
		case KEY_F3:
			key = DOOM_KEY_F3;
			break;
		case KEY_F4:
			key = DOOM_KEY_F4;
			break;
		case KEY_F5:
			key = DOOM_KEY_F5;
			break;
		case KEY_F6:
			key = DOOM_KEY_F6;
			break;
		case KEY_F7:
			key = DOOM_KEY_F7;
			break;
		case KEY_F8:
			key = DOOM_KEY_F8;
			break;
		case KEY_F9:
			key = DOOM_KEY_F9;
			break;
		case KEY_F10:
			key = DOOM_KEY_F10;
			break;
		case KEY_F11:
			key = DOOM_KEY_F11;
			break;
		case KEY_EQUAL:
			key = KEY_EQUALS;
			break;
		case KEY_MINUS:
			key = DOOM_KEY_MINUS;
			break;
		case KEY_BACKSPACE:
			key = DOOM_KEY_BACKSPACE;
			break;
		case KEY_TAB:
			key = DOOM_KEY_TAB;
			break;

		// WiFi Pineapple Pager button mappings
		case 0x130:  // BTN_SOUTH (304) - red button
			key = KEY_FIRE;
			break;
		case 0x131:  // BTN_EAST (305) - green button
			key = DOOM_KEY_ENTER;
			break;

		// sadly, yes, we need to handle every single alphanumeric
		// key here, since evdev doesn't spit out keys in anything
		// remotely resembling ASCII.....
		// though, we can take many shortcuts
		case KEY_1:
		case KEY_2:
		case KEY_3:
		case KEY_4:
		case KEY_5:
		case KEY_6:
		case KEY_7:
		case KEY_8:
		case KEY_9:
		case KEY_0:
			if (shiftPressed)
				key = evdevShiftKeysToASCII1[key - KEY_1];
			else
				key = evdevKeysToASCII1[key - KEY_1];
			break;
		case KEY_Q:
		case KEY_W:
		case KEY_E:
		case KEY_R:
		case KEY_T:
		case KEY_Y:
		case KEY_U:
		case KEY_I:
		case KEY_O:
		case KEY_P:
		case KEY_LEFTBRACE:
		case KEY_RIGHTBRACE:
			if (shiftPressed)
				key = evdevShiftKeysToASCII2[key - KEY_Q];
			else
				key = evdevKeysToASCII2[key - KEY_Q];
			break;
		case KEY_A:
		case KEY_S:
		case KEY_D:
		case KEY_F:
		case KEY_G:
		case KEY_H:
		case KEY_J:
		case KEY_K:
		case KEY_L:
		case KEY_SEMICOLON:
		case KEY_APOSTROPHE:
			if (shiftPressed)
				key = evdevShiftKeysToASCII3[key - KEY_A];
			else
				key = evdevKeysToASCII3[key - KEY_A];
			break;
		case KEY_BACKSLASH:
		case KEY_Z:
		case KEY_X:
		case KEY_C:
		case KEY_V:
		case KEY_B:
		case KEY_N:
		case KEY_M:
		case KEY_COMMA:
		case KEY_DOT:
		case KEY_SLASH:
			if (shiftPressed)
				key = evdevShiftKeysToASCII4[key - KEY_BACKSLASH];
			else
				key = evdevKeysToASCII4[key - KEY_BACKSLASH];
			break;
		default:
			key = 0xFF;
			break;
	}

	return key;
}

static void addKeyToQueue(int pressed, unsigned int keyCode) {
	if ((keyCode == KEY_LEFTSHIFT || keyCode == KEY_RIGHTSHIFT) &&
		(pressed == 1 || pressed == 0))
		shiftPressed = pressed;

	// Track button states for combo detection
	if (keyCode == 0x130) {  // Red button
		redButtonPressed = pressed;
	} else if (keyCode == 0x131) {  // Green button
		greenButtonPressed = pressed;
	}
	
	// Track D-pad states
	if (keyCode == KEY_UP) dpadUpPressed = pressed;
	else if (keyCode == KEY_DOWN) dpadDownPressed = pressed;
	else if (keyCode == KEY_LEFT) dpadLeftPressed = pressed;
	else if (keyCode == KEY_RIGHT) dpadRightPressed = pressed;
	
	// Both buttons pressed together = ESC (main menu)
	if (redButtonPressed && greenButtonPressed && pressed) {
		unsigned short escData = (1 << 8) | KEY_ESCAPE;
		s_KeyQueue[s_KeyQueueWriteIndex] = escData;
		s_KeyQueueWriteIndex++;
		s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
		return;  // Don't also send the individual button
	}
	
	// Green + D-pad combos (when green is held)
	if (greenButtonPressed && pressed) {
		unsigned char comboKey = 0;
		
		if (keyCode == KEY_UP) {
			comboKey = KEY_USE;  // Open doors/switches (0xa2)
		} else if (keyCode == KEY_DOWN) {
			comboKey = DOOM_KEY_TAB;  // Automap toggle
		} else if (keyCode == KEY_LEFT) {
			comboKey = KEY_STRAFE_L;  // Strafe left (0xa0)
		} else if (keyCode == KEY_RIGHT) {
			comboKey = KEY_STRAFE_R;  // Strafe right (0xa1)
		}
		
		if (comboKey != 0) {
			unsigned short comboData = (1 << 8) | comboKey;
			s_KeyQueue[s_KeyQueueWriteIndex] = comboData;
			s_KeyQueueWriteIndex++;
			s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
			return;  // Don't send the regular D-pad key
		}
	}
	
	// Handle key release for combo keys (need to release the combo key too)
	if (greenButtonPressed && !pressed) {
		unsigned char comboKey = 0;
		
		if (keyCode == KEY_UP) comboKey = KEY_USE;
		else if (keyCode == KEY_DOWN) comboKey = DOOM_KEY_TAB;
		else if (keyCode == KEY_LEFT) comboKey = KEY_STRAFE_L;
		else if (keyCode == KEY_RIGHT) comboKey = KEY_STRAFE_R;
		
		if (comboKey != 0) {
			unsigned short comboData = (0 << 8) | comboKey;  // Release
			s_KeyQueue[s_KeyQueueWriteIndex] = comboData;
			s_KeyQueueWriteIndex++;
			s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
			return;
		}
	}
		
	unsigned char key = convertToDoomKey(keyCode);
	if (key == 0xFF) // unknown, don't process it
		return;

	if (pressed > 1 || pressed < 0) // bogus value
		return;

	unsigned short keyData = (pressed << 8) | key;

	s_KeyQueue[s_KeyQueueWriteIndex] = keyData;
	s_KeyQueueWriteIndex++;
	s_KeyQueueWriteIndex %= KEYQUEUE_SIZE;
}



static void checkKeys() {
	int ret, i;
	bool keepGoing;
	struct input_event ev;

	keepGoing = true;
	while (keepGoing) {
		keepGoing = false; // exit if we haven't gotten anything
		ret = poll(pollfds, numInputFds, 0);  // don't wait at all, just give anything we've got

		if (ret < 0) {
			// borked
			return;
		}

		for (i = 0; i < MAX_INPUT_DEVS; i++) {
			if (pollfds[i].revents & POLLIN) {
				read(inputFds[i], &ev, sizeof(ev));
				keepGoing = true; // we read something, so keep trying
				addKeyToQueue(ev.value, ev.code);
			}
		}
	}

	// we read the entire backlog, get back to doom
	return;
}

// Check for lobby-specific input: returns 1=start, -1=quit, 0=nothing
// Pager buttons: 0x131 (305) = GREEN button, 0x130 (304) = RED button
int DG_CheckLobbyInput(void)
{
	int ret, i;
	struct input_event ev;
	int result = 0;
	
	ret = poll(pollfds, numInputFds, 0);
	if (ret <= 0) return 0;
	
	for (i = 0; i < MAX_INPUT_DEVS; i++) {
		if (pollfds[i].revents & POLLIN) {
			read(inputFds[i], &ev, sizeof(ev));
			if (ev.type == EV_KEY && ev.value == 1) {  // Key press
				// Use exact codes: 0x131 = GREEN, 0x130 = RED
				if (ev.code == 0x131 || ev.code == KEY_SPACE || ev.code == KEY_ENTER) {
					result = 1;  // GREEN = Start game
				} else if (ev.code == 0x130 || ev.code == KEY_ESC) {
					result = -1; // RED = Quit
				}
			}
		}
	}
	return result;
}

#define TEST_KEY(k) (keybits[(k)/8] & (1 << ((k)%8)))
static int isKeyboard(const char *devPath) {
	unsigned long evbits;
	unsigned char keybits[KEY_MAX/8 + 1];
	int fd = open(devPath, O_RDONLY);
	if (fd < 0) {
		perror("Failed to open device");
		return 0;
	}

	evbits = 0;
	if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), &evbits) < 0) {
		close(fd);
		return 0;
	}

	/* Must support EV_KEY */
	if (!(evbits & (1 << EV_KEY))) {
		close(fd);
		return 0;
	}


	memset(keybits, 0, sizeof(keybits));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0) {
		close(fd);
		return 0;
	}

	/* Accept full keyboards OR any device with button keys (for GPIO buttons) */
	if (TEST_KEY(KEY_A) && TEST_KEY(KEY_ENTER)) {
		close(fd);
		return 1;  /* looks like a keyboard */
	}
	
	/* Also accept devices with BTN_SOUTH/BTN_EAST (Pineapple Pager buttons) */
	if (TEST_KEY(0x130) || TEST_KEY(0x131)) {
		close(fd);
		return 1;  /* has GPIO buttons */
	}

	close(fd);
	return 0;
}

static void checkInputDevs() {
	struct dirent *dp;
	DIR *dir;

	/* check for devices */
	dir = opendir("/dev/input");

	while ((dp = readdir(dir)) != NULL) {
		char fullpath[268]; /* d_name is 256 bytes */
		int fd;

		if (numInputFds >= MAX_INPUT_DEVS)
			I_Error("Out of room in input devices array");

		printf("checking %s\n", dp->d_name);
		if (strncmp(dp->d_name, "event", 5) != 0)
			continue;

		sprintf(fullpath, "/dev/input/%s", dp->d_name);

		printf("%s is a valid event device\n", fullpath);
		if (!isKeyboard(fullpath)) {
			printf("%s is not a keyboard, moving on\n", fullpath);
			continue;
		}

		fd = open(fullpath, O_RDONLY | O_NONBLOCK);
		if (fd < 0) {
			// not necessarily fatal
			perror("Failed to open device");
			continue;
		}

		pollfds[numInputFds].fd = fd;
		pollfds[numInputFds].events = POLLIN;
		inputFds[numInputFds++] = fd;
		printf("adding %s keyboard\n", fullpath);
		ioctl(fd, EVIOCGRAB, 1); // grab exclusive access to the device
	}
	closedir(dir);
}

void DG_Init() {
	int ret;
	struct fb_var_screeninfo info;
	struct fb_fix_screeninfo finfo;

	// Check for -vsync command line arg (uses fsync for tear-free but ~10 FPS)
	if (M_CheckParm("-vsync")) {
		useVsync = 1;
		printf("VSync enabled (tear-free but slower)\n");
	}

	// Set up signal handlers for clean exit
	signal(SIGINT, cleanup_and_exit);
	signal(SIGTERM, cleanup_and_exit);
	signal(SIGSEGV, cleanup_and_exit);

	//
	// Try to get exclusive graphics mode access
	// This prevents the console/other apps from interfering
	//
	ttyFd = open("/dev/tty0", O_RDWR);
	if (ttyFd < 0)
		ttyFd = open("/dev/tty", O_RDWR);
	if (ttyFd < 0)
		ttyFd = open("/dev/console", O_RDWR);
	
	if (ttyFd >= 0) {
		// Switch to graphics mode - prevents console from drawing
		if (ioctl(ttyFd, KDSETMODE, KD_GRAPHICS) == 0) {
			printf("Switched to graphics mode\n");
		} else {
			printf("Warning: Could not switch to graphics mode\n");
		}
	}

	//
	// set up the framebuffer
	//
	fbFd = open("/dev/fb0", O_RDWR);
	if (fbFd < 0)
		I_Error("Failed to open /dev/fb0: %s", strerror(errno));

	// get info
	ret = ioctl(fbFd, FBIOGET_VSCREENINFO, &info);
	if (ret != 0)
		I_Error("Failed to get framebuffer info: %s", strerror(errno));

	// get other info (this can optionally fail, since we can guess the stride)
	ret = ioctl(fbFd, FBIOGET_FSCREENINFO, &finfo);
	if (ret != 0) {
		printf("Failed to get framebuffer info: %s", strerror(errno));
		fbStride = fbWidth * fbBytesPerPixel;
	}
	else {
		fbStride = finfo.line_length;
	}

	fbWidth = info.xres;
	fbHeight = info.yres;
	fbBytesPerPixel = info.bits_per_pixel / 8;
	
	// Detect 16-bit RGB565 framebuffer
	if (info.bits_per_pixel == 16) {
		fbIs16Bit = 1;
		printf("Framebuffer: %dx%d, 16-bit RGB565\n", fbWidth, fbHeight);
		
		// Use write() for SPI displays - mmap causes glitches with fbtft
		// OPTIMIZED: Cache-aligned allocation for better memory access patterns
		renderBufferSize = fbWidth * fbHeight * sizeof(uint16_t);
		renderBuffer = (uint16_t *)aligned_alloc_cached(renderBufferSize);
		if (!renderBuffer)
			I_Error("Failed to allocate render buffer");
		memset(renderBuffer, 0, renderBufferSize);
		
		// Clear the display
		lseek(fbFd, 0, SEEK_SET);
		write(fbFd, renderBuffer, renderBufferSize);
		
		fbPtr = NULL;
		fbOffsetX = 0;
		fbOffsetY = 0;
		
		// Fill entire 222x480 display with 320x200 Doom + 90° CCW rotation
		// After rotation: Doom width (320) -> display height (480), Doom height (200) -> display width (222)
		// Scale factors: 480/320 = 1.5 for height, 222/200 = 1.11 for width
		scaledOutW = fbWidth;   // 222 (fills display width)
		scaledOutH = fbHeight;  // 480 (fills display height)
		scaledOffY = 0;  // No offset, fills entire screen
		
		// Precompute lookup tables for STRETCHED scaling + rotation (gameplay)
		// OPTIMIZED: Cache-aligned for sequential reads in render loop
		srcXLookup = (unsigned int *)aligned_alloc_cached(scaledOutH * sizeof(unsigned int));
		srcYLookup = (unsigned int *)aligned_alloc_cached(scaledOutW * sizeof(unsigned int));
		
		// For each display row (y), compute which Doom column (X) to sample
		// Display Y maps to Doom X: srcX = y * DoomWidth / displayHeight
		for (unsigned int y = 0; y < scaledOutH; y++) {
			srcXLookup[y] = (y * DOOMGENERIC_RESX) / scaledOutH;
			if (srcXLookup[y] >= DOOMGENERIC_RESX) srcXLookup[y] = DOOMGENERIC_RESX - 1;
		}
		// For each display col (x), compute which Doom row (Y) to sample (flipped for CCW)
		// Display X maps to Doom Y (inverted): srcY = (width-1-x) * DoomHeight / displayWidth
		for (unsigned int x = 0; x < scaledOutW; x++) {
			srcYLookup[x] = ((scaledOutW - 1 - x) * DOOMGENERIC_RESY) / scaledOutW;
			if (srcYLookup[x] >= DOOMGENERIC_RESY) srcYLookup[x] = DOOMGENERIC_RESY - 1;
		}
		
		printf("Full-screen: Doom %dx%d -> Display %dx%d (scaled + rotated)\n", 
		       DOOMGENERIC_RESX, DOOMGENERIC_RESY, fbWidth, fbHeight);
		
		// Precompute lookup tables for ASPECT-CORRECT scaling (title screens)
		// Display: 222 wide x 480 tall. After 90° CCW rotation from Doom's POV: 480 wide x 222 tall
		// Doom is 320x200 (1.6:1 aspect). To maintain aspect with 222 vertical pixels:
		// Scaled width = 222 * (320/200) = 222 * 1.6 = 355 display rows
		// Black bars: (480 - 355) / 2 = 62 pixels on each side
		aspectOutW = fbWidth;  // 222 - use full display columns (becomes Doom's vertical)
		aspectOutH = (aspectOutW * DOOMGENERIC_RESX) / DOOMGENERIC_RESY;  // 222 * 320 / 200 = 355
		if (aspectOutH > fbHeight) aspectOutH = fbHeight;
		aspectOffY = (fbHeight - aspectOutH) / 2;  // Center vertically: (480 - 355) / 2 = 62
		
		// OPTIMIZED: Cache-aligned lookup tables
		srcXLookupAspect = (unsigned int *)aligned_alloc_cached(aspectOutH * sizeof(unsigned int));
		srcYLookupAspect = (unsigned int *)aligned_alloc_cached(aspectOutW * sizeof(unsigned int));
		
		// For each output row in the aspect-correct region, which Doom X to sample
		for (unsigned int y = 0; y < aspectOutH; y++) {
			srcXLookupAspect[y] = (y * DOOMGENERIC_RESX) / aspectOutH;
			if (srcXLookupAspect[y] >= DOOMGENERIC_RESX) srcXLookupAspect[y] = DOOMGENERIC_RESX - 1;
		}
		// For each output column, which Doom Y to sample (inverted for CCW rotation)
		for (unsigned int x = 0; x < aspectOutW; x++) {
			srcYLookupAspect[x] = ((aspectOutW - 1 - x) * DOOMGENERIC_RESY) / aspectOutW;
			if (srcYLookupAspect[x] >= DOOMGENERIC_RESY) srcYLookupAspect[x] = DOOMGENERIC_RESY - 1;
		}
		
		printf("Aspect-correct: Doom %dx%d -> %dx%d rows + %d row offset (for menus)\n",
		       DOOMGENERIC_RESX, DOOMGENERIC_RESY, aspectOutW, aspectOutH, aspectOffY);
	} else {
		printf("Framebuffer: %dx%d, %d-bit\n", fbWidth, fbHeight, info.bits_per_pixel);
		
		// For 32-bit displays, use mmap as normal
		fbOffsetX = ((fbWidth - DOOMGENERIC_RESX) / 2) * fbBytesPerPixel;
		fbOffsetY = ((fbHeight - DOOMGENERIC_RESY) / 2) * fbStride;

		fbPtr = mmap(NULL, fbStride * fbHeight, PROT_READ | PROT_WRITE,
				MAP_SHARED, fbFd, 0);

		if (!fbPtr)
			I_Error("Failed to mmap /dev/fb0: %s", strerror(errno));

		// clear the screen
		memset(fbPtr, 0, fbStride * fbHeight);
	}

	//
	// set up input
	//
	checkInputDevs();
	
	if (numInputFds == 0)
		I_Error("Failed to find any compatible input device, see the logs above for potential problems");

	// get the start time
	gettimeofday(&startTime, NULL);
}

// Fallback: Convert 32-bit ARGB to 16-bit RGB565 (only used for 32-bit path)
static inline uint16_t rgb32_to_rgb565(uint32_t pixel) {
	uint8_t r = (pixel >> 16) & 0xFF;
	uint8_t g = (pixel >> 8) & 0xFF;
	uint8_t b = pixel & 0xFF;
	return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void DG_DrawFrame() {
	if (fbIs16Bit) {
		// OPTIMIZED 16-bit RGB565 path with 90° CCW rotation
		// Uses precomputed palette lookup - reads directly from I_VideoBuffer (8-bit indexed)
		// This bypasses DG_ScreenBuffer entirely, eliminating per-pixel RGB conversion!
		byte *srcBuf = I_VideoBuffer;
		
		// Cache palette pointer locally for faster access
		const uint16_t *palette = rgb565_palette;
		
		// Use aspect-correct rendering for title/menu screens, stretched for gameplay
		int useAspectCorrect = (gamestate != GS_LEVEL);
		
		if (useAspectCorrect && srcXLookupAspect && srcYLookupAspect) {
			// Clear buffer first (for black bars at top/bottom)
			memset(renderBuffer, 0, renderBufferSize);
			
			// Render with correct aspect ratio (centered with vertical black bars)
			// Direct palette lookup: srcBuf[y*320+x] -> palette[index]
			for (unsigned int y = 0; y < aspectOutH; y++) {
				uint16_t *dst = renderBuffer + (y + aspectOffY) * fbWidth;
				unsigned int srcX = srcXLookupAspect[y];
				const unsigned int *yLookup = srcYLookupAspect;
				
				// Process 4 pixels at a time with direct palette lookup
				unsigned int x = 0;
				for (; x + 3 < aspectOutW; x += 4) {
					dst[x]   = palette[srcBuf[yLookup[x]   * DOOMGENERIC_RESX + srcX]];
					dst[x+1] = palette[srcBuf[yLookup[x+1] * DOOMGENERIC_RESX + srcX]];
					dst[x+2] = palette[srcBuf[yLookup[x+2] * DOOMGENERIC_RESX + srcX]];
					dst[x+3] = palette[srcBuf[yLookup[x+3] * DOOMGENERIC_RESX + srcX]];
				}
				// Handle remaining pixels
				for (; x < aspectOutW; x++) {
					dst[x] = palette[srcBuf[yLookup[x] * DOOMGENERIC_RESX + srcX]];
				}
			}
		} else {
			// Full-screen stretched rendering (gameplay with FOV correction)
			// Direct palette lookup from I_VideoBuffer
			for (unsigned int y = 0; y < scaledOutH; y++) {
				uint16_t *dst = renderBuffer + (y + scaledOffY) * fbWidth;
				unsigned int srcX = srcXLookup[y];
				const unsigned int *yLookup = srcYLookup;
				
				// Process 4 pixels at a time with direct palette lookup
				unsigned int x = 0;
				for (; x + 3 < scaledOutW; x += 4) {
					dst[x]   = palette[srcBuf[yLookup[x]   * DOOMGENERIC_RESX + srcX]];
					dst[x+1] = palette[srcBuf[yLookup[x+1] * DOOMGENERIC_RESX + srcX]];
					dst[x+2] = palette[srcBuf[yLookup[x+2] * DOOMGENERIC_RESX + srcX]];
					dst[x+3] = palette[srcBuf[yLookup[x+3] * DOOMGENERIC_RESX + srcX]];
				}
				// Handle remaining pixels
				for (; x < scaledOutW; x++) {
					dst[x] = palette[srcBuf[yLookup[x] * DOOMGENERIC_RESX + srcX]];
				}
			}
		}
		
		// Write frame to display (measure time)
		uint32_t writeStart = DG_GetTicksMs();
		lseek(fbFd, 0, SEEK_SET);
		write(fbFd, renderBuffer, renderBufferSize);
		if (useVsync) {
			fsync(fbFd);  // Wait for SPI transfer (~95ms, ~10 FPS but no tearing)
		}
		totalWriteTimeMs += DG_GetTicksMs() - writeStart;
	} else {
		// Original 32-bit mmap path
		for (int line = 0; line < DOOMGENERIC_RESY; line++) {
			memcpy(
				(void *)((uintptr_t)(fbPtr) + (fbStride * line) + fbOffsetY + fbOffsetX),
				(void *)(((uintptr_t)DG_ScreenBuffer) + (DOOMGENERIC_RESX * line * fbBytesPerPixel)),
				(fbBytesPerPixel * DOOMGENERIC_RESX)
			);
		}
	}

	// FPS tracking - write to file (stderr causes SIGIL to crash!)
	frameCount++;
	uint32_t now = DG_GetTicksMs();
	if (now - lastFpsTime >= FPS_UPDATE_INTERVAL_MS) {
		currentFps = (frameCount * 1000) / (now - lastFpsTime);
		avgWriteTimeMs = frameCount > 0 ? totalWriteTimeMs / frameCount : 0;
		uint32_t displayFps = avgWriteTimeMs > 0 ? 1000 / avgWriteTimeMs : 0;
		// Write to file instead of stderr - stderr causes crashes on intensive maps
		if (fpsFd < 0) {
			fpsFd = open("/tmp/fps.log", O_WRONLY | O_CREAT | O_TRUNC, 0644);
		}
		if (fpsFd >= 0) {
			char buf[64];
			int len = snprintf(buf, sizeof(buf), "FPS: %u | write: %ums | display: ~%u fps\n", 
				currentFps, avgWriteTimeMs, displayFps);
			write(fpsFd, buf, len);
		}
		frameCount = 0;
		totalWriteTimeMs = 0;
		lastFpsTime = now;
	}

	checkKeys();
}

void DG_SleepMs(uint32_t ms) {
	usleep(ms * 1000);
}

uint32_t DG_GetTicksMs() {
	struct timeval curTime;
	long seconds, usec;

	gettimeofday(&curTime, NULL);
	seconds = curTime.tv_sec - startTime.tv_sec;
	usec = curTime.tv_usec - startTime.tv_usec;

	return (seconds * 1000) + (usec / 1000);
}

int DG_GetKey(int* pressed, unsigned char* doomKey) {
	checkKeys();

	if (s_KeyQueueReadIndex == s_KeyQueueWriteIndex) {
		//key queue is empty

		return 0;
	}
	else {
		unsigned short keyData = s_KeyQueue[s_KeyQueueReadIndex];
		s_KeyQueueReadIndex++;
		s_KeyQueueReadIndex %= KEYQUEUE_SIZE;

		*pressed = keyData >> 8;
		*doomKey = keyData & 0xFF;

		return 1;
	}

}

void DG_SetWindowTitle(const char * title) {
	printf("Window Title: %s\n", title);
}

int main(int argc, char **argv) {
	doomgeneric_Create(argc, argv);

	while (1)
	{
		doomgeneric_Tick();
	}


	return 0;
}
