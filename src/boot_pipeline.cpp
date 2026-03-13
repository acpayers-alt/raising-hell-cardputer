// -----------------------------------------------------------------------------
// Raising Hell — Cardputer ADV Edition
// Boot pipeline implementation (moved out of .ino / out of headers)
// -----------------------------------------------------------------------------
#include "boot_pipeline.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <SD.h>
#include <cstring>

// Keep includes broad/safe like you’ve been doing
#include "app_state.h"
#include "asset_ota.h"
#include "asset_provision_request.h"
#include "boot_state.h"
#include "brightness_state.h"
#include "controls_help_state.h"
#include "debug.h"
#include "display.h"
#include "display_dims_state.h"
#include "display_state.h"
#include "eeprom_addrs.h"
#include "flow_boot_wizard.h"
#include "flow_time_editor.h"
#include "graphics.h"
#include "input.h"
#include "inventory.h"
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
#include "wifi_power.h"
#include "wifi_time.h"
#include <WiFi.h>
#include <esp_system.h>

// -----------------------------------------------------------------------------
// SD Asset Check (all builds)
// -----------------------------------------------------------------------------
static const char *kSdAssetsMarkerPath = "/raising_hell/ASSET_MANIFEST.txt";
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

// -----------------------------------------------------------------------------
// First Run Flag (factory reset helper)
// -----------------------------------------------------------------------------
static const char *kFirstRunFlagPath = "/raising_hell/first_run.flag";

static bool consumeFirstRunFlagIfPresent()
{
  if (!g_sdReady)
    return false;
  if (!SD.exists(kFirstRunFlagPath))
    return false;

  SD.remove(kFirstRunFlagPath);
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
  return SD.exists(kSdAssetsMarkerPath) || SD.exists(kSdAssetsLocalManifestPath);
}

// -----------------------------------------------------------------------------
// Deferred init state machine (boot pipeline)
// -----------------------------------------------------------------------------
uint32_t g_sdFirstTryMs = 0;
bool g_sdGaveUp = false;

static bool g_postBootInitDone = false;
static bool g_sdTriedLoad = false;
static bool g_ntpSaved = false;
static bool g_wifiApplied = false;
uint8_t g_sdTryCount = 0;
static bool g_bootProvisionWifiStarted = false;
static uint32_t g_bootProvisionWifiStartMs = 0;
static bool g_bootLandingDeferredForAssetProvision = false;
static bool g_bootLandingDone = false;

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

static bool bootAssetProvisionRequested() { return assetProvisionBootRequested(); }

static void drawBootAssetProvisionScreen(const char *line1, const char *line2)
{
  displayInit();

  spr.fillScreen(TFT_BLACK);
  spr.setTextDatum(textdatum_t::middle_center);
  spr.setTextFont(2);
  spr.setTextSize(1);

  spr.setTextColor(TFT_RED, TFT_BLACK);
  spr.drawString("ASSET PROVISIONING", SCREEN_W / 2, SCREEN_H / 2 - 26);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  if (line1 && line1[0])
    spr.drawString(line1, SCREEN_W / 2, SCREEN_H / 2 + 2);
  if (line2 && line2[0])
    spr.drawString(line2, SCREEN_W / 2, SCREEN_H / 2 + 22);

  spr.pushSprite(0, 0);
}

static bool runBootAssetProvision()
{
  static bool s_bootAssetProvisionHandled = false;

  if (!bootAssetProvisionRequested())
    return false;

  if (s_bootAssetProvisionHandled)
    return true;

  s_bootAssetProvisionHandled = true;

  ui_setBootSplashActive(false);
  clearInputLatch();
  inputForceClear();

  drawBootAssetProvisionScreen("Checking asset package.", "Please wait...");

  String msg;
  const bool ok = assetOtaCheckNow(&msg);

  clearAssetProvisionBootRequest();

  drawBootAssetProvisionScreen("Asset check result", msg.c_str());
  delay(1500);

  if (ok)
  {
    ESP.restart();
  }

  Serial.printf("[BOOT][ASSET_PROVISION] failed: %s\n", msg.c_str());

  // If assets are still missing, fall back to the normal missing-assets gate.
  g_assetsChecked = true;
  g_assetsMissing = !sdAssetsPresent();

  if (!g_assetsMissing)
  {
    drawBootSplash();
    requestUIRedraw();
    renderUI();
  }

  return g_assetsMissing;
}

static bool bootAssetProvisionWifiReady()
{
  if (!bootAssetProvisionRequested())
    return false;

  if (!settingsWifiEnabled())
    return true;

  if (!g_bootProvisionWifiStarted)
  {
    wifiSetEnabled(true);
    applyWifiPower(true);
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

        drawBootSplash();
        invalidateBackgroundCache();
        requestUIRedraw();
        renderUI();

        // Asset pack check — ONLY after SD is ready
        if (!g_assetsChecked)
        {
          g_assetsChecked = true;
          g_assetsMissing = !sdAssetsPresent();

          if (g_assetsMissing)
          {
            ui_setBootSplashActive(false);
            requestUIRedraw();
            return; // stop boot pipeline until assets are installed
          }
        }
      }
    }

    // If SD still not ready (and we haven't timed out), stop here.
    if (!g_sdReady && !g_sdGaveUp)
      return;
  }

  // If SD is up but the asset pack is missing, block further boot work until fixed.
  if (g_assetsMissing)
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
    if (consumeFirstRunFlagIfPresent())
    {
      g_controlsHelpSeen = 0;
      saveSettingsToSD();
    }

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

    const bool firstBootWizard = !settingsLoaded;
    const bool saveFileExists = bootSaveFileExists();
    const UIState afterOk = saveFileExists ? UIState::PET_SCREEN : UIState::CHOOSE_PET;

    const bool deferForAssetProvision = bootAssetProvisionRequested() && !firstBootWizard && timeIsValid();

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

    if (deferForAssetProvision)
    {
      g_bootLandingDeferredForAssetProvision = true;
      ui_setBootSplashActive(false);
      drawBootAssetProvisionScreen("Preparing asset check.", "Please wait...");
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
      extern UIState g_bootWizardAfterOkState;
      extern Tab g_bootWizardAfterOkTab;
      g_bootWizardAfterOkState = afterOk;
      g_bootWizardAfterOkTab = Tab::TAB_PET;

      if (!g_controlsHelpSeen)
      {
        controlsHelpBegin(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET);
        return;
      }

      bootWizardBegin(afterOk, Tab::TAB_PET);
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

      bootWizardBegin(afterOk, Tab::TAB_PET);
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

      if (!g_wifiApplied)
      {
        const bool pref = settingsWifiEnabled();
        wifiSetEnabled(pref);
        applyWifiPower(pref);
        g_wifiApplied = true;
      }

      wifiTimeInit();

      if (g_bootLandingDeferredForAssetProvision && !g_bootLandingDone)
      {
        finalizeBootLanding();
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
  if (bootAssetProvisionRequested())
  {
    if (runBootAssetProvision())
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