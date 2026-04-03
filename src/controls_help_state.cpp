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
#include "input.h"
#include "graphics.h"

// --- Persistence / storage ----------------------------------------------------
#include "save_manager.h"
#include "sdcard.h"

// --- End of includes

extern UIState g_bootWizardAfterOkState;
extern Tab g_bootWizardAfterOkTab;

uint8_t g_controlsHelpSeen = 0;

static uint32_t s_controlsHelpEnterMs = 0;
static ReturnTarget s_controlsHelpReturn{};

void controlsHelpOnEnter()
{
  s_controlsHelpEnterMs = millis();
  controlsHelpResetScroll();
  inputForceClear();
  clearInputLatch();
}

bool controlsHelpDismissAllowed()
{
  return (uint32_t)(millis() - s_controlsHelpEnterMs) >= 250;
}

void controlsHelpBegin(UIState returnState, Tab returnTab)
{
  s_controlsHelpReturn.state = returnState;
  s_controlsHelpReturn.tab = returnTab;

  uiActionEnterState(UIState::CONTROLS_HELP, returnTab, true);

  // Prevent the opening key from immediately dismissing help.
  controlsHelpOnEnter();

  requestFullUIRedraw();
}

void controlsHelpDismiss()
{
  if (!g_controlsHelpSeen)
  {
    // Only mark seen if we can persist it.
    if (sdReady())
    {
      g_controlsHelpSeen = 1;
      saveSettingsToSD();
    }
  }

  inputForceClear();
  clearInputLatch();

  // Special case: on first boot, Controls Help should continue into the
  // boot wizard entry function, not directly into a wizard state.
  if (s_controlsHelpReturn.state == UIState::BOOT_WIFI_PROMPT)
  {
    bootWizardBegin(g_bootWizardAfterOkState, g_bootWizardAfterOkTab);
    requestFullUIRedraw();
    return;
  }
  
  if (s_controlsHelpReturn.state == UIState::CHOOSE_PET)
{
  g_choosePetInputUnlockMs = millis() + 350;
  g_choosePetBlockHatchUntilRelease = true;
}

  uiActionEnterState(s_controlsHelpReturn.state, s_controlsHelpReturn.tab, true);
  requestFullUIRedraw();
}