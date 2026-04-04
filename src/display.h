#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include "M5Cardputer.h"
#include <stdint.h>
#include "display_dims_state.h"

void updateBattery();

// Public API
void setScreenPower(bool on);
void setBacklight(uint8_t level);

// Debug-tagged variants (if you have them)
void setScreenPowerTagged(bool on, const char* file, int line);
void setBacklightTagged(uint8_t level, const char* file, int line);

// Macros used across codebase
#ifndef SET_SCREEN_POWER
#define SET_SCREEN_POWER(on) setScreenPowerTagged((on), __FILE__, __LINE__)
#endif

#ifndef SET_BACKLIGHT
#define SET_BACKLIGHT(level) setBacklightTagged((level), __FILE__, __LINE__)
#endif

// Screen power API (AppState-owned)
bool isScreenOn();
void setScreenPower(bool on);
void toggleScreenPower();

void setBacklightTagged(uint8_t level, const char* file, int line);
#define SET_BACKLIGHT(level) setBacklightTagged((level), __FILE__, __LINE__)

void setScreenPowerTagged(bool on, const char* file, int line);
#define SET_SCREEN_POWER(on) setScreenPowerTagged((on), __FILE__, __LINE__)

void batteryProtectionTick(uint32_t now);

// Cardputer canvas framebuffer
extern M5Canvas spr;

// Backlight (best-effort)
void initBacklight();
void setBacklight(uint8_t level); // level 0..255

// Init / power helpers
void displayInit();
void screenOff();
void screenOnRestore();

// Screen
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;

// Regions
constexpr int TOP_BAR_H = 18;
constexpr int TAB_BAR_H = 18;

constexpr int PET_AREA_Y = TOP_BAR_H;
constexpr int PET_AREA_H = SCREEN_H - TOP_BAR_H - TAB_BAR_H;

constexpr int CONTENT_Y = TOP_BAR_H;
constexpr int CONTENT_H = SCREEN_H - TOP_BAR_H - TAB_BAR_H;

void backlightPulseBegin(uint8_t level);
void backlightPulseEnd();

uint32_t screenPowerLastManualToggleMs();
void markScreenPowerManualToggle(uint32_t now);

uint32_t screenPowerLastManualToggleMs();
void markScreenPowerManualToggle(uint32_t now);

void displayRememberUserBrightness(uint8_t level);

uint8_t displayGetUserBrightnessLevel();
void displayRememberUserBrightness(uint8_t level);

void forceBacklightDuringFade(uint8_t level);