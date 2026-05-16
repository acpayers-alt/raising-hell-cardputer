#include "ui_state_handlers.h"

// -----------------------------------------------------------------------------
// Flow / boot / system flows
// -----------------------------------------------------------------------------
#include "flow_boot_wifi.h"     // uiBootWifi*Handle
#include "flow_controls_help.h" // uiControlsHelpHandle
#include "flow_power_menu.h"    // uiPowerMenuHandle
#include "flow_time_editor.h"   // uiSetTimeHandle
#include "flow_whats_new.h"     // uiWhatsNewHandle

// -----------------------------------------------------------------------------
// Core UI states
// -----------------------------------------------------------------------------
#include "ui_state_backup_pet_list.h" // uiBackupPetListHandle
#include "ui_state_burial.h"          // uiBurialHandle
#include "ui_state_choose_pet.h"      // uiChoosePetHandle
#include "ui_state_clock_mode.h"
#include "ui_state_console.h"         // uiConsoleHandle
#include "ui_state_death.h"           // uiDeathHandle
#include "ui_state_evolution.h"       // uiEvolutionHandle
#include "ui_state_hatching.h"        // uiHatchingHandle
#include "ui_state_import_pet_list.h" // uiImportPetListHandle
#include "ui_state_inventory.h"       // uiInventoryHandle
#include "ui_state_mg_pause.h"        // uiMgPauseHandle
#include "ui_state_mini_game.h"       // uiMiniGameHandle
#include "ui_state_name_pet.h"        // uiNamePetHandle
#include "ui_state_pet.h"             // uiPetScreenHandle
#include "ui_state_pet_sleeping.h"    // uiPetSleepingHandle
#include "ui_state_photo_gallery.h"
#include "ui_state_settings.h"   // uiSettingsHandle
#include "ui_state_shop.h"       // uiShopHandle
#include "ui_state_sleep_menu.h" // uiSleepMenuHandle
#include "ui_state_tab_driven.h" // uiTabDrivenHandle
#include "ui_state_title_menu.h" // uiTitleMenuHandle

#include "activity_fishing.h"

// -----------------------------------------------------------------------------
// WiFi states
// -----------------------------------------------------------------------------
#include "ui_state_wifi_connect_wait.h" // uiWifiConnectWaitHandle
#include "ui_state_wifi_setup.h"        // uiWifiSetupHandle

static constexpr int kUiStateCount = 35;

static inline int toIndex(UIState s) { return (int)s; }

static StateHandlerFn kHandlers[kUiStateCount] = {
    /*  0 BOOT                       */ nullptr,
    /*  1 PET_SCREEN                 */ uiTabDrivenHandle,
    /*  2 POWER_MENU                 */ uiPowerMenuHandle,
    /*  3 MINI_GAME                  */ uiMiniGameHandle,
    /*  4 CHOOSE_PET                 */ uiChoosePetHandle,
    /*  5 NAME_PET                   */ uiNamePetHandle,
    /*  6 WIFI_SETUP                 */ uiWifiSetupHandle,
    /*  7 WIFI_CONNECT_WAIT          */ uiWifiConnectWaitHandle,
    /*  8 DEATH                      */ uiDeathHandle,
    /*  9 DEATH_TRANSITION           */ nullptr,
    /* 10 BURIAL_SCREEN              */ uiBurialHandle,
    /* 11 PET_SLEEPING               */ uiPetSleepingHandle,
    /* 12 SETTINGS                   */ uiSettingsHandle,
    /* 13 CONSOLE                    */ uiConsoleHandle,
    /* 14 INVENTORY                  */ uiInventoryHandle,
    /* 15 SHOP                       */ uiShopHandle,
    /* 16 SLEEP_MENU                 */ uiSleepMenuHandle,
    /* 17 SET_TIME                   */ uiSetTimeHandle,
    /* 18 HATCHING                   */ uiHatchingHandle,
    /* 19 CONTROLS_HELP              */ uiControlsHelpHandle,
    /* 20 BOOT_WIFI_PROMPT           */ uiBootWifiPromptHandle,
    /* 21 BOOT_WIFI_WAIT             */ uiBootWifiWaitHandle,
    /* 22 BOOT_TZ_PICK               */ uiBootTzPickHandle,
    /* 23 BOOT_NTP_WAIT              */ uiBootNtpWaitHandle,
    /* 24 EVOLUTION                  */ uiEvolutionHandle,
    /* 25 MG_PAUSE                   */ uiMgPauseHandle,
    /* 26 BOOT_WIFI_IMPORTED         */ uiBootWifiImportedHandle,
    /* 27 BOOT_ASSET_WIFI_REQUIRED   */ uiBootAssetWifiRequiredHandle,
    /* 28 TITLE_MENU                 */ uiTitleMenuHandle,
    /* 29 IMPORT_PET_LIST            */ uiImportPetListHandle,
    /* 30 BACKUP_PET_LIST            */ uiBackupPetListHandle,
    /* 31 CLOCK_MODE                 */ uiClockModeHandle,
    /* 32 WHATS_NEW                  */ uiWhatsNewHandle,
    /* 33 ACTIVITY_FISHING           */ activityFishingHandle,
    /* 34 PHOTO_GALLERY              */ uiPhotoGalleryHandle,
};

StateHandlerFn uiGetStateHandler(UIState state)
{
  const int idx = toIndex(state);
  if (idx < 0 || idx >= kUiStateCount)
    return nullptr;
  return kHandlers[idx];
}

bool uiDispatchToStateHandler(UIState state, InputState &in)
{
  StateHandlerFn fn = uiGetStateHandler(state);
  if (!fn)
    return false;
  fn(in);
  return true;
}