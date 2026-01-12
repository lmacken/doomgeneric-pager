/*
 * i_buzzer.c - WiFi Pineapple Pager buzzer control
 *
 * Controls the pager's piezoelectric buzzer via sysfs LED interface.
 * Interface: /sys/class/leds/buzzer/{frequency,brightness}
 *
 * Copyright (C) 2026
 * License: GPLv2
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>

#include "i_buzzer.h"

#define BUZZER_FREQ_PATH "/sys/class/leds/buzzer/frequency"
#define BUZZER_BRIGHTNESS_PATH "/sys/class/leds/buzzer/brightness"

static int buzzer_available = -1;  // -1 = unchecked, 0 = no, 1 = yes

// Check if buzzer is available
static int I_BuzzerCheck(void)
{
    if (buzzer_available < 0) {
        buzzer_available = (access(BUZZER_BRIGHTNESS_PATH, W_OK) == 0) ? 1 : 0;
        if (buzzer_available) {
            fprintf(stderr, "I_Buzzer: Pager buzzer available\n");
        }
    }
    return buzzer_available;
}

// Write a value to a sysfs file
static void write_sysfs(const char *path, int value)
{
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "%d", value);
        write(fd, buf, len);
        close(fd);
    }
}

// Sleep for milliseconds
static void msleep(int ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

// Play a tone at given frequency for duration_ms
void I_BuzzerTone(int freq_hz, int duration_ms)
{
    if (!I_BuzzerCheck()) return;
    
    write_sysfs(BUZZER_FREQ_PATH, freq_hz);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 200);  // Volume
    
    if (duration_ms > 0) {
        msleep(duration_ms);
        write_sysfs(BUZZER_BRIGHTNESS_PATH, 0);
    }
}

// Stop any playing tone
void I_BuzzerStop(void)
{
    if (!I_BuzzerCheck()) return;
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 0);
}

// Player joined sound - rising tone (friendly, welcoming)
void I_BuzzerPlayerJoin(void)
{
    if (!I_BuzzerCheck()) return;
    
    // Rising two-tone chirp
    write_sysfs(BUZZER_FREQ_PATH, 600);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 180);
    msleep(80);
    write_sysfs(BUZZER_FREQ_PATH, 900);
    msleep(100);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 0);
}

// Player left sound - falling tone (departure)
void I_BuzzerPlayerLeave(void)
{
    if (!I_BuzzerCheck()) return;
    
    // Two descending beeps
    write_sysfs(BUZZER_FREQ_PATH, 500);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 150);
    msleep(80);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 0);
    msleep(60);
    write_sysfs(BUZZER_FREQ_PATH, 350);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 150);
    msleep(100);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 0);
}

// Game starting sound - triumphant!
void I_BuzzerGameStart(void)
{
    if (!I_BuzzerCheck()) return;
    
    // Ascending fanfare (C5, E5, G5, C6)
    int freqs[] = {523, 659, 784, 1047};
    int i;
    for (i = 0; i < 4; i++) {
        write_sysfs(BUZZER_FREQ_PATH, freqs[i]);
        write_sysfs(BUZZER_BRIGHTNESS_PATH, 200);
        msleep(80);
    }
    msleep(150);
    write_sysfs(BUZZER_BRIGHTNESS_PATH, 0);
}

// Connection established beep
void I_BuzzerConnected(void)
{
    I_BuzzerTone(800, 100);
}
