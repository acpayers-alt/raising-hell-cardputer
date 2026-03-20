#include "ui_settings_pages.h"

#include "app_state.h"
#include "input.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_defs.h"
#include "ui_runtime.h"

#include "asset_ota.h"
#include "graphics.h"
#include "menu_actions.h"
#include "ui_input_common.h"
#include "ui_input_utils.h"
#include "ui_settings_actions.h"
#include "wifi_power.h"
#include "wifi_setup_state.h"
#include "wifi_store.h"
#include "wifi_time.h"

namespace UiSettingsPages
{

void Handle_WIFI(InputState &input, int move)
{
#if defined(BUILD_PUBLIC)
  const int totalItems = 6;
  const int otaChannelRow = -1;
#else
  const int totalItems = 7;
  const int otaChannelRow = 5;
#endif

  if (move != 0)
  {
    g_wifi.wifiSettingsIndex += move;
    if (g_wifi.wifiSettingsIndex < 0)
      g_wifi.wifiSettingsIndex = totalItems - 1;
    if (g_wifi.wifiSettingsIndex > totalItems - 1)
      g_wifi.wifiSettingsIndex = 0;

    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  // Timezone cycling on the TZ row (row 3)
  if (g_wifi.wifiSettingsIndex == 3 && (input.leftOnce || input.rightOnce))
  {
    settingsCycleTimeZone(input.leftOnce ? -1 : 1);
    return;
  }

#if !defined(PUBLIC_BUILD)
  // Asset OTA channel cycle on row 5
  if (g_wifi.wifiSettingsIndex == otaChannelRow && (input.leftOnce || input.rightOnce))
  {
    const AssetOtaChannel cur = (AssetOtaChannel)assetOtaGetConfig().channel;
    const AssetOtaChannel next = (cur == AssetOtaChannel::DEV) ? AssetOtaChannel::PUBLIC : AssetOtaChannel::DEV;
    assetOtaSetChannel(next);
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }
#endif

  if (uiIsSelect(input))
  {
    switch (g_wifi.wifiSettingsIndex)
    {
    case 0:
    { // WiFi ON/OFF toggle
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

    case 1:
    { // Set WiFi Network (SSID/PASS entry)
      g_wifiSetupFromBootWizard = false;

      g_wifi.setupStage = WIFI_SETUP_STAGE_SCAN;
      g_wifi.buf[0] = '\0';
      g_wifi.ssid[0] = '\0';
      g_wifi.pass[0] = '\0';

      g_wifi.scanStarted = false;
      g_wifi.scanInProgress = false;
      g_wifi.scanCount = 0;
      g_wifi.scanIndex = 0;
      g_wifi.returnState = UIState::SETTINGS;
      g_wifi.returnTab = g_app.currentTab;

      g_app.uiState = UIState::WIFI_SETUP;
      requestUIRedraw();

      inputSetTextCapture(true);
      g_textCaptureMode = true;

      uiDrainKb(input);
      clearInputLatch();
      playBeep();
      return;
    }

    case 2:
    { // Reset WiFi Settings
      wifiResetSettings();
      wifiStoreClear();

      ui_showMessage("WiFi reset");
      requestUIRedraw();

      playBeep();
      clearInputLatch();
      return;
    }

    case 3:
    { // Time zone row: cycle forward on select
      settingsCycleTimeZone(+1);
      return;
    }

    case 4:
    { // Check Asset OTA
      assetOtaSetConfirmActive(true);
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }

#if !defined(PUBLIC_BUILD)
    case 5:
    { // Asset OTA Channel
      const AssetOtaChannel cur = (AssetOtaChannel)assetOtaGetConfig().channel;
      const AssetOtaChannel next = (cur == AssetOtaChannel::DEV) ? AssetOtaChannel::PUBLIC : AssetOtaChannel::DEV;
      assetOtaSetChannel(next);
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return;
    }
#endif

    default:
      break;
    }
  }
}

} // namespace UiSettingsPages