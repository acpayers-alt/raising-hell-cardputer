#include "flow_controls_help.h"

#include "app_state.h"
#include "controls_help_state.h"
#include "graphics.h"
#include "input.h"
#include "sound.h"
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

static int controlsHelpHeldScrollRepeat(const InputState &in)
{
  static uint32_t s_holdStartMs = 0;
  static uint32_t s_lastRepeatMs = 0;
  static int s_lastDir = 0;

  int dir = 0;
  if (in.uiUpHeld)
    dir = -1;
  else if (in.uiDownHeld)
    dir = 1;

  if (dir == 0)
  {
    s_holdStartMs = 0;
    s_lastRepeatMs = 0;
    s_lastDir = 0;
    return 0;
  }

  const uint32_t now = millis();

  if (dir != s_lastDir)
  {
    s_holdStartMs = now;
    s_lastRepeatMs = now;
    s_lastDir = dir;
    return 0;
  }

  if ((uint32_t)(now - s_holdStartMs) < 360)
    return 0;

  if ((uint32_t)(now - s_lastRepeatMs) < 95)
    return 0;

  s_lastRepeatMs = now;
  return dir;
}

// Controls help UIState handler (routed by ui_input_router)
void uiControlsHelpHandle(InputState &in)
{
  if (!controlsHelpDismissAllowed())
  {
    uiActionSwallowAll(in);
    return;
  }

  int move = in.encoderDelta;
  bool heldRepeatMove = false;

  if (in.upOnce)
    move = -1;
  else if (in.downOnce)
    move = 1;
  else
  {
    move = controlsHelpHeldScrollRepeat(in);
    heldRepeatMove = (move != 0);
  }

  if (move < 0)
  {
    uiActionSwallowAll(in);
    if (controlsHelpScrollUp() && !heldRepeatMove)
      playBeep();
    return;
  }

  if (move > 0)
  {
    uiActionSwallowAll(in);
    if (controlsHelpScrollDown() && !heldRepeatMove)
      playBeep();
    return;
  }
  
  if (in.selectOnce || in.menuOnce || in.escOnce)
  {
    uiActionSwallowAll(in);
    controlsHelpDismiss();
    return;
  }
}
