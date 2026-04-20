#pragma once

#include "input.h"
#include "return_target.h"
#include "ui_defs.h"

// Centralizes Settings page navigation state.
//
// settingsPage:
//   The page currently being shown inside Settings.
//
// settingsReturnPage:
//   A one-step nested-subflow return target used by Settings-owned child screens
//   (for example backup/import browsers returning to Pet Options).
//   This is not a general global "where all UI should go next" field.
struct SettingsFlowState
{
  SettingsPage settingsPage = SettingsPage::TOP;
  SettingsPage settingsReturnPage = SettingsPage::TOP;

  bool powerMenuReturnToSleep = false;
};

extern SettingsFlowState g_settingsFlow;

void openSettingsWithReturn(UIState returnState, Tab returnTab, SettingsPage page = SettingsPage::TOP);
void closeSettingsAndReturn(InputState &in);
void returnToSettingsPage(SettingsPage page, Tab tab, InputState &in);
bool settingsHasReturnTarget();
