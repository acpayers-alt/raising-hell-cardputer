// settings_nav_state.h
#pragma once
#include <stdint.h>

#include "ui_defs.h"
#include "ui_state_utils.h"   
#include "input.h"

void resetSettingsNav(bool resetTopIndex);
void openSettingsWithReturn(UIState returnState, Tab returnTab);
void closeSettingsAndReturn(InputState& in);
bool settingsHasReturnTarget();
UIState settingsReturnState();
Tab settingsReturnTab();