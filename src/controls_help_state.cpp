#include "controls_help_state.h"

#include "app_state.h"     // uiState (extern) or accessors
#include "display_state.h" // uiNeedsRedraw (extern)
#include "flow_boot_wizard.h"
#include "graphics.h"
#include "input.h"
#include "return_target.h"
#include "save_manager.h"
#include "sdcard.h"
#include "ui_actions.h"
#include "ui_menu_state.h" // currentTab (extern) or accessors

uint8_t g_controlsHelpSeen = 0;

static uint32_t s_controlsHelpEnterMs = 0;
static ReturnTarget s_controlsHelpReturn{};

void controlsHelpOnEnter()
{
  s_controlsHelpEnterMs = millis();
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
    bootWizardBegin(UIState::CHOOSE_PET, s_controlsHelpReturn.tab);
    requestFullUIRedraw();
    return;
  }

  uiActionEnterState(s_controlsHelpReturn.state, s_controlsHelpReturn.tab, true);
  requestFullUIRedraw();
}