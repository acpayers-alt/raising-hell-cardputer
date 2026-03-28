#include "flow_boot_wizard.h"

#include "flow_boot_wifi.h"
#include "launcher_wifi_import.h"
#include "ui_actions.h"
#include "wifi_store.h"
#include "wifi_time.h"
#include "ui_state_wifi_connect_wait.h"

UIState g_bootWizardAfterOkState = UIState::BOOT;
Tab g_bootWizardAfterOkTab = Tab::TAB_PET;

void bootWizardBegin(UIState afterOkState, Tab afterOkTab)
{
  g_bootWizardAfterOkState = afterOkState;
  g_bootWizardAfterOkTab = afterOkTab;

  Serial.println("[BOOTWIZ] bootWizardBegin entered");

  String importedSsid;
  String importedPwd;

  const bool imported = launcherImportWifiCreds(importedSsid, importedPwd);
  Serial.printf("[BOOTWIZ] launcherImportWifiCreds=%d ssid='%s'\n", imported ? 1 : 0, importedSsid.c_str());

  // Try stored creds first.
  {
    String storedSsid, storedPwd;
    if (wifiStoreHasCreds() && wifiStoreLoad(storedSsid, storedPwd) && storedSsid.length() > 0)
    {
      Serial.printf("[BOOTWIZ] entering BOOT_WIFI_WAIT from stored creds ssid='%s'\n", storedSsid.c_str());
      wifiConsoleBeginConnect(storedSsid.c_str(), storedPwd.c_str());
      uiActionEnterState(UIState::BOOT_WIFI_WAIT, Tab::TAB_PET, true);
      return;
    }
  }

  // If no stored creds, try launcher import once.
  {
    String importedSsid, importedPwd;
    const bool imported = launcherImportWifiCreds(importedSsid, importedPwd);
    Serial.printf("[BOOTWIZ] launcherImportWifiCreds=%d ssid='%s'\n", imported ? 1 : 0, importedSsid.c_str());

    if (imported)
    {
      Serial.println("[BOOTWIZ] importing launcher wifi and entering BOOT_WIFI_IMPORTED");
      wifiResetConnectUiState();
      wifiConsoleBeginConnect(importedSsid.c_str(), importedPwd.c_str());
      bootWifiSetImportedInfo(importedSsid.c_str());
      uiActionEnterState(UIState::BOOT_WIFI_IMPORTED, Tab::TAB_PET, true);
      return;
    }
  }

  Serial.println("[BOOTWIZ] entering BOOT_WIFI_PROMPT");
  uiActionEnterState(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET, true);
}