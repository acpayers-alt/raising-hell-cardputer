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
#include "ui_state_wifi_connect_wait.h"
#include "wifi_setup_state.h"
#include "wifi_store.h"

// These are defined in flow_boot_wizard.cpp
extern UIState g_bootWizardAfterOkState;
extern Tab g_bootWizardAfterOkTab;
static uint32_t s_bootNtpWaitStartMs = 0;
static bool s_bootNtpWaitStarted = false;
static constexpr uint32_t kBootNtpWaitTimeoutMs = 20000;
static bool s_bootWifiTryingStoredProfiles = false;
static int s_bootWifiProfileIndex = 0;
static int s_bootWifiProfileCount = 0;

// -----------------------------------------------------------------------------
// Launcher Import
// -----------------------------------------------------------------------------
static bool s_bootWifiImported = false;
static char s_bootWifiImportedSsid[33] = {0};
static uint32_t s_bootWifiImportedAtMs = 0;
bool bootAssetProvisionRequired();

void bootWifiBeginNtpWait()
{
  s_bootNtpWaitStartMs = millis();
  s_bootNtpWaitStarted = true;
}

void bootWifiClearStoredProfileFailover()
{
  s_bootWifiTryingStoredProfiles = false;
  s_bootWifiProfileIndex = 0;
  s_bootWifiProfileCount = 0;
}

void bootWifiBeginStoredProfileFailover()
{
  s_bootWifiTryingStoredProfiles = true;
  s_bootWifiProfileIndex = 0;
  s_bootWifiProfileCount = wifiStoreCount();
}

bool bootWifiBeginStoredProfileConnect(int profileIndex)
{
  String ssid;
  String pass;

  if (!wifiStoreLoadProfile(profileIndex, ssid, pass) || ssid.length() == 0)
    return false;

  Serial.printf("[BOOTWIFI] trying stored wifi profile %d/%d ssid='%s'\n", profileIndex + 1, s_bootWifiProfileCount,
                ssid.c_str());

  settingsSetWifiEnabled(true);
  saveSettingsToSD();

  g_wifi.connectFailCount = 0;
  g_wifi.aborted = false;
  g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
  g_wifi.returnTab = g_bootWizardAfterOkTab;

  strlcpy(wifiSetupSsid, ssid.c_str(), sizeof(wifiSetupSsid));
  strlcpy(wifiSetupPass, pass.c_str(), sizeof(wifiSetupPass));
  wifiSetupBuf[0] = 0;

  wifiResetConnectUiState();
  wifiConsoleBeginConnect(ssid.c_str(), pass.c_str());

  uiActionEnterState(UIState::BOOT_WIFI_WAIT, g_bootWizardAfterOkTab, true);
  requestUIRedraw();
  return true;
}

bool bootWifiTryNextStoredProfile()
{
  if (!s_bootWifiTryingStoredProfiles)
    return false;

  ++s_bootWifiProfileIndex;

  while (s_bootWifiProfileIndex < s_bootWifiProfileCount && s_bootWifiProfileIndex < WIFI_PROFILE_MAX)
  {
    if (bootWifiBeginStoredProfileConnect(s_bootWifiProfileIndex))
      return true;

    ++s_bootWifiProfileIndex;
  }

  Serial.println("[BOOTWIFI] no more stored wifi profiles to try");
  bootWifiClearStoredProfileFailover();
  return false;
}

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

  const int detectedIdx = tzFindIndexByIana(tzNameRaw);

  Serial.printf("[BOOTWIFI] timezone detect raw='%s' mapped=%d\n", tzNameRaw, detectedIdx);

  if (detectedIdx < 0)
  {
    Serial.printf("[BOOTWIFI] timezone detect unsupported IANA zone '%s'\n", tzNameRaw);
    return false;
  }

  tzIndex = detectedIdx;
  applyTimezoneIndex((uint8_t)tzIndex);
  saveTzIndexToNVS((uint8_t)tzIndex);

  Serial.printf("[BOOTWIFI] timezone applied idx=%d label='%s' iana='%s' rule='%s'\n", tzIndex,
                tzName((uint8_t)tzIndex), tzIanaName((uint8_t)tzIndex), tzPosixRule((uint8_t)tzIndex));

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

  String importedSsid;
  String importedPwd;

  // Prefer saved Wi-Fi profiles.
  if (wifiStoreHasCreds())
  {
    bootWifiBeginStoredProfileFailover();

    if (bootWifiBeginStoredProfileConnect(0))
    {
      uiActionSwallowAll(in);
      uiDrainKb(in);
      clearInputLatch();
      return;
    }

    bootWifiClearStoredProfileFailover();
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
  bootWifiClearStoredProfileFailover();

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
    {
      bootWifiFallBackToManualEntry(in);
    }
    else if (bootWifiTryNextStoredProfile())
    {
      uiActionSwallowAll(in);
      uiDrainKb(in);
      clearInputLatch();
      return;
    }
    else
    {
      bootWifiRetryOrReturnToScan(in);
    }

    return;
  }

  if (reallyConnected)
  {
    bootWifiClearStoredProfileFailover();
    g_wifi.connectFailCount = 0;
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();

    // Persist working creds immediately on boot/provisioning success.
    // Prefer the live setup buffers if they are populated.
    const char *ssidToSave = nullptr;
    const char *passToSave = nullptr;

    if (wifiSetupSsid[0] && wifiSetupPass[0])
    {
      ssidToSave = wifiSetupSsid;
      passToSave = wifiSetupPass;
    }
    else if (g_wifi.ssid[0] && g_wifi.pass[0])
    {
      ssidToSave = g_wifi.ssid;
      passToSave = g_wifi.pass;
    }

    if (ssidToSave && passToSave)
    {
      wifiStoreSave(String(ssidToSave), String(passToSave));
      Serial.printf("[WIFI] boot flow saved creds for SSID: %s\n", ssidToSave);
    }
    else
    {
      Serial.printf("[WIFI] boot flow skip save: setupPassLen=%u gpassLen=%u setupSsid='%s' gssid='%s'\n",
                    (unsigned)strlen(wifiSetupPass), (unsigned)strlen(g_wifi.pass), wifiSetupSsid, g_wifi.ssid);
    }

    if (g_bootAssetProvisionMustComplete && bootAssetProvisionRequired())
    {
      Serial.printf("[BOOTWIFI] WiFi connected -> returning to BOOT for mandatory asset provisioning afterOk=%d\n",
                    (int)g_bootWizardAfterOkState);

      bootSetupClearPendingFlag();
      g_bootProvisionWifiOnboardingStarted = false;

      ui_setBootSplashActive(false);
      uiActionEnterState(UIState::BOOT, g_bootWizardAfterOkTab, true);
      requestFullUIRedraw();
      requestUIRedraw();
      return;
    }

    const bool tzDetected = bootTryDetectTimezoneFromWifi();

    if (tzDetected)
    {
      wifiStartSntpNow();
      bootWifiBeginNtpWait();

      Serial.println("[BOOT][NTP] wait started (auto TZ)");

      uiActionEnterState(UIState::BOOT_NTP_WAIT, g_bootWizardAfterOkTab, true);
      requestUIRedraw();
      return;
    }

    s_bootNtpWaitStarted = false;
    s_bootNtpWaitStartMs = 0;

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
    s_bootNtpWaitStarted = false;
    s_bootNtpWaitStartMs = 0;

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

  if (s_bootNtpWaitStarted)
  {
    const uint32_t elapsed = millis() - s_bootNtpWaitStartMs;
    if (elapsed >= kBootNtpWaitTimeoutMs)
    {
      Serial.printf("[BOOT][NTP] timeout after %lu ms -> fallback to manual time\n", (unsigned long)elapsed);

      s_bootNtpWaitStarted = false;
      s_bootNtpWaitStartMs = 0;

      uiActionSwallowAll(in);
      uiDrainKb(in);
      clearInputLatch();
      beginForcedSetTimeBootGate(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
      requestUIRedraw();
      return;
    }
  }

  if (timeIsSynced())
  {
    s_bootNtpWaitStarted = false;
    s_bootNtpWaitStartMs = 0;

    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();

    bootSetupClearPendingFlag();
    Serial.println("[BOOTWIFI] cleared boot setup pending");

    ui_setBootSplashActive(false);

    if (bootAssetProvisionRequired())
    {
      Serial.printf("[BOOTWIFI] NTP synced -> returning to BOOT for deferred asset provisioning afterOk=%d\n",
                    (int)g_bootWizardAfterOkState);

      g_bootProvisionWifiOnboardingStarted = false;

      uiActionEnterState(UIState::BOOT, g_bootWizardAfterOkTab, true);
      requestFullUIRedraw();
      requestUIRedraw();
      return;
    }

    Serial.printf("[BOOTWIFI] NTP synced -> returning to final state afterOk=%d\n", (int)g_bootWizardAfterOkState);

    uiActionEnterState(g_bootWizardAfterOkState, g_bootWizardAfterOkTab, true);
    requestFullUIRedraw();
    requestUIRedraw();
    return;
  }

  uiActionSwallowAll(in);
}