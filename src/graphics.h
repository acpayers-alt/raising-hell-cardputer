#pragma once

#include <Arduino.h>
#include "activity.h"
#include "ui_defs.h"

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif

void graphicsRecoverAfterOta();
void graphicsReleasePetLayerForOta();

// -----------------------------------------------------------------------------
// Modal / messages
// -----------------------------------------------------------------------------
void ui_drawMessageWindow(const char* title,
                          const char* line1,
                          const char* line2,
                          bool maskLine2 = false,
                          bool showCursor = false);

void ui_showMessage(const char *msg);

// -----------------------------------------------------------------------------
// Backgrounds (JPEG-only backgrounds)
// -----------------------------------------------------------------------------
void drawBackground(const char *path);
void drawBootSplash();

void ui_setBootSplashActive(bool on);
bool ui_isBootSplashActive();

// Background cache invalidation
void invalidateBackgroundCache();
bool consumeBackgroundInvalidation();
bool backgroundCacheInvalidated();

void sleepAnimHeartbeat(uint32_t now);

void sleepBgNotifyScreenWake();

void sleepBgKickNow();

bool sprDrawPngFromSD(const char* path, int x, int y);
bool sprDrawJpgFromSD(const char* path, int x, int y);

// -----------------------------------------------------------------------------
// RAW streaming helpers for sprites/icons
// -----------------------------------------------------------------------------
bool streamRawImage(const char *path, int x, int y, int w, int h);
bool streamRawImageFast(const char *path, int x, int y, int w, int h);

// -----------------------------------------------------------------------------
// Screen renderers / UI
// -----------------------------------------------------------------------------
void drawPetScreen();
void drawSleepScreen();

void drawSettingsMenu();
void drawSleepMenu();
void drawInventoryMenu();
void drawPowerMenu();
void drawFeedMenu();
void drawHatchingScreen(bool redrawBg);

// Console
void drawConsoleMenu();
void drawConsoleScreen();

// Force one immediate render pass
void forceRenderUIOnce();

// Main UI dispatcher
void renderUI();

// Level-up modal
void uiShowLevelUpPopup(uint16_t newLevel);
bool uiIsLevelUpPopupActive();
void uiDismissLevelUpPopup();
void uiDrawLevelUpPopup();   // draws overlay into spr (does not push)

// First-Boot Wifi
void drawBootWifiPromptScreen();
void drawBootWifiWaitScreen(bool connected, int rssi);
void drawBootTimezonePickScreen();
void drawBootNtpWaitScreen(bool connected, bool synced);

// -----------------------------------------------------------------------------
// Global chrome
// -----------------------------------------------------------------------------
void drawTopBar();
void drawTabBar();
