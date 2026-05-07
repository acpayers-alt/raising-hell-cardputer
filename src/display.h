#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <stdint.h>

#include <M5GFX.h>
#include "M5Cardputer.h"
#include "display_dims_state.h"

// Cardputer canvas framebuffer
extern M5Canvas spr;

// Public screen/backlight API
bool isScreenOn();
void setScreenPower(bool on);
void toggleScreenPower();

void initBacklight();
void setBacklight(uint8_t level); // level 0..255
bool isBacklightPulseActive();

void setScreenPowerTagged(bool on, const char *file, int line);
void setBacklightTagged(uint8_t level, const char *file, int line);

#ifndef SET_SCREEN_POWER
#define SET_SCREEN_POWER(on) setScreenPowerTagged((on), __FILE__, __LINE__)
#endif

#ifndef SET_BACKLIGHT
#define SET_BACKLIGHT(level) setBacklightTagged((level), __FILE__, __LINE__)
#endif

// Init / power helpers
void displayInit();
void screenOff();
void screenOnRestore();

void batteryProtectionTick(uint32_t now);
void updateBattery();
bool displayUsbPowerLikely();

// Screen dimensions
constexpr int SCREEN_W = 240;
constexpr int SCREEN_H = 135;

// Regions
constexpr int TOP_BAR_H = 18;
constexpr int TAB_BAR_H = 18;

constexpr int PET_AREA_Y = TOP_BAR_H;
constexpr int PET_AREA_H = SCREEN_H - TOP_BAR_H - TAB_BAR_H;

constexpr int CONTENT_Y = TOP_BAR_H;
constexpr int CONTENT_H = SCREEN_H - TOP_BAR_H - TAB_BAR_H;

// Backlight pulse helpers
void backlightPulseBegin(uint8_t level);
void backlightPulseEnd();

void backlightRailPulseBegin(uint8_t level);
void backlightRailPulseEnd();
void backlightRailPulseAdoptScreenOn();
void backlightRailPulseShowColor(uint8_t r, uint8_t g, uint8_t b);

// Manual screen toggle tracking
uint32_t screenPowerLastManualToggleMs();
void markScreenPowerManualToggle(uint32_t now);

// Brightness helpers
uint8_t displayGetUserBrightnessLevel();
void displayRememberUserBrightness(uint8_t level);
void forceBacklightDuringFade(uint8_t level);
bool displayWakeBlackoutPending();
void displayFinishWakeBlackoutAfterFrame();