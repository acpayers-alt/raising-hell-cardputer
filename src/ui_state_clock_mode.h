#pragma once

#include "app_state.h"
#include "input.h"

void uiClockModeHandle(InputState &in);
void uiClockModeSetReturnState(UIState state, Tab tab);
void openClockModeWithReturn(UIState state, Tab tab, InputState &in, uint16_t drainMs = 120);
void uiClockModeExitToReturn(InputState &in, uint16_t drainMs = 150);