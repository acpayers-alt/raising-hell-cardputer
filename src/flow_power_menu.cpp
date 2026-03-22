// flow_power_menu.cpp
#include "flow_power_menu.h"

#include <Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <stdint.h>

#include "app_state.h"
#include "auto_screen.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "input_activity_state.h"
#include "pet.h"
#include "save_manager.h"
#include "settings_flow_state.h"
#include "sleep_state.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_input_common.h"
#include "ui_menu_state.h"
#include "ui_runtime.h"
#include "ui_suppress.h"
#include "wifi_power.h"

#include "ui_power_menu.h"
#include "ui_sleep_menu_actions.h"

bool powerMenuSleepWakeSuppressedNow() { return uiIsSleepWakeSuppressed(); }

void openPowerMenuFromHere(uint32_t nowMs)
{
  (void)nowMs;

  if (g_app.uiState == UIState::POWER_MENU)
    return;

  g_settingsFlow.powerMenuReturn.state = g_app.uiState;
  g_settingsFlow.powerMenuReturn.tab = g_app.currentTab;

  powerMenuIndex = 0;
  uiActionEnterState(UIState::POWER_MENU, g_app.currentTab, true);

  clearInputLatch();
  inputForceClear();

  requestFullUIRedraw();
}

void powerMenuActSleep()
{
  // Sleep (pet sleep, not deep sleep)
  powerMenuClose();
  uiSleepMenuEnterSleep(millis());
}

void powerMenuActReboot()
{
  saveManagerForce();
  delay(40);
  ESP.restart();
}

void powerMenuActShutdown()
{
  saveManagerForce();
  applyWifiPower(false);

  SET_SCREEN_POWER(false);

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);

  rtc_gpio_deinit(GPIO_NUM_0);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);

  delay(60);
  esp_deep_sleep_start();
}

void powerMenuClose()
{
  const bool returningToSleep = g_settingsFlow.powerMenuReturnToSleep;

  if (returningToSleep)
  {
    uiActionEnterState(UIState::PET_SLEEPING, Tab::TAB_PET, true);
  }
  else
  {
    uiActionEnterState(g_settingsFlow.powerMenuReturn.state,
                       g_settingsFlow.powerMenuReturn.tab,
                       true);
  }

  g_settingsFlow.powerMenuReturnToSleep = false;
  g_settingsFlow.powerMenuReturn = ReturnTarget{};
}

static void handlePowerMenuInput(InputState &input)
{
  bool exitPressed = (input.menuOnce || input.escOnce);

  if (!exitPressed)
  {
    while (input.kbHasEvent())
    {
      KeyEvent e = input.kbPop();
      const uint8_t c = e.code;

      const bool isQ = (c == 'q' || c == 'Q');
      const bool isDelOrBackspace =
          (c == (uint8_t)KEY_BACKSPACE) || (c == '\b') || (c == 127) || (c == 0x2A);
      const bool isEsc = (c == 27);

      if (isQ || isDelOrBackspace || isEsc)
      {
        exitPressed = true;
        break;
      }
    }
  }
  else
  {
    uiDrainKb(input);
  }

  if (exitPressed)
  {
    const bool returningToSleep = g_settingsFlow.powerMenuReturnToSleep;

    if (returningToSleep)
    {
      uiActionEnterState(UIState::PET_SLEEPING, Tab::TAB_PET, true);
    }
    else
    {
      uiActionEnterState(g_settingsFlow.powerMenuReturn.state,
                         g_settingsFlow.powerMenuReturn.tab,
                         true);
    }

    g_settingsFlow.powerMenuReturnToSleep = false;
    g_settingsFlow.powerMenuReturn = ReturnTarget{};

    uiGuardTransition(input, returningToSleep ? 400 : 0);
    return;
  }

  const int itemCount = uiPowerMenuCount();

  int mv = 0;
  if (input.upOnce) mv = -1;
  else if (input.downOnce) mv = +1;
  else if (input.encoderDelta < 0) mv = -1;
  else if (input.encoderDelta > 0) mv = +1;

  if (mv != 0)
  {
    powerMenuIndex += mv;
    while (powerMenuIndex < 0) powerMenuIndex += itemCount;
    powerMenuIndex %= itemCount;

    requestUIRedraw();
    playBeep();

    input.clearEdges();
    return;
  }

  const bool doSelect = (input.selectOnce || input.encoderPressOnce);
  if (doSelect)
  {
    playBeep();

    input.clearEdges();
    inputForceClear();
    clearInputLatch();

    uiPowerMenuActivate(powerMenuIndex, input);
    return;
  }

  uiDrainKb(input);
  input.clearEdges();
}

  void uiPowerMenuHandle(InputState & in) { handlePowerMenuInput(in); }