#include "ui_settings_pages.h"
#include "ui_input_utils.h"

#include "input.h"
#include "app_state.h"
#include "settings_flow_state.h"
#include "ui_runtime.h"
#include "sound.h"
#include "sdcard.h"
#include "save_manager.h"
#include "motion.h"

#include "display.h"
#include "auto_screen.h"
#include "brightness_state.h"

namespace UiSettingsPages {

void Handle_SCREEN(InputState& input, int move) {
  if (move != 0) {
    const int totalItems = 3;
    g_app.screenSettingsIndex += move;
    if (g_app.screenSettingsIndex < 0) g_app.screenSettingsIndex = totalItems - 1;
    if (g_app.screenSettingsIndex > totalItems - 1) g_app.screenSettingsIndex = 0;

    requestUIRedraw();
    playBeep();
    return;
  }

  const bool leftPulse  = input.leftOnce;
  const bool rightPulse = input.rightOnce;

  if (g_app.screenSettingsIndex == 0 && (leftPulse || rightPulse)) {
    brightnessLevel += (rightPulse ? 1 : -1);
    if (brightnessLevel < 0) brightnessLevel = 2;
    if (brightnessLevel > 2) brightnessLevel = 0;

    setBacklight((uint16_t)brightnessValues[brightnessLevel]);

    saveSettingsToSD();
    saveManagerMarkDirty();
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  if (g_app.screenSettingsIndex == 1 && uiIsSelect(input)) {
    g_app.autoScreenIndex             = (int)autoScreenTimeoutSel;
    g_settingsFlow.settingsReturnPage = SettingsPage::SCREEN;
    g_settingsFlow.settingsPage       = SettingsPage::AUTO_SCREEN;
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }
  if (g_app.screenSettingsIndex == 2 && (leftPulse || rightPulse)) {
    int sel = (int)motionGetShakeSensitivity();
    sel += (rightPulse ? 1 : -1);
    if (sel < 0) sel = 3;
    if (sel > 3) sel = 0;
  
    motionSetShakeSensitivity((uint8_t)sel);
  
    saveSettingsToSD();
    saveManagerMarkDirty();
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }
}

} // namespace UiSettingsPages