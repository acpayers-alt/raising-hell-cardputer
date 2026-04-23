#include "ui_input_utils.h"
#include "ui_settings_pages.h"

#include "app_state.h"
#include "input.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_runtime.h"

#include "auto_screen.h"
#include "display.h"

namespace UiSettingsPages
{

void Handle_AUTO_SCREEN(InputState &input, int move)
{
  (void)move;

  const int kCount = 4;

  int mv = input.encoderDelta;
  if (input.upOnce)
    mv = -1;
  if (input.downOnce)
    mv = 1;

  if (mv != 0)
  {
    g_app.autoScreenIndex = (g_app.autoScreenIndex + (mv < 0 ? (kCount - 1) : 1)) % kCount;
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  if (uiIsSelect(input))
  {
    autoScreenTimeoutSel = (uint8_t)g_app.autoScreenIndex;
    autoScreenSetEnabled(autoScreenTimeoutSel != 3);
    screenWake();
    saveSettingsToSD();
    saveManagerMarkDirty();
    g_settingsFlow.settingsPage = g_settingsFlow.settingsReturnPage;
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  if (uiIsBack(input))
  {
    g_settingsFlow.settingsPage = g_settingsFlow.settingsReturnPage;
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }
}

} // namespace UiSettingsPages