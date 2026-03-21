#include "ui_state_settings.h"

// Needs the full InputState definition
#include "input.h"

// App + settings flow globals/types
#include "app_state.h"
#include "settings_flow_state.h"
#include "ui_defs.h"

// UI + audio
#include "ui_runtime.h"
#include "sound.h"

// Saving
#include "sdcard.h"
#include "save_manager.h"

// Screen/brightness/auto screen
#include "display.h"
#include "auto_screen.h"
#include "brightness_state.h"

// Console + flows invoked from settings
#include "console.h"
#include "flow_controls_help.h"
#include "flow_factory_reset.h"
#include "flow_time_editor.h"

// WiFi controls used from settings
#include "wifi_power.h"
#include "wifi_store.h"
#include "wifi_setup_state.h"

// Misc toggles used in settings pages
#include "led_status.h"
#include "game_options_state.h"

#include <stdint.h>

#include "settings_nav_state.h"
#include "time_persist.h"
#include "ui_input_utils.h"
#include "menu_actions.h"
#include "wifi_time.h"      // wifiIsEnabled, wifiSetEnabled
#include "graphics.h"       // ui_showMessage
#include "ui_settings_pages.h"
#include "ui_settings_menu.h"
#include "ui_input_common.h"
#include "ui_actions.h"

void resetSettingsNav(bool resetTopIndex);

void openSettingsWithReturn(UIState returnState, Tab returnTab, SettingsPage startPage)
{
  g_settingsFlow.settingsReturnState = returnState;
  g_settingsFlow.settingsReturnTab = returnTab;
  g_settingsFlow.settingsReturnValid = true;
  g_settingsFlow.settingsPage = startPage;

  uiActionEnterState(UIState::SETTINGS, returnTab, true);
  requestUIRedraw();
}

void uiSettingsHandle(InputState& input)
{
  if (g_settingsFlow.settingsPage == SettingsPage::SYSTEM)
  {
    if (factoryResetSystemSettingsHook(input, g_app.systemSettingsIndex))
      return;
  }

  auto backToReturnPage = [&]() -> bool {
    if (g_settingsFlow.settingsPage == SettingsPage::DECAY_MODE ||
        g_settingsFlow.settingsPage == SettingsPage::AUTO_SCREEN) {
      g_settingsFlow.settingsPage = g_settingsFlow.settingsReturnPage;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }
    return false;
  };

  auto backToTopLevel = [&]() -> bool {
    if (g_settingsFlow.settingsPage != SettingsPage::TOP) {
      g_settingsFlow.settingsPage = SettingsPage::TOP;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }
    return false;
  };

  auto exitSettings = [&]() {
    resetSettingsNav(true);

    UIState retState = UIState::PET_SCREEN;
    Tab     retTab   = Tab::TAB_PET;

    if (g_settingsFlow.settingsReturnValid) {
      retState = g_settingsFlow.settingsReturnState;
      retTab   = g_settingsFlow.settingsReturnTab;
    }

    g_settingsFlow.settingsReturnValid = false;

    uiActionEnterState(retState, retTab, true);
    playBeep();
    clearInputLatch();
  };

  if (input.menuOnce || input.escOnce) {
    if (backToReturnPage())
      return;

    if (backToTopLevel())
      return;

    exitSettings();
    return;
  }

  int move = input.encoderDelta;
  if (input.upOnce) move = -1;
  if (input.downOnce) move = 1;

  if (UiSettingsMenu::Handle(input, move)) {
    return;
  }

  if (g_settingsFlow.settingsPage == SettingsPage::CREDITS) {
    return;
  }
}