// ui_menu_state.cpp
#include "ui_menu_state.h"
#include "app_state.h"
#include "feed_menu_state.h"
#include "shop_screen_state.h"

// Aliases into g_app (single source of truth)
int &shopIndex = g_shopScreen.selectedIndex;
int &feedMenuIndex = g_feedMenu.selectedIndex;
int &powerMenuIndex = g_app.powerMenuIndex;
uint32_t &lastRenderTimeMs = g_app.lastRenderTimeMs;

// Migrated legacy indices: also live in g_app now
int &sleepMenuIndex = g_app.sleepMenuIndex;
int &playMenuIndex = g_app.playMenuIndex;
int &selectedMenu = g_app.selectedMenu;

// Migrated from g_ui
int &screenSettingsIndex = g_app.screenSettingsIndex;
int &systemSettingsIndex = g_app.systemSettingsIndex;
int &gameOptionsIndex = g_app.gameOptionsIndex;
int &playIndex = g_app.playIndex;
int &autoScreenIndex = g_app.autoScreenIndex;
int &decayModeIndex = g_app.decayModeIndex;