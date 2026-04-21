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
// Also aliased into g_app now as part of the shared UI state.

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
extern int &autoScreenIndex;
extern int &decayModeIndex;