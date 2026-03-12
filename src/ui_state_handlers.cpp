#include "ui_state_handlers.h"

// Handlers implemented across ui_state_* and flow_* modules.

#include "flow_boot_wifi.h"       // uiBootWifi*Handle
#include "flow_controls_help.h"   // uiControlsHelpHandle
#include "flow_power_menu.h"      // uiPowerMenuHandle
#include "flow_time_editor.h"     // uiSetTimeHandle

#include "ui_state_burial.h"      // uiBurialHandle
#include "ui_state_choose_pet.h"  // uiChoosePetHandle
#include "ui_state_console.h"     // uiConsoleHandle
#include "ui_state_death.h"       // uiDeathHandle
#include "ui_state_evolution.h"   // uiEvolutionHandle
#include "ui_state_hatching.h"    // uiHatchingHandle
#include "ui_state_inventory.h"   // uiInventoryHandle
#include "ui_state_mg_pause.h"    // uiMgPauseHandle
#include "ui_state_mini_game.h"   // uiMiniGameHandle
#include "ui_state_name_pet.h"    // uiNamePetHandle
#include "ui_state_pet.h"         // uiPetScreenHandle
#include "ui_state_pet_sleeping.h"// uiPetSleepingHandle
#include "ui_state_settings.h"    // uiSettingsHandle
#include "ui_state_shop.h"        // uiShopHandle
#include "ui_state_sleep_menu.h"  // uiSleepMenuHandle
#include "ui_state_tab_driven.h"  // uiTabDrivenHandle
#include "ui_state_wifi_setup.h"  // uiWifiSetupHandle

static constexpr int kUiStateCount = 26; // UIState is 0..25 in ui_defs.h

static inline int toIndex(UIState s) { return (int)s; }

// IMPORTANT: indices MUST match UIState enum values in ui_defs.h exactly.
// If the enum is renumbered, update this table to match.
//
// Current enum (ui_defs.h):
//   BOOT=0, HOME=1, PET_SCREEN=2, POWER_MENU=3, MINI_GAME=4,
//   CHOOSE_PET=5, NAME_PET=6, WIFI_SETUP=7, DEATH=8, BURIAL_SCREEN=9,
//   PET_SLEEPING=10, SETTINGS=11, CONSOLE=12, INVENTORY=13, SHOP=14,
//   SLEEP_MENU=15, SET_TIME=16, HATCHING=17, CONTROLS_HELP=18,
//   BOOT_WIFI_PROMPT=19, BOOT_WIFI_WAIT=20, BOOT_TZ_PICK=21,
//   BOOT_NTP_WAIT=22, EVOLUTION=23, MG_PAUSE=24, BOOT_WIFI_IMPORTED=25
static StateHandlerFn kHandlers[kUiStateCount] = {
    /*  0 BOOT               */ nullptr,
    /*  1 HOME               */ uiTabDrivenHandle,
    /*  2 PET_SCREEN         */ uiTabDrivenHandle,
    /*  3 POWER_MENU         */ uiPowerMenuHandle,
    /*  4 MINI_GAME          */ uiMiniGameHandle,
    /*  5 CHOOSE_PET         */ uiChoosePetHandle,
    /*  6 NAME_PET           */ uiNamePetHandle,
    /*  7 WIFI_SETUP         */ uiWifiSetupHandle,
    /*  8 DEATH              */ uiDeathHandle,
    /*  9 BURIAL_SCREEN      */ uiBurialHandle,
    /* 10 PET_SLEEPING       */ uiPetSleepingHandle,
    /* 11 SETTINGS           */ uiSettingsHandle,
    /* 12 CONSOLE            */ uiConsoleHandle,
    /* 13 INVENTORY          */ uiInventoryHandle,
    /* 14 SHOP               */ uiShopHandle,
    /* 15 SLEEP_MENU         */ uiSleepMenuHandle,
    /* 16 SET_TIME           */ uiSetTimeHandle,
    /* 17 HATCHING           */ uiHatchingHandle,
    /* 18 CONTROLS_HELP      */ uiControlsHelpHandle,
    /* 19 BOOT_WIFI_PROMPT   */ uiBootWifiPromptHandle,
    /* 20 BOOT_WIFI_WAIT     */ uiBootWifiWaitHandle,
    /* 21 BOOT_TZ_PICK       */ uiBootTzPickHandle,
    /* 22 BOOT_NTP_WAIT      */ uiBootNtpWaitHandle,
    /* 23 EVOLUTION          */ uiEvolutionHandle,
    /* 24 MG_PAUSE           */ uiMgPauseHandle,
    /* 25 BOOT_WIFI_IMPORTED */ uiBootWifiImportedHandle,
};

StateHandlerFn uiGetStateHandler(UIState state)
{
  const int idx = toIndex(state);
  if (idx < 0 || idx >= kUiStateCount)
    return nullptr;
  return kHandlers[idx];
}

bool uiDispatchToStateHandler(UIState state, InputState& in)
{
  StateHandlerFn fn = uiGetStateHandler(state);
  if (!fn)
    return false;
  fn(in);
  return true;
}