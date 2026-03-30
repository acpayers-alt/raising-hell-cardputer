#include "ui_settings_pages.h"

#include "input.h"
#include "app_state.h"
#include "settings_flow_state.h"
#include "ui_defs.h"
#include "ui_runtime.h"
#include "sound.h"
#include "sdcard.h"
#include "save_manager.h"
#include "flow_controls_help.h"
#include "menu_actions.h"
#include "ui_input_common.h"
#include "graphics.h"
#include "ui_input_utils.h"
#include "flow_console.h"

namespace UiSettingsPages {

void Handle_TOP(InputState& input, int move) {
  if (move != 0) {
    const int totalItems = 8;
    g_app.settingsIndex += move;
    if (g_app.settingsIndex < 0) g_app.settingsIndex = totalItems - 1;
    if (g_app.settingsIndex > totalItems - 1) g_app.settingsIndex = 0;

    requestUIRedraw();
    playBeep();
    return;
  }

  if (g_app.settingsIndex == 0) {
    if (input.leftOnce || input.rightOnce) {
      soundAdjustVolume(input.leftOnce ? -1 : 1);
      saveSettingsToSD();
      saveManagerMarkDirty();
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }
  }

  if (uiIsSelect(input)) {
    switch (g_app.settingsIndex) {
      case 0: { // Volume cycles
        soundAdjustVolume(+1);
        saveSettingsToSD();
        saveManagerMarkDirty();
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 1: { // Controls
        openControlsHelpFromSettings();
        playBeep();
        clearInputLatch();
        return;
      }

      case 2: { // Screen Settings
        g_settingsFlow.settingsPage = SettingsPage::SCREEN;
        g_app.screenSettingsIndex    = 0;
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 3: { // System
        g_settingsFlow.settingsPage = SettingsPage::SYSTEM;
        g_app.systemSettingsIndex    = 0;
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 4: { // Game
        g_settingsFlow.settingsPage = SettingsPage::GAME;
        g_app.gameOptionsIndex       = 0;
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 5: { // Console
        openConsoleWithReturn(UIState::SETTINGS, g_app.currentTab, true, g_settingsFlow.settingsPage);
        uiDrainKb(input);
        clearInputLatch();
        requestUIRedraw();
        playBeep();
        return;
      }

      case 6: { // System Status
        g_settingsFlow.settingsPage = SettingsPage::STATUS;
        g_app.statusScreenIndex = 0;
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 7: { // Credits
        g_settingsFlow.settingsPage = SettingsPage::CREDITS;
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      default:
        break;
    }
  }
}

} // namespace UiSettingsPages