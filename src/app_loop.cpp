#include "app_loop.h"

// -----------------------------------------------------------------------------
// Arduino / platform
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <cstring>

// -----------------------------------------------------------------------------
// External libraries
// -----------------------------------------------------------------------------
#include "M5Cardputer.h"

// -----------------------------------------------------------------------------
// App / core state
// -----------------------------------------------------------------------------
#include "app_state.h"
#include "build_flags.h"

// -----------------------------------------------------------------------------
// Asset / boot / provisioning
// -----------------------------------------------------------------------------
#include "asset_ota.h"
#include "boot_pipeline.h"
#include "flow_boot_wifi.h"
#include "flow_controls_help.h"
#include "launcher_wifi_import.h"
#include "settings_flow_state.h"

// -----------------------------------------------------------------------------
// Display / rendering / UI
// -----------------------------------------------------------------------------
#include "auto_screen.h"
#include "console.h"
#include "display.h"
#include "display_state.h"
#include "graphics.h"
#include "menu_actions.h"
#include "settings_flow_state.h"
#include "ui_actions.h"
#include "ui_input_router.h"
#include "ui_level_popup.h"
#include "ui_runtime.h"
#include "ui_state_clock_mode.h"
#include "ui_state_console.h"
#include "ui_state_pet_sleeping.h"
#include "ui_tabs.h"

// -----------------------------------------------------------------------------
// Input / interaction
// -----------------------------------------------------------------------------
#include "input.h"
#include "input_activity_state.h"
#include "motion.h"
#include "power_button.h"

// -----------------------------------------------------------------------------
// Gameplay / simulation
// -----------------------------------------------------------------------------
#include "anim_engine.h"
#include "anomaly_manager.h"
#include "death_state.h"
#include "evolution_flow.h"
#include "game_options_state.h"
#include "hatching_flow.h"
#include "passive_xp.h"
#include "pet.h"
#include "pet_autonomy.h"
#include "sleep_state.h"
#include "sound.h"

// -----------------------------------------------------------------------------
// Storage / persistence / time / networking
// -----------------------------------------------------------------------------
#include "save_manager.h"
#include "sdcard.h"
#include "time_persist.h"
#include "time_state.h"
#include "wardrive_steps.h"
#include "wifi_store.h"
#include "wifi_time.h"

// -----------------------------------------------------------------------------
// Debug / misc
// -----------------------------------------------------------------------------
#include "debug_state.h"
#include "led_status.h"
#include "no_legacy_aliases.h"
#include "support_logging_state.h"

bool handleMenuInput(InputState &in);

static bool s_forcedFirstRender = false;
static bool s_bootKeepAwakeInited = false;
static uint32_t s_bootKeepAwakeUntilMs = 0;
static bool s_prevSleeping = false;
static bool s_prevScreenOn = true;

void appServicesTick(uint32_t nowMs)
{
  M5Cardputer.update();
  pollDebugPort();
  keyboardDebugTick();
  powerButtonTick(nowMs);
  postBootInitTick();

  if (g_app.uiState != UIState::DEATH_TRANSITION)
  {
    soundTick();
  }
}

static bool petWarningAudioAllowedForUi(UIState ui)
{
  switch (ui)
  {
  case UIState::PET_SCREEN:
  case UIState::PET_SLEEPING:
  case UIState::CLOCK_MODE:
  case UIState::INVENTORY:
  case UIState::SHOP:
  case UIState::SLEEP_MENU:
    return true;

  default:
    return false;
  }
}

static inline UIState uiStateForTab(Tab t)
{
  switch (t)
  {
  case Tab::TAB_SLEEP:
    return UIState::SLEEP_MENU;
  case Tab::TAB_INV:
    return UIState::INVENTORY;
  case Tab::TAB_SHOP:
    return UIState::SHOP;
  case Tab::TAB_PET:
  case Tab::TAB_STATS:
  case Tab::TAB_ACTIVITIES:
  case Tab::TAB_PLAY:
  default:
    return UIState::PET_SCREEN;
  }
}

static inline bool shouldTickAssetOtaNow()
{
  // HARD STOP: never run OTA if SD is not ready
  if (!g_sdReady)
    return false;

  // NEVER tick OTA if assets are missing at runtime
  if (g_assetsMissing)
    return false;

  switch (g_app.uiState)
  {
  case UIState::BOOT_WIFI_PROMPT:
  case UIState::BOOT_WIFI_IMPORTED:
  case UIState::BOOT_WIFI_WAIT:
  case UIState::BOOT_TZ_PICK:
  case UIState::BOOT_NTP_WAIT:
  case UIState::BOOT_ASSET_WIFI_REQUIRED:
    return true;

  default:
    break;
  }

  return g_bootAssetProvisionActive || g_bootUiBlockedForAssetProvision;
}

static bool uiStateAllowsConsoleHotkey(UIState s)
{
  switch (s)
  {
  case UIState::MINI_GAME:
  case UIState::MG_PAUSE:
    return false;

  default:
    return true;
  }
}

// ---------------------------------------------------------------------------
// Blocking UI pattern:
// - modal owns the frame
// - run auto screen-off
// - render if needed
// - tick background services
// - return (no further input/gameplay)
// ---------------------------------------------------------------------------

static void tickBlockingUiBackgroundServices(uint32_t nowMs, bool tickSoundAgain)
{
  wifiTimeTick();

  if (shouldTickAssetOtaNow())
    assetOtaTick();

  if (g_timeAnchorAttempted || timeIsSynced())
    updateTime();

  updateBattery();
  batteryProtectionTick(nowMs);
  saveManagerTick();
  maybePeriodicTimeSave();

  if (tickSoundAgain)
    soundTick();
}

static bool blockingUiAutoScreenOffCheck(uint32_t nowMs)
{
  (void)nowMs;

  autoScreenTick();

  if (!isScreenOn())
  {
#if LED_STATUS_ENABLED
    ledSetScreenOff(true);
    ledUpdatePetStatus(computeLedMode());
#endif
    delay(5);
    return true;
  }

  return false;
}

static void finishBlockingUiFrame(uint32_t nowMs, bool tickSoundAgain)
{
  tickBlockingUiBackgroundServices(nowMs, tickSoundAgain);
  delay(10);
}

static void consumeConfirmInput(InputState &input)
{
  input.selectOnce = false;
  input.encoderPressOnce = false;
  input.mgSelectOnce = false;
  input.menuOnce = false;
  input.homeOnce = false;
  input.escOnce = false;
}

void appMainLoopTick()
{
  // ---------------------------------------------------------------------------
  // TRUE LOOP-ENTRY TIMESTAMP
  // ---------------------------------------------------------------------------
  const uint32_t now = millis();

  // ---------------------------------------------------------------------------
  // BOOT KEEP-AWAKE (MUST run before SCREEN OFF PATH early return)
  // Prevents booting into a permanently blank screen if display begins OFF.
  // ---------------------------------------------------------------------------
  if (!s_bootKeepAwakeInited)
  {
    s_bootKeepAwakeInited = true;
    s_bootKeepAwakeUntilMs = now + 6000;
  }

  if ((int32_t)(now - s_bootKeepAwakeUntilMs) < 0)
  {
    // Feed inactivity timer during early boot.
    noteUserActivity();

    // If the panel starts off, force it ON during the boot window.
    if (!isScreenOn() && (uint32_t)(now - screenPowerLastManualToggleMs()) > 250)
    {
      SET_SCREEN_POWER(true);
      invalidateBackgroundCache();
      requestUIRedraw();
      clearInputLatch();
    }
  }

  // ---------------------------------------------------------------------------
  // SERIAL SAFE PRINT HELPERS
  // ---------------------------------------------------------------------------
  auto dbgCanWrite = [&](size_t need) -> bool
  {
    if (!g_debugEnabled)
      return false;
    return Serial.availableForWrite() >= (int)need;
  };

  auto dbgPrintln = [&](const char *s)
  {
    if (!g_debugEnabled)
      return;
    const size_t need = strlen(s) + 2;
    if (dbgCanWrite(need))
      Serial.println(s);
  };

  auto dbgPrintf = [&](const char *fmt, auto... args)
  {
    if (!g_debugEnabled)
      return;
    if (!dbgCanWrite(128))
      return;
    Serial.printf(fmt, args...);
  };

  // ---------------------------------------------------------------------------
  // MAIN LOOP
  // ---------------------------------------------------------------------------
  appServicesTick(now);

  const bool inDeathFlow = (g_app.uiState == UIState::DEATH) || (g_app.uiState == UIState::DEATH_TRANSITION) ||
                           (g_app.uiState == UIState::MINI_GAME) || (g_app.uiState == UIState::MG_PAUSE) ||
                           (g_app.uiState == UIState::BURIAL_SCREEN);

  const bool screenOnNow = isScreenOn();

  if (!s_prevScreenOn && screenOnNow)
  {
    invalidateBackgroundCache();
    requestUIRedraw();
    sleepBgNotifyScreenWake();
  }

  s_prevScreenOn = screenOnNow;

  // ---------------------------------------------------------------------------
  // SCREEN OFF PATH
  // ---------------------------------------------------------------------------
  if (!isScreenOn())
  {
    wifiTimeTick();
    if (shouldTickAssetOtaNow())
      assetOtaTick();
    updateTime();
    updateBattery();
    batteryProtectionTick(now);
    saveManagerTick();
    maybePeriodicTimeSave();

    // Pocket Mode: screen is blank, but safe idle states still track fictional
    // War Walking steps. This does not scan Wi-Fi or touch real networks.
    wardriveStepsTick(now);

    const bool hasLivePet_off = saveManagerSaveFileExists();
    bool sleepingNow_off = false;

    if (hasLivePet_off)
    {
      sleepingNow_off = isPetSleepingNow();

      if (sleepingNow_off)
      {
        pet.petSleepTick();
        petResetUpdateTimers(); // prevent decay "catch-up" on wake
      }
      else
      {
        petAutonomyTick(now);

        if (isPetSleepingNow())
        {
          sleepingNow_off = true;
          pet.petSleepTick();
          petResetUpdateTimers();
        }
        else
        {
          pet.update();
          passiveXpTick(now);
        }
      }

      if (pet.health <= 0 && petDeathEnabled && petDeathShouldAutoEnterForUi(g_app.uiState))
      {
        uiEndAlertScreenFlash();
#if LED_STATUS_ENABLED
        ledSetScreenOff(true);
        ledUpdatePetStatus(LED_PET_OFF);
#endif
        petEnterDeathState();
        clearInputLatch();
        return;
      }

      // Near-death beep MUST work even with screen off, but only while a pet-owned
      // screen owns the experience. Title/settings/boot should not beep without
      // on-screen pet context.
      if (petWarningAudioAllowedForUi(g_app.uiState))
      {
        soundLowHealthTick((uint8_t)pet.health, sleepingNow_off,
                           /*screenOn=*/isScreenOn(),
                           /*inDeathScreen=*/inDeathFlow);
      }
    }

    if (motionAvailable && motionShakeDetected())
    {
      screenWake();
      motionResetShakeDetector(2500);
      setLastInputActivityMs(now);
      invalidateBackgroundCache();
      requestFullUIRedraw();

#if LED_STATUS_ENABLED
      ledSetScreenOff(false);
      ledUpdatePetStatus(computeLedMode());
#endif

      renderUI();
      return;
    }

#if LED_STATUS_ENABLED
    ledSetScreenOff(true);
    ledUpdatePetStatus(computeLedMode());
#endif

    delay(5);
    return;
  }

  // ---------------------------------------------------------------------------
  // SCREEN ON PATH
  // ---------------------------------------------------------------------------
  // Force one render after boot so we never sit on a blank screen because
  // uiNeedsRedraw was never set by the boot pipeline.
  if (!s_forcedFirstRender)
  {
    s_forcedFirstRender = true;
    noteUserActivity();
    invalidateBackgroundCache();
    requestUIRedraw();
    renderUI();
  }

  // Sync text-capture mode *before* scanning input so Backspace, Enter, etc.
  // are interpreted correctly on text entry screens (SSID, password, console, etc.).
  {
    const bool wantText = uiWantsTextCaptureNow();
    if (wantText != g_textCaptureMode)
      inputSetTextCapture(wantText);
  }

  InputState input = readInput();

  // War Walking is accelerometer/RNG only. No real Wi-Fi scanning occurs.
  wardriveStepsTick(now);

#if LED_STATUS_ENABLED
  if (ledInputLockActive())
  {
    // Keep alert system in logical screen-off mode during the pulse.
    ledSetScreenOff(true);
    ledUpdatePetStatus(computeLedMode());

    // Keep critical background warning audio alive while the LED/backlight
    // alert owns input/display. soundTick() still runs from appServicesTick(),
    // but the low-health scheduler lives in the main loop paths and would
    // otherwise pause for the duration of the LED alert pulse.
    const bool sleepingNow_ledLock = isPetSleepingNow();
    if (petWarningAudioAllowedForUi(g_app.uiState))
    {
      soundLowHealthTick((uint8_t)pet.health, sleepingNow_ledLock,
                         /*screenOn=*/false,
                         /*inDeathScreen=*/inDeathFlow);
    }

    if (motionAvailable && motionShakeDetected())
    {
      screenWake();
      motionResetShakeDetector(2500);
      setLastInputActivityMs(now);
      invalidateBackgroundCache();
      requestFullUIRedraw();

      ledSetScreenOff(false);
      ledUpdatePetStatus(computeLedMode());
      renderUI();
      return;
    }

    input = InputState{};
    clearInputLatch();

    // IMPORTANT: the alert overlay requested a redraw; do it now before returning.
    renderUI();
    delay(5);
    return;
  }

  ledSetScreenOff(false);
  ledUpdatePetStatus(computeLedMode());
#endif

  const bool allowRuntimeSdProbe = (g_app.uiState == UIState::PET_SCREEN) || (g_app.uiState == UIState::SLEEP_MENU) ||
                                   (g_app.uiState == UIState::INVENTORY) || (g_app.uiState == UIState::SHOP) ||
                                   (g_app.uiState == UIState::PET_SLEEPING);

  // ---------------------------------------------------------------------------
  // SD / asset / provisioning state transition logging
  // ---------------------------------------------------------------------------
  if (allowRuntimeSdProbe)
  {
    static bool s_prevLoggedSdReady = true;
    static bool s_prevLoggedAssetsMissing = false;
    static bool s_prevLoggedProvisionMustComplete = false;
    static bool s_prevLoggedProvisionActive = false;
    static bool s_prevLoggedUiBlockedForProvision = false;

    const bool sdReadyNow = g_sdReady;
    const bool assetsMissingNow = g_assetsMissing;
    const bool provisionMustCompleteNow = g_bootAssetProvisionMustComplete;
    const bool provisionActiveNow = g_bootAssetProvisionActive;
    const bool uiBlockedForProvisionNow = g_bootUiBlockedForAssetProvision;

    if (sdReadyNow != s_prevLoggedSdReady || assetsMissingNow != s_prevLoggedAssetsMissing ||
        provisionMustCompleteNow != s_prevLoggedProvisionMustComplete ||
        provisionActiveNow != s_prevLoggedProvisionActive ||
        uiBlockedForProvisionNow != s_prevLoggedUiBlockedForProvision)
    {
      Serial.printf("[SDRUNTIME] sdReady=%d assetsMissing=%d provisionMust=%d provisionActive=%d uiBlocked=%d ui=%d\n",
                    sdReadyNow ? 1 : 0, assetsMissingNow ? 1 : 0, provisionMustCompleteNow ? 1 : 0,
                    provisionActiveNow ? 1 : 0, uiBlockedForProvisionNow ? 1 : 0, (int)g_app.uiState);

      s_prevLoggedSdReady = sdReadyNow;
      s_prevLoggedAssetsMissing = assetsMissingNow;
      s_prevLoggedProvisionMustComplete = provisionMustCompleteNow;
      s_prevLoggedProvisionActive = provisionActiveNow;
      s_prevLoggedUiBlockedForProvision = uiBlockedForProvisionNow;
    }

    // ---------------------------------------------------------------------------
    // Runtime SD / asset probe
    // Detect mid-session SD removal or asset disappearance.
    // ---------------------------------------------------------------------------
    static uint32_t s_nextSdProbeMs = 0;
    static uint8_t s_assetProbeFailCount = 0;

    if ((int32_t)(now - s_nextSdProbeMs) >= 0)
    {
      s_nextSdProbeMs = now + 1000;

      const bool sdReadyBeforeProbe = g_sdReady;
      const bool assetsMissingBeforeProbe = g_assetsMissing;

      bool assetsPresentNow = false;

      if (g_sdReady)
      {
        assetsPresentNow = sdAssetsPresent();

        if (!assetsPresentNow && supportLoggingEnabled())
        {
          Serial.printf("[SDRUNTIME] probe: sdReady=1 but assetsPresent=0 ui=%d\n", (int)g_app.uiState);
        }
      }

      const bool assetsOkNow = (g_sdReady && assetsPresentNow);

      if (assetsOkNow)
      {
        s_assetProbeFailCount = 0;
        g_assetsMissing = false;
      }
      else
      {
        if (s_assetProbeFailCount < 5)
          s_assetProbeFailCount++;

        if (s_assetProbeFailCount >= 3)
        {
          g_assetsMissing = true;

          if (supportLoggingEnabled())
          {
            Serial.printf("[SDRUNTIME] assets missing after %u consecutive failures\n", s_assetProbeFailCount);
          }
        }
      }

      g_assetsChecked = true;

      if (assetsMissingBeforeProbe && !g_assetsMissing)
      {
        Serial.println("[SDRUNTIME] assets recovered");

        invalidateBackgroundCache();
        requestUIRedraw();
      }

      // -------------------------------------------------------------------------
      // HARD STOP: kill any active provisioning session if assets disappear
      // -------------------------------------------------------------------------
      const bool badSdStateNow = (g_assetsMissing || !g_sdReady);
      const bool badSdStateBefore = (assetsMissingBeforeProbe || !sdReadyBeforeProbe);

      if (badSdStateNow)
      {
        const bool wasActive = g_bootAssetProvisionActive || g_bootUiBlockedForAssetProvision ||
                               g_bootProvisionWifiOnboardingStarted || g_bootAssetProvisionMustComplete;

        if (wasActive)
        {
          Serial.printf("[SDRUNTIME] FORCE STOP provisioning (assets missing) ui=%d\n", (int)g_app.uiState);
        }

        g_bootAssetProvisionMustComplete = false;
        g_bootUiBlockedForAssetProvision = false;
        g_bootAssetProvisionActive = false;
        g_bootProvisionWifiOnboardingStarted = false;

        if (!badSdStateBefore)
        {
          assetOtaResetState();
        }
      }
    }
  }

  // ---------------------------------------------------------------------------
  // MODAL: SD assets missing / SD card missing
  // ---------------------------------------------------------------------------
  {
    static bool s_assetsMissingModalDrawn = false;
    static bool s_assetsMissingModalLogged = false;
    static bool s_prevAssetsMissingModalActive = false;
    static bool s_prevAssetsMissingModalSdReady = true;

    const bool assetsMissingModalActive =
        (g_assetsMissing && !g_bootAssetProvisionMustComplete && g_app.uiState != UIState::CONSOLE);

    // If the modal just became inactive, fully reset its one-shot state.
    if (!assetsMissingModalActive && s_prevAssetsMissingModalActive)
    {
      s_assetsMissingModalDrawn = false;
      s_assetsMissingModalLogged = false;
    }

    // If SD readiness changed while the modal was/should be active,
    // force the modal to redraw so the message switches correctly between
    // "ASSETS MISSING" and "SD CARD NOT DETECTED".
    if (assetsMissingModalActive && (g_sdReady != s_prevAssetsMissingModalSdReady))
    {
      s_assetsMissingModalDrawn = false;
      s_assetsMissingModalLogged = false;
    }

    s_prevAssetsMissingModalActive = assetsMissingModalActive;
    s_prevAssetsMissingModalSdReady = g_sdReady;

    if (assetsMissingModalActive)
    {
      bool retryRequested = false;

      if (input.selectOnce)
        retryRequested = true;

      if (input.consoleOnce)
      {
        Serial.printf("[SDRUNTIME] console requested from assets-missing modal sdReady=%d ui=%d\n", g_sdReady ? 1 : 0,
                      (int)g_app.uiState);

        openConsoleWithReturn(g_app.uiState, g_app.currentTab,
                              /*retToSettings=*/false, g_settingsFlow.settingsPage);

        invalidateBackgroundCache();
        requestUIRedraw();
        renderUI();

        input = InputState{};
        clearInputLatch();
        return;
      }

      while (input.kbHasEvent())
      {
        KeyEvent ev = input.kbPop();
        const uint8_t c = ev.code;

        if (c == RH_KEY_ENTER || c == '\n' || c == '\r' || c == 'g' || c == 'G')
          retryRequested = true;
      }

      if (!s_assetsMissingModalLogged)
      {
        Serial.printf("[SDRUNTIME] entering assets-missing modal sdReady=%d assetsMissing=%d provisionMust=%d ui=%d\n",
                      g_sdReady ? 1 : 0, g_assetsMissing ? 1 : 0, g_bootAssetProvisionMustComplete ? 1 : 0,
                      (int)g_app.uiState);
        clearInputLatch();
        s_assetsMissingModalLogged = true;
      }

      if (!s_assetsMissingModalDrawn)
      {
        drawAssetsMissingScreen();
        s_assetsMissingModalDrawn = true;
      }

      if (input.consoleOnce)
      {
        Serial.printf("[SDRUNTIME] console requested from assets-missing modal sdReady=%d ui=%d\n", g_sdReady ? 1 : 0,
                      (int)g_app.uiState);

        openConsoleWithReturn(g_app.uiState, g_app.currentTab,
                              /*retToSettings=*/false, g_settingsFlow.settingsPage);

        invalidateBackgroundCache();
        requestUIRedraw();

        input = InputState{};
        clearInputLatch();
        return;
      }

      if (retryRequested)
      {
        Serial.printf("[SDRUNTIME] retry requested sdReady=%d ui=%d\n", g_sdReady ? 1 : 0, (int)g_app.uiState);

        g_sdReady = remountSDWithRetry(3);

        Serial.printf("[SDRUNTIME] retry remount result sdReady=%d ui=%d\n", g_sdReady ? 1 : 0, (int)g_app.uiState);

        if (g_sdReady)
        {
          g_sdGaveUp = false;
          g_sdFirstTryMs = 0;
          g_sdTryCount = 0;
        }

        g_assetsMissing = !(g_sdReady && sdAssetsPresent());
        g_assetsChecked = true;

        if (g_assetsMissing)
        {
          Serial.printf("[SDRUNTIME] retry: assets still missing sdReady=%d\n", g_sdReady ? 1 : 0);

          g_bootAssetProvisionMustComplete = false;
          g_bootUiBlockedForAssetProvision = false;
          g_bootAssetProvisionActive = false;
          g_bootProvisionWifiOnboardingStarted = false;

          assetOtaResetState();

          s_assetsMissingModalDrawn = false;
          s_assetsMissingModalLogged = false;

          // If the card is still not mountable after explicit user retry,
          // do a cold reboot. Hot-remount is not recovering on this hardware,
          // but cold boot does.
          if (!g_sdReady)
          {
            Serial.println("[SDRUNTIME] retry failed with sdReady=0; staying in SD missing modal");

            // HARD STOP: kill provisioning state
            g_bootAssetProvisionMustComplete = false;
            g_bootUiBlockedForAssetProvision = false;
            g_bootAssetProvisionActive = false;
            g_bootProvisionWifiOnboardingStarted = false;

            assetOtaResetState();

            // Force modal redraw
            s_assetsMissingModalDrawn = false;
            s_assetsMissingModalLogged = false;

            clearInputLatch();
          }
        }
        else
        {
          g_bootAssetProvisionMustComplete = false;
          g_bootUiBlockedForAssetProvision = false;
          g_bootAssetProvisionActive = false;
          g_bootProvisionWifiOnboardingStarted = false;

          s_assetsMissingModalDrawn = false;
          s_assetsMissingModalLogged = false;

          drawBootSplash();
          invalidateBackgroundCache();
          requestUIRedraw();
          renderUI();
        }
      }

      soundTick();
      delay(10);
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // PERSISTENT TOAST / RESULT POPUP
  // ---------------------------------------------------------------------------
  if (uiToastIsPersistent())
  {
    if (blockingUiAutoScreenOffCheck(now))
      return;

    if (input.selectOnce || input.encoderPressOnce || input.mgSelectOnce || input.menuOnce || input.homeOnce ||
        input.escOnce)
    {
      playBeep();

      if (wardriveStepsNoticeActive())
        wardriveStepsDismissNotice();

      uiDismissToast();

      // HARD CONSUME: this input must not leak into the rest of the frame or
      // remain latched into the next menu/action after the toast closes.
      consumeConfirmInput(input);
      clearInputLatch();

      requestUIRedraw();

      if (consumeUIRedrawRequest())
        renderUI();

      // Keep background systems ticking, but DO NOT process gameplay/input
      finishBlockingUiFrame(now, true);
      return;
    }

    // No dismiss yet — just keep it visible
    requestUIRedraw();

    if (consumeUIRedrawRequest())
      renderUI();

    finishBlockingUiFrame(now, true);
    return;
  }

  // ---------------------------------------------------------------------------
  // LEVEL UP MODAL (blocks input until dismissed with ENTER or G)
  // ---------------------------------------------------------------------------
  if (uiIsLevelUpPopupActive())
  {
    if (blockingUiAutoScreenOffCheck(now))
      return;

    if (input.selectOnce || input.encoderPressOnce)
    {
      consumeConfirmInput(input);

      if (uiLevelUpPopupCanDismiss())
      {
        uiDismissLevelUpPopup();
        requestUIRedraw();
      }
      else
      {
        inputForceClear();
        requestUIRedraw();
      }
    }
    else
    {
      requestUIRedraw();
    }
    if (consumeUIRedrawRequest())
    {
      renderUI();
    }

    finishBlockingUiFrame(now, false);
    return;
  }

  // ---------------------------------------------------------------------------
  // FAST TAB SWITCH PATH (apply state immediately, do NOT render here)
  // ---------------------------------------------------------------------------
  {
    const bool allowTabLR_fast = (g_app.uiState == UIState::PET_SCREEN) || (g_app.uiState == UIState::SLEEP_MENU) ||
                                 (g_app.uiState == UIState::INVENTORY) || (g_app.uiState == UIState::SHOP);

    if (allowTabLR_fast && (input.leftOnce || input.rightOnce))
    {
      if (input.leftOnce)
        tabPrev();
      if (input.rightOnce)
        tabNext();

      noteUserActivity();

      invalidateBackgroundCache();

      syncUiToTab();
      requestUIRedraw();
      soundClick();
      return;
    }
  }

  if (g_app.uiState != UIState::CONSOLE && g_app.uiState != UIState::MINI_GAME &&
      g_app.uiState != UIState::ACTIVITY_FISHING)
  {
    if (input.upOnce || input.downOnce || (input.encoderDelta != 0))
      soundMenuTick();
    if (input.leftOnce || input.rightOnce)
      soundClick();

    const bool suppressGlobalConfirm = (g_app.uiState == UIState::DEATH) || (g_app.uiState == UIState::SHOP);

    if ((input.selectOnce || input.encoderPressOnce) && !suppressGlobalConfirm)
      soundConfirm();

    if (input.menuOnce || input.homeOnce || input.escOnce)
      soundCancel();
  }

  // AUTO SCREEN
  const bool keepScreenAwakeForProvision = g_bootAssetProvisionActive || g_bootUiBlockedForAssetProvision;

  if (keepScreenAwakeForProvision)
  {
    noteUserActivity();

    if (!isScreenOn() && (uint32_t)(now - screenPowerLastManualToggleMs()) > 250)
    {
      SET_SCREEN_POWER(true);
      invalidateBackgroundCache();
      requestUIRedraw();
      clearInputLatch();
    }
  }
  else
  {
    if (hasUserActivity(input))
    {
      noteUserActivity();
      anomalyNotifyUserActivity(now);
      wardriveStepsNotifyUserActivity();
    }

    // Non-pet settings-owned flows should eventually unwind back to PET.
    // Keep this separate from Auto Screen / Auto Clock so menus don't just sit forever.
    const bool settingsOwnedFlow = (g_app.uiState == UIState::SETTINGS);

    static const uint32_t kSettingsIdleReturnMs = 120000;

    if (settingsOwnedFlow && settingsHasReturnTarget())
    {
      const uint32_t nowMs = millis();
      if ((uint32_t)(nowMs - g_lastInputActivityMs) >= kSettingsIdleReturnMs)
      {
        if (g_app.uiState == UIState::SETTINGS)
        {
          closeSettingsAndReturn(input);

          invalidateBackgroundCache();

          if (g_app.uiState == UIState::TITLE_MENU)
            requestFullUIRedraw();
          else
            requestUIRedraw();

          renderUI();
        }
        else
        {
          returnToSettingsPage(g_settingsFlow.settingsPage, g_app.currentTab, input);
          invalidateBackgroundCache();
          requestUIRedraw();
          renderUI();
        }

        input = InputState{};
        clearInputLatch();
        return;
      }
    }

    if (autoClockIsEnabled())
      autoClockTick();
    else
      autoScreenTick();
  }

  if (!isScreenOn())
  {

#if LED_STATUS_ENABLED
    ledSetScreenOff(true);
    ledUpdatePetStatus(computeLedMode());
#endif
    delay(5);
    return;
  }

  // DEATH/BURIAL special flow
  if (g_app.uiState == UIState::DEATH)
  {
    // Allow support console access from the death menu so recovery commands
    // like 'resurrect' are reachable before burial.
    if (input.consoleOnce)
    {
      noteUserActivity();

      openConsoleWithReturn(g_app.uiState, g_app.currentTab,
                            /*retToSettings=*/false, g_settingsFlow.settingsPage);

      invalidateBackgroundCache();
      requestUIRedraw();
      input = InputState{};
      clearInputLatch();
      return;
    }

    const UIState before = g_app.uiState;
    handleMenuInput(input);

    if (g_app.uiState != before)
    {
      g_app.uiNeedsRedraw = true;
      renderUI();
      return;
    }

    if (input.upOnce || input.downOnce || input.leftOnce || input.rightOnce || input.selectOnce || input.menuOnce ||
        input.escOnce || input.encoderPressOnce || input.encoderDelta != 0)
    {
      anomalyNotifyUserActivity(now);
      requestUIRedraw();
    }

    renderUI();
    return;
  }

  if (g_app.uiState == UIState::BURIAL_SCREEN)
  {
    // Allow support console access while the burial confirmation screen is up.
    // If the user has not confirmed burial yet, 'resurrect' can still recover.
    if (input.consoleOnce)
    {
      noteUserActivity();

      openConsoleWithReturn(g_app.uiState, g_app.currentTab,
                            /*retToSettings=*/false, g_settingsFlow.settingsPage);

      invalidateBackgroundCache();
      requestUIRedraw();
      input = InputState{};
      clearInputLatch();
      return;
    }

    handleMenuInput(input);
    if (input.selectOnce || input.encoderPressOnce)
      requestUIRedraw();
    renderUI();
    return;
  }

  // AUTO-RETURN TO PET TAB
  if (g_app.uiState == UIState::PET_SCREEN && g_app.currentTab != Tab::TAB_PET)
  {
    const uint32_t nowMs = millis();
    if ((uint32_t)(nowMs - getLastInputActivityMs()) >= 60000UL)
    {
      uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, false, input, 120);
    }
  }

  // ---------------------------------------------------------------------------
  // HOTKEYS: Console + Settings (must run BEFORE handleMenuInput)
  // ---------------------------------------------------------------------------

  // SET TIME: lock out global hotkeys so the editor can't be bypassed
  if (g_app.uiState == UIState::SET_TIME)
  {
    input.tabJump = 255;
    input.consoleOnce = false;
    input.hotSettings = false;
    input.homeOnce = false;
  }

  // If sleeping, block focus-stealing tab hotkeys.
  const bool sleepingNow = isPetSleepingNow();
  if (sleepingNow)
  {
    input.tabJump = 255;

    if (g_app.uiState == UIState::PET_SLEEPING)
    {
      input.upOnce = false;
      input.downOnce = false;
      input.leftOnce = false;
      input.rightOnce = false;
    }
  }

  // Keep title screen isolated from global tab/home/settings shortcuts,
  // but allow the title menu to handle ESC/menu locally.
  if (g_app.uiState == UIState::TITLE_MENU)
  {
    input.hotSettings = false;
    input.homeOnce = false;
    input.tabJump = 255;
  }

  // Don't allow ESC/Q/tab jumps to steal focus on New Pet flow screens
  else if (g_app.uiState == UIState::CHOOSE_PET)
  {
    // Allow ESC to back out of egg select, but still block global menu/tab steals.
    input.hotSettings = false;
    input.menuOnce = false;
    input.homeOnce = false;
    input.tabJump = 255;
  }
  else if (g_app.uiState == UIState::NAME_PET)
  {
    // Name entry owns its own cancel behavior locally.
    input.homeOnce = false;
    input.tabJump = 255;
  }
  else if (g_app.uiState == UIState::CLOCK_MODE)
  {
    input.hotSettings = false;
    input.tabJump = 255;
  }
  else
  {
    // Bottom-row tab hotkeys (z x c v b n m) — only when not in restricted screens
    if (g_app.uiState != UIState::NAME_PET && g_app.uiState != UIState::SET_TIME)
    {
      if (sleepingNow && input.tabJump != 255)
      {
        input.tabJump = 255;
      }

      // Don't allow ESC/Q/tab jumps to steal focus during Hatching/Evolution
      if (g_app.uiState == UIState::HATCHING || g_app.flow.evo.active || g_app.uiState == UIState::EVOLUTION)
      {
        input.tabJump = 255;
        input.escOnce = false;
        input.hotSettings = false;
        input.menuOnce = false;
        input.homeOnce = false;
      }

      if (input.tabJump != 255)
      {
        noteUserActivity();

        const Tab nt = (Tab)input.tabJump;
        uiActionEnterStateClean(uiStateForTab(nt), nt, false, input, 120);

        invalidateBackgroundCache();
        // do not clear latch here — allow next UI to consume input
        return;
      }
    }
  }

  // "/" toggles console
  if (uiStateAllowsConsoleHotkey(g_app.uiState) && g_app.uiState != UIState::SET_TIME && input.consoleOnce)
  {
    noteUserActivity();

    if (g_app.uiState != UIState::CONSOLE)
    {
      const UIState returnState = g_app.uiState;
      const Tab returnTab = g_app.currentTab;
      const bool retSettings = (returnState == UIState::SETTINGS);
      const SettingsPage retPage = g_settingsFlow.settingsPage;

      openConsoleWithReturn(returnState, returnTab, retSettings, retPage);
    }
    else
    {
      closeConsoleAndReturn(input);
    }

    invalidateBackgroundCache();
    requestUIRedraw();
    input = InputState{};
    clearInputLatch();
    return;
  }

  // ---------------------------------------------------------------------------
  // HOME KEY (Q): unwind modal flows first, otherwise return to the main pet flow
  // IMPORTANT: this is separate from MENU/ESC which are for opening/dismissing menus.
  // ---------------------------------------------------------------------------  //
  // ---------------------------------------------------------------------------
  if (input.homeOnce)
  {
    const bool settingsOwnedFlow = (g_app.uiState == UIState::SETTINGS) ||
                                   (g_app.uiState == UIState::IMPORT_PET_LIST) ||
                                   (g_app.uiState == UIState::BACKUP_PET_LIST);

    if (settingsOwnedFlow && settingsHasReturnTarget())
    {
      noteUserActivity();

      if (g_app.uiState == UIState::SETTINGS)
      {
        closeSettingsAndReturn(input);

        invalidateBackgroundCache();

        if (g_app.uiState == UIState::TITLE_MENU)
          requestFullUIRedraw();
        else
          requestUIRedraw();
      }
      else
      {
        returnToSettingsPage(g_settingsFlow.settingsPage, g_app.currentTab, input);
        invalidateBackgroundCache();
        requestUIRedraw();
      }

      input = InputState{};
      clearInputLatch();
      return;
    }

    if (g_app.uiState == UIState::CLOCK_MODE)
    {
      noteUserActivity();
      uiClockModeExitToReturn(input, 120);
      input = InputState{};
      clearInputLatch();
      return;
    }

    if (g_app.uiState == UIState::PET_SLEEPING)
    {
      noteUserActivity();
      uiPetSleepingWakeAndReturn(input, 120, true);
      input = InputState{};
      clearInputLatch();
      return;
    }

    const bool canHome = (g_app.uiState != UIState::SET_TIME) && (g_app.uiState != UIState::POWER_MENU) &&
                         (g_app.uiState != UIState::DEATH) && (g_app.uiState != UIState::BURIAL_SCREEN) &&
                         (g_app.uiState != UIState::MINI_GAME) && (g_app.uiState != UIState::HATCHING) &&
                         (g_app.uiState != UIState::EVOLUTION);

    if (canHome && g_app.uiState != UIState::PET_SLEEPING)
    {
      noteUserActivity();

      const bool shouldEnterSleeping = pet.isSleeping || g_app.isSleeping || g_app.sleepingByTimer ||
                                       g_app.sleepUntilRested || g_app.sleepUntilAwakened ||
                                       saveManagerSleepPendingFlagExists();

      if (shouldEnterSleeping)
      {
        enterSleepFlow(g_app.uiState, g_app.currentTab, input, 200);
      }
      else
      {
        uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, false, input, 200);
      }

      invalidateBackgroundCache();
      requestUIRedraw();
      input = InputState{};
      clearInputLatch();
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // Waking from sleep screen state
  // ---------------------------------------------------------------------------
  if (g_app.uiState == UIState::PET_SCREEN && isPetSleepingNow())
  {
    enterSleepFlow(UIState::PET_SCREEN, Tab::TAB_PET, input, 200);
    invalidateBackgroundCache();
    input = InputState{};
    clearInputLatch();
    return;
  }

  // ---------------------------------------------------------------------------
  // Input-driven redraw hint (single copy)
  // ---------------------------------------------------------------------------
  if (input.menuOnce || input.escOnce || input.selectOnce || input.upOnce || input.downOnce ||
      (input.encoderDelta != 0))
  {
    requestUIRedraw();
  }

  // ---------------------------------------------------------------------------
  // HATCHING: modal tick, then render, then return
  // ---------------------------------------------------------------------------
  if (g_app.uiState == UIState::HATCHING)
  {
    updateHatching();
    if (isScreenOn())
      requestUIRedraw();

    if (consumeUIRedrawRequest())
    {
      renderUI();
    }

    finishBlockingUiFrame(now, false);
    return;
  }

  // ---------------------------------------------------------------------------
  // EVOLUTION: modal tick, then render, then return
  // ---------------------------------------------------------------------------
  if (g_app.flow.evo.active || g_app.uiState == UIState::EVOLUTION)
  {
    updateEvolution();
    if (isScreenOn())
      requestUIRedraw();

    if (consumeUIRedrawRequest())
    {
      renderUI();
    }

    finishBlockingUiFrame(now, false);

    return;
  }

  // ---------------------------------------------------------------------------
  // Pet tick (ALWAYS run even if Console is open, but only with a saved pet)
  // ---------------------------------------------------------------------------
  const bool hasLivePet = saveManagerSaveFileExists();

  if (!inDeathFlow && hasLivePet)
  {
    if (isPetSleepingNow())
    {
      pet.petSleepTick();
      petResetUpdateTimers();
    }
    else
    {
      petAutonomyTick(now);

      if (isPetSleepingNow())
      {
        pet.petSleepTick();
        petResetUpdateTimers();
      }
      else
      {
        pet.update();
        passiveXpTick(now);
        petAutonomyNotifyIfPending(now);
        uiMaybeShowLevelUpPopup();
      }
    }

    if (pet.health <= 0 && petDeathEnabled && petDeathShouldAutoEnterForUi(g_app.uiState))
    {
      petEnterDeathState();
      requestUIRedraw();
      clearInputLatch();
    }
  }

  // ---------------------------------------------------------------------------
  // Menu input (includes global interceptors)
  // ---------------------------------------------------------------------------
  handleMenuInput(input);

  static bool s_prevDbgRedraw = false;
  static int s_prevDbgUiState = -1;

  static constexpr bool kLogUiStateTransitions = false;

  if (kLogUiStateTransitions && (int)g_app.uiState != s_prevDbgUiState)
  {
    Serial.printf("[UI STATE] uiState=%d screenOn=%d\n", (int)g_app.uiState, (int)isScreenOn());
    s_prevDbgUiState = (int)g_app.uiState;
  }

  const bool hasLivePetPostMenu = saveManagerSaveFileExists();
  const bool sleepingNow2 = hasLivePetPostMenu && isPetSleepingNow();

  if (hasLivePetPostMenu)
  {
    if (!s_prevSleeping && sleepingNow2)
      soundSleep();
    if (s_prevSleeping && !sleepingNow2)
      soundWake();
  }
  else
  {
    s_prevSleeping = false;
  }

  s_prevSleeping = sleepingNow2;

  const bool inDeathTransition = (g_app.uiState == UIState::DEATH_TRANSITION);

  if (!inDeathTransition)
  {
    if (hasLivePetPostMenu && petWarningAudioAllowedForUi(g_app.uiState))
    {
      soundLowHealthTick((uint8_t)pet.health, sleepingNow2,
                         /*screenOn=*/isScreenOn(),
                         /*inDeathScreen=*/inDeathFlow);
    }

    if (g_sdReady)
    {
      animTick();
    }

    anomalyTick(now);

    sleepAnimHeartbeat(now);
    sleepMiniStatsHeartbeat(now);
  }

  if (g_app.uiState == UIState::DEATH_TRANSITION)
  {
    tickDeathTransition(millis());
  }

  if (consumeUIRedrawRequest())
  {
    renderUI();
  }

  wifiTimeTick();
  if (g_timeAnchorAttempted || timeIsSynced())
    updateTime();

  static bool s_birthHealAfterTimeSyncDone = false;

  if (!s_birthHealAfterTimeSyncDone && timeIsNtpSyncedStrict())
  {
    s_birthHealAfterTimeSyncDone = true;

    if (saveManagerAutoHeal())
    {
      Serial.println("[TIME] post-sync birth autoheal applied");

      saveManagerMarkDirty();
      requestUIRedraw();
    }
  }

  updateBattery();
  batteryProtectionTick(now);
  saveManagerTick();
  maybePeriodicTimeSave();

#if LED_STATUS_ENABLED
  ledSetScreenOff(false);
  ledUpdatePetStatus(computeLedMode());
#endif
}