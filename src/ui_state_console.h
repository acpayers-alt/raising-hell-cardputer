#pragma once

#include "ui_defs.h"
#include "input.h"

// Console UI state handler
void uiConsoleHandle(InputState& input);
// Closes the console and returns to the remembered UI state.
// Used by ESC/MENU in the console handler and by the '/' toggle in app_loop.cpp.
bool closeConsoleAndReturn(InputState& input);

// Opens the console and remembers where to return.
void openConsoleWithReturn(UIState returnState, Tab returnTab, bool retToSettings, SettingsPage retSettingsPage);

