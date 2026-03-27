#include "app_loop.h"

#include "M5Cardputer.h"
#include "anim_engine.h"
#include "app_state.h"
#include "asset_ota.h"
#include "auto_screen.h"
#include "boot_pipeline.h"
#include "build_flags.h"
#include "console.h"
#include "death_state.h"
#include "debug_state.h"
#include "display.h"
#include "display_state.h"
#include "evolution_flow.h"
#include "flow_boot_wifi.h"
#include "flow_controls_help.h"
#include "game_options_state.h"
#include "graphics.h"
#include "hatching_flow.h"
#include "input.h"
#include "input_activity_state.h"
#include "launcher_wifi_import.h"
#include "led_status.h"
#include "menu_actions.h"
#include "motion.h"
#include "no_legacy_aliases.h"
#include "pet.h"
#include "power_button.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sleep_state.h"
#include "sound.h"
#include "time_persist.h"
#include "time_state.h"
#include "ui_actions.h"
#include "ui_input_router.h"
#include "ui_level_popup.h"
#include "ui_runtime.h"
#include "ui_state_console.h"
#include "ui_tabs.h"
#include "wifi_store.h"
#include "wifi_time.h"
#include "ui_actions.h"

#include <Arduino.h>
#include <cstring>

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

  const bool inDeathFlow = (g_app.uiState == UIState::DEATH) ||
                           (g_app.uiState == UIState::DEATH_TRANSITION) ||
                           (g_app.uiState == UIState::MINI_GAME) ||
                           (g_app.uiState == UIState::BURIAL_SCREEN);

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
    assetOtaTick();
    updateTime();
    updateBattery();
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

  // ---------------------------------------------------------------------------
  // MODAL: SD assets missing screen (all builds)
  // Draw ONCE to prevent flicker; retry check on ENTER.
  // ---------------------------------------------------------------------------
  if (g_assetsMissing && !g_bootAssetProvisionMustComplete)
  {
    static bool s_prevSelectHeld_assets = false;
    const bool enterOnce = (input.selectHeld && !s_prevSelectHeld_assets);
    s_prevSelectHeld_assets = input.selectHeld;

    static bool s_drawn = false;
    if (!s_drawn)
    {
      drawAssetsMissingScreen();
      s_drawn = true;
    }

    if (enterOnce || input.selectOnce)
    {
      // Re-check SD + marker file
      if (!g_sdReady)
      {
        g_sdReady = initSD();
      }

      // If SD comes back, allow the normal boot pipeline to proceed again.
      if (g_sdReady)
      {
        g_sdGaveUp = false;
        g_sdFirstTryMs = 0;
        g_sdTryCount = 0;
      }

      g_assetsMissing = !(g_sdReady && sdAssetsPresent());
      g_assetsChecked = true;

      if (!g_assetsMissing)
      {
        s_drawn = false;
        drawBootSplash();
        invalidateBackgroundCache();
        requestUIRedraw();
        renderUI();
      }
      else
      {
        s_drawn = false;
      }
    }

    soundTick();
    delay(10);
    return;
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
      assetOtaTick();
      if (g_timeAnchorAttempted || timeIsSynced())
        updateTime();
      updateBattery();
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
  if (hasUserActivity(input))
    noteUserActivity();

  autoScreenTick();

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

  // Don't allow ESC/C/Q/tab jumps to steal focus on New Pet flow screens
  if (g_app.uiState == UIState::CHOOSE_PET)
  {
    input.consoleOnce = false;
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

      // Don't allow ESC/C/Q/tab jumps to steal focus during Hatching/Evolution
      if (g_app.uiState == UIState::HATCHING || g_app.flow.evo.active || g_app.uiState == UIState::EVOLUTION)
      {
        input.tabJump = 255;
        input.consoleOnce = false;
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

    // C toggles console
    if (g_app.uiState != UIState::SET_TIME && input.consoleOnce)
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
        // Make '/' close behave the same as ESC
        closeConsoleAndReturn(input);
      }

      invalidateBackgroundCache();
      requestUIRedraw();
      input = InputState{};
      clearInputLatch();
      return;
    }
  }

  // ---------------------------------------------------------------------------
  // HOME KEY (Q): return to PET tab from anywhere reasonable
  // IMPORTANT: this is separate from MENU/ESC which are for opening/dismissing menus.
  // ---------------------------------------------------------------------------
  if (input.homeOnce)
  {
    const bool canHome = (g_app.uiState != UIState::SET_TIME) && (g_app.uiState != UIState::POWER_MENU) &&
                         (g_app.uiState != UIState::DEATH) && (g_app.uiState != UIState::BURIAL_SCREEN) &&
                         (g_app.uiState != UIState::MINI_GAME) && (g_app.uiState != UIState::HATCHING) &&
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
  saveManagerTick();
  maybePeriodicTimeSave();
  
  #if LED_STATUS_ENABLED
  ledSetScreenOff(false);
  ledUpdatePetStatus(computeLedMode());
  #endif
  }