#pragma once

#include <stdint.h>

#include "ui_defs.h"
#include "input.h"

struct UIReturnTarget
{
  UIState state;
  Tab tab;
};

void uiSetReturnTarget(UIState state, Tab tab);
UIReturnTarget uiGetReturnTarget();
void uiReturnToTarget();

void uiPushReturnTarget(UIState state, Tab tab);
UIReturnTarget uiPopReturnTarget();
bool uiHasReturnTarget();

// -----------------------------------------------------------------------------
// Core state transition API
// -----------------------------------------------------------------------------
//
// NOTE:
// uiActionEnterState(...) is the "pure" transition (no input draining).
// For user-driven transitions (ESC/menu/enter), prefer uiActionEnterStateClean(...)
// so we consistently drain kb + clear edges + clear latch + suppress menu, etc.
//

void uiActionEnterState(UIState state, Tab tab, bool fullRedraw);

// Enter a state AND do the boring-but-critical stuff consistently:
// - drain keyboard queue
// - clear edge flags (escOnce/menuOnce/selectOnce/etc.)
// - clear input latch
// - optional menu suppression window
// - redraw choice
void uiActionEnterStateClean(UIState state,
                             Tab tab,
                             bool fullRedraw,
                             InputState& in,
                             uint32_t suppressMenuMs = 250);

// -----------------------------------------------------------------------------
// Input helpers
// -----------------------------------------------------------------------------

// Swallow edge-style inputs (escOnce/menuOnce/selectOnce/etc.)
void uiActionSwallowEdges(InputState& in);

// Drain queued keyboard events safely
void uiActionDrainKb(InputState& in);

// Swallow EVERYTHING for this frame: drain kb + clear edges + clear latch
void uiActionSwallowAll(InputState& in);