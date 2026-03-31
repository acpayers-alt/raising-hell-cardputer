#pragma once

#include "ui_defs.h"
#include "settings_flow_state.h"

struct InputState;

// Settings screen input handler (moved out of menu_actions.cpp)
void uiSettingsHandle(InputState& input);
