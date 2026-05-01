#pragma once

#include <Arduino.h>

void uiResetLevelUpPopupState();

void ui_drawMessageWindow(const char *title, const char *line1, const char *line2, bool maskLine2,
    bool showCursor);
    
void uiShowLevelUpPopup(uint16_t newLevel);
bool uiIsLevelUpPopupActive();
void uiDismissLevelUpPopup();
void uiDrawLevelUpPopup();

void ui_showMessage(const char *msg);
void ui_showTimedMessage(const char *msg, uint32_t durationMs);
bool uiToastIsActive();
bool uiToastIsPersistent();
void uiDismissToast();
void ui_showSuccessMessage(const char *msg);

void uiBeginAlertScreenFlash(uint8_t r, uint8_t g, uint8_t b);
void uiEndAlertScreenFlash();

void uiDrawAlertScreenFlashOverlay();
void uiDrawToastOverlay();

void drawPowerMenu();