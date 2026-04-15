#pragma once

#include "ui_defs.h"
#include "return_target.h"
#include "input.h"

// Centralizes "where Settings should return to" and Settings page navigation state.
struct SettingsFlowState {
  SettingsPage settingsPage = SettingsPage::TOP;
  SettingsPage settingsReturnPage = SettingsPage::TOP;

  UIState settingsReturnState = UIState::PET_SCREEN;
  Tab     settingsReturnTab   = Tab::TAB_PET;
  bool    settingsReturnValid = false;

  bool         powerMenuReturnToSleep = false;
  ReturnTarget powerMenuReturn{};
};

extern SettingsFlowState g_settingsFlow;

void openSettingsWithReturn(UIState returnState, Tab returnTab, SettingsPage page = SettingsPage::TOP);
void closeSettingsAndReturn(InputState& in);
void returnToSettingsPage(SettingsPage page, Tab tab, InputState& in);
void clearSettingsReturnTarget();
bool settingsHasReturnTarget();
UIState settingsReturnState();
Tab settingsReturnTab();
extern bool g_importPetListReturnToSettings;
extern SettingsPage g_importPetListReturnPage;