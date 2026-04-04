// -----------------------------------------------------------------------------
// Raising Hell — Cardputer ADV Edition
// Boot pipeline implementation
// -----------------------------------------------------------------------------

#include "boot_pipeline.h"

// -----------------------------------------------------------------------------
// Standard / C
// -----------------------------------------------------------------------------
#include <cstring>

// -----------------------------------------------------------------------------
// Arduino / ESP / Platform
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <EEPROM.h>
#include <esp_system.h>

// -----------------------------------------------------------------------------
// External libraries
// -----------------------------------------------------------------------------
#include <SD.h>
#include <WiFi.h>

// -----------------------------------------------------------------------------
// Project headers
// -----------------------------------------------------------------------------

// App / lifecycle
#include "app_lifecycle.h"
#include "app_state.h"

// Asset / OTA
#include "asset_manifest.h"
#include "asset_ota.h"
#include "asset_ota_config.h"
#include "asset_provision_request.h"

// Boot / system
#include "boot_firmware_marker.h"
#include "boot_state.h"

// Display / graphics
#include "brightness_state.h"
#include "display.h"
#include "display_dims_state.h"
#include "display_state.h"
#include "graphics.h"

// Input / UI
#include "controls_help_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_level_popup.h"
#include "ui_runtime.h"

// Flow / boot UX
#include "flow_boot_wifi.h"
#include "flow_boot_wizard.h"
#include "flow_time_editor.h"
#include "launcher_wifi_import.h"
#include "new_pet_flow_state.h"

// Game systems
#include "inventory.h"
#include "save_manager.h"
#include "settings_state.h"

// Storage / SD
#include "eeprom_addrs.h"
#include "sdcard.h"

// Time
#include "time_persist.h"
#include "time_state.h"
#include "timezone.h"

// WiFi
#include "wifi_power.h"
#include "wifi_store.h"
#include "wifi_time.h"

// Misc
#include "debug.h"
#include "esp_heap_caps.h"
#include "runtime_log.h"
#include "ui_state_pet_sleeping.h"
#include "version.h"
// End of includes

static void logMem(const char *tag)
{
  Serial.printf("[MEM][%s] free=%u largest=%u\n", tag, heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
}

UIState g_bootWizardAfterOkState = UIState::TITLE_MENU;
Tab g_bootWizardAfterOkTab = Tab::TAB_PET;

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

  runtimeLogf("[BOOT][ASSET_VER] minRequired=%s installed=%s haveInstalled=%d tooOld=%d", RH_MIN_REQUIRED_ASSET_PACK,
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

  const char *localManifestPath = "/raising_hell/assets/manifest_local.json";

  if (!SD.exists(localManifestPath))
    return false;

  const AssetOtaStatus st = assetOtaStatus();

  // Do not trust local manifest while OTA is actively checking/downloading/installing/failing.
  if (st != AssetOtaStatus::IDLE && st != AssetOtaStatus::SUCCESS)
    return false;

  return true;
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
static bool g_postProvisionControlsHelpPending = false;
static UIState s_bootFinalLandingState = UIState::TITLE_MENU;

// -----------------------------------------------------------------------------
// Boot Rescue
// -----------------------------------------------------------------------------
bool bootPostProvisionControlsHelpPending()
{
  if (g_postProvisionControlsHelpPending)
    return true;

  if (!g_sdReady)
    return false;

  return SD.exists(kPostProvisionControlsHelpFlagPath);
}

void bootPostProvisionControlsHelpClear()
{
  g_postProvisionControlsHelpPending = false;

  if (!g_sdReady)
    return;

  if (SD.exists(kPostProvisionControlsHelpFlagPath))
    SD.remove(kPostProvisionControlsHelpFlagPath);
}

// ---- Early TZ/anchor latches (Stage 0) ----
static bool g_tzAppliedEarly = false;
static bool g_anchorAppliedEarly = false;

// Time display gating (loop also checks these)
bool g_timeAnchorAttempted = false;
bool g_timeAnchorRestored = false;

// -----------------------------------------------------------------------------
// See if a save file already exists
// -----------------------------------------------------------------------------
static bool bootSaveFileExists() { return saveManagerSaveFileExists(); }

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

static bool bootAssetProvisionMandatory()
{
  if (!g_sdReady)
    return false;

  return !sdAssetsPresent();
}

bool g_bootUiBlockedForAssetProvision = false;

bool bootAssetProvisionRequired()
{
  // Never allow the boot provisioning pipeline to re-arm itself once we are
  // already past boot and assets disappear at runtime. Runtime missing-assets
  // handling owns that case.
  if (g_assetsMissing && !g_bootLandingDeferredForAssetProvision)
    return false;

  if (!g_sdReady)
    return false;

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
  {
    s_bootAssetProvisionHandled = false;
    return false;
  }

  if (s_bootAssetProvisionHandled)
    return true;

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
    s_bootAssetProvisionHandled = true;

    if (g_bootAssetProvisionMustComplete)
    {
      // Only route through post-provision controls help when this is effectively
      // a no-save / onboarding situation. Existing pets should resume normally.
      const bool saveExistsNow = bootSaveFileExists();
      if (!saveExistsNow)
      {
        writePostProvisionControlsHelpFlag();
      }

      settingsSetWifiEnabled(true);
      saveSettingsToSD();
    }

    g_bootAssetProvisionActive = false;
    g_bootUiBlockedForAssetProvision = false;
    ESP.restart();
  }

  runtimeLogf("[BOOT][ASSET_PROVISION] failed: %s", msg.c_str());

  // Allow retry after failure (for example, after the user fixes Wi-Fi creds).
  s_bootAssetProvisionHandled = false;

  // If assets are still missing, this is a mandatory provisioning failure.
  // Stay blocked on the provisioning/error screen, but do not permanently latch failure.
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
// This function MUST run before boot is considered complete.
// It owns final system bring-up (Wi-Fi, UI landing, etc).
{
  if (g_bootLandingDone)
    return;

  g_bootLandingDone = true;
  g_bootAssetProvisionMustComplete = false;
  g_bootProvisionWifiOnboardingStarted = false;

  Serial.printf("[BOOT][LAND] finalLanding=%d controlsHelpSeen=%d postProvisionHelp=%d\n", (int)s_bootFinalLandingState,
                g_controlsHelpSeen ? 1 : 0, g_postProvisionControlsHelpPending ? 1 : 0);

  // If we are landing on the title screen with no save file, this is a
  // "no-save title menu" landing, not an active new-pet flow.
  //
  // saveManagerLoad() may already have staged a fresh in-memory new-pet flow
  // and written name_pending.flag before the first-boot wizard ran.
  // If we keep those flags here, the next reboot will incorrectly think
  // onboarding/new-pet flow is still pending.
  if (s_bootFinalLandingState == UIState::TITLE_MENU && !bootSaveFileExists())
  {
    const bool hadNamePending = saveManagerNamePendingFlagExists();
    const bool hadBootSetupPending = bootSetupPendingFlagExists();

    if (hadNamePending)
      saveManagerClearNamePendingFlag();

    if (hadBootSetupPending)
      bootSetupClearPendingFlag();

    g_app.newPetFlowActive = false;

    Serial.printf("[BOOT][LAND] no-save title cleanup namePending=%d bootSetupPending=%d\n", hadNamePending ? 1 : 0,
                  hadBootSetupPending ? 1 : 0);

    Serial.printf("[BOOT][LAND] post-cleanup namePending=%d bootSetupPending=%d saveExists=%d\n",
                  saveManagerNamePendingFlagExists() ? 1 : 0, bootSetupPendingFlagExists() ? 1 : 0,
                  bootSaveFileExists() ? 1 : 0);
  }

  if (g_postProvisionControlsHelpPending)
  {
    g_postProvisionControlsHelpPending = false;
    g_controlsHelpSeen = 0;
    clearInputLatch();
    inputForceClear();

    controlsHelpBegin(UIState::TITLE_MENU, Tab::TAB_PET);
    return;
  }

  if (!g_controlsHelpSeen)
  {
    clearInputLatch();
    inputForceClear();

    if (s_bootFinalLandingState == UIState::CHOOSE_PET)
    {
      g_choosePetInputUnlockMs = millis() + 350;
      g_choosePetBlockHatchUntilRelease = true;
    }

    Serial.printf("[BOOT][LAND] controlsHelpBegin returnState=%d\n", (int)s_bootFinalLandingState);

    controlsHelpBegin(s_bootFinalLandingState, Tab::TAB_PET);
    return;
  }

  uint16_t seedMarkNow = 0;
  EEPROM.get(SEED_MARK_ADDR, seedMarkNow);

  if (seedMarkNow != SEED_MARK)
  {
    EEPROM.put(SEED_MARK_ADDR, (uint16_t)SEED_MARK);
    EEPROM.commit();
  }

  // Ensure Wi-Fi is actually applied before leaving boot
  if (!g_wifiApplied)
  {
    const bool pref = settingsWifiEnabled();

    Serial.printf("[BOOT WIFI APPLY - FINALIZE] pref=%d runtime_before=%d\n", pref ? 1 : 0, wifiIsEnabled() ? 1 : 0);

    wifiSetEnabled(false);
    applyWifiPower(false);

    wifiSetEnabled(pref);
    applyWifiPower(pref);

    Serial.printf("[BOOT WIFI APPLY - FINALIZE] pref=%d runtime_after=%d\n", pref ? 1 : 0, wifiIsEnabled() ? 1 : 0);

    g_wifiApplied = true;
  }

  if (g_app.uiState == UIState::BOOT)
  {
    if (s_bootFinalLandingState == UIState::PET_SLEEPING)
    {
      // Critical: free any stale UI/sleep buffers before entering sleep screen.
      graphicsReleaseUiCachesForMiniGame();
      Serial.printf("[HEAPCHK] boot-sleep-landing after-release free=%u largest=%u\n",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));
    }

    enterState(s_bootFinalLandingState, Tab::TAB_PET, false);

    if (s_bootFinalLandingState == UIState::PET_SLEEPING)
    {
      uiPetSleepingBootEnter();
    }
  }

  ui_setBootSplashActive(false);

  invalidateBackgroundCache();

  if (s_bootFinalLandingState == UIState::PET_SLEEPING)
  {
    requestFullUIRedraw();
    sleepBgKickNow();
    return;
  }

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
  // Runtime guard: if assets disappear after normal boot has already landed,
  // never let the boot pipeline re-enter provisioning. Hand off to the runtime
  // assets-missing flow instead.
  // ---------------------------------------------------------------------------
  if (!g_bootLandingDeferredForAssetProvision && g_app.uiState == UIState::PET_SCREEN && g_sdReady &&
      !sdAssetsPresent())
  {
    g_assetsChecked = true;
    g_assetsMissing = true;

    g_bootAssetProvisionActive = false;
    g_bootUiBlockedForAssetProvision = false;
    g_bootAssetProvisionMustComplete = false;
    g_bootProvisionWifiOnboardingStarted = false;

    return;
  }

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

    const bool saveFileExistsNow = bootSaveFileExists();
    runtimeLogf("[BOOT][SAVECHK] loadedFromSD=%d saveFileExists=%d", loadedFromSD ? 1 : 0, saveFileExistsNow ? 1 : 0);

    DBG_ON("[LOAD] saveManagerLoad -> %d\n", (int)loadedFromSD);

    updateTime();
    uiInitLevelPopupTracker();

    const bool setupPending = appLifecycleHasPendingOnboarding();

    // Post-provision help is a UI detour only.
    // It must never erase the fact that a valid save was loaded.
    const bool loadedSaveExists = loadedFromSD;
    const bool noLiveSave = !loadedSaveExists;
    
    // IMPORTANT:
    // A no-save boot must land at the title menu, not get forced into the old
    // first-boot wizard just because onboarding/time setup is pending.
    // Keep the wizard for:
    //   - missing settings
    //   - explicit forced first run
    //   - pending onboarding only when a live save actually exists
    const bool firstBootWizard =
        !settingsLoaded ||
        forcedFirstRun ||
        (setupPending && !noLiveSave);
    
    (void)saveFileExistsNow;
    
    UIState afterOk = appLifecycleResolveBootAfterOkState(loadedSaveExists);

    s_bootFinalLandingState = afterOk;

    bootMarkFirmwareSeenAndRequestProvisionIfChanged();
    const bool provisionRequested = bootAssetProvisionRequested();
    const bool provisionMandatory = bootAssetProvisionMandatory();
    const bool provisionTooOld = bootAssetPackTooOld();
    g_bootAssetPackTooOldCached = provisionTooOld;
    const bool assetsPresentNow = sdAssetsPresent();

    runtimeLogf("[BOOT][ASSET] requested=%d mandatory=%d tooOld=%d assetsPresent=%d minRequired=%s",
                provisionRequested ? 1 : 0, provisionMandatory ? 1 : 0, provisionTooOld ? 1 : 0,
                assetsPresentNow ? 1 : 0, RH_MIN_REQUIRED_ASSET_PACK);

    const bool deferForAssetProvision = provisionRequested || provisionMandatory || provisionTooOld;

    runtimeLogf("[BOOTPIPE] settingsLoaded=%d saveLoaded=%d timeValid=%d firstBootWizard=%d afterOk=%d",
                settingsLoaded ? 1 : 0, loadedFromSD ? 1 : 0, timeIsValid() ? 1 : 0, firstBootWizard ? 1 : 0,
                (int)afterOk);

    if (deferForAssetProvision)
      runtimeLogLine("[BOOT] path=ASSET_PROVISION_PENDING");
    else if (firstBootWizard)
      runtimeLogLine("[BOOT] path=FIRST_BOOT_WIZARD");
    else if (!timeIsValid())
      runtimeLogLine("[BOOT] path=TIME_INVALID_WIFI_RECOVERY");
    else if (loadedSaveExists && saveManagerSleepPendingFlagExists())
      runtimeLogLine("[BOOT] path=PET_SLEEPING_RESTORE");
    else if (!loadedSaveExists)
      runtimeLogLine("[BOOT] path=TITLE_MENU_NO_SAVE");
    else
      runtimeLogLine("[BOOT] path=TITLE_MENU_NORMAL");

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

      ui_setBootSplashActive(false);

      if (g_bootAssetProvisionMustComplete)
      {
        g_bootAssetProvisionActive = false;
        Serial.printf("[BOOT][PROVISION_UI] enter BOOT_ASSET_WIFI_REQUIRED sdReady=%d assetsMissing=%d must=%d "
                      "active=%d requested=%d ui=%d\n",
                      g_sdReady ? 1 : 0, g_assetsMissing ? 1 : 0, g_bootAssetProvisionMustComplete ? 1 : 0,
                      g_bootAssetProvisionActive ? 1 : 0, bootAssetProvisionRequested() ? 1 : 0, (int)g_app.uiState);
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
      g_bootWizardAfterOkState = afterOk;
      g_bootWizardAfterOkTab = Tab::TAB_PET;

      if (!g_controlsHelpSeen)
      {
        controlsHelpBegin(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET);
        return;
      }

      // First prefer creds we previously saved ourselves.
      String storedSsid, storedPwd;
      if (wifiStoreHasCreds() && wifiStoreLoad(storedSsid, storedPwd) && storedSsid.length() > 0)
      {
        Serial.printf("[BOOTPIPE] using stored wifi creds ssid='%s'\n", storedSsid.c_str());
        wifiConsoleBeginConnect(storedSsid.c_str(), storedPwd.c_str());
        uiActionEnterState(UIState::BOOT_WIFI_WAIT, Tab::TAB_PET, true);
        return;
      }

      // If none are stored, try launcher import.
      String importedSsid, importedPwd;
      if (launcherImportWifiCreds(importedSsid, importedPwd))
      {
        Serial.printf("[BOOTPIPE] launcher creds found for SSID: %s\n", importedSsid.c_str());
        wifiConsoleBeginConnect(importedSsid.c_str(), importedPwd.c_str());
        bootWifiSetImportedInfo(importedSsid.c_str());
        uiActionEnterState(UIState::BOOT_WIFI_IMPORTED, Tab::TAB_PET, true);
        return;
      }

      Serial.println("[BOOTPIPE] no stored wifi creds and no launcher import; entering BOOT_WIFI_PROMPT");
      uiActionEnterState(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET, true);
      requestFullUIRedraw();
      invalidateBackgroundCache();
      requestUIRedraw();
      renderUI();
      clearInputLatch();
      return;
    }

    // -----------------------------------------------------------------------
    // NOT first boot: if time is invalid, re-enter boot Wi-Fi flow
    // so the user gets another chance to import/use Wi-Fi before
    // falling back to manual time entry.
    // -----------------------------------------------------------------------
    if (!timeIsValid())
    {
      g_bootWizardAfterOkState = afterOk;
      g_bootWizardAfterOkTab = Tab::TAB_PET;

      // Try stored creds first (same as first boot wizard)
      String storedSsid, storedPwd;
      if (wifiStoreHasCreds() && wifiStoreLoad(storedSsid, storedPwd) && storedSsid.length() > 0)
      {
        Serial.printf("[BOOTPIPE] (time recovery) using stored wifi creds ssid='%s'\n", storedSsid.c_str());
        wifiConsoleBeginConnect(storedSsid.c_str(), storedPwd.c_str());
        uiActionEnterState(UIState::BOOT_WIFI_WAIT, Tab::TAB_PET, true);
        return;
      }

      // fallback → prompt
      Serial.println("[BOOTPIPE] (time recovery) no stored creds; entering BOOT_WIFI_PROMPT");

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
      // Boot is complete — finalize landing NOW (this runs Wi-Fi apply)
      if (!g_bootLandingDone)
      {
        Serial.printf("[BOOT] finalize direct landing=%d postBootInitDone=1 wifiApplied=%d\n",
                      (int)s_bootFinalLandingState, g_wifiApplied ? 1 : 0);

        finalizeBootLanding();
        return;
      }

      g_postBootInitDone = true;
    }
  }

  // ---------------------------------------------------------------------------
  // Stage 3.5: Deferred boot asset provisioning
  // ---------------------------------------------------------------------------
  // HARD GUARD: if assets disappeared during runtime, NEVER re-enter boot provisioning
  if (g_assetsMissing && g_app.uiState == UIState::PET_SCREEN)
  {
    g_bootAssetProvisionActive = false;
    g_bootUiBlockedForAssetProvision = false;
    g_bootAssetProvisionMustComplete = false;
    return;
  }

  if (bootAssetProvisionRequired())
  {
    const int wifiNow = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    const int uiNow = (int)g_app.uiState;
    const int onboardingNow = g_bootProvisionWifiOnboardingStarted ? 1 : 0;
    const int activeNow = g_bootAssetProvisionActive ? 1 : 0;
  
    static int s_prevAssetStage35Ui = -1;
    static int s_prevAssetStage35Wifi = -1;
    static int s_prevAssetStage35Onboarding = -1;
    static int s_prevAssetStage35Active = -1;
  
    const bool changed =
        (uiNow != s_prevAssetStage35Ui) ||
        (wifiNow != s_prevAssetStage35Wifi) ||
        (onboardingNow != s_prevAssetStage35Onboarding) ||
        (activeNow != s_prevAssetStage35Active);
  
    if (changed)
    {
      Serial.printf(
          "[BOOT][ASSET_STAGE35] required=%d deferred=%d must=%d blocked=%d active=%d wifi=%d ui=%d onboarding=%d\n",
          bootAssetProvisionRequired() ? 1 : 0,
          g_bootLandingDeferredForAssetProvision ? 1 : 0,
          g_bootAssetProvisionMustComplete ? 1 : 0,
          g_bootUiBlockedForAssetProvision ? 1 : 0,
          activeNow,
          wifiNow,
          uiNow,
          onboardingNow);
  
      s_prevAssetStage35Ui = uiNow;
      s_prevAssetStage35Wifi = wifiNow;
      s_prevAssetStage35Onboarding = onboardingNow;
      s_prevAssetStage35Active = activeNow;
    }
  
    if (bootAssetProvisionRequired() &&
        g_bootLandingDeferredForAssetProvision &&
        !g_bootAssetProvisionActive &&
        !g_bootProvisionWifiOnboardingStarted &&
        WiFi.status() == WL_CONNECTED &&
        g_app.uiState != UIState::BOOT)
    {
      Serial.printf("[BOOT][ASSET_STAGE35] forcing return to BOOT from ui=%d\n", (int)g_app.uiState);
      uiActionEnterState(UIState::BOOT, Tab::TAB_PET, true);
      requestFullUIRedraw();
      requestUIRedraw();
      return;
    }
  
    // Do not start/retry provisioning while the boot Wi-Fi flow is still active.
    if (bootAssetProvisionWifiOnboardingActive())
      return;
  
    // For mandatory provisioning, only run OTA once Wi-Fi is actually connected.
    if (g_bootAssetProvisionMustComplete && WiFi.status() != WL_CONNECTED)
      return;
  
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

    //   // ---------------------------------------------------------------------------
  // Stage 4: Persist time anchor once we have synced time
  // ---------------------------------------------------------------------------
  if (!g_ntpSaved && timeIsSynced())
  {
    g_ntpSaved = true;
    saveTimeAnchor();

    // If boot was paused for time recovery, time is now valid and we may still
    // be sitting on the splash/BOOT state without ever completing the normal
    // post-boot landing. Finish that handoff now.
    if (!g_bootLandingDeferredForAssetProvision && !g_bootLandingDone && g_app.uiState == UIState::BOOT)
    {
      Serial.printf("[BOOT][TIME_RECOVERY] synced -> finalize landing=%d\n", (int)s_bootFinalLandingState);
      finalizeBootLanding();
      return;
    }
  }
}