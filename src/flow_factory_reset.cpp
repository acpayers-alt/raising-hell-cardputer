#include "flow_factory_reset.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <SD.h>

#include "app_state.h"
#include "debug.h"
#include "display.h"
#include "factory_reset_state.h"
#include "flow_factory_reset.h"
#include "graphics.h"
#include "input.h"
#include "save_manager.h"
#include "sdcard.h"
#include "sound.h"
#include "ui_runtime.h"
#include "wifi_power.h"

static uint32_t g_factoryResetHoldStartMs = 0;
static const uint32_t FACTORY_RESET_HOLD_MS = 1200;

void doFactoryResetNow()
{
  saveManagerFactoryReset(); // wipes SD saves, NVS, wifi, then restarts
}

static void handleFactoryResetInput(InputState &in) { (void)in; }

void factoryResetResetUiState()
{
  g_factoryReset.confirmActive = false;
  g_factoryReset.confirmIndex = 0;
  g_factoryResetHoldStartMs = 0;
}

bool factoryResetSystemSettingsHook(InputState &input, int systemSettingsIndex)
{
  // If confirm dialog is active, handle it entirely here.
  if (g_factoryReset.confirmActive)
  {
    if (input.menuOnce || input.escOnce)
    {
      g_factoryReset.confirmActive = false;
      g_factoryResetHoldStartMs = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    if (input.leftOnce || input.upOnce)
    {
      g_factoryReset.confirmIndex = 0; // NO
      g_factoryResetHoldStartMs = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }
    if (input.rightOnce || input.downOnce)
    {
      g_factoryReset.confirmIndex = 1; // YES
      g_factoryResetHoldStartMs = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    const bool onYes = (g_factoryReset.confirmIndex == 1);
    if (onYes)
    {
      const bool held = input.selectHeld; // same as isEnterHeldLevel()
      if (held)
      {
        if (g_factoryResetHoldStartMs == 0)
        {
          g_factoryResetHoldStartMs = millis();
        }
        else if ((millis() - g_factoryResetHoldStartMs) >= FACTORY_RESET_HOLD_MS)
        {
          ui_showMessage("Resetting...");
          g_factoryResetHoldStartMs = 0;
          g_factoryReset.confirmActive = false;
          doFactoryResetNow();
          return true;
        }
        requestUIRedraw();
        return true;
      }
      else
      {
        if (g_factoryResetHoldStartMs != 0)
        {
          g_factoryResetHoldStartMs = 0;
          requestUIRedraw();
        }
        return true;
      }
    }

    if (input.selectOnce || input.encoderPressOnce)
    {
      factoryResetResetUiState();
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    return true;
  }

  // If confirm is NOT active, intercept the "Factory Reset" item activate.
  // System rows are:
  //   0 Set Time
  //   1 Time Zone
  //   2 Factory Reset
  //   3 WiFi
  if ((input.selectOnce || input.encoderPressOnce) && systemSettingsIndex == 2)
  {
    g_factoryReset.confirmActive = true;
    g_factoryReset.confirmIndex = 0;
    g_factoryResetHoldStartMs = 0;
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return true;
  }

  return false;
}

void uiFactoryResetHandle(InputState &in) { handleFactoryResetInput(in); }