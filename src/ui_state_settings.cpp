#include "ui_state_settings.h"

// Needs the full InputState definition
#include "input.h"

// App + settings flow globals/types
#include "app_state.h"
#include "settings_flow_state.h"
#include "ui_defs.h"

// UI + audio
#include "sound.h"
#include "ui_runtime.h"

// Saving
#include "save_manager.h"
#include "sdcard.h"

// Screen/brightness/auto screen
#include "auto_screen.h"
#include "brightness_state.h"
#include "display.h"

// Console + flows invoked from settings
#include "console.h"
#include "flow_controls_help.h"
#include "flow_factory_reset.h"
#include "flow_time_editor.h"

// WiFi controls used from settings
#include "wifi_power.h"
#include "wifi_setup_state.h"
#include "wifi_store.h"

// Misc toggles used in settings pages
#include "game_options_state.h"
#include "led_status.h"

#include <stdint.h>

#include "asset_ota.h"
#include "graphics.h" // ui_showMessage
#include "menu_actions.h"
#include "settings_nav_state.h"
#include "time_persist.h"
#include "ui_actions.h"
#include "ui_input_common.h"
#include "ui_input_utils.h"
#include "ui_settings_menu.h"
#include "ui_settings_pages.h"
#include "wifi_time.h" // wifiIsEnabled, wifiSetEnabled

void resetSettingsNav(bool resetTopIndex);

void uiSettingsHandle(InputState &input)
{
  if (g_settingsFlow.settingsPage == SettingsPage::SYSTEM)
  {
    if (factoryResetSystemSettingsHook(input, g_app.systemSettingsIndex))
      return;
  }

  auto backToReturnPage = [&]() -> bool
  {
    if (g_settingsFlow.settingsPage == SettingsPage::DECAY_MODE ||
        g_settingsFlow.settingsPage == SettingsPage::AUTO_SCREEN)
    {
      g_settingsFlow.settingsPage = g_settingsFlow.settingsReturnPage;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }
    return false;
  };

  auto backToTopLevel = [&]() -> bool
  {
    if (g_settingsFlow.settingsPage != SettingsPage::TOP)
    {
      g_settingsFlow.settingsPage = SettingsPage::TOP;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }
    return false;
  };

  auto exitSettings = [&]()
  {
    resetSettingsNav(true);
    playBeep();
    closeSettingsAndReturn(input);
    clearInputLatch();
  };

  int move = input.encoderDelta;
  if (input.upOnce)
    move = -1;
  if (input.downOnce)
    move = 1;

  if (g_settingsFlow.settingsPage == SettingsPage::STATUS)
  {
    if (input.menuOnce || input.escOnce)
    {
      g_settingsFlow.settingsPage = SettingsPage::TOP;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    UiSettingsPages::Handle_STATUS(input, move);
    return;
  }

  if (assetOtaConfirmActive())
  {
    if (UiSettingsMenu::Handle(input, move))
    {
      return;
    }
  }

  if (input.menuOnce || input.escOnce)
  {
    if (backToReturnPage())
      return;

    if (backToTopLevel())
      return;

    exitSettings();
    return;
  }

  if (g_settingsFlow.settingsPage == SettingsPage::GAME &&
    UiSettingsPages::GameNewPetConfirmActive())
{
  UiSettingsPages::Handle_GAME(input, move);
  return;
}
  if (UiSettingsMenu::Handle(input, move))
  {
    return;
  }

  if (g_settingsFlow.settingsPage == SettingsPage::CREDITS)
  {
    return;
  }
}