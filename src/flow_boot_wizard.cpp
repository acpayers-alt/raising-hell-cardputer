#include "flow_boot_wizard.h"

#include "ui_actions.h"
#include "launcher_wifi_import.h"
#include "wifi_store.h"
#include "flow_boot_wifi.h"
#include "wifi_time.h"

UIState g_bootWizardAfterOkState = UIState::BOOT;
Tab     g_bootWizardAfterOkTab   = Tab::TAB_PET;

void bootWizardBegin(UIState afterOkState, Tab afterOkTab)
{
  g_bootWizardAfterOkState = afterOkState;
  g_bootWizardAfterOkTab = afterOkTab;

  Serial.println("[BOOTWIZ] bootWizardBegin entered");

  String importedSsid;
  String importedPwd;

  const bool imported = launcherImportWifiCreds(importedSsid, importedPwd);
  Serial.printf("[BOOTWIZ] launcherImportWifiCreds=%d ssid='%s'\n",
                imported ? 1 : 0,
                importedSsid.c_str());

  if (imported)
  {
    Serial.println("[BOOTWIZ] importing launcher wifi and entering BOOT_WIFI_IMPORTED");

    wifiStoreSave(importedSsid, importedPwd);
    bootWifiSetImportedInfo(importedSsid.c_str());
    wifiConsoleBeginConnect(importedSsid.c_str(), importedPwd.c_str());

    uiActionEnterState(UIState::BOOT_WIFI_IMPORTED, Tab::TAB_PET, true);
    return;
  }

  const bool hasStored = wifiStoreHasCreds();
  Serial.printf("[BOOTWIZ] wifiStoreHasCreds=%d\n", hasStored ? 1 : 0);

  if (hasStored)
  {
    String ssid;
    String pwd;
    if (wifiStoreLoad(ssid, pwd))
    {
      Serial.printf("[BOOTWIZ] entering BOOT_WIFI_WAIT from stored creds ssid='%s'\n", ssid.c_str());
      wifiConsoleBeginConnect(ssid.c_str(), pwd.c_str());
      uiActionEnterState(UIState::BOOT_WIFI_WAIT, Tab::TAB_PET, true);
      return;
    }
  }

  Serial.println("[BOOTWIZ] entering BOOT_WIFI_PROMPT");
  uiActionEnterState(UIState::BOOT_WIFI_PROMPT, Tab::TAB_PET, true);
}