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
static void leavePetSleepingToReturnTarget(InputState &in, uint16_t drainMs, bool forceRenderNow)
{
  const UIReturnTarget target = uiGetReturnTarget();
  uiPopReturnTarget();

  uiActionEnterStateClean(target.state, target.tab, true, in, drainMs);
  invalidateBackgroundCache();
  requestFullUIRedraw();

  if (forceRenderNow)
    forceRenderUIOnce();
}

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
  graphicsReleaseUiCachesForMiniGame();
  Serial.printf("[HEAPCHK] uiPetSleepingBootEnter after-release free=%u largest=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

  clearInputLatch();
  inputForceClear();
}

void uiPetSleepingSetReturnState(UIState state, Tab tab) { uiSetReturnTarget(state, tab); }

void uiEnterPetSleepingWithReturn(UIState returnState, Tab returnTab, InputState &in, uint16_t drainMs)
{
  uiPushReturnTarget(returnState, returnTab);
  uiActionEnterStateClean(UIState::PET_SLEEPING, Tab::TAB_PET, true, in, drainMs);
  requestFullUIRedraw();
}

void enterSleepFlow(UIState returnState, Tab returnTab, InputState &in, uint16_t drainMs)
{
  uiEnterPetSleepingWithReturn(returnState, returnTab, in, drainMs);
  uiPetSleepingBootEnter();
  sleepBgKickNow();
}

void uiPetSleepingWakeAndReturn(InputState &in, uint16_t drainMs, bool forceRenderNow)
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

  leavePetSleepingToReturnTarget(in, drainMs, forceRenderNow);
}

void uiPetSleepingHandle(InputState &in)
{
  if (!isPetSleepingNow())
  {
    leavePetSleepingToReturnTarget(in, 200, false);
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
    inputForceClear(); // critical: kill carried input
    openSettingsWithReturn(g_app.uiState, g_app.currentTab, SettingsPage::TOP);
    uiActionSwallowAll(in);
    return;
  }

  // Wake explicitly on enter/select (but not immediately on entry)
  if (allowWake && (in.selectOnce || in.encoderPressOnce || in.mgSelectOnce || selectEdgeFallback))
  {
    uiPetSleepingWakeAndReturn(in, 200, true);
    return;
  }
  return;
}
