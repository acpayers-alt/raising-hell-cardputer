#include "ui_state_handlers.h"

// Handlers implemented across ui_state_* and flow_* modules.

#include "flow_boot_wifi.h"       // uiBootWifi*Handle
#include "flow_controls_help.h"   // uiControlsHelpHandle
#include "flow_power_menu.h"      // uiPowerMenuHandle
#include "flow_time_editor.h"     // uiSetTimeHandle
#include "flow_boot_wifi.h"

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
#include "ui_state_wifi_connect_wait.h"

static constexpr int kUiStateCount = 29; // UIState is 0..26 in ui_defs.h

static inline int toIndex(UIState s) { return (int)s; }

static StateHandlerFn kHandlers[kUiStateCount] = {
  /*  0 BOOT                       */ nullptr,
  /*  1 HOME                       */ uiTabDrivenHandle,
  /*  2 PET_SCREEN                 */ uiTabDrivenHandle,
  /*  3 POWER_MENU                 */ uiPowerMenuHandle,
  /*  4 MINI_GAME                  */ uiMiniGameHandle,
  /*  5 CHOOSE_PET                 */ uiChoosePetHandle,
  /*  6 NAME_PET                   */ uiNamePetHandle,
  /*  7 WIFI_SETUP                 */ uiWifiSetupHandle,
  /*  8 WIFI_CONNECT_WAIT          */ uiWifiConnectWaitHandle,
  /*  9 DEATH                      */ uiDeathHandle,
  /* 10 DEATH_TRANSITION           */ nullptr,
  /* 11 BURIAL_SCREEN              */ uiBurialHandle,
  /* 12 PET_SLEEPING               */ uiPetSleepingHandle,
  /* 13 SETTINGS                   */ uiSettingsHandle,
  /* 14 CONSOLE                    */ uiConsoleHandle,
  /* 15 INVENTORY                  */ uiInventoryHandle,
  /* 16 SHOP                       */ uiShopHandle,
  /* 17 SLEEP_MENU                 */ uiSleepMenuHandle,
  /* 18 SET_TIME                   */ uiSetTimeHandle,
  /* 19 HATCHING                   */ uiHatchingHandle,
  /* 20 CONTROLS_HELP              */ uiControlsHelpHandle,
  /* 21 BOOT_WIFI_PROMPT           */ uiBootWifiPromptHandle,
  /* 22 BOOT_WIFI_WAIT             */ uiBootWifiWaitHandle,
  /* 23 BOOT_TZ_PICK               */ uiBootTzPickHandle,
  /* 24 BOOT_NTP_WAIT              */ uiBootNtpWaitHandle,
  /* 25 EVOLUTION                  */ uiEvolutionHandle,
  /* 26 MG_PAUSE                   */ uiMgPauseHandle,
  /* 27 BOOT_WIFI_IMPORTED         */ uiBootWifiImportedHandle,
  /* 28 BOOT_ASSET_WIFI_REQUIRED   */ uiBootAssetWifiRequiredHandle,
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