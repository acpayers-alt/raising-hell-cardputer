#pragma once

#include "ui_defs.h"
#include "input.h"

void uiClockModeHandle(InputState &in);
void openClockModeWithReturn(UIState state, Tab tab, InputState &in, uint16_t drainMs = 120);
void openClockModeWithReturnNoInput(UIState state, Tab tab);
void uiClockModeExitToReturn(InputState &in, uint16_t drainMs = 150);