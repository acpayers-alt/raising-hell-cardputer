// ui_menu_state.h
#pragma once

#include "ui_runtime.h"
#include <Arduino.h>
#include <stdint.h>

// Menu indices / UI selection state.
//
// IMPORTANT:
// - shopIndex aliases g_shopScreen.selectedIndex
// - feedMenuIndex aliases g_feedMenu.selectedIndex
// - powerMenuIndex and lastRenderTimeMs alias into g_app
//   (single source of truth).
// - sleepMenuIndex/playMenuIndex/selectedMenu remain standalone legacy globals
//   until you migrate them too.

extern int &shopIndex;
extern int &feedMenuIndex;
extern int &powerMenuIndex;
extern uint32_t &lastRenderTimeMs;

// Also aliases into g_app now (single source of truth)
extern int &sleepMenuIndex;
extern int &playMenuIndex;
extern int &selectedMenu;

// Migrated from g_ui
extern int &screenSettingsIndex;
extern int &systemSettingsIndex;
extern int &gameOptionsIndex;
extern int &playIndex;
extern int &autoScreenIndex;
extern int &decayModeIndex;