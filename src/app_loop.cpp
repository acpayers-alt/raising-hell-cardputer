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
#include "ui_state_console.h"
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
#include "death_state.h"
#include "evolution_flow.h"
#include "game_options_state.h"
#include "hatching_flow.h"
#include "pet.h"
#include "sleep_state.h"
#include "sound.h"

// -----------------------------------------------------------------------------
// Storage / persistence / time / networking
// -----------------------------------------------------------------------------
#include "save_manager.h"
#include "sdcard.h"
#include "time_persist.h"
#include "time_state.h"
#include "wifi_store.h"
#include "wifi_time.h"

// -----------------------------------------------------------------------------
// Debug / misc
// -----------------------------------------------------------------------------
#include "debug_state.h"
#include "led_status.h"
#include "no_legacy_aliases.h"

bool handleMenuInput(InputState &in);

static bool s_forcedFirstRender = false;
static uint32_t s_hbNextMs = 0;
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
  case Tab::TAB_FEED:
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
                           (g_app.uiState == UIState::MINI_GAME) || (g_app.uiState == UIState::BURIAL_SCREEN);

  const bool screenOnNow = isScreenOn();

  if (!s_prevScreenOn && screenOnNow)
  {
    invalidateBackgroundCache();
    requestUIRedraw();
    sleepBgNotifyScreenWake();
    clearInputLatch();
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

    const bool sleepingNow_off = isPetSleepingNow();

    if (sleepingNow_off)
    {
      pet.petSleepTick();
      petResetUpdateTimers(); // prevent decay "catch-up" on wake
    }
    else
    {
      pet.update();
    }

    // Near-death beep MUST work even with screen off.
    if (g_app.uiState != UIState::DEATH_TRANSITION)
    {
      soundLowHealthTick((uint8_t)pet.health, sleepingNow_off,
                         /*screenOn=*/isScreenOn(),
                         /*inDeathScreen=*/inDeathFlow);
    }

    if (motionAvailable && motionShakeDetected())
    {
      SET_SCREEN_POWER(true);
      motionResetShakeDetector(2500);
      noteUserActivity();
      invalidateBackgroundCache();
      requestUIRedraw();
      clearInputLatch();
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
    if ((int32_t)(now - s_nextSdProbeMs) >= 0)
    {
      s_nextSdProbeMs = now + 300;

      const bool sdReadyBeforeProbe = g_sdReady;
      const bool assetsMissingBeforeProbe = g_assetsMissing;

      bool assetsPresentNow = false;

      // Always verify SD state — don't trust stale g_sdReady.
      // IMPORTANT: do NOT auto-remount here, because that blocks the UI and
      // makes the pet screen appear frozen. The modal retry path will handle
      // remount explicitly when the user presses Enter.
      if (g_sdReady)
      {
        if (!SD.exists("/"))
        {
          Serial.printf("[SDRUNTIME] probe: SD root missing -> forcing sdReady=0 ui=%d\n", (int)g_app.uiState);

          g_sdReady = false;
          assetsPresentNow = false;
        }
        else
        {
          assetsPresentNow = sdAssetsPresent();
          if (!assetsPresentNow)
          {
            Serial.printf("[SDRUNTIME] probe: sdReady=1 but assetsPresent=0 -> forcing assetsMissing ui=%d\n",
                          (int)g_app.uiState);
          }
        }
      }

      g_assetsMissing = !(g_sdReady && assetsPresentNow);
      g_assetsChecked = true;

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

  //   // ---------------------------------------------------------------------------
  // MODAL: SD assets missing / SD card missing
  // ---------------------------------------------------------------------------
  {
    static bool s_assetsMissingModalDrawn = false;
    static bool s_assetsMissingModalLogged = false;
    static bool s_prevAssetsMissingModalActive = false;
    static bool s_prevAssetsMissingModalSdReady = true;

    const bool assetsMissingModalActive = (g_assetsMissing && !g_bootAssetProvisionMustComplete);

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
            Serial.println("[SDRUNTIME] retry failed with sdReady=0 -> rebooting for cold SD init");
            delay(100);
            ESP.restart();
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
  // LEVEL UP MODAL (blocks input until dismissed with ENTER or G)
  // ---------------------------------------------------------------------------
  {
    static bool s_prevSelectHeld_levelUp = false;
    const bool enterOnce = (input.selectHeld && !s_prevSelectHeld_levelUp);
    s_prevSelectHeld_levelUp = input.selectHeld;

    if (uiIsLevelUpPopupActive())
    {
      if (enterOnce || input.selectOnce)
      {
        uiDismissLevelUpPopup();
        clearInputLatch();
        requestUIRedraw();
      }
      else
      {
        requestUIRedraw();
      }
      if (consumeUIRedrawRequest())
      {
        renderUI();
      }

      wifiTimeTick();

      if (shouldTickAssetOtaNow())
        assetOtaTick();
      if (g_timeAnchorAttempted || timeIsSynced())
        updateTime();
      updateBattery();
      batteryProtectionTick(now);
      saveManagerTick();
      maybePeriodicTimeSave();

#if LED_STATUS_ENABLED
      ledSetScreenOff(false);
      ledUpdatePetStatus(computeLedMode());
#endif
      return;
    }
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
      clearInputLatch();

      invalidateBackgroundCache();

      syncUiToTab();
      requestUIRedraw();
      soundClick();
      return;
    }
  }

  if (g_app.uiState != UIState::CONSOLE)
  {
    if (input.upOnce || input.downOnce || (input.encoderDelta != 0))
      soundMenuTick();
    if (input.leftOnce || input.rightOnce)
      soundClick();
    if (input.selectOnce || input.encoderPressOnce)
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
      noteUserActivity();

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
    const UIState before = g_app.uiState;
    handleMenuInput(input);

    if (g_app.uiState != before)
    {
      g_app.uiNeedsRedraw = true;
      renderUI();
      return;
    }

    if (input.upOnce || input.downOnce || input.selectOnce || input.encoderPressOnce || (input.encoderDelta != 0))
    {
      requestUIRedraw();
    }

    renderUI();
    return;
  }

  if (g_app.uiState == UIState::BURIAL_SCREEN)
  {
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
      uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, false);
      clearInputLatch();
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
  
  // Keep title screen isolated from tabs/settings, but allow console as rescue surface.
  if (g_app.uiState == UIState::TITLE_MENU)
  {
    input.escOnce = false;
    input.hotSettings = false;
    input.menuOnce = false;
    input.homeOnce = false;
    input.tabJump = 255;
  }
  // Don't allow ESC/Q/tab jumps to steal focus on New Pet flow screens
  else if (g_app.uiState == UIState::CHOOSE_PET)
  {
    input.escOnce = false;
    input.hotSettings = false;
    input.menuOnce = false;
    input.homeOnce = false;
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
        clearInputLatch();
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
        clearInputLatch();
        return;
      }
    }
  }
  
  // "/" toggles console
  if (uiStateAllowsConsoleHotkey(g_app.uiState) &&
      g_app.uiState != UIState::SET_TIME &&
      input.consoleOnce)
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
    // HOME KEY (Q): return to PET tab from anywhere reasonable
    // IMPORTANT: this is separate from MENU/ESC which are for opening/dismissing menus.
    // ---------------------------------------------------------------------------
    if (input.homeOnce)
    {
      if (g_app.uiState == UIState::SETTINGS && settingsHasReturnTarget())
      {
        noteUserActivity();
        closeSettingsAndReturn(input);
        invalidateBackgroundCache();
        requestUIRedraw();
        input = InputState{};
        clearInputLatch();
        return;
      }
    
      const bool canHome = (g_app.uiState != UIState::SET_TIME) &&
                           (g_app.uiState != UIState::POWER_MENU) &&
                           (g_app.uiState != UIState::DEATH) &&
                           (g_app.uiState != UIState::BURIAL_SCREEN) &&
                           (g_app.uiState != UIState::MINI_GAME) &&
                           (g_app.uiState != UIState::HATCHING) &&
                           (g_app.uiState != UIState::EVOLUTION);
    
      if (canHome && g_app.uiState != UIState::PET_SLEEPING)
      {
        noteUserActivity();
    
        uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, false, input, 200);
    
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
    if (g_app.uiState == UIState::PET_SLEEPING && !isPetSleepingNow())
    {
      petResetUpdateTimers();
      uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, false, input, 200);
      invalidateBackgroundCache();
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

      wifiTimeTick();
      if (g_timeAnchorAttempted || timeIsSynced())
        updateTime();
      updateBattery();
      batteryProtectionTick(now);
      saveManagerTick();
      maybePeriodicTimeSave();

#if LED_STATUS_ENABLED
      ledSetScreenOff(false);
      ledUpdatePetStatus(computeLedMode());
#endif
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

      wifiTimeTick();
      if (g_timeAnchorAttempted || timeIsSynced())
        updateTime();
      updateBattery();
      batteryProtectionTick(now);
      saveManagerTick();
      maybePeriodicTimeSave();

#if LED_STATUS_ENABLED
      ledSetScreenOff(false);
      ledUpdatePetStatus(computeLedMode());
#endif

      return;
    }

    // ---------------------------------------------------------------------------
    // Pet tick (ALWAYS run even if Console is open)
    // ---------------------------------------------------------------------------
    if (!inDeathFlow)
    {
      if (isPetSleepingNow())
      {
        pet.petSleepTick();
        petResetUpdateTimers();
      }
      else
      {
        pet.update();
        uiMaybeShowLevelUpPopup();
      }

      if (pet.health <= 0 && petDeathEnabled && g_app.uiState != UIState::DEATH &&
          g_app.uiState != UIState::DEATH_TRANSITION && g_app.uiState != UIState::BURIAL_SCREEN)
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

    const bool sleepingNow2 = isPetSleepingNow();

    if (!s_prevSleeping && sleepingNow2)
      soundSleep();
    if (s_prevSleeping && !sleepingNow2)
      soundWake();
    s_prevSleeping = sleepingNow2;

    const bool inDeathTransition = (g_app.uiState == UIState::DEATH_TRANSITION);

    if (!inDeathTransition)
    {
      soundLowHealthTick((uint8_t)pet.health, sleepingNow2,
                         /*screenOn=*/isScreenOn(),
                         /*inDeathScreen=*/inDeathFlow);

      if (g_sdReady)
      {
        animTick();
      }

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
    updateBattery();
    batteryProtectionTick(now);
    saveManagerTick();
    maybePeriodicTimeSave();

#if LED_STATUS_ENABLED
    ledSetScreenOff(false);
    ledUpdatePetStatus(computeLedMode());
#endif
  }