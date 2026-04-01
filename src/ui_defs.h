#ifndef RAISING_HELL_UI_DEFS_H
#define RAISING_HELL_UI_DEFS_H

#include <stdint.h>

// --------------------
// Tabs
// --------------------
enum class Tab : uint8_t {
  TAB_PET,
  TAB_STATS,
  TAB_FEED,
  TAB_PLAY,
  TAB_SLEEP,
  TAB_INV,
  TAB_SHOP,
  TAB_COUNT
};

// --------------------
// UI States
// --------------------
enum class UIState : uint8_t {
  BOOT             = 0,
  HOME             = 1,
  PET_SCREEN       = 2,
  POWER_MENU       = 3,
  MINI_GAME        = 4,
  CHOOSE_PET       = 5,
  NAME_PET         = 6,
  WIFI_SETUP       = 7,
  WIFI_CONNECT_WAIT = 8,
  DEATH            = 9,
  DEATH_TRANSITION = 10,
  BURIAL_SCREEN    = 11,
  PET_SLEEPING     = 12,
  SETTINGS         = 13,
  CONSOLE          = 14,
  INVENTORY        = 15,
  SHOP             = 16,
  SLEEP_MENU       = 17,
  SET_TIME         = 18,
  HATCHING         = 19,
  CONTROLS_HELP    = 20,
  BOOT_WIFI_PROMPT = 21,
  BOOT_WIFI_WAIT   = 22,
  BOOT_TZ_PICK     = 23,
  BOOT_NTP_WAIT    = 24,
  EVOLUTION        = 25,
  MG_PAUSE         = 26,
  BOOT_WIFI_IMPORTED = 27,
  BOOT_ASSET_WIFI_REQUIRED = 28,
  TITLE_MENU       = 29,
  IMPORT_PET_LIST  = 30,
};

// --------------------
// Home Dock Apps
// --------------------
enum class HomeApp : uint8_t {
  PET,
  STATS,
  FEED,
  PLAY,
  SLEEP,
  INVENTORY,
  SHOP
};

// --------------------
// Settings Pages
// --------------------
enum class SettingsPage : uint8_t {
  TOP,
  PET,
  SCREEN,
  SYSTEM,
  GAME,
  DECAY_MODE,
  WIFI,
  CONSOLE,
  STATUS,
  CREDITS,
  AUTO_SCREEN
};

constexpr int TAB_COUNT_INT() { return (int)Tab::TAB_COUNT; }

#endif // RAISING_HELL_UI_DEFS_H
