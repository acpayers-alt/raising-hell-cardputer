#include "ui_input_utils.h"
#include "ui_settings_pages.h"

#include "app_state.h"
#include "input.h"
#include "settings_flow_state.h"
#include "settings_state.h"
#include "sound.h"
#include "save_manager.h"
#include "timezone.h"
#include "ui_defs.h"
#include "ui_runtime.h"

#include "flow_factory_reset.h"
#include "flow_time_editor.h"
#include "wifi_setup_state.h"

namespace UiSettingsPages
{

void Handle_SYSTEM(InputState &input, int move)
{
  // Factory Reset confirm/hold UI is owned by flow_factory_reset.cpp.
  // IMPORTANT: This must run BEFORE normal navigation (move handling),
  // otherwise left/right/up/down will be consumed as menu movement and
  // never reach the confirm overlay.
  if (factoryResetSystemSettingsHook(input, g_app.systemSettingsIndex))
  {
    return;
  }

  if (move != 0)
  {
    const int totalItems = 4;
    g_app.systemSettingsIndex += move;
    if (g_app.systemSettingsIndex < 0)
      g_app.systemSettingsIndex = totalItems - 1;
    if (g_app.systemSettingsIndex > totalItems - 1)
      g_app.systemSettingsIndex = 0;

    requestUIRedraw();
    playBeep();
    return;
  }

  if (g_app.systemSettingsIndex == 1 && (input.leftOnce || input.rightOnce))
  {
    const int dir = input.leftOnce ? -1 : +1;
    int next = (int)tzIndex + dir;

    if (next >= (int)tzCount())
      next = 0;
    if (next < 0)
      next = (int)tzCount() - 1;

    tzIndex = next;
    applyTimezoneIndex((uint8_t)tzIndex);
    saveTzIndexToNVS((uint8_t)tzIndex);
    saveSettingsToSD();
    saveManagerMarkDirty();
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }
  
  if (uiIsSelect(input))
  {
    switch (g_app.systemSettingsIndex)
    {
    case 0:
      beginSetTimeEditorFromSettings(SettingsPage::SYSTEM, UIState::SETTINGS, g_app.currentTab);
      playBeep();
      return;

      case 1:
      {
        int next = (int)tzIndex + 1;
        if (next >= (int)tzCount())
          next = 0;
        if (next < 0)
          next = 0;

        tzIndex = next;
        applyTimezoneIndex((uint8_t)tzIndex);
        saveTzIndexToNVS((uint8_t)tzIndex);
        saveSettingsToSD();
        saveManagerMarkDirty();
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

    case 3:
      g_settingsFlow.settingsPage = SettingsPage::WIFI;
      g_wifi.wifiSettingsIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;

    default:
      break;
    }
  }
}

} // namespace UiSettingsPages