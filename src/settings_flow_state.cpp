#include "settings_flow_state.h"

#include "settings_state.h"
#include "ui_actions.h"
#include "ui_runtime.h"

// Single global instance
SettingsFlowState g_settingsFlow;

bool g_importPetListReturnToSettings = false;
SettingsPage g_importPetListReturnPage = SettingsPage::TOP;

void openSettingsWithReturn(UIState returnState, Tab returnTab, SettingsPage page)
{
  g_settingsFlow.settingsReturnValid = true;
  g_settingsFlow.settingsReturnState = returnState;
  g_settingsFlow.settingsReturnTab   = returnTab;
  g_settingsFlow.settingsPage        = page;

  uiActionEnterState(UIState::SETTINGS, returnTab, true);
  requestFullUIRedraw();
  clearInputLatch();
}

void closeSettingsAndReturn(InputState& in)
{
  const UIState targetState =
      g_settingsFlow.settingsReturnValid
          ? g_settingsFlow.settingsReturnState
          : UIState::PET_SCREEN;

  const Tab targetTab =
      g_settingsFlow.settingsReturnValid
          ? g_settingsFlow.settingsReturnTab
          : Tab::TAB_PET;

  g_settingsFlow.settingsReturnValid = false;

  uiActionEnterStateClean(targetState, targetTab, true, in, 120);
}

bool settingsHasReturnTarget()
{
  return g_settingsFlow.settingsReturnValid;
}

UIState settingsReturnState()
{
  return g_settingsFlow.settingsReturnState;
}

Tab settingsReturnTab()
{
  return g_settingsFlow.settingsReturnTab;
}