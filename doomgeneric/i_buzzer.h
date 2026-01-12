/*
 * i_buzzer.h - WiFi Pineapple Pager buzzer control
 *
 * Simple interface to the pager's piezoelectric buzzer for game notifications.
 * Hardware: /sys/class/leds/buzzer/{frequency,brightness}
 *
 * Copyright (C) 2026
 * License: GPLv2
 */

#ifndef I_BUZZER_H
#define I_BUZZER_H

// Play a tone at given frequency (Hz) for duration (ms)
// If duration is 0, tone plays until I_BuzzerStop() is called
void I_BuzzerTone(int freq_hz, int duration_ms);

// Stop any currently playing tone
void I_BuzzerStop(void);

// === Lobby notification sounds ===

// Player joined the lobby - rising chirp
void I_BuzzerPlayerJoin(void);

// Player left the lobby - descending beeps
void I_BuzzerPlayerLeave(void);

// Game is starting - fanfare
void I_BuzzerGameStart(void);

// Successfully connected to server
void I_BuzzerConnected(void);

#endif // I_BUZZER_H
