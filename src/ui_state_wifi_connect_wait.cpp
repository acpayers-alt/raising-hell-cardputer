#include "ui_state_wifi_connect_wait.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_defs.h"
#include "ui_runtime.h"
#include "wifi_setup_state.h"
#include "wifi_store.h"
#include "wifi_time.h"

// These are defined in flow_boot_wifi.cpp
extern Tab g_bootWizardAfterOkTab;

static bool s_autoRetryIssued = false;
static uint32_t s_connectWaitStartedAtMs = 0;

void wifiResetConnectUiState()
{
  s_autoRetryIssued = false;
  s_connectWaitStartedAtMs = 0;
  g_wifi.connectResultPending = false;
  g_wifi.connectResultSuccess = false;
  g_wifi.connectResultShownAtMs = 0;
  g_wifi.aborted = false;
}

static const char *wifiStatusLabel()
{
  switch (WiFi.status())
  {
  case WL_CONNECTED:
    return "Connected";
  case WL_IDLE_STATUS:
    return "Authorizing...";
  case WL_NO_SSID_AVAIL:
    return "SSID not found";
  case WL_SCAN_COMPLETED:
    return "Scan complete";
  case WL_CONNECT_FAILED:
    return "Connect failed";
  case WL_CONNECTION_LOST:
    return "Connection lost";
  case WL_DISCONNECTED:
    return "Connecting...";
  default:
    return "Connecting...";
  }
}

void uiWifiConnectWaitHandle(InputState &in)
{
  (void)in;

  if (s_connectWaitStartedAtMs == 0)
    s_connectWaitStartedAtMs = millis();

  if (g_wifi.connectResultPending)
  {
    const uint32_t elapsed = millis() - g_wifi.connectResultShownAtMs;
    if (elapsed < 1000)
      return;

    g_wifi.connectResultPending = false;

    if (g_wifi.connectResultSuccess)
    {
      if (g_wifiSetupFromBootWizard)
      {
        uiActionEnterState(UIState::BOOT_WIFI_WAIT, g_bootWizardAfterOkTab, true);
      }
      else
      {
        uiActionEnterState(g_wifi.returnState, g_wifi.returnTab, true);
      }

      requestUIRedraw();
      return;
    }

    // Failure path after message delay
    wifiSetupPass[0] = 0;
    wifiSetupBuf[0] = 0;

    if (g_wifi.connectFailCount < 2)
    {
      wifiSetupStage = WIFI_SETUP_STAGE_PASS;
    }
    else
    {
      g_wifi.connectFailCount = 0;
      wifiSetupStage = WIFI_SETUP_STAGE_SCAN;
      g_wifi.scanStarted = false;
      g_wifi.scanInProgress = false;
      g_wifi.scanCount = 0;
      g_wifi.scanIndex = 0;
    }

    clearInputLatch();
    inputForceClear();

    g_wifi.aborted = false;

    uiActionEnterState(UIState::WIFI_SETUP, g_wifi.returnTab, true);
    requestUIRedraw();
    return;
  }

  const int wifiStatus = wifiConsoleStatus();
  const uint32_t connectAgeMs = wifiConsoleConnectAgeMs();
  const bool reallyConnected = wifiIsConnectedNow();

  const bool failedStatus =
      (wifiStatus == WL_CONNECT_FAILED) || (wifiStatus == WL_NO_SSID_AVAIL) || (wifiStatus == WL_CONNECTION_LOST);

  const uint32_t effectiveAgeMs = connectAgeMs ? connectAgeMs : (millis() - s_connectWaitStartedAtMs);
  const bool timedOut = (effectiveAgeMs >= 25000) && !reallyConnected;

  if (reallyConnected)
  {
    s_autoRetryIssued = false;
    s_connectWaitStartedAtMs = 0;
    g_wifi.connectFailCount = 0;

    if (g_wifi.ssid[0] && g_wifi.pass[0])
    {
      wifiStoreSave(String(g_wifi.ssid), String(g_wifi.pass));
      Serial.printf("[WIFI] saved working creds for SSID: %s\n", g_wifi.ssid);
    }
    else
    {
      Serial.printf("[WIFI] skip save: ssid='%s' passLen=%u\n",
                    g_wifi.ssid,
                    (unsigned)strlen(g_wifi.pass));
    }

    wifiResetConnectUiState();
    g_wifi.connectResultPending = true;
    g_wifi.connectResultSuccess = true;
    g_wifi.connectResultShownAtMs = millis();

    clearInputLatch();
    inputForceClear();
    requestUIRedraw();
    return;
  }

  if (failedStatus || timedOut)
  {
    // One automatic retry before bouncing the user back to password entry.
    if (!s_autoRetryIssued && g_wifi.ssid[0] && g_wifi.pass[0])
    {
      s_autoRetryIssued = true;

      wifiConsoleDisconnect(false);
      delay(250);
      wifiConsoleBeginConnect(g_wifi.ssid, g_wifi.pass);
      s_connectWaitStartedAtMs = millis();

      clearInputLatch();
      inputForceClear();
      requestUIRedraw();
      return;
    }

    s_autoRetryIssued = false;
    s_connectWaitStartedAtMs = 0;

    wifiConsoleDisconnect(false);

    g_wifi.connectFailCount++;
    wifiResetConnectUiState();
    g_wifi.connectResultPending = true;
    g_wifi.connectResultSuccess = false;
    g_wifi.connectResultShownAtMs = millis();

    clearInputLatch();
    inputForceClear();
    requestUIRedraw();
    return;
  }
}