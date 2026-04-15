#include <Arduino.h>

#include "app_state.h"
#include "settings_flow_state.h"

#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_suppress.h"

// -----------------------------------------------------------------------------
// Small return stack (overlay-safe)
// -----------------------------------------------------------------------------

struct UiReturnFrame
{
  UIState state;
  Tab     tab;
};

static constexpr int kReturnStackMax = 6;
static UiReturnFrame s_returnStack[kReturnStackMax];
static int s_returnTop = 0;

// -----------------------------------------------------------------------------
// Core state transition
// -----------------------------------------------------------------------------

void uiActionEnterState(UIState state, Tab tab, bool fullRedraw)
{
  const UIState prevState = g_app.uiState;
  const Tab prevTab = g_app.currentTab;

  const bool changed =
      (g_app.uiState != state) ||
      (g_app.currentTab != tab);

  if (state == UIState::SETTINGS && !settingsHasReturnTarget())
  {
    g_settingsFlow.settingsReturnValid = true;
    g_settingsFlow.settingsReturnState = prevState;
    g_settingsFlow.settingsReturnTab   = prevTab;
  }

  g_app.uiState    = state;
  g_app.currentTab = tab;

  if (changed)
  {
    if (fullRedraw)
      requestFullUIRedraw();
    else
      requestUIRedraw();
  }
}

void uiActionEnterStateClean(UIState state,
                             Tab tab,
                             bool fullRedraw,
                             InputState& in,
                             uint32_t suppressMenuMs)
{
  // Make sure nothing "leaks" into the next state.
  uiActionSwallowAll(in);

  uiActionEnterState(state, tab, fullRedraw);

  if (suppressMenuMs > 0)
    uiSuppressMenuForMs(suppressMenuMs);
}

// -----------------------------------------------------------------------------
// Return stack
// -----------------------------------------------------------------------------

void uiPushReturn(UIState state, Tab tab)
{
  if (s_returnTop >= kReturnStackMax)
    return;

  s_returnStack[s_returnTop++] = { state, tab };
}

bool uiPopReturn(UIState& stateOut, Tab& tabOut)
{
  if (s_returnTop <= 0)
    return false;

  s_returnTop--;
  stateOut = s_returnStack[s_returnTop].state;
  tabOut   = s_returnStack[s_returnTop].tab;
  return true;
}

void uiClearReturnStack()
{
  s_returnTop = 0;
}

bool uiActionReturnToPrevious(InputState& in,
                              bool fullRedraw,
                              uint32_t suppressMenuMs)
{
  UIState st;
  Tab     tab;
  if (!uiPopReturn(st, tab))
    return false;

  uiActionEnterStateClean(st, tab, fullRedraw, in, suppressMenuMs);
  return true;
}

// -----------------------------------------------------------------------------
// Input helpers
// -----------------------------------------------------------------------------

void uiActionDrainKb(InputState& in)
{
  while (in.kbHasEvent())
    (void)in.kbPop();
}

void uiActionSwallowEdges(InputState& in)
{
  in.clearEdges();
  clearInputLatch();
}

void uiActionSwallowAll(InputState& in)
{
  uiActionDrainKb(in);
  in.clearEdges();
  clearInputLatch();
}