// settings_flow_state.h
#pragma once

#include "ui_defs.h"
#include "return_target.h"

// Centralizes "where Settings should return to" and Settings page navigation state.
// This was previously stored inside g_ui (ui_runtime.h). Keep it separate from other
// runtime UI indices so console/text-capture can block modals/flows cleanly.
struct SettingsFlowState {
  // Current settings sub-page being shown
  SettingsPage settingsPage = SettingsPage::TOP;

  // When temporarily navigating away (e.g., into a sub-flow), remember which page
  // to return to when backing out.
  SettingsPage settingsReturnPage = SettingsPage::TOP;

  // Return target when exiting Settings back to the main UI
  UIState settingsReturnState = UIState::PET_SCREEN;
  Tab     settingsReturnTab   = Tab::TAB_PET;
  bool    settingsReturnValid = false;

  // Return target when exiting Power Menu
  bool         powerMenuReturnToSleep = false;
  ReturnTarget powerMenuReturn{};
};

extern SettingsFlowState g_settingsFlow;
