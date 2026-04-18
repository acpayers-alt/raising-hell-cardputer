#include "ui_state_console.h"

#include "app_state.h"
#include "console.h"
#include "input.h"
#include "return_target.h"
#include "settings_flow_state.h"
#include "ui_actions.h"
#include "ui_input_common.h"
#include "ui_input_utils.h"
#include "ui_runtime.h"

// Console return target
static ReturnTarget g_consoleReturn{UIState::PET_SCREEN, Tab::TAB_PET};
static SettingsPage g_consoleReturnPage = SettingsPage::TOP;

void openConsoleWithReturn(UIState returnState, Tab returnTab, bool retToSettings, SettingsPage retSettingsPage)
{
  g_consoleReturn.state = returnState;
  g_consoleReturn.tab = returnTab;
  g_consoleReturnPage = retSettingsPage;
  uiPushReturnTarget(returnState, returnTab);
    
  if (retToSettings)
  {
    g_settingsFlow.settingsReturnPage = retSettingsPage;
  }

  consoleOpen();

  uiActionEnterState(UIState::CONSOLE, returnTab, true);
  clearInputLatch();
  requestUIRedraw();
}

static inline void swallowTypingAndEdges(InputState &in)
{
  // Drain any typed chars accumulated this frame
  uiActionDrainKb(in);

  // Clear one-shot edges so we don't immediately close/submit/reopen things
  in.escOnce = false;
  in.menuOnce = false;
  in.selectOnce = false;
  in.encoderPressOnce = false;

  // Flush latches/queue; preserves held state so "held ESC" doesn't re-edge next tick
  clearInputLatch();
}

void uiConsoleHandle(InputState &input)
{
  if (input.menuOnce || input.escOnce)
  {
    consoleClose();

    // IMPORTANT: swallow BEFORE changing state so no edge leaks into the next state
    swallowTypingAndEdges(input);

    if (g_consoleReturn.state == UIState::SETTINGS)
    {
      g_settingsFlow.settingsPage = g_consoleReturnPage;
      closeSettingsAndReturn(input);
      requestUIRedraw();
      return;
    }

    // Normal behavior: return to the UI state we came from
    uiActionEnterStateClean(g_consoleReturn.state, g_consoleReturn.tab, true, input, 120);
    requestUIRedraw();
    return;
  }

  // Let the console module handle keystrokes, cursor, etc.
  consoleUpdate(input);
  requestUIRedraw();
}

bool closeConsoleAndReturn(InputState &input)
{
  // Only makes sense if console is actually active
  if (g_app.uiState != UIState::CONSOLE)
    return false;

  consoleClose();

  // Swallow BEFORE changing state so no edge leaks into the next state
  swallowTypingAndEdges(input);

  if (g_consoleReturn.state == UIState::SETTINGS)
  {
    g_settingsFlow.settingsPage = g_consoleReturnPage;
    closeSettingsAndReturn(input);
    requestUIRedraw();
    return true;
  }
  
  uiActionEnterStateClean(g_consoleReturn.state, g_consoleReturn.tab, true, input, 120);
  requestUIRedraw();
  return true;
}