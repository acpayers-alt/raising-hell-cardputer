#include "ui_settings_pages.h"

#include "app_state.h"
#include "flow_console.h"
#include "flow_controls_help.h"
#include "graphics.h"
#include "input.h"
#include "menu_actions.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "settings_nav_state.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_defs.h"
#include "ui_input_common.h"
#include "ui_input_utils.h"
#include "ui_runtime.h"

namespace UiSettingsPages
{

void Handle_TOP(InputState &input, int move)
{
  if (move != 0)
  {
    const int totalItems = 11;
    g_app.settingsIndex += move;
    if (g_app.settingsIndex < 0)
      g_app.settingsIndex = totalItems - 1;
    if (g_app.settingsIndex > totalItems - 1)
      g_app.settingsIndex = 0;

    requestUIRedraw();
    playBeep();
    return;
  }

  // Volume row is index 1.
  if (g_app.settingsIndex == 1)
  {
    if (input.leftOnce || input.rightOnce)
    {
      soundAdjustVolume(input.leftOnce ? -1 : 1);
      saveSettingsToSD();
      saveManagerMarkDirty();
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }
  }

  if (uiIsSelect(input))
  {
    switch (g_app.settingsIndex)
    {
    case 0:
    { // Controls
      openControlsHelpFromSettings();
      playBeep();
      clearInputLatch();
      return;
    }

    case 1:
    { // Volume cycles
      soundAdjustVolume(+1);
      saveSettingsToSD();
      saveManagerMarkDirty();
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    case 2:
    { // Pet Options
      g_settingsFlow.settingsPage = SettingsPage::PET;
      g_app.petSettingsIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    case 3:
    { // Screen
      g_settingsFlow.settingsPage = SettingsPage::SCREEN;
      g_app.screenSettingsIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    case 4:
    { // System
      g_settingsFlow.settingsPage = SettingsPage::SYSTEM;
      g_app.systemSettingsIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    case 5:
    { // Game
      g_settingsFlow.settingsPage = SettingsPage::GAME;
      g_app.gameOptionsIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    case 6:
    { // Console
      openConsoleWithReturn(UIState::SETTINGS, g_app.currentTab, true, g_settingsFlow.settingsPage);
      uiDrainKb(input);
      clearInputLatch();
      requestUIRedraw();
      playBeep();
      return;
    }

    case 7:
    { // System Status
      g_settingsFlow.settingsPage = SettingsPage::STATUS;
      g_app.statusScreenIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    case 8:
    { // Credits
      g_settingsFlow.settingsPage = SettingsPage::CREDITS;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

    case 9:
    { // Store Pet
      char parkedPath[128];
      if (!saveManagerExportCurrentBubJson(parkedPath, sizeof(parkedPath)))
      {
        ui_showMessage("Store failed");
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      resetSettingsNav(true);
      g_settingsFlow.settingsPage = SettingsPage::TOP;
      clearSettingsReturnTarget();

      playBeep();
      uiActionEnterStateClean(UIState::TITLE_MENU, Tab::TAB_PET, true, input, 120);
      return;
    }

    case 10:
    { // Main Menu
      resetSettingsNav(true);
      g_settingsFlow.settingsPage = SettingsPage::TOP;
      clearSettingsReturnTarget();

      playBeep();
      uiActionEnterStateClean(UIState::TITLE_MENU, Tab::TAB_PET, true, input, 120);
      return;
    }

    default:
      break;
    }
  }
}

} // namespace UiSettingsPages