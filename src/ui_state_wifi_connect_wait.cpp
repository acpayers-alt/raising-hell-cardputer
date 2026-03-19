#include "ui_state_wifi_connect_wait.h"

#include <WiFi.h>

#include "app_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_defs.h"
#include "wifi_setup_state.h"
#include "wifi_time.h"

// These are defined in flow_boot_wifi.cpp
extern Tab g_bootWizardAfterOkTab;

void uiWifiConnectWaitHandle(InputState &in)
{
  (void)in;

  const int wifiStatus = wifiConsoleStatus();
  const uint32_t connectAgeMs = wifiConsoleConnectAgeMs();

  const bool failedStatus =
      (wifiStatus == WL_CONNECT_FAILED) ||
      (wifiStatus == WL_NO_SSID_AVAIL) ||
      (wifiStatus == WL_CONNECTION_LOST);

  const bool timedOut =
      (connectAgeMs >= 15000) &&
      !wifiIsConnected();

  if (wifiIsConnected())
  {
    g_wifi.connectFailCount = 0;

    clearInputLatch();
    inputForceClear();

    if (g_wifiSetupFromBootWizard)
    {
      uiActionEnterState(UIState::BOOT_WIFI_WAIT, g_bootWizardAfterOkTab, true);
    }
    else
    {
      uiActionEnterState(UIState::SETTINGS, g_app.currentTab, true);
    }

    requestUIRedraw();
    return;
  }

  if (failedStatus || timedOut)
  {
    wifiConsoleDisconnect(false);
    g_wifi.connectFailCount++;

    // Clear the password/buffer so retry is clean.
    wifiSetupPass[0] = 0;
    wifiSetupBuf[0] = 0;

    if (g_wifi.connectFailCount < 2)
    {
      // First failure: keep same SSID, retry password.
      wifiSetupStage = WIFI_SETUP_STAGE_PASS;
    }
    else
    {
      // Second failure: back to scan picker.
      g_wifi.connectFailCount = 0;
      wifiSetupStage = WIFI_SETUP_STAGE_SCAN;

      g_wifi.scanStarted = false;
      g_wifi.scanInProgress = false;
      g_wifi.scanCount = 0;
      g_wifi.scanIndex = 0;
    }

    clearInputLatch();
    inputForceClear();

    uiActionEnterState(UIState::WIFI_SETUP, g_app.currentTab, true);
    requestUIRedraw();
    return;
  }
}