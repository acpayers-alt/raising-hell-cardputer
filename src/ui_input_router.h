#pragma once

#include "ui_defs.h"

struct InputState;

// Returns whether the current UI context should treat keyboard input as "text capture"
// (i.e., Backspace should delete text, keys should feed the kb queue, etc.).
bool uiWantsTextCaptureNow();
bool uiWantsTextCaptureForState(UIState s);

// Central UI input entry point.
// Applies global interceptors then dispatches to the current UIState handler table.
bool uiHandleInput(InputState& in);

extern bool g_suppressMenuTick;