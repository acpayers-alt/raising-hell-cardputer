#include "flow_boot_wifi.h"

#include "app_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_input_common.h"   // uiDrainKb()

#include <FS.h>
#include <SD.h>
#include "sdcard.h"

#include "wifi_time.h"
#include "flow_time_editor.h"
#include "new_pet_flow_state.h"

#include "ui_boot_wizard_menu.h"
#include "boot_pipeline.h"
#include "time_state.h"
#include "time_persist.h"

#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "flow_boot_wizard.h"
#include "timezone.h"

#include "launcher_wifi_import.h"
#include "wifi_store.h"
#include "settings_state.h"
#include "wifi_setup_state.h"
#include "save_manager.h"

// These are defined in flow_boot_wizard.cpp
extern UIState g_bootWizardAfterOkState;
extern Tab     g_bootWizardAfterOkTab;

// -----------------------------------------------------------------------------
// Launcher Import
// -----------------------------------------------------------------------------
static bool s_bootWifiImported = false;
static char s_bootWifiImportedSsid[33] = {0};
static uint32_t s_bootWifiImportedAtMs = 0;

void bootWifiSetImportedInfo(const char *ssid)
{
  s_bootWifiImported = true;
  s_bootWifiImportedAtMs = millis();

  if (ssid && ssid[0])
  {
    strlcpy(s_bootWifiImportedSsid, ssid, sizeof(s_bootWifiImportedSsid));
  }
  else
  {
    s_bootWifiImportedSsid[0] = 0;
  }
}

void bootWifiClearImportedInfo()
{
  s_bootWifiImported = false;
  s_bootWifiImportedSsid[0] = 0;
  s_bootWifiImportedAtMs = 0;
}

const char *bootWifiImportedSsid()
{
  return s_bootWifiImportedSsid;
}

static int tzIndexFromDetectedName(const String &tzNameStr)
{
  if (tzNameStr.isEmpty())
    return -1;

  if (tzNameStr == "UTC" || tzNameStr == "Etc/UTC")
    return 0;

  if (tzNameStr == "America/New_York" ||
      tzNameStr == "America/Detroit" ||
      tzNameStr == "America/Indiana/Indianapolis" ||
      tzNameStr == "America/Indiana/Marengo" ||
      tzNameStr == "America/Indiana/Petersburg" ||
      tzNameStr == "America/Indiana/Vevay" ||
      tzNameStr == "America/Indiana/Vincennes" ||
      tzNameStr == "America/Indiana/Winamac" ||
      tzNameStr == "America/Kentucky/Louisville" ||
      tzNameStr == "America/Kentucky/Monticello")
    return 1;

  if (tzNameStr == "America/Chicago" ||
      tzNameStr == "America/Indiana/Knox" ||
      tzNameStr == "America/Indiana/Tell_City" ||
      tzNameStr == "America/Menominee" ||
      tzNameStr == "America/North_Dakota/Beulah" ||
      tzNameStr == "America/North_Dakota/Center" ||
      tzNameStr == "America/North_Dakota/New_Salem")
    return 2;

  if (tzNameStr == "America/Denver" ||
      tzNameStr == "America/Boise")
    return 3;

  if (tzNameStr == "America/Los_Angeles")
    return 4;

  if (tzNameStr == "America/Anchorage" ||
      tzNameStr == "America/Juneau" ||
      tzNameStr == "America/Nome" ||
      tzNameStr == "America/Sitka" ||
      tzNameStr == "America/Yakutat" ||
      tzNameStr == "America/Metlakatla")
    return 5;

  if (tzNameStr == "Pacific/Honolulu")
    return 6;

  return -1;
}

static bool bootTryDetectTimezoneFromWifi()
{
  if (!wifiIsConnectedNow())
    return false;

  HTTPClient http;
  http.setReuse(false);
  http.setTimeout(8000);
  http.addHeader("Accept", "application/json");
  http.addHeader("Cache-Control", "no-cache");
  http.setUserAgent("RaisingHellCardputer/1.0");

  WiFiClient client;
  const char *url = "http://ip-api.com/json/?fields=status,timezone";

  if (!http.begin(client, url))
  {
    Serial.println("[BOOTWIFI] timezone detect http.begin failed");
    return false;
  }

  const int code = http.GET();
  Serial.printf("[BOOTWIFI] timezone detect http=%d\n", code);

  if (code != HTTP_CODE_OK)
  {
    http.end();
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, *http.getStreamPtr());
  http.end();

  if (err)
  {
    Serial.printf("[BOOTWIFI] timezone detect json failed: %s\n", err.c_str());
    return false;
  }

  const char *status = doc["status"] | "";
  const char *tzNameRaw = doc["timezone"] | "";

  if (strcmp(status, "success") != 0 || !tzNameRaw[0])
  {
    Serial.println("[BOOTWIFI] timezone detect no usable timezone");
    return false;
  }

  String tzNameStr = String(tzNameRaw);
  const int detectedIdx = tzIndexFromDetectedName(tzNameStr);

  Serial.printf("[BOOTWIFI] timezone detect raw='%s' mapped=%d\n",
                tzNameStr.c_str(),
                detectedIdx);

  if (detectedIdx < 0)
    return false;

  tzIndex = detectedIdx;
  applyTimezoneIndex((uint8_t)tzIndex);
  saveTzIndexToNVS((uint8_t)tzIndex);

  return true;
}

void uiBootWifiImportedHandle(InputState &in)
{
  const uint32_t kImportedShowMs = 1200;

  const bool continueNow =
      in.selectOnce ||
      in.upOnce ||
      (s_bootWifiImportedAtMs != 0 &&
       (millis() - s_bootWifiImportedAtMs) >= kImportedShowMs);

  if (continueNow)
  {
    uiActionEnterState(UIState::BOOT_WIFI_WAIT, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  uiActionSwallowAll(in);
}

// -----------------------------------------------------------------------------
// BOOT_WIFI_PROMPT
// -----------------------------------------------------------------------------
void uiBootAssetWifiRequiredHandle(InputState &in)
{
  if (!in.selectOnce)
  {
    uiActionSwallowAll(in);
    return;
  }

  g_bootProvisionWifiOnboardingStarted = true;

  String importedSsid;
  String importedPwd;

  if (launcherImportWifiCreds(importedSsid, importedPwd))
  {
    wifiStoreSave(importedSsid, importedPwd);
    settingsSetWifiEnabled(true);
    saveSettingsToSD();

    bootWifiSetImportedInfo(importedSsid.c_str());
    wifiConsoleBeginConnect(importedSsid.c_str(), importedPwd.c_str());

    uiActionEnterState(UIState::BOOT_WIFI_IMPORTED, g_bootWizardAfterOkTab, true);
  }
  else
  {
    g_wifiSetupFromBootWizard = true;
    wifiSetupStage = 0;
    wifiSetupSsid[0] = 0;
    wifiSetupPass[0] = 0;
    wifiSetupBuf[0] = 0;

    settingsSetWifiEnabled(true);
    saveSettingsToSD();

    uiActionEnterState(UIState::WIFI_SETUP, g_bootWizardAfterOkTab, true);
  }

  requestUIRedraw();
  uiActionSwallowAll(in);
  uiDrainKb(in);
  clearInputLatch();
}

void uiBootWifiPromptHandle(InputState& in)
{
  (void)UiBootWizardMenu::HandleWifiPrompt(in);
}

// -----------------------------------------------------------------------------
// Fall back to manual entry if imported wifi hotspot fails to connect
// -----------------------------------------------------------------------------
static void bootWifiFallBackToManualEntry(InputState &in)
{
  wifiConsoleDisconnect(false);
  bootWifiClearImportedInfo();

  g_wifiSetupFromBootWizard = true;
  wifiSetupStage = 0;
  wifiSetupSsid[0] = 0;
  wifiSetupPass[0] = 0;
  wifiSetupBuf[0] = 0;

  uiActionEnterState(UIState::WIFI_SETUP, g_bootWizardAfterOkTab, true);
  requestUIRedraw();
  uiActionSwallowAll(in);
  uiDrainKb(in);
  clearInputLatch();
}

// -----------------------------------------------------------------------------
// BOOT_WIFI_WAIT
// After WiFi setup returns, wait for connection then advance.
// ESC always skips to manual time.
// -----------------------------------------------------------------------------
void uiBootWifiWaitHandle(InputState& in)
{
  if (!g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    beginForcedSetTimeBootGate(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
    requestUIRedraw();
    return;
  }

  if (g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  const int wifiStatus = wifiConsoleStatus();
  const uint32_t connectAgeMs = wifiConsoleConnectAgeMs();

  const bool failedStatus =
      (wifiStatus == WL_CONNECT_FAILED) ||
      (wifiStatus == WL_NO_SSID_AVAIL) ||
      (wifiStatus == WL_CONNECTION_LOST);

  const bool timedOut =
      (connectAgeMs >= 15000) &&
      !wifiIsConnected();

  if (failedStatus || timedOut)
  {
    bootWifiFallBackToManualEntry(in);
    return;
  }
  
  if (wifiIsConnected())
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();

    const bool tzDetected = bootTryDetectTimezoneFromWifi();

    if (tzDetected)
    {
      uiActionEnterState(UIState::BOOT_NTP_WAIT, g_bootWizardAfterOkTab, true);
      requestUIRedraw();
      return;
    }

    uiActionEnterState(UIState::BOOT_TZ_PICK, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    return;
  }

  uiActionSwallowAll(in);
}

// -----------------------------------------------------------------------------
// BOOT_TZ_PICK
// -----------------------------------------------------------------------------
void uiBootTzPickHandle(InputState& in)
{
  (void)UiBootWizardMenu::HandleTimezonePick(in);
}

// -----------------------------------------------------------------------------
// BOOT_NTP_WAIT
// Wait for time sync; ESC skips to manual. Once synced, continue.
// -----------------------------------------------------------------------------
void uiBootNtpWaitHandle(InputState& in)
{
  if (!g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    beginForcedSetTimeBootGate(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
    requestUIRedraw();
    return;
  }

  if (g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  if (timeIsSynced())
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();

    if (g_bootAssetProvisionMustComplete)
    {
      uiActionEnterState(UIState::BOOT, g_bootWizardAfterOkTab, true);
      requestFullUIRedraw();
      requestUIRedraw();
      return;
    }

    UIState nextState = g_bootWizardAfterOkState;

    if (g_sdReady &&
        (SD.exists("/raising_hell/save/save.bin") ||
         SD.exists("raising_hell/save/save.bin")))
    {
      nextState = UIState::PET_SCREEN;
    }

    if (nextState == UIState::CHOOSE_PET)
    {
      g_choosePetBlockHatchUntilRelease = true;
    }

    uiActionEnterState(nextState, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    return;
  }

  uiActionSwallowAll(in);
}