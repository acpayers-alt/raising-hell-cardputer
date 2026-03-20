#include "ui_boot_wizard_menu.h"

#include "app_state.h"
#include "boot_pipeline.h"
#include "flow_time_editor.h"
#include "input.h"
#include "timezone.h"
#include "ui_actions.h"
#include "ui_input_common.h" // uiDrainKb()
#include "ui_runtime.h"
#include "wifi_time.h"

// Boot-wizard WiFi setup needs the boot flag + the legacy aliases for fields
#include "wifi_setup_state.h" // g_wifiSetupFromBootWizard, wifiSetupStage/Buf/Ssid/Pass

// These are defined in flow_boot_wizard.cpp
extern UIState g_bootWizardAfterOkState;
extern Tab g_bootWizardAfterOkTab;

// -----------------------------------------------------------------------------
// Local wizard choice state
// -----------------------------------------------------------------------------
static uint8_t s_wifiPromptChoice = 0; // 0 = WiFi/NTP path, 1 = manual time

void UiBootWizardMenu::ResetWifiPromptChoice() { s_wifiPromptChoice = 0; }

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static void bootWizardSkipToManualTime()
{
  beginForcedSetTimeBootGate(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
  requestUIRedraw();
}

static void bootWizardEnterWifiSetup()
{
  // Critical: tell WIFI_SETUP state we're in boot-wizard flow so it returns to
  // BOOT_WIFI_WAIT after password (instead of SETTINGS).
  g_wifiSetupFromBootWizard = true;

  // Reset WiFi setup state so the wizard starts clean.
  wifiSetupStage = WIFI_SETUP_STAGE_SCAN;
  wifiSetupSsid[0] = 0;
  wifiSetupPass[0] = 0;
  wifiSetupBuf[0] = 0;

  g_wifi.scanStarted = false;
  g_wifi.scanInProgress = false;
  g_wifi.scanCount = 0;
  g_wifi.scanIndex = 0;

  g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
  g_wifi.returnTab = g_bootWizardAfterOkTab;
  g_wifi.aborted = false;

  uiActionEnterState(UIState::WIFI_SETUP, g_bootWizardAfterOkTab, true);
  requestUIRedraw();
}

static void tzPrev()
{
  const uint8_t n = tzCount();
  if (n == 0)
    return;
  if (tzIndex <= 0)
    tzIndex = (int)n - 1;
  else
    tzIndex--;
}

static void tzNext()
{
  const uint8_t n = tzCount();
  if (n == 0)
    return;
  tzIndex++;
  if (tzIndex >= (int)n)
    tzIndex = 0;
}

static void tzCommit()
{
  applyTimezoneIndex((uint8_t)tzIndex);
  saveTzIndexToNVS((uint8_t)tzIndex);
}

// -----------------------------------------------------------------------------
// BOOT_WIFI_PROMPT
// -----------------------------------------------------------------------------
bool UiBootWizardMenu::HandleWifiPrompt(InputState &in)
{
  // For mandatory asset provisioning, do not allow skipping WiFi.
  if (!g_bootAssetProvisionMustComplete && in.escOnce)
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    bootWizardSkipToManualTime();
    return true;
  }

  // MENU inactive here
  if (in.menuOnce)
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return true;
  }

  // For mandatory asset provisioning, force the WiFi path.
  if (!g_bootAssetProvisionMustComplete && (in.upOnce || in.downOnce))
  {
    s_wifiPromptChoice = (s_wifiPromptChoice == 0) ? 1 : 0;
    requestUIRedraw();
    uiActionSwallowAll(in);
    return true;
  }

  if (in.selectOnce)
  {
    if (g_bootAssetProvisionMustComplete || s_wifiPromptChoice == 0)
      bootWizardEnterWifiSetup();
    else
      bootWizardSkipToManualTime();

    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return true;
  }

  uiActionSwallowAll(in);
  return true;
}

// -----------------------------------------------------------------------------
// BOOT_TZ_PICK
// -----------------------------------------------------------------------------
bool UiBootWizardMenu::HandleTimezonePick(InputState &in)
{
  if (!g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    bootWizardSkipToManualTime();
    return true;
  }

  if (g_bootAssetProvisionMustComplete && (in.escOnce || in.menuOnce))
  {
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return true;
  }

  if (in.upOnce)
  {
    tzPrev();
    requestUIRedraw();
    uiActionSwallowAll(in);
    return true;
  }

  if (in.downOnce)
  {
    tzNext();
    requestUIRedraw();
    uiActionSwallowAll(in);
    return true;
  }

  if (in.selectOnce)
  {
    tzCommit();
    uiActionEnterState(UIState::BOOT_NTP_WAIT, g_bootWizardAfterOkTab, true);
    requestUIRedraw();
    uiActionSwallowAll(in);
    uiDrainKb(in);
    clearInputLatch();
    return true;
  }

  uiActionSwallowAll(in);
  return true;
}