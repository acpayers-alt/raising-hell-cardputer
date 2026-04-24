#include "auto_screen.h"

#include <Arduino.h>

#include "app_state.h"
#include "brightness_state.h"
#include "display.h"
#include "input.h"
#include "input_activity_state.h"
#include "ui_invalidate.h"
#include "ui_state_clock_mode.h"
#include "user_toggles_state.h"

uint8_t autoScreenTimeoutSel = 0;
uint8_t autoClockTimeoutSel = 3;

bool g_screenIsOff = false;

static bool s_autoScreenOffEnabled = false;
static bool s_autoClockModeEnabled = false;

bool autoScreenIsEnabled() { return s_autoScreenOffEnabled && autoScreenTimeoutSel != 3; }

void autoScreenSetEnabled(bool enabled) { s_autoScreenOffEnabled = enabled; }

bool autoClockIsEnabled() { return s_autoClockModeEnabled && autoClockTimeoutSel != 3; }

void autoClockSetEnabled(bool enabled) { s_autoClockModeEnabled = enabled; }

const char *autoScreenToText(uint8_t sel)
{
  switch (sel)
  {
  case 0:
    return "5m";
  case 1:
    return "30m";
  case 2:
    return "1h";
  default:
    return "Off";
  }
}

const char *autoClockToText(uint8_t sel) { return autoScreenToText(sel); }

uint32_t autoScreenTimeoutMsForSel(uint8_t sel)
{
  switch (sel)
  {
  case 0:
    return 5UL * 60UL * 1000UL;
  case 1:
    return 30UL * 60UL * 1000UL;
  case 2:
    return 60UL * 60UL * 1000UL;
  default:
    return 0; // Off
  }
}

uint32_t autoScreenTimeoutMs() { return autoScreenTimeoutMsForSel((uint8_t)autoScreenTimeoutSel); }

uint32_t autoClockTimeoutMsForSel(uint8_t sel) { return autoScreenTimeoutMsForSel(sel); }

uint32_t autoClockTimeoutMs() { return autoClockTimeoutMsForSel((uint8_t)autoClockTimeoutSel); }

void screenSleep()
{
  if (!isScreenOn())
    return;
  SET_SCREEN_POWER(false);
}

void screenWake()
{
  if (isScreenOn())
    return;
  SET_SCREEN_POWER(true);
  requestUIRedraw();
}

void noteUserActivity()
{
  const uint32_t now = millis();
  setLastInputActivityMs(now);

  // Add a window where we suppress waking if screen was turned off manually in the last 250 ms.
  if (!isScreenOn() && (uint32_t)(now - screenPowerLastManualToggleMs()) < 250)
  {
    return;
  }

  if (!isScreenOn())
  {
    screenWake();
  }
}

void autoScreenTick()
{
  const uint8_t sel = (uint8_t)autoScreenTimeoutSel;
  const uint32_t timeout = autoScreenTimeoutMsForSel(sel);
  if (timeout == 0)
    return;

  const uint32_t now = millis();

  const uint32_t kManualToggleGuardMs = 800;
  if ((uint32_t)(now - screenPowerLastManualToggleMs()) < kManualToggleGuardMs)
    return;

  if (isBacklightPulseActive())
    return;

  if (!isScreenOn())
    return;

  if ((uint32_t)(now - g_lastInputActivityMs) >= timeout)
  {
    screenSleep();
  }
}

void autoClockTick()
{
  const uint8_t sel = (uint8_t)autoClockTimeoutSel;
  const uint32_t timeout = autoClockTimeoutMsForSel(sel);
  if (timeout == 0)
    return;

  const uint32_t now = millis();

  const uint32_t kManualToggleGuardMs = 800;
  if ((uint32_t)(now - screenPowerLastManualToggleMs()) < kManualToggleGuardMs)
    return;

  if (isBacklightPulseActive())
    return;

  if (!isScreenOn())
    return;

  // Auto-clock may trigger from the normal PET screen or the sleeping PET screen.
  // Keep the PET-tab requirement so this still behaves like a pet-idle feature.
  const bool petIdleEligibleState = (g_app.uiState == UIState::PET_SCREEN) || (g_app.uiState == UIState::PET_SLEEPING);

  if (!petIdleEligibleState || g_app.currentTab != Tab::TAB_PET)
    return;

  if ((uint32_t)(now - g_lastInputActivityMs) >= timeout)
  {
    openClockModeWithReturnNoInput(g_app.uiState, g_app.currentTab);
  }
}