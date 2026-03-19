#include "ui_input_router.h"

#include "app_state.h"
#include "input.h"
#include "wifi_setup_state.h"
#include "ui_actions.h"
#include "ui_input_interceptors.h"
#include "ui_state_handlers.h"
#include "ui_state_pet_sleeping.h"

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
      return (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID) ||
             (g_wifi.setupStage == WIFI_SETUP_STAGE_PASS);

    default:
      return false;
  }
}

bool uiWantsTextCaptureNow()
{
  return uiWantsTextCaptureForState(g_app.uiState);
}

// ----------------------------------------------------------------------------
// No-input states (swallow edges)
// ----------------------------------------------------------------------------
static inline bool isNoInputState(UIState s)
{
  switch (s)
  {
    case UIState::BOOT:
      return true;
    default:
      return false;
  }
}

static bool dispatchToHandler(UIState s, InputState& in)
{
  return uiDispatchToStateHandler(s, in);
}

bool uiHandleInput(InputState& in)
{
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
    if (g_app.uiState == UIState::PET_SLEEPING)
      uiPetSleepingOnEnter(in);

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
