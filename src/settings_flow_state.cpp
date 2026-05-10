#include "settings_flow_state.h"
#include "settings_nav_state.h"
#include "settings_state.h"
#include "input.h"

#include "ui_defs.h"
#include "ui_actions.h"
#include "ui_runtime.h"

// Single global instance
SettingsFlowState g_settingsFlow;

void openSettingsWithReturn(UIState returnState, Tab returnTab, SettingsPage page)
{
  g_settingsFlow.settingsPage = page;
  g_settingsFlow.settingsReturnPage = page;

  if (page == SettingsPage::TOP)
    resetSettingsNav(false);

  uiPushReturnTarget(returnState, returnTab);

  uiActionEnterState(UIState::SETTINGS, returnTab, true);
  requestFullUIRedraw();

  // Clear the shortcut that opened Settings now, but do not defer another
  // force-clear into the next input tick. A deferred clear can swallow the
  // user's first Enter press if they select an item immediately after opening
  // the menu.
  clearInputLatch();
}

void closeSettingsAndReturn(InputState &in)
{
  const UIReturnTarget ret = uiGetReturnTarget();
  uiPopReturnTarget();

  resetSettingsNav(true);
  g_settingsFlow.settingsPage = SettingsPage::TOP;
  g_settingsFlow.settingsReturnPage = SettingsPage::TOP;

  uiActionEnterStateClean(ret.state, ret.tab, true, in, 120);
}

void returnToSettingsPage(SettingsPage page, Tab tab, InputState &in)
{
  // Re-enter Settings on a specific page without resetting the whole settings nav.
  g_settingsFlow.settingsPage = page;
  uiActionEnterStateClean(UIState::SETTINGS, tab, true, in, 120);
  requestFullUIRedraw();
}

bool settingsHasReturnTarget() { return uiHasReturnTarget(); }

UIState settingsReturnState() { return uiGetReturnTarget().state; }

Tab settingsReturnTab() { return uiGetReturnTarget().tab; }
