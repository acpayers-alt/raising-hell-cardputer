#include "ui_state_pet_sleeping.h"

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "led_status.h"
#include "pet.h"
#include "save_manager.h"
#include "settings_flow_state.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_state_settings.h"

// Entry guard to prevent "carried-held ENTER" from instantly waking the pet.
static uint32_t s_enterSleepUiMs = 0;
static bool s_prevSelectHeld = false;

void uiPetSleepingOnEnter(const InputState &in)
{
  s_enterSleepUiMs = millis();

  // Sync this so a held key from the previous screen doesn't look like a fresh edge.
  s_prevSelectHeld = in.selectHeld;

  // Extra safety: clear latches next tick.
  inputForceClear();
}

void uiPetSleepingBootEnter()
{
  s_enterSleepUiMs = millis();

  // We do not have a meaningful live InputState during boot landing,
  // so start from a safe "not held" baseline.
  s_prevSelectHeld = false;

  clearInputLatch();
  inputForceClear();
}

void uiPetSleepingHandle(InputState &in)
{
  if (!isPetSleepingNow())
  {
    uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, true, in, 200);
    invalidateBackgroundCache();
    requestUIRedraw();
    return;
  }

  const uint32_t now = millis();
  const bool allowWake = ((uint32_t)(now - s_enterSleepUiMs) >= 250);

  // held->edge fallback, but only after entry grace period
  const bool selectEdgeFallback = allowWake && (in.selectHeld && !s_prevSelectHeld);
  s_prevSelectHeld = in.selectHeld;

  // MENU/ESC opens Settings WITHOUT waking the pet
  if (in.menuOnce || in.escOnce)
  {
    g_settingsFlow.settingsReturnPage = SettingsPage::TOP;
    openSettingsWithReturn(g_app.uiState, g_app.currentTab, SettingsPage::TOP);
    uiActionSwallowAll(in);
    return;
  }

  // Wake explicitly on enter/select (but not immediately on entry)
  if (allowWake && (in.selectOnce || in.encoderPressOnce || in.mgSelectOnce || selectEdgeFallback))
  {
    g_app.isSleeping = false;
    g_app.sleepUntilAwakened = false;
    g_app.sleepUntilRested = false;
    g_app.sleepingByTimer = false;
    g_app.sleepTargetEnergy = 0;
    g_app.sleepStartTime = 0;
    g_app.sleepDurationMs = 0;

    pet.isSleeping = false;

    saveManagerClearSleepPendingFlag();
    saveManagerMarkDirty();

    graphicsReleaseUiCachesForMiniGame();

    uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, true, in, 200);
    requestFullUIRedraw();
    forceRenderUIOnce();
    return;
  }
  return;
}
