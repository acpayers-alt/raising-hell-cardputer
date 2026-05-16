#ifndef RAISING_HELL_UI_DEFS_H
#define RAISING_HELL_UI_DEFS_H

#include <stdint.h>

// --------------------
// Tabs
// --------------------
enum class Tab : uint8_t
{
  TAB_PET,
  TAB_STATS,
  TAB_ACTIVITIES,
  TAB_PLAY,
  TAB_SLEEP,
  TAB_INV,
  TAB_SHOP,
  TAB_COUNT
};

// --------------------
// UI States
// --------------------
enum class UIState : uint8_t
{
  BOOT = 0,
  PET_SCREEN = 1,
  POWER_MENU = 2,
  MINI_GAME = 3,
  CHOOSE_PET = 4,
  NAME_PET = 5,
  WIFI_SETUP = 6,
  WIFI_CONNECT_WAIT = 7,
  DEATH = 8,
  DEATH_TRANSITION = 9,
  BURIAL_SCREEN = 10,
  PET_SLEEPING = 11,
  SETTINGS = 12,
  CONSOLE = 13,
  INVENTORY = 14,
  SHOP = 15,
  SLEEP_MENU = 16,
  SET_TIME = 17,
  HATCHING = 18,
  CONTROLS_HELP = 19,
  BOOT_WIFI_PROMPT = 20,
  BOOT_WIFI_WAIT = 21,
  BOOT_TZ_PICK = 22,
  BOOT_NTP_WAIT = 23,
  EVOLUTION = 24,
  MG_PAUSE = 25,
  BOOT_WIFI_IMPORTED = 26,
  BOOT_ASSET_WIFI_REQUIRED = 27,
  TITLE_MENU = 28,
  IMPORT_PET_LIST = 29,
  BACKUP_PET_LIST = 30,
  CLOCK_MODE = 31,
  WHATS_NEW = 32,
  ACTIVITY_FISHING = 33,
  PHOTO_GALLERY = 34,
};

// --------------------
// Home Dock Apps
// --------------------
enum class HomeApp : uint8_t
{
  PET,
  STATS,
  ACTIVITIES,
  PLAY,
  SLEEP,
  INVENTORY,
  SHOP
};

// --------------------
// Settings Pages
// --------------------
enum class SettingsPage : uint8_t
{
  TOP,
  PET,
  SCREEN,
  SYSTEM,
  GAME,
  DECAY_MODE,
  WIFI,
  STORED_NETWORKS,
  CONSOLE,
  STATUS,
  CREDITS,
  AUTO_SCREEN
};

constexpr int TAB_COUNT_INT() { return (int)Tab::TAB_COUNT; }

#endif // RAISING_HELL_UI_DEFS_H
