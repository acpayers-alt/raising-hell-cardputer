#include "flow_boot_wizard.h"

#include "flow_boot_wifi.h"
#include "graphics.h"
#include "launcher_wifi_import.h"
#include "ui_actions.h"
#include "ui_invalidate.h"
#include "ui_runtime.h"
#include "ui_state_wifi_connect_wait.h"
#include "wifi_store.h"
#include "wifi_time.h"

extern UIState g_bootWizardAfterOkState;
extern Tab g_bootWizardAfterOkTab;

void bootWizardBegin(UIState afterOkState, Tab afterOkTab)
{
  g_bootWizardAfterOkState = afterOkState;
  g_bootWizardAfterOkTab = afterOkTab;

  Serial.println("[BOOTWIZ] bootWizardBegin entered");

  ui_setBootSplashActive(false);
  requestFullUIRedraw();

  // Try stored Wi-Fi profiles first.
  if (wifiStoreHasCreds())
  {
    bootWifiBeginStoredProfileFailover();

    if (bootWifiBeginStoredProfileConnect(0))
    {
      Serial.println("[BOOTWIZ] entering BOOT_WIFI_WAIT from stored wifi profile");
      return;
    }

    bootWifiClearStoredProfileFailover();
  }

  // If no stored creds, try launcher import once.
  {
    String importedSsid, importedPwd;
    const bool imported = launcherImportWifiCreds(importedSsid, importedPwd);
    Serial.printf("[BOOTWIZ] launcherImportWifiCreds=%d ssid='%s'\n", imported ? 1 : 0, importedSsid.c_str());

    if (imported)
    {
      ui_showMessage("Importing WiFi...");
      requestUIRedraw();
      renderUI();

      if (!launcherWifiSsidVisible(importedSsid.c_str()))
      {
        Serial.printf("[BOOTWIZ] imported SSID not visible; falling back to manual scan ssid='%s'\n",
                      importedSsid.c_str());
      }
      else
      {
        Serial.println("[BOOTWIZ] importing launcher wifi and entering BOOT_WIFI_IMPORTED");
        wifiResetConnectUiState();
        wifiConsoleBeginConnect(importedSsid.c_str(), importedPwd.c_str());
        bootWifiSetImportedInfo(importedSsid.c_str());
        uiActionEnterState(UIState::BOOT_WIFI_IMPORTED, Tab::TAB_PET, true);
        return;
      }
    }
  }

  Serial.println("[BOOTWIZ] entering BOOT_WIFI_PROMPT");
  uiActionEnterState(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET, true);
}