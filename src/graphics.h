#pragma once

#include "activity.h"
#include "ui_defs.h"
#include <Arduino.h>
#include <M5GFX.h>
#include "graphics_sd_draw.h"

#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#endif

void graphicsRecoverAfterOta();
void graphicsReleasePetLayerForOta();
void graphicsReleaseUiCachesForMiniGame();
void graphicsPrewarmPetBackgroundCache();

// -----------------------------------------------------------------------------
// Modal / messages
// -----------------------------------------------------------------------------
void ui_drawMessageWindow(const char *title, const char *line1, const char *line2, bool maskLine2 = false,
                          bool showCursor = false);

void ui_showMessage(const char *msg);
void ui_showTimedMessage(const char *msg, uint32_t durationMs);
bool uiToastIsActive();
bool uiToastIsPersistent();
void uiDismissToast();
void ui_showSuccessMessage(const char *msg);

// -----------------------------------------------------------------------------
// Backgrounds (JPEG-only backgrounds)
// -----------------------------------------------------------------------------
void drawBackground(const char *path);
void drawBootSplash();
void drawBootAssetWifiRequiredScreen();

void ui_setBootSplashActive(bool on);
bool ui_isBootSplashActive();

// Background cache invalidation
void invalidateBackgroundCache();
bool consumeBackgroundInvalidation();
bool backgroundCacheInvalidated();

void sleepAnimHeartbeat(uint32_t now);

void sleepBgNotifyScreenWake();

void sleepBgKickNow();

// -----------------------------------------------------------------------------
// RAW streaming helpers for sprites/icons
// -----------------------------------------------------------------------------
bool streamRawImage(const char *path, int x, int y, int w, int h);
bool streamRawImageFast(const char *path, int x, int y, int w, int h);
void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100, const char *label);
void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100);
void drawTinyBarV(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100);

// -----------------------------------------------------------------------------
// Screen renderers / UI
// -----------------------------------------------------------------------------
void drawSleepScreen();

void drawSettingsMenu();
void drawSleepMenu();
void drawInventoryMenu();
void drawPowerMenu();
void drawFeedMenu();
void drawHatchingScreen(bool redrawBg);
void drawTitleMenuScreen(bool redrawBg);
void controlsHelpResetScroll();
bool controlsHelpScrollUp();
bool controlsHelpScrollDown();
void whatsNewResetScroll();
bool whatsNewScrollUp();
bool whatsNewScrollDown();
void uiResetLevelUpPopupState();
bool isPetScreenIntroFadeActive();
void startPetScreenIntroFadeNow();
void resetClockModePetPresentation();
void uiTriggerAlertScreenFlash(uint8_t r, uint8_t g, uint8_t b, uint32_t durationMs = 90);
void uiBeginAlertScreenFlash(uint8_t r, uint8_t g, uint8_t b);
void uiEndAlertScreenFlash();

// Console
void drawConsoleMenu();
void drawConsoleScreen();

// Force one immediate render pass
void forceRenderUIOnce();

// Main UI dispatcher
void renderUI();

// UI Resets
void resetPetScreenPositionToHome();
void startPetIntroWalkFromLeft();

// Level-up modal
void uiShowLevelUpPopup(uint16_t newLevel);
bool uiIsLevelUpPopupActive();
void uiDismissLevelUpPopup();
void uiDrawLevelUpPopup(); // draws overlay into spr (does not push)

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
