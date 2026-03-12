#include "ui_settings_pages.h"

#include "input.h"
#include "app_state.h"
#include "settings_flow_state.h"
#include "ui_defs.h"
#include "ui_runtime.h"
#include "sound.h"
#include "sdcard.h"
#include "save_manager.h"

#include "wifi_power.h"
#include "wifi_store.h"
#include "wifi_setup_state.h"
#include "wifi_time.h"
#include "menu_actions.h"
#include "ui_input_common.h"
#include "graphics.h"
#include "ui_input_utils.h"
#include "ui_settings_actions.h"
#include "asset_ota.h"

namespace UiSettingsPages {

void Handle_WIFI(InputState& input, int move) {
  const int totalItems = 7;

  if (move != 0) {
    g_wifi.wifiSettingsIndex += move;
    if (g_wifi.wifiSettingsIndex < 0) g_wifi.wifiSettingsIndex = totalItems - 1;
    if (g_wifi.wifiSettingsIndex > totalItems - 1) g_wifi.wifiSettingsIndex = 0;

    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  // Timezone cycling on the TZ row (row 3)
  if (g_wifi.wifiSettingsIndex == 3 && (input.leftOnce || input.rightOnce)) {
    settingsCycleTimeZone(input.leftOnce ? -1 : 1);
    // settingsCycleTimeZone() already redraws + beeps + clears latch
    return;
  }

  // Asset OTA auto-check toggle on row 5
  if (g_wifi.wifiSettingsIndex == 5 && (input.leftOnce || input.rightOnce)) {
    const bool enabled = assetOtaGetConfig().autoCheckEnabled != 0;
    assetOtaSetAutoCheckEnabled(!enabled);
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  // Asset OTA channel cycle on row 6
  if (g_wifi.wifiSettingsIndex == 6 && (input.leftOnce || input.rightOnce)) {
    const AssetOtaChannel cur = (AssetOtaChannel)assetOtaGetConfig().channel;
    const AssetOtaChannel next =
        (cur == AssetOtaChannel::DEV) ? AssetOtaChannel::PUBLIC : AssetOtaChannel::DEV;
    assetOtaSetChannel(next);
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  if (uiIsSelect(input)) {
    switch (g_wifi.wifiSettingsIndex) {
      case 0: { // WiFi ON/OFF toggle
        const bool en = !wifiIsEnabled();

        wifiSetEnabled(en);
        settingsSetWifiEnabled(en);
        applyWifiPower(en);

        saveSettingsToSD();
        saveManagerMarkDirty();

        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 1: { // Set WiFi Network (SSID/PASS entry)
        g_wifiSetupFromBootWizard = false;

        g_wifi.setupStage = 0;
        g_wifi.buf[0]     = '\0';
        g_wifi.ssid[0]    = '\0';
        g_wifi.pass[0]    = '\0';

        g_app.uiState = UIState::WIFI_SETUP;
        requestUIRedraw();

        inputSetTextCapture(true);
        g_textCaptureMode = true;

        uiDrainKb(input);
        clearInputLatch();
        playBeep();
        return;
      }

      case 2: { // Reset WiFi Settings
        wifiResetSettings();
        wifiStoreClear();

        ui_showMessage("WiFi reset");
        requestUIRedraw();

        playBeep();
        clearInputLatch();
        return;
      }

      case 3: { // Time zone row: cycle forward on select
        settingsCycleTimeZone(+1);
        // settingsCycleTimeZone() already redraws + beeps + clears latch
        return;
      }

      case 4: { // Check Asset OTA
        String msg;
        const bool ok = assetOtaCheckNow(&msg);
        ui_showMessage(msg.length() ? msg.c_str() : (ok ? "Asset OTA done" : "Asset OTA failed"));
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 5: { // Asset OTA Auto Check
        const bool enabled = assetOtaGetConfig().autoCheckEnabled != 0;
        assetOtaSetAutoCheckEnabled(!enabled);
        requestUIRedraw();
        playBeep();
        clearInputLatch();
        return;
      }

      case 6: { // Asset OTA Channel
        const AssetOtaChannel cur = (AssetOtaChannel)assetOtaGetConfig().channel;
        const AssetOtaChannel next =
            (cur == AssetOtaChannel::DEV) ? AssetOtaChannel::PUBLIC : AssetOtaChannel::DEV;
        assetOtaSetChannel(next);
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