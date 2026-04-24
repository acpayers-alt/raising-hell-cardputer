#pragma once
#include <stdint.h>
#include <stdbool.h>

// Timeout selections:
// 0 = 5m, 1 = 30m, 2 = 1h, 3 = Off
extern uint8_t autoScreenTimeoutSel;
extern uint8_t autoClockTimeoutSel;

// Legacy/diagnostic flag (keep for compatibility; your real pipeline uses isScreenOn()).
extern bool g_screenIsOff;

// UI-friendly string for timeout selections.
const char *autoScreenToText(uint8_t sel);
const char *autoClockToText(uint8_t sel);

// Helpers
uint32_t autoScreenTimeoutMsForSel(uint8_t sel);
uint32_t autoScreenTimeoutMs();

uint32_t autoClockTimeoutMsForSel(uint8_t sel);
uint32_t autoClockTimeoutMs();

// Called whenever user input occurs (updates timestamp + wakes screen if needed)
void noteUserActivity();

// Called from loop for inactivity actions
void autoScreenTick();
void autoClockTick();

// Exported screen power helpers used across the UI
void screenWake();
void screenSleep();

// Enable/disable auto screen-off behavior (and query current setting)
bool autoScreenIsEnabled();
void autoScreenSetEnabled(bool enabled);

// Enable/disable auto clock-mode behavior (and query current setting)
bool autoClockIsEnabled();
void autoClockSetEnabled(bool enabled);