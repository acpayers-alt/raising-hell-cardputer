#include "ui_sleep_menu_actions.h"

#include <Arduino.h>

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "pet.h"
#include "save_manager.h"
#include "sleep_state.h"
#include "ui_runtime.h"
#include "ui_suppress.h"
#include "ui_actions.h"
#include "ui_state_pet_sleeping.h"

void uiSleepMenuSetUntilAwakened(uint32_t nowMs)
{
  g_app.sleepUntilRested   = false;
  g_app.sleepUntilAwakened = true;
  g_app.sleepStartTime     = nowMs;
  g_app.sleepDurationMs    = 0;
}

void uiSleepMenuSetUntilRested(uint32_t nowMs)
{
  g_app.sleepUntilRested   = true;
  g_app.sleepUntilAwakened = false;
  g_app.sleepStartTime     = nowMs;
  g_app.sleepDurationMs    = 0;
}

void uiSleepMenuSetForHours(uint32_t nowMs, uint32_t hours)
{
  g_app.sleepUntilRested   = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepStartTime     = nowMs;
  g_app.sleepDurationMs    = (uint32_t)(hours) * 60UL * 60UL * 1000UL;
}

void uiSleepMenuEnterSleep(uint32_t nowMs, InputState& in)
{
  (void)nowMs;

  pet.isSleeping   = true;
  g_app.isSleeping = true;
  g_app.sleepTargetEnergy = 0;
  saveManagerMarkDirty();

  uiEnterPetSleepingWithReturn(g_app.uiState, g_app.currentTab, in, 120);

  // Suppress wake detection so the same Enter press that selected
  // a sleep option can't immediately wake the pet on the next tick.
  uiSuppressSleepWakeForMs(400);

  invalidateBackgroundCache();
  sleepBgKickNow();
}