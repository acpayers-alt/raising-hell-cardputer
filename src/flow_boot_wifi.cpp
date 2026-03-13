#include "flow_boot_wifi.h"

#include "app_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_input_common.h"   // uiDrainKb()

#include <FS.h>
#include <SD.h>
#include "sdcard.h"

#include "wifi_time.h"
#include "flow_time_editor.h"
#include "new_pet_flow_state.h"

#include "ui_boot_wizard_menu.h"
#include "boot_pipeline.h"
#include "time_state.h"
#include "time_persist.h"

// These are defined in flow_boot_wizard.cpp
extern UIState g_bootWizardAfterOkState;
extern Tab     g_bootWizardAfterOkTab;

// -----------------------------------------------------------------------------
// Launcher Import
// -----------------------------------------------------------------------------
static bool s_bootWifiImported = false;
static char s_bootWifiImportedSsid[33] = {0};
static uint32_t s_bootWifiImportedAtMs = 0;

void bootWifiSetImportedInfo(const char *ssid)
{
  s_bootWifiImported = true;
  s_bootWifiImportedAtMs = millis();

  if (ssid && ssid[0])
  {
    strlcpy(s_bootWifiImportedSsid, ssid, sizeof(s_bootWifiImportedSsid));
  }
  else
  {
    s_bootWifiImportedSsid[0] = 0;
  }
}

void bootWifiClearImportedInfo()
{
  s_bootWifiImported = false;
  s_bootWifiImportedSsid[0] = 0;
  s_bootWifiImportedAtMs = 0;
}

const char *bootWifiImportedSsid()
{
  return s_bootWifiImportedSsid;
}

void uiBootWifiImportedHandle(InputState &in)
{
  const uint32_t kImportedShowMs = 1200;

  const bool continueNow =
      in.selectOnce ||
      in.upOnce ||
      (s_bootWifiImportedAtMs != 0 &&
       (millis() - s_bootWifiImportedAtMs) >= kImportedShowMs);

  if (continueNow)
  {
    uiActionEnterState(UIState::BOOT_WIFI_WAIT, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  uiActionSwallowAll(in);
}

// -----------------------------------------------------------------------------
// BOOT_WIFI_PROMPT
// -----------------------------------------------------------------------------
void uiBootWifiPromptHandle(InputState& in)
{
  (void)UiBootWizardMenu::HandleWifiPrompt(in);
}

// -----------------------------------------------------------------------------
// BOOT_WIFI_WAIT
// After WiFi setup returns, wait for connection then advance.
// ESC always skips to manual time.
// -----------------------------------------------------------------------------
void uiBootWifiWaitHandle(InputState& in)
{
  if (!g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    beginForcedSetTimeBootGate(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
    requestUIRedraw();
    return;
  }

  if (g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  if (wifiIsConnected())
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();

    // Mandatory asset provisioning does not need TZ/NTP flow if time is already valid.
    // Return to BOOT so provisioning can resume immediately.
    if (g_bootAssetProvisionMustComplete && timeIsSynced())    {
      uiActionEnterState(UIState::BOOT, g_bootWizardAfterOkTab, true);
      requestFullUIRedraw();
      requestUIRedraw();
      return;
    }

    // Normal boot wizard behavior
    uiActionEnterState(UIState::BOOT_TZ_PICK, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    return;
  }

  uiActionSwallowAll(in);
}

// -----------------------------------------------------------------------------
// BOOT_TZ_PICK
// -----------------------------------------------------------------------------
void uiBootTzPickHandle(InputState& in)
{
  (void)UiBootWizardMenu::HandleTimezonePick(in);
}

// -----------------------------------------------------------------------------
// BOOT_NTP_WAIT
// Wait for time sync; ESC skips to manual. Once synced, continue.
// -----------------------------------------------------------------------------
void uiBootNtpWaitHandle(InputState& in)
{
  if (!g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    beginForcedSetTimeBootGate(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
    requestUIRedraw();
    return;
  }

  if (g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  if (timeIsSynced())
  {
    UIState nextState = g_bootWizardAfterOkState;
  
    if (g_sdReady &&
        (SD.exists("/raising_hell/save/save.bin") ||
         SD.exists("raising_hell/save/save.bin")))
    {
      nextState = UIState::PET_SCREEN;
    }
  
    if (nextState == UIState::CHOOSE_PET)
    {
      g_choosePetBlockHatchUntilRelease = true;
    }
  
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
  
    uiActionEnterState(nextState, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    return;
  }
  
  uiActionSwallowAll(in);
}