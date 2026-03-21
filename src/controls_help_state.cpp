#include "controls_help_state.h"

#include "flow_boot_wizard.h"
#include "app_state.h"        // uiState (extern) or accessors
#include "ui_menu_state.h"    // currentTab (extern) or accessors
#include "display_state.h"    // uiNeedsRedraw (extern)
#include "graphics.h"
#include "input.h"
#include "save_manager.h"
#include "sdcard.h"   
#include "ui_actions.h"

uint8_t g_controlsHelpSeen = 0;

static UIState s_controlsHelpReturnState = UIState::PET_SCREEN;
static Tab     s_controlsHelpReturnTab   = Tab::TAB_PET;

void controlsHelpBegin(UIState returnState, Tab returnTab) {
s_controlsHelpReturnState = returnState;
s_controlsHelpReturnTab   = returnTab;

uiActionEnterState(UIState::CONTROLS_HELP, returnTab, true);
requestFullUIRedraw();
}

void controlsHelpDismiss() {
  if (!g_controlsHelpSeen) {
    // Only mark seen if we can persist it.
    if (sdReady()) {
      g_controlsHelpSeen = 1;
      saveSettingsToSD();
    }
  }

  inputForceClear();
  clearInputLatch();

  // Special case: on first boot, Controls Help should continue into the
  // boot wizard entry function, not directly into a wizard state.
  if (s_controlsHelpReturnState == UIState::BOOT_WIFI_PROMPT) {
    bootWizardBegin(UIState::CHOOSE_PET, s_controlsHelpReturnTab);
    requestFullUIRedraw();
    return;
  }

  uiActionEnterState(s_controlsHelpReturnState, s_controlsHelpReturnTab, true);
  requestFullUIRedraw();
}