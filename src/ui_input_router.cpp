#include "ui_input_router.h"

#include "app_state.h"
#include "controls_help_state.h"
#include "flow_power_menu.h"
#include "input.h"
#include "settings_nav_state.h"
#include "ui_actions.h"
#include "ui_input_interceptors.h"
#include "ui_state_backup_pet_list.h"
#include "ui_state_choose_pet.h"
#include "ui_state_handlers.h"
#include "ui_state_import_pet_list.h"
#include "ui_state_pet_sleeping.h"
#include "ui_state_title_menu.h"
#include "whats_new_state.h"
#include "wifi_setup_state.h"

#include "activity_fishing.h"

bool g_suppressMenuTick = false;

// ----------------------------------------------------------------------------
// Text capture policy
// ----------------------------------------------------------------------------
bool uiWantsTextCaptureForState(UIState s)
{
  switch (s)
  {
  case UIState::CONSOLE:
  case UIState::NAME_PET:
    return true;

  case UIState::WIFI_SETUP:
    return (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID) || (g_wifi.setupStage == WIFI_SETUP_STAGE_PASS);

  default:
    return false;
  }
}

bool uiWantsTextCaptureNow() { return uiWantsTextCaptureForState(g_app.uiState); }

// ----------------------------------------------------------------------------
// No-input states (swallow edges)
// ----------------------------------------------------------------------------
static inline bool isNoInputState(UIState s)
{
  switch (s)
  {
  case UIState::BOOT:
  case UIState::DEATH_TRANSITION:
    return true;
  default:
    return false;
  }
}

static bool dispatchToHandler(UIState s, InputState &in) { return uiDispatchToStateHandler(s, in); }

static void uiRunStateEntryHooks(UIState state, InputState &in)
{
  switch (state)
  {
  case UIState::PET_SLEEPING:
    uiPetSleepingOnEnter(in);
    break;

  case UIState::TITLE_MENU:
    uiTitleMenuOnEnter(in);
    break;

  case UIState::BACKUP_PET_LIST:
    uiBackupPetListOnEnter(in);
    break;

  case UIState::IMPORT_PET_LIST:
    uiImportPetListOnEnter(in);
    break;

  case UIState::CHOOSE_PET:
    uiChoosePetOnEnter(in);
    break;

  case UIState::CONTROLS_HELP:
    controlsHelpOnEnter();
    break;

  case UIState::WHATS_NEW:
    whatsNewOnEnter();
    break;

  case UIState::SETTINGS:
    // Do not reset the settings page here.
    // Some flows intentionally return to a specific subpage
    // (for example Pet Options after Restore/Import browsers).
    //
    // Also do not clear input here. Settings entry is already consumed by the
    // transition/opening path. Clearing edges on the first router tick after
    // entry can swallow the user's first real Enter press in Settings.
    break;

  case UIState::POWER_MENU:
    clearInputLatch();
    in.clearEdges();
    break;

  case UIState::ACTIVITY_FISHING:
    activityFishingOnEnter();
    break;

  default:
    break;
  }
}

bool uiHandleInput(InputState &in)
{
  g_suppressMenuTick = false;

  static bool s_lastTextCapture = false;
  const bool desiredTextCapture = uiWantsTextCaptureNow();
  if (desiredTextCapture != s_lastTextCapture)
  {
    inputSetTextCapture(desiredTextCapture);
    s_lastTextCapture = desiredTextCapture;
  }

  // Phase 1: global interceptors
  if (uiHandleGlobalInterceptors(in))
    return true;

  // ---- State entry hook (prevents carried-held edges on entry) ----
  static UIState s_prevState = UIState::BOOT;
  if (g_app.uiState != s_prevState)
  {
    uiRunStateEntryHooks(g_app.uiState, in);
    s_prevState = g_app.uiState;
  }

  // ---------------------------------------------------------------

  if (isNoInputState(g_app.uiState))
  {
    uiActionSwallowEdges(in);
    return true;
  }

  // Phase 2: per-state handler
  return dispatchToHandler(g_app.uiState, in);
}
