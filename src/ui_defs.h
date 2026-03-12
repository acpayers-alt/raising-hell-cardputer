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
  DEATH            = 8,
  BURIAL_SCREEN    = 9,
  PET_SLEEPING     = 10,
  SETTINGS         = 11,
  CONSOLE          = 12,
  INVENTORY        = 13,
  SHOP             = 14,
  SLEEP_MENU       = 15,
  SET_TIME         = 16,
  HATCHING         = 17,
  CONTROLS_HELP    = 18,
  BOOT_WIFI_PROMPT = 19,
  BOOT_WIFI_WAIT   = 20,
  BOOT_TZ_PICK     = 21,
  BOOT_NTP_WAIT    = 22,
  EVOLUTION        = 23,
  MG_PAUSE         = 24,
  BOOT_WIFI_IMPORTED,
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
  SCREEN,
  SYSTEM,
  GAME,
  DECAY_MODE,
  WIFI,
  CONSOLE,
  CREDITS,
  AUTO_SCREEN
};

constexpr int TAB_COUNT_INT() { return (int)Tab::TAB_COUNT; }

#endif // RAISING_HELL_UI_DEFS_H
