#include "flow_boot_wifi.h"

#include "app_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_input_common.h" // uiDrainKb()
#include "ui_runtime.h"

#include "sdcard.h"
#include <FS.h>
#include <SD.h>

#include "flow_time_editor.h"
#include "new_pet_flow_state.h"
#include "wifi_time.h"

#include "boot_pipeline.h"
#include "time_persist.h"
#include "time_state.h"
#include "ui_boot_wizard_menu.h"

#include "flow_boot_wizard.h"
#include "timezone.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "graphics.h"
#include "launcher_wifi_import.h"
#include "save_manager.h"
#include "settings_state.h"
#include "wifi_setup_state.h"
#include "wifi_store.h"
#include "ui_state_wifi_connect_wait.h"

// These are defined in flow_boot_wizard.cpp
extern UIState g_bootWizardAfterOkState;
extern Tab g_bootWizardAfterOkTab;

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

const char *bootWifiImportedSsid() { return s_bootWifiImportedSsid; }

static int tzIndexFromDetectedName(const String &tzNameStr)
{
  if (tzNameStr.isEmpty())
    return -1;

  if (tzNameStr == "UTC" || tzNameStr == "Etc/UTC")
    return 0;

  if (tzNameStr == "America/New_York" || tzNameStr == "America/Detroit" ||
      tzNameStr == "America/Indiana/Indianapolis" || tzNameStr == "America/Indiana/Marengo" ||
      tzNameStr == "America/Indiana/Petersburg" || tzNameStr == "America/Indiana/Vevay" ||
      tzNameStr == "America/Indiana/Vincennes" || tzNameStr == "America/Indiana/Winamac" ||
      tzNameStr == "America/Kentucky/Louisville" || tzNameStr == "America/Kentucky/Monticello")
    return 1;

  if (tzNameStr == "America/Chicago" || tzNameStr == "America/Indiana/Knox" ||
      tzNameStr == "America/Indiana/Tell_City" || tzNameStr == "America/Menominee" ||
      tzNameStr == "America/North_Dakota/Beulah" || tzNameStr == "America/North_Dakota/Center" ||
      tzNameStr == "America/North_Dakota/New_Salem")
    return 2;

  if (tzNameStr == "America/Denver" || tzNameStr == "America/Boise")
    return 3;

  if (tzNameStr == "America/Los_Angeles")
    return 4;

  if (tzNameStr == "America/Anchorage" || tzNameStr == "America/Juneau" || tzNameStr == "America/Nome" ||
      tzNameStr == "America/Sitka" || tzNameStr == "America/Yakutat" || tzNameStr == "America/Metlakatla")
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

  Serial.printf("[BOOTWIFI] timezone detect raw='%s' mapped=%d\n", tzNameStr.c_str(), detectedIdx);

  if (detectedIdx < 0)
    return false;

  tzIndex = detectedIdx;
  applyTimezoneIndex((uint8_t)tzIndex);
  saveTzIndexToNVS((uint8_t)tzIndex);

  return true;
}

void uiBootWifiImportedHandle(InputState &in)
{
  if (g_wifi.aborted)
  {
    Serial.println("[BOOTWIFI] aborted by user");

    g_wifi.aborted = false;

    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();

    wifiResetConnectUiState();
    uiActionEnterState(UIState::BOOT_WIFI_PROMPT, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    return;
  }

  const uint32_t kImportedShowMs = 1200;

  const bool continueNow = in.selectOnce || in.upOnce ||
                           (s_bootWifiImportedAtMs != 0 && (millis() - s_bootWifiImportedAtMs) >= kImportedShowMs);

  if (continueNow)
  {
    wifiResetConnectUiState();
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

  String storedSsid;
  String storedPwd;
  String importedSsid;
  String importedPwd;

  // Prefer creds we already saved ourselves.
  if (wifiStoreLoad(storedSsid, storedPwd) && storedSsid.length() > 0)
  {
    settingsSetWifiEnabled(true);
    saveSettingsToSD();
  
    Serial.printf("[BOOTWIFI] using stored wifi creds ssid='%s'\n", storedSsid.c_str());
  
    g_wifi.connectFailCount = 0;
    g_wifi.aborted = false;
    g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
    g_wifi.returnTab = g_bootWizardAfterOkTab;
  
    strlcpy(wifiSetupSsid, storedSsid.c_str(), sizeof(wifiSetupSsid));
    strlcpy(wifiSetupPass, storedPwd.c_str(), sizeof(wifiSetupPass));
    wifiSetupBuf[0] = 0;
  
    wifiResetConnectUiState();
    wifiConsoleBeginConnect(storedSsid.c_str(), storedPwd.c_str());
  
    uiActionEnterState(UIState::BOOT_WIFI_WAIT, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  // If no stored creds exist, try launcher import once.
  if (launcherImportWifiCreds(importedSsid, importedPwd))
  {
    settingsSetWifiEnabled(true);
    saveSettingsToSD();
  
    Serial.printf("[BOOTWIFI] using imported wifi creds ssid='%s'\n", importedSsid.c_str());
  
    bootWifiSetImportedInfo(importedSsid.c_str());
  
    g_wifi.connectFailCount = 0;
    g_wifi.aborted = false;
    g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
    g_wifi.returnTab = g_bootWizardAfterOkTab;
  
    strlcpy(wifiSetupSsid, importedSsid.c_str(), sizeof(wifiSetupSsid));
    strlcpy(wifiSetupPass, importedPwd.c_str(), sizeof(wifiSetupPass));
    wifiSetupBuf[0] = 0;
  
    wifiResetConnectUiState();
    wifiConsoleBeginConnect(importedSsid.c_str(), importedPwd.c_str());
  
    uiActionEnterState(UIState::BOOT_WIFI_IMPORTED, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }
  
  // Otherwise go to manual setup.
  g_wifiSetupFromBootWizard = true;
  wifiSetupStage = WIFI_SETUP_STAGE_SCAN;
  wifiSetupSsid[0] = 0;
  wifiSetupPass[0] = 0;
  wifiSetupBuf[0] = 0;

  g_wifi.scanStarted = false;
  g_wifi.scanInProgress = false;
  g_wifi.scanCount = 0;
  g_wifi.scanIndex = 0;
  g_wifi.connectFailCount = 0;

  settingsSetWifiEnabled(true);
  saveSettingsToSD();

  g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
  g_wifi.returnTab = g_bootWizardAfterOkTab;
  g_wifi.aborted = false;

  wifiResetConnectUiState();
  uiActionEnterState(UIState::WIFI_SETUP, g_bootWizardAfterOkTab, true);

  requestUIRedraw();
  uiActionSwallowAll(in);
  uiDrainKb(in);
  clearInputLatch();
}

void uiBootWifiPromptHandle(InputState &in) { (void)UiBootWizardMenu::HandleWifiPrompt(in); }

// -----------------------------------------------------------------------------
// Fall back to manual entry if imported wifi hotspot fails to connect
// -----------------------------------------------------------------------------
static void bootWifiFallBackToManualEntry(InputState &in)
{
  wifiConsoleDisconnect(false);
  bootWifiClearImportedInfo();

  g_wifiSetupFromBootWizard = true;
  g_wifi.connectFailCount = 0;

  // If we already know an SSID, start at password entry.
  if (wifiSetupSsid[0] != 0)
  {
    wifiSetupStage = WIFI_SETUP_STAGE_PASS;
  }
  else
  {
    wifiSetupStage = WIFI_SETUP_STAGE_SCAN;
  }

  wifiSetupPass[0] = 0;
  wifiSetupBuf[0] = 0;

  g_wifi.scanStarted = false;
  g_wifi.scanInProgress = false;
  g_wifi.scanCount = 0;
  g_wifi.scanIndex = 0;

  g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
  g_wifi.returnTab = g_bootWizardAfterOkTab;
  g_wifi.aborted = false;

  wifiResetConnectUiState();
  uiActionEnterState(UIState::WIFI_SETUP, g_bootWizardAfterOkTab, true);
  requestUIRedraw();
  uiActionSwallowAll(in);
  uiDrainKb(in);
  clearInputLatch();
}

static void bootWifiRetryOrReturnToScan(InputState &in)
{
  wifiConsoleDisconnect(false);

  g_wifi.connectFailCount++;

  ui_showMessage("Connection failed\nPlease retry");
  requestUIRedraw();
  delay(900);

  g_wifiSetupFromBootWizard = true;
  wifiSetupPass[0] = 0;
  wifiSetupBuf[0] = 0;

  const bool haveKnownSsid = (wifiSetupSsid[0] != 0);

  if (g_wifi.connectFailCount < 2 && haveKnownSsid)
  {
    // First failure: retry password for same SSID
    wifiSetupStage = WIFI_SETUP_STAGE_PASS;
  }
  else
  {
    // Second failure, or no SSID known: back to scan picker
    g_wifi.connectFailCount = 0;
    wifiSetupStage = WIFI_SETUP_STAGE_SCAN;
    wifiSetupSsid[0] = 0;

    g_wifi.scanStarted = false;
    g_wifi.scanInProgress = false;
    g_wifi.scanCount = 0;
    g_wifi.scanIndex = 0;
  }

  g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
  g_wifi.returnTab = g_bootWizardAfterOkTab;
  g_wifi.aborted = false;

  wifiResetConnectUiState();
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
void uiBootWifiWaitHandle(InputState &in)
{
  if (g_wifi.aborted)
  {
    g_wifi.aborted = false;
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    wifiResetConnectUiState();
    uiActionEnterState(UIState::BOOT_WIFI_PROMPT, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    return;
  }

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
  const bool reallyConnected = wifiIsConnectedNow();

  const bool failedStatus =
      (wifiStatus == WL_CONNECT_FAILED) || (wifiStatus == WL_NO_SSID_AVAIL) || (wifiStatus == WL_CONNECTION_LOST);

  const bool timedOut = (connectAgeMs >= 15000) && !reallyConnected;

  if (failedStatus || timedOut)
  {
    if (bootWifiImportedSsid()[0] != 0)
      bootWifiFallBackToManualEntry(in);
    else
      bootWifiRetryOrReturnToScan(in);
    return;
  }

  if (reallyConnected)
  {
    g_wifi.connectFailCount = 0;
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();

    const bool tzDetected = bootTryDetectTimezoneFromWifi();

    if (tzDetected)
    {
      wifiStartSntpNow();
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
void uiBootTzPickHandle(InputState &in) { (void)UiBootWizardMenu::HandleTimezonePick(in); }

// -----------------------------------------------------------------------------
// BOOT_NTP_WAIT
// Wait for time sync; ESC skips to manual. Once synced, continue.
// -----------------------------------------------------------------------------
void uiBootNtpWaitHandle(InputState &in)
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

    if (nextState == UIState::CHOOSE_PET)
    {
      g_choosePetBlockHatchUntilRelease = true;
    }
    else
    {
      bootSetupClearPendingFlag();
    }

    uiActionEnterState(nextState, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    return;
  }

  uiActionSwallowAll(in);
}