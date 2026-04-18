#include "controls_help_state.h"

// --- Core app / state ---------------------------------------------------------
#include "app_state.h"
#include "display_state.h"
#include "ui_menu_state.h"

// --- Flow / navigation --------------------------------------------------------
#include "flow_boot_wizard.h"
#include "return_target.h"
#include "ui_actions.h"

// --- New pet flow -------------------------------------------------------------
#include "new_pet_flow_state.h"

// --- Input / rendering --------------------------------------------------------
#include "graphics.h"
#include "input.h"

// --- Persistence / storage ----------------------------------------------------
#include "save_manager.h"
#include "sdcard.h"

// --- End of includes

extern UIState g_bootWizardAfterOkState;
extern Tab g_bootWizardAfterOkTab;

uint8_t g_controlsHelpSeen = 0;

static uint32_t s_controlsHelpEnterMs = 0;

void controlsHelpOnEnter()
{
  s_controlsHelpEnterMs = millis();
  controlsHelpResetScroll();
  inputForceClear();
  clearInputLatch();
}

bool controlsHelpDismissAllowed() { return (uint32_t)(millis() - s_controlsHelpEnterMs) >= 250; }

void controlsHelpBegin(UIState returnState, Tab returnTab)
{
  uiPushReturnTarget(returnState, returnTab);

  uiActionEnterState(UIState::CONTROLS_HELP, returnTab, true);
  requestFullUIRedraw();
}

void controlsHelpDismiss()
{
  if (!g_controlsHelpSeen)
  {
    if (sdReady())
    {
      g_controlsHelpSeen = 1;
      saveSettingsToSD();
    }
  }

  inputForceClear();
  clearInputLatch();

  const UIReturnTarget ret = uiGetReturnTarget();
  uiPopReturnTarget();

  if (ret.state == UIState::BOOT_WIFI_PROMPT)
  {
    bootWizardBegin(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
    requestFullUIRedraw();
    return;
  }

  if (ret.state == UIState::CHOOSE_PET)
  {
    g_choosePetInputUnlockMs = millis() + 350;
    g_choosePetBlockHatchUntilRelease = true;
  }

  uiActionEnterState(ret.state, ret.tab, true);
  requestFullUIRedraw();
}