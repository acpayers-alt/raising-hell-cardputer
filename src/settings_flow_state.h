#pragma once

#include "input.h"
#include "return_target.h"
#include "ui_defs.h"

// Centralizes "where Settings should return to" and Settings page navigation state.
struct SettingsFlowState
{
  SettingsPage settingsPage = SettingsPage::TOP;
  SettingsPage settingsReturnPage = SettingsPage::TOP;

  bool powerMenuReturnToSleep = false;
  ReturnTarget powerMenuReturn{};
};

extern SettingsFlowState g_settingsFlow;

void openSettingsWithReturn(UIState returnState, Tab returnTab, SettingsPage page = SettingsPage::TOP);
void closeSettingsAndReturn(InputState &in);
void returnToSettingsPage(SettingsPage page, Tab tab, InputState &in);
bool settingsHasReturnTarget();
extern bool g_importPetListReturnToSettings;
extern SettingsPage g_importPetListReturnPage;