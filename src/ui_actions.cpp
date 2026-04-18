#include <Arduino.h>

#include "app_state.h"
#include "settings_flow_state.h"

#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_state_lifecycle.h"
#include "ui_suppress.h"

static UIReturnTarget g_uiReturnStack[8];
static int g_uiReturnDepth = 0;

static UIReturnTarget defaultReturnTarget() { return {UIState::TITLE_MENU, Tab::TAB_PET}; }

void uiSetReturnTarget(UIState state, Tab tab)
{
  if (g_uiReturnDepth > 0)
  {
    g_uiReturnStack[g_uiReturnDepth - 1].state = state;
    g_uiReturnStack[g_uiReturnDepth - 1].tab = tab;
    return;
  }

  g_uiReturnStack[0].state = state;
  g_uiReturnStack[0].tab = tab;
  g_uiReturnDepth = 1;
}

void uiPushReturnTarget(UIState state, Tab tab)
{
  if (g_uiReturnDepth >= (int)(sizeof(g_uiReturnStack) / sizeof(g_uiReturnStack[0])))
  {
    g_uiReturnStack[(sizeof(g_uiReturnStack) / sizeof(g_uiReturnStack[0])) - 1].state = state;
    g_uiReturnStack[(sizeof(g_uiReturnStack) / sizeof(g_uiReturnStack[0])) - 1].tab = tab;
    return;
  }

  g_uiReturnStack[g_uiReturnDepth].state = state;
  g_uiReturnStack[g_uiReturnDepth].tab = tab;
  g_uiReturnDepth++;
}

UIReturnTarget uiGetReturnTarget()
{
  if (g_uiReturnDepth <= 0)
    return defaultReturnTarget();

  return g_uiReturnStack[g_uiReturnDepth - 1];
}

UIReturnTarget uiPopReturnTarget()
{
  if (g_uiReturnDepth <= 0)
    return defaultReturnTarget();

  g_uiReturnDepth--;
  if (g_uiReturnDepth <= 0)
  {
    g_uiReturnDepth = 0;
    return defaultReturnTarget();
  }

  return g_uiReturnStack[g_uiReturnDepth - 1];
}

bool uiHasReturnTarget() { return g_uiReturnDepth > 0; }

void uiReturnToTarget()
{
  const UIReturnTarget ret = uiGetReturnTarget();
  uiActionEnterState(ret.state, ret.tab, true);
}

// -----------------------------------------------------------------------------
// Core state transition
// -----------------------------------------------------------------------------

void uiActionEnterState(UIState state, Tab tab, bool fullRedraw)
{
  const UIState prevState = g_app.uiState;
  const Tab prevTab = g_app.currentTab;

  const bool changed = (g_app.uiState != state) || (g_app.currentTab != tab);

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