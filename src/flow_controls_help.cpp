#include "flow_controls_help.h"

#include "app_state.h"
#include "controls_help_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_defs.h"

// Public entry points used by app hotkeys / settings menu
void openControlsHelpFromSettings()
{
  // Return to Settings and preserve the current tab.
  controlsHelpBegin(UIState::SETTINGS, g_app.currentTab);
}

void openControlsHelpFromAnywhere()
{
  // Return to wherever we are right now, preserving the current tab.
  controlsHelpBegin(g_app.uiState, g_app.currentTab);
}

// Controls help UIState handler (routed by ui_input_router)
void uiControlsHelpHandle(InputState &in)
{
  if (!controlsHelpDismissAllowed())
  {
    uiActionSwallowAll(in);
    return;
  }

  if (in.selectOnce || in.menuOnce || in.escOnce)
  {
    uiActionSwallowAll(in);
    controlsHelpDismiss();
    return;
  }
}