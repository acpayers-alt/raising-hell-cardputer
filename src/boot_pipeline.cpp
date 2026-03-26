// -----------------------------------------------------------------------------
// Raising Hell — Cardputer ADV Edition
// Boot pipeline implementation
// -----------------------------------------------------------------------------
#include "boot_pipeline.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <SD.h>
#include <cstring>

#include "app_lifecycle.h"
#include "app_state.h"
#include "asset_manifest.h"
#include "asset_ota.h"
#include "asset_ota_config.h"
#include "asset_provision_request.h"
#include "boot_state.h"
#include "brightness_state.h"
#include "controls_help_state.h"
#include "debug.h"
#include "display.h"
#include "display_dims_state.h"
#include "display_state.h"
#include "eeprom_addrs.h"
#include "flow_boot_wifi.h"
#include "flow_boot_wizard.h"
#include "flow_time_editor.h"
#include "graphics.h"
#include "input.h"
#include "inventory.h"
#include "launcher_wifi_import.h"
#include "new_pet_flow_state.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_state.h"
#include "time_persist.h"
#include "time_state.h"
#include "timezone.h"
#include "ui_actions.h"
#include "ui_level_popup.h"
#include "ui_runtime.h"
#include "version.h"
#include "wifi_power.h"
#include "wifi_store.h"
#include "wifi_time.h"
#include <WiFi.h>
#include <esp_system.h>

// -----------------------------------------------------------------------------
// SD Asset Check (all builds)
// -----------------------------------------------------------------------------
static const char *kSdAssetsLocalManifestPath = "/raising_hell/assets/manifest_local.json";

bool g_assetsChecked = false;
bool g_assetsMissing = false;

void drawAssetsMissingScreen()
{
  displayInit();

  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(textdatum_t::middle_center);

  const bool sdOk = g_sdReady;

  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.drawString(sdOk ? "ASSETS MISSING" : "SD CARD NOT DETECTED", SCREEN_W / 2, SCREEN_H / 2 - 36);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  if (!sdOk)
  {
    spr.drawString("Insert SD card with assets", SCREEN_W / 2, SCREEN_H / 2 - 10);
    spr.drawString("then press ENTER to retry", SCREEN_W / 2, SCREEN_H / 2 + 14);
  }
  else
  {
    spr.drawString("Assets folder missing or incomplete", SCREEN_W / 2, SCREEN_H / 2 - 10);
    spr.drawString("Press ENTER to retry", SCREEN_W / 2, SCREEN_H / 2 + 14);
  }

  spr.pushSprite(0, 0);
}

static bool parseSemver3(const String &s, int &maj, int &min, int &pat)
{
  maj = min = pat = 0;

  int d1 = s.indexOf('.');
  if (d1 < 0)
    return false;
  int d2 = s.indexOf('.', d1 + 1);
  if (d2 < 0)
    return false;

  String a = s.substring(0, d1);
  String b = s.substring(d1 + 1, d2);
  String c = s.substring(d2 + 1);

  a.trim();
  b.trim();
  c.trim();

  if (!a.length() || !b.length() || !c.length())
    return false;

  maj = a.toInt();
  min = b.toInt();
  pat = c.toInt();
  return true;
}

static int compareSemver3(const String &lhs, const String &rhs)
{
  int lMaj, lMin, lPat;
  int rMaj, rMin, rPat;

  if (!parseSemver3(lhs, lMaj, lMin, lPat))
    return -1;
  if (!parseSemver3(rhs, rMaj, rMin, rPat))
    return 1;

  if (lMaj != rMaj)
    return (lMaj < rMaj) ? -1 : 1;
  if (lMin != rMin)
    return (lMin < rMin) ? -1 : 1;
  if (lPat != rPat)
    return (lPat < rPat) ? -1 : 1;
  return 0;
}

static bool bootAssetPackTooOld()
{
  if (!g_sdReady)
    return false;

  String installedPack;
  const bool haveInstalled = assetManifestLoadLocalPackVersion(&installedPack);

  const bool tooOld =
      (!haveInstalled || !installedPack.length() || compareSemver3(installedPack, RH_MIN_REQUIRED_ASSET_PACK) < 0);

  Serial.printf("[BOOT][ASSET_VER] minRequired=%s installed=%s haveInstalled=%d tooOld=%d\n",
                RH_MIN_REQUIRED_ASSET_PACK,
                (haveInstalled && installedPack.length()) ? installedPack.c_str() : "(none)", haveInstalled ? 1 : 0,
                tooOld ? 1 : 0);

  return tooOld;
}

// -----------------------------------------------------------------------------
// First Run Flag (factory reset helper)
// -----------------------------------------------------------------------------
static const char *kFirstRunFlagPath = "/raising_hell/first_run.flag";
static const char *kPostProvisionControlsHelpFlagPath = "/raising_hell/post_provision_controls.flag";
static const char *kBootSetupPendingFlagPath = "/raising_hell/boot_setup_pending.flag";

bool bootSetupPendingFlagExists()
{
  if (!g_sdReady)
    return false;
  return SD.exists(kBootSetupPendingFlagPath);
}

void bootSetupWritePendingFlag()
{
  if (!g_sdReady)
    return;

  if (!SD.exists("/raising_hell"))
    SD.mkdir("/raising_hell");
  if (!SD.exists("/raising_hell/save"))
    SD.mkdir("/raising_hell/save");

  File f = SD.open(kBootSetupPendingFlagPath, FILE_WRITE);
  if (f)
  {
    f.print("1\n");
    f.close();
  }
}

void bootSetupClearPendingFlag()
{
  if (!g_sdReady)
    return;

  if (SD.exists(kBootSetupPendingFlagPath))
    SD.remove(kBootSetupPendingFlagPath);
}

static bool consumeFirstRunFlagIfPresent()
{
  if (!g_sdReady)
    return false;
  if (!SD.exists(kFirstRunFlagPath))
    return false;

  SD.remove(kFirstRunFlagPath);
  return true;
}

static bool writePostProvisionControlsHelpFlag()
{
  if (!g_sdReady)
    return false;

  File f = SD.open(kPostProvisionControlsHelpFlagPath, FILE_WRITE);
  if (!f)
    return false;

  f.print("1\n");
  f.close();
  return true;
}

static bool consumePostProvisionControlsHelpFlagIfPresent()
{
  if (!g_sdReady)
    return false;

  if (!SD.exists(kPostProvisionControlsHelpFlagPath))
    return false;

  SD.remove(kPostProvisionControlsHelpFlagPath);
  return true;
}

static uint32_t g_nextSdTryMs = 0;
static uint32_t g_nextWifiTryMs = 0;

void bootPipelineKick(uint32_t now, bool usbOpen)
{
  // Nudge retry timers so first attempts are slightly delayed
  // (gives USB serial time to open, avoids spamming init early).
  g_nextSdTryMs = now + (usbOpen ? 800 : 200);
  g_nextWifiTryMs = now + (usbOpen ? 1200 : 500);
}

bool sdAssetsPresent()
{
  if (!g_sdReady)
    return false;

  return SD.exists("/raising_hell/assets/manifest_local.json");
}

// -----------------------------------------------------------------------------
// Deferred init state machine (boot pipeline)
// -----------------------------------------------------------------------------
uint32_t g_sdFirstTryMs = 0;
bool g_sdGaveUp = false;
uint8_t g_sdTryCount = 0;
static uint32_t g_bootProvisionWifiStartMs = 0;
bool g_bootAssetProvisionActive = false;
extern bool g_bootAssetProvisionMustComplete;
bool g_bootAssetProvisionMustComplete = false;
static bool g_bootAssetPackTooOldCached = false;

bool g_bootProvisionWifiOnboardingStarted = false;
static bool g_bootLandingDeferredForAssetProvision = false;
static bool g_bootLandingDone = false;
static bool g_postBootInitDone = false;
static bool g_sdTriedLoad = false;
static bool g_ntpSaved = false;
static bool g_wifiApplied = false;
static bool g_bootProvisionWifiStarted = false;
static bool g_forceControlsHelpAfterProvision = false;
static bool g_postProvisionControlsHelpPending = false;

// ---- Early TZ/anchor latches (Stage 0) ----
static bool g_tzAppliedEarly = false;
static bool g_anchorAppliedEarly = false;

// Time display gating (loop also checks these)
bool g_timeAnchorAttempted = false;
bool g_timeAnchorRestored = false;

// -----------------------------------------------------------------------------
// See if a save file already exists
// -----------------------------------------------------------------------------
static bool bootSaveFileExists()
{
  if (!g_sdReady)
    return false;

  return SD.exists("/raising_hell/save/save.bin") || SD.exists("raising_hell/save/save.bin");
}

// -----------------------------------------------------------------------------
// Small helper: centralized state enter + redraw (keeps transitions consistent)
// -----------------------------------------------------------------------------
static inline void enterState(UIState s, Tab t, bool fullRedraw)
{
  uiActionEnterState(s, t, true);
  if (fullRedraw)
    requestFullUIRedraw();
  else
    requestUIRedraw();
  clearInputLatch();
}

static bool bootVisualsLockedForProvisioning()
{
  return g_bootAssetProvisionActive || g_bootLandingDeferredForAssetProvision;
}

static bool bootAssetProvisionRequested() { return assetProvisionBootRequested(); }

static bool bootAssetProvisionMandatory() { return g_sdReady && !sdAssetsPresent(); }

bool g_bootUiBlockedForAssetProvision = false;

static bool bootAssetProvisionRequired()
{
  const bool requested = bootAssetProvisionRequested();
  const bool mandatory = bootAssetProvisionMandatory();
  const bool tooOld = !mandatory && g_bootAssetPackTooOldCached;
  return requested || mandatory || tooOld;
}

void drawBootAssetProvisionScreen(const char *line1, const char *line2)
{
  displayInit();

  const uint16_t cur = assetOtaCurrentFileIndex();
  const uint16_t total = assetOtaTotalFileCount();
  const AssetOtaStatus st = assetOtaStatus();
  const char *statusText = assetOtaStatusString();

  auto drawUi = [&](auto &d)
  {
    d.fillScreen(TFT_BLACK);
    d.setTextFont(1);
    d.setTextSize(1);
    d.setTextDatum(TL_DATUM);

    d.setTextColor(TFT_RED, TFT_BLACK);
    d.drawString("ASSET PROVISIONING", 12, 8);

    d.setTextColor(TFT_WHITE, TFT_BLACK);
    if (line1 && line1[0])
      d.drawString(line1, 12, 28);
    if (line2 && line2[0])
      d.drawString(line2, 12, 40);

    char dbg1[64];
    snprintf(dbg1, sizeof(dbg1), "st=%d  cur=%u  total=%u", (int)st, (unsigned)cur, (unsigned)total);
    d.drawString(dbg1, 12, 56);

    char dbg2[48];
    snprintf(dbg2, sizeof(dbg2), "status=%s", statusText ? statusText : "");
    d.drawString(dbg2, 12, 68);

    // Force a visible test box exactly where the bar should live
    d.fillRect(12, 84, SCREEN_W - 24, 14, TFT_DARKGREY);
    d.drawRect(12, 84, SCREEN_W - 24, 14, TFT_WHITE);

    // Draw progress fill even if total is 0, so we can see the area
    if (total > 0)
    {
      int fillW = (int)(((uint32_t)(SCREEN_W - 26) * cur) / total);
      if (fillW < 0)
        fillW = 0;
      if (fillW > (SCREEN_W - 26))
        fillW = SCREEN_W - 26;

      if (fillW > 0)
        d.fillRect(13, 85, fillW, 12, TFT_GREEN);

      char prog[40];
      snprintf(prog, sizeof(prog), "File %u / %u", (unsigned)cur, (unsigned)total);
      d.setTextColor(TFT_WHITE, TFT_BLACK);
      d.drawString(prog, 12, 104);
    }
    else
    {
      d.setTextColor(TFT_YELLOW, TFT_BLACK);
      d.drawString("total == 0", 12, 104);
    }
  };

  if (assetOtaDidReleaseGraphics())
  {
    auto &d = M5Cardputer.Display;
    drawUi(d);
    return;
  }

  drawUi(spr);
  spr.pushSprite(0, 0);
}
void bootAssetProvisionRedraw(const char *line1, const char *line2) { drawBootAssetProvisionScreen(line1, line2); }

static bool bootAssetProvisionWaitingAtIntroScreen()
{
  switch (g_app.uiState)
  {
  case UIState::BOOT_ASSET_WIFI_REQUIRED:
  case UIState::CONSOLE:
    return true;
  default:
    return false;
  }
}

static bool bootAssetProvisionWifiOnboardingActive()
{
  switch (g_app.uiState)
  {
  case UIState::BOOT_WIFI_PROMPT:
  case UIState::BOOT_WIFI_IMPORTED:
  case UIState::BOOT_WIFI_WAIT:
  case UIState::BOOT_TZ_PICK:
  case UIState::BOOT_NTP_WAIT:
  case UIState::BOOT_ASSET_WIFI_REQUIRED:
  case UIState::WIFI_SETUP:
    return true;
  default:
    return false;
  }
}

static bool runBootAssetProvision()
{
  static bool s_bootAssetProvisionHandled = false;

  if (!bootAssetProvisionRequired())
    return false;

  if (s_bootAssetProvisionHandled)
    return true;

  s_bootAssetProvisionHandled = true;

  ui_setBootSplashActive(false);
  clearInputLatch();
  inputForceClear();

  drawBootAssetProvisionScreen("Checking asset package.", "Please wait...");

  clearAssetProvisionBootRequest();

  String msg;
  const bool ok = assetOtaRunInWorkerTask(&msg);

  // Re-check asset presence after OTA work completes.
  g_assetsChecked = true;
  g_assetsMissing = !sdAssetsPresent();

  drawBootAssetProvisionScreen("Asset check result", msg.c_str());
  delay(1500);

  // Success only counts if assets are actually present afterward.
  if (ok && !g_assetsMissing)
  {
    if (g_bootAssetProvisionMustComplete && !bootSaveFileExists())
    {
      writePostProvisionControlsHelpFlag();
    }

    if (g_bootAssetProvisionMustComplete)
    {
      settingsSetWifiEnabled(true);
      saveSettingsToSD();
    }

    g_bootAssetProvisionActive = false;
    g_bootUiBlockedForAssetProvision = false;
    ESP.restart();
  }

  Serial.printf("[BOOT][ASSET_PROVISION] failed: %s\n", msg.c_str());

  // If assets are still missing, this is a mandatory provisioning failure.
  // Stay blocked on the provisioning/error screen.
  if (g_assetsMissing)
  {
    return true;
  }

  // Optional OTA request failed or had no effect, but assets already exist.
  // Allow normal boot to continue.
  g_bootAssetProvisionActive = false;
  g_bootUiBlockedForAssetProvision = false;

  drawBootSplash();
  requestUIRedraw();
  renderUI();

  return false;
}

static bool bootAssetProvisionWifiReady()
{
  if (!bootAssetProvisionRequired())
    return false;

  // Optional OTA request: if WiFi is disabled in settings, let OTA report it and continue boot.
  if (!g_bootAssetProvisionMustComplete && !settingsWifiEnabled())
    return true;

  if (!g_bootProvisionWifiStarted)
  {
    const bool shouldEnableWifi = g_bootAssetProvisionMustComplete ? true : settingsWifiEnabled();

    wifiSetEnabled(shouldEnableWifi);
    applyWifiPower(shouldEnableWifi);

    if (g_bootAssetProvisionMustComplete && shouldEnableWifi)
    {
      settingsSetWifiEnabled(true);
      saveSettingsToSD();
    }

    wifiTimeInit();

    g_bootProvisionWifiStarted = true;
    g_bootProvisionWifiStartMs = millis();

    drawBootAssetProvisionScreen("Connecting to WiFi.", "Please wait...");
    return false;
  }

  wifiTimeInit();

  if (WiFi.status() == WL_CONNECTED)
    return true;

  if ((uint32_t)(millis() - g_bootProvisionWifiStartMs) >= 8000)
    return true;

  drawBootAssetProvisionScreen("Connecting to WiFi.", "Please wait...");
  return false;
}

static void finalizeBootLanding()
{
  if (g_bootLandingDone)
    return;

  g_bootLandingDone = true;
  g_bootAssetProvisionMustComplete = false;
  g_bootProvisionWifiOnboardingStarted = false;
  const bool saveFileExists = bootSaveFileExists();
  const UIState afterOk = saveFileExists ? UIState::PET_SCREEN : UIState::CHOOSE_PET;

  bool loadedFromSD = saveFileExists;
  uint16_t seedMarkNow = 0;
  EEPROM.get(SEED_MARK_ADDR, seedMarkNow);

  if (!loadedFromSD)
  {
    DBG_ON("[BOOT] No SD save -> UIState::CHOOSE_PET\n");

    g_app.inventory.resetToDefaults();
    ui_setBootSplashActive(false);

    if (g_postProvisionControlsHelpPending)
    {
      g_postProvisionControlsHelpPending = false;
      g_controlsHelpSeen = 0;
      beginForcedSetTimeBootGate(UIState::CHOOSE_PET, Tab::TAB_PET);
      controlsHelpBegin(UIState::SET_TIME, Tab::TAB_PET);
      return;
    }

    if (!g_controlsHelpSeen)
    {
      beginForcedSetTimeBootGate(UIState::CHOOSE_PET, Tab::TAB_PET);
      controlsHelpBegin(UIState::SET_TIME, Tab::TAB_PET);
      return;
    }

    enterState(UIState::CHOOSE_PET, Tab::TAB_PET, false);
    uiInitLevelPopupTracker();

    invalidateBackgroundCache();
    requestUIRedraw();
    renderUI();
    return;
  }

  if (seedMarkNow != SEED_MARK)
  {
    EEPROM.put(SEED_MARK_ADDR, (uint16_t)SEED_MARK);
    EEPROM.commit();
  }

  if (g_app.uiState == UIState::BOOT)
  {
    enterState(UIState::PET_SCREEN, Tab::TAB_PET, false);
  }

  ui_setBootSplashActive(false);

  invalidateBackgroundCache();
  requestUIRedraw();
  renderUI();
}

// -----------------------------------------------------------------------------
// postBootInitTick()
// -----------------------------------------------------------------------------
void postBootInitTick()
{
  const uint32_t now = millis();

  // ---------------------------------------------------------------------------
  // Stage 0: Apply TZ + anchor early (so localtime_r() is sane ASAP)
  // ---------------------------------------------------------------------------
  if (!g_tzAppliedEarly)
  {
    uint8_t nvsTz;
    if (loadTzIndexFromNVS(&nvsTz))
    {
      tzIndex = nvsTz;
    }
    applyTimezoneIndex(tzIndex);
    g_tzAppliedEarly = true;
  }

  if (!g_anchorAppliedEarly)
  {
    if (!g_timeAnchorAttempted)
    {
      g_timeAnchorRestored = restoreTimeFromAnchor();
      g_timeAnchorAttempted = true;
    }
    updateTime();
    g_anchorAppliedEarly = true;
  }

  // ---------------------------------------------------------------------------
  // Stage 1: SD init retry window (up to 5s)
  // ---------------------------------------------------------------------------
  if (!g_sdReady && !g_sdGaveUp)
  {
    if (g_sdFirstTryMs == 0)
      g_sdFirstTryMs = now;

    if ((uint32_t)(now - g_sdFirstTryMs) > 5000)
    {
      g_sdGaveUp = true;
      ui_setBootSplashActive(false);

      // HARD GATE: SD missing -> treat as assets missing so loop blocks.
      g_assetsChecked = true;
      g_assetsMissing = true;

      requestUIRedraw();
    }
    else if (now >= g_nextSdTryMs)
    {
      g_sdTryCount++;
      uint32_t backoff = 250 + (uint32_t)g_sdTryCount * 250;
      if (backoff > 1500)
        backoff = 1500;
      g_nextSdTryMs = now + backoff;

      requestUIRedraw();

      g_sdReady = initSD();
      DBG_ON("[SD] initSD -> %d\n", (int)g_sdReady);

      if (g_sdReady)
      {
        g_sdTryCount = 0;

        if (!bootVisualsLockedForProvisioning())
        {
          drawBootSplash();
          invalidateBackgroundCache();
          requestUIRedraw();
          renderUI();
        }

        // Asset pack check — ONLY after SD is ready
        if (!g_assetsChecked)
        {
          g_assetsChecked = true;
          g_assetsMissing = !sdAssetsPresent();

          if (g_assetsMissing)
          {
            g_bootUiBlockedForAssetProvision = true;
            requestUIRedraw();
          }
        }
      }
    }

    // If SD still not ready (and we haven't timed out), stop here.
    if (!g_sdReady && !g_sdGaveUp)
      return;
  }

  // Hard block only when SD itself is not ready/present.
  // If SD is ready but assets are missing, continue into provisioning flow.
  if (g_assetsMissing && !g_sdReady)
    return;

  // ---------------------------------------------------------------------------
  // Stage 2: One-time SD load pipeline (settings + save + time anchor)
  // ---------------------------------------------------------------------------
  if (!g_sdTriedLoad)
  {
    g_sdTriedLoad = true;

    inputSetTextCapture(false);
    clearInputLatch();
    inputForceClear();

    bool settingsLoaded = false;
    if (g_sdReady)
    {
      settingsLoaded = loadSettingsFromSD();
      if (!settingsLoaded)
        saveSettingsToSD();
    }

    // FIRST RUN FLAG (from factory reset)
    const bool forcedFirstRun = consumeFirstRunFlagIfPresent();
    if (forcedFirstRun)
    {
      g_controlsHelpSeen = 0;
      saveSettingsToSD();
    }

    g_postProvisionControlsHelpPending = consumePostProvisionControlsHelpFlagIfPresent();

    // Apply loaded brightness immediately
    if (isScreenOn())
    {
      applyBrightnessLevel(brightnessLevel);
    }

    // Ensure TZ is applied again after settings load (tzIndex may have changed)
    applyTimezoneIndex(tzIndex);

    // Try restore time anchor again (safe) after settings/tz is applied
    if (!g_timeAnchorAttempted)
    {
      g_timeAnchorRestored = restoreTimeFromAnchor();
      g_timeAnchorAttempted = true;
    }

    bool loadedFromSD = false;
    if (g_sdReady)
      loadedFromSD = saveManagerLoad();

    DBG_ON("[LOAD] saveManagerLoad -> %d\n", (int)loadedFromSD);

    updateTime();
    uiInitLevelPopupTracker();

    const bool setupPending = appLifecycleHasPendingOnboarding();
    const bool firstBootWizard = !settingsLoaded || forcedFirstRun || setupPending;
    const bool saveFileExists = forcedFirstRun ? false : bootSaveFileExists();

    const UIState afterOk = appLifecycleResolveBootAfterOkState(saveFileExists);

    const bool provisionRequested = bootAssetProvisionRequested();
    const bool provisionMandatory = bootAssetProvisionMandatory();
    const bool provisionTooOld = bootAssetPackTooOld();
    g_bootAssetPackTooOldCached = provisionTooOld;
    const bool assetsPresentNow = sdAssetsPresent();

    Serial.printf("[BOOT][ASSET] requested=%d mandatory=%d tooOld=%d assetsPresent=%d minRequired=%s\n",
                  provisionRequested ? 1 : 0, provisionMandatory ? 1 : 0, provisionTooOld ? 1 : 0,
                  assetsPresentNow ? 1 : 0, RH_MIN_REQUIRED_ASSET_PACK);

    const bool deferForAssetProvision = provisionRequested || provisionMandatory || provisionTooOld;

    Serial.printf("[BOOTPIPE] settingsLoaded=%d saveLoaded=%d timeValid=%d firstBootWizard=%d afterOk=%d\n",
                  settingsLoaded ? 1 : 0, loadedFromSD ? 1 : 0, timeIsValid() ? 1 : 0, firstBootWizard ? 1 : 0,
                  (int)afterOk);

    if (deferForAssetProvision)
      Serial.println("[BOOT] path=ASSET_PROVISION_PENDING");
    else if (firstBootWizard)
      Serial.println("[BOOT] path=FIRST_BOOT_WIZARD");
    else if (!timeIsValid())
      Serial.println("[BOOT] path=TIME_INVALID_WIFI_RECOVERY");
    else if (!loadedFromSD)
      Serial.println("[BOOT] path=NEW_PET_FLOW");
    else
      Serial.println("[BOOT] path=NORMAL_BOOT");

    if (!loadedFromSD)
    {
      g_app.inventory.init();
    }

    if (forcedFirstRun)
    {
      g_app.inventory.resetToDefaults();
    }

    if (deferForAssetProvision)
    {
      g_bootLandingDeferredForAssetProvision = true;
      g_bootAssetProvisionMustComplete = provisionMandatory || provisionTooOld;
      g_bootUiBlockedForAssetProvision = true;
      g_bootProvisionWifiOnboardingStarted = false;
      g_forceControlsHelpAfterProvision = g_bootAssetProvisionMustComplete && !bootSaveFileExists();

      ui_setBootSplashActive(false);

      if (g_bootAssetProvisionMustComplete)
      {
        g_bootAssetProvisionActive = false;
        uiActionEnterState(UIState::BOOT_ASSET_WIFI_REQUIRED, Tab::TAB_PET, true);
        requestFullUIRedraw();
        requestUIRedraw();
        renderUI();
        clearInputLatch();
        return;
      }

      drawBootAssetProvisionScreen("Preparing asset check.", "Please wait...");
      g_bootAssetProvisionActive = true;
      requestUIRedraw();
      renderUI();
      return;
    }

    ui_setBootSplashActive(false);

    // -----------------------------------------------------------------------
    // FIRST BOOT WIZARD
    // Missing settings.bin means onboarding should run, regardless of save.bin
    // and regardless of whether time currently appears valid.
    // -----------------------------------------------------------------------
    if (firstBootWizard)
    {
      bootSetupWritePendingFlag();
      extern UIState g_bootWizardAfterOkState;
      extern Tab g_bootWizardAfterOkTab;
      g_bootWizardAfterOkState = afterOk;
      g_bootWizardAfterOkTab = Tab::TAB_PET;

      if (!g_controlsHelpSeen)
      {
        controlsHelpBegin(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET);
        return;
      }

      String importedSsid;
      String importedPwd;

      if (launcherImportWifiCreds(importedSsid, importedPwd))
      {
        wifiStoreSave(importedSsid, importedPwd);
        settingsSetWifiEnabled(true);
        saveSettingsToSD();

        bootWifiSetImportedInfo(importedSsid.c_str());
        wifiConsoleBeginConnect(importedSsid.c_str(), importedPwd.c_str());

        uiActionEnterState(UIState::BOOT_WIFI_IMPORTED, Tab::TAB_PET, true);
      }
      else
      {
        uiActionEnterState(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET, true);
      }

      return;
    }

    // -----------------------------------------------------------------------
    // NOT first boot: if time is invalid, re-enter boot Wi-Fi flow
    // so the user gets another chance to import/use Wi-Fi before
    // falling back to manual time entry.
    // -----------------------------------------------------------------------
    if (!timeIsValid())
    {
      extern UIState g_bootWizardAfterOkState;
      extern Tab g_bootWizardAfterOkTab;
      g_bootWizardAfterOkState = afterOk;
      g_bootWizardAfterOkTab = Tab::TAB_PET;

      uiActionEnterState(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET, true);
      requestFullUIRedraw();
      invalidateBackgroundCache();
      requestUIRedraw();
      renderUI();
      clearInputLatch();
      return;
    }

    if (!deferForAssetProvision)
    {
      finalizeBootLanding();
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // Stage 3: WiFi/NTP init (deferred)
  // ---------------------------------------------------------------------------
  if (!g_postBootInitDone)
  {
    if (now >= g_nextWifiTryMs)
    {
      g_nextWifiTryMs = now + 1000;

      if (bootAssetProvisionRequired())
      {
        // For mandatory provisioning, wait on the explicit intro screen until
        // the user presses ENTER to begin Wi-Fi setup.
        if (g_bootAssetProvisionMustComplete && !g_bootProvisionWifiOnboardingStarted)
        {
          if (bootAssetProvisionWaitingAtIntroScreen())
            return;
        
          g_bootProvisionWifiOnboardingStarted = true;
          g_bootAssetProvisionActive = false;
          ui_setBootSplashActive(false);
        
          uiActionEnterState(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET, true);
          requestFullUIRedraw();
          requestUIRedraw();
          clearInputLatch();
          return;
        }
        
        if (bootAssetProvisionWifiOnboardingActive())
          return;
      }
      if (!g_wifiApplied)
      {
        const bool pref = settingsWifiEnabled();
        wifiSetEnabled(pref);
        applyWifiPower(pref);
        g_wifiApplied = true;
      }

      wifiTimeInit();

      if (bootAssetProvisionRequired())
      {
        g_bootAssetProvisionActive = true;

        if (!bootAssetProvisionWifiReady())
          return;
      }

      g_postBootInitDone = true;
      requestUIRedraw();
    }
    return;
  }

  // ---------------------------------------------------------------------------
  // Stage 3.5: Deferred boot asset provisioning
  // ---------------------------------------------------------------------------
  if (bootAssetProvisionRequired())
  {
    if (runBootAssetProvision())
      return;
  }

  if (g_bootLandingDeferredForAssetProvision && !g_bootLandingDone)
  {
    g_bootAssetProvisionActive = false;
    g_bootUiBlockedForAssetProvision = false;
    finalizeBootLanding();
    return;
  }

  // ---------------------------------------------------------------------------
  // Stage 4: Persist time anchor once we have synced time
  // ---------------------------------------------------------------------------
  if (!g_ntpSaved && timeIsSynced())
  {
    g_ntpSaved = true;
    saveTimeAnchor();
  }
}