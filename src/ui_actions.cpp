#include <Arduino.h>

#include "app_state.h"
#include "settings_flow_state.h"

#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_state_lifecycle.h"
#include "ui_suppress.h"

static UIReturnTarget g_uiReturnTarget = {UIState::TITLE_MENU, Tab::TAB_PET};

void uiSetReturnTarget(UIState state, Tab tab)
{
  g_uiReturnTarget.state = state;
  g_uiReturnTarget.tab = tab;
}

UIReturnTarget uiGetReturnTarget()
{
  return g_uiReturnTarget;
}

void uiReturnToTarget()
{
  uiActionEnterState(g_uiReturnTarget.state, g_uiReturnTarget.tab, true);
}

// -----------------------------------------------------------------------------
// Core state transition
// -----------------------------------------------------------------------------

void uiActionEnterState(UIState state, Tab tab, bool fullRedraw)
{
  const UIState prevState = g_app.uiState;
  const Tab prevTab = g_app.currentTab;

  const bool changed = (g_app.uiState != state) || (g_app.currentTab != tab);

  if (state == UIState::SETTINGS)
  {
    g_settingsFlow.settingsReturnValid = true;
    g_settingsFlow.settingsReturnState = prevState;
    g_settingsFlow.settingsReturnTab = prevTab;
  }

  if (!changed)
    return;

  if (prevState != state)
    uiStateOnExit(prevState);

  g_app.uiState = state;
  g_app.currentTab = tab;

  if (prevState != state)
    uiStateOnEnter(state);

  if (fullRedraw)
    requestFullUIRedraw();
  else
    requestUIRedraw();
}

void uiActionEnterStateClean(UIState state, Tab tab, bool fullRedraw, InputState &in, uint32_t suppressMenuMs)
{
  // Make sure nothing "leaks" into the next state.
  uiActionSwallowAll(in);

  uiActionEnterState(state, tab, fullRedraw);

  if (suppressMenuMs > 0)
    uiSuppressMenuForMs(suppressMenuMs);
}

// -----------------------------------------------------------------------------
// Input helpers
// -----------------------------------------------------------------------------

void uiActionDrainKb(InputState &in)
{
  while (in.kbHasEvent())
    (void)in.kbPop();
}

void uiActionSwallowEdges(InputState &in)
{
  in.clearEdges();
  clearInputLatch();
}

void uiActionSwallowAll(InputState &in)
{
  uiActionDrainKb(in);
  in.clearEdges();
  clearInputLatch();
}