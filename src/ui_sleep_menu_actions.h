#pragma once

#include <stdint.h>

struct InputState;

// Perform the actual transition into sleeping state, including wake suppression,
// redraw, cache invalidation, and marking save dirty.
void uiSleepMenuEnterSleep(uint32_t nowMs, InputState& in);

// Configure the sleep mode parameters for each menu choice.
// These functions only set g_app sleep flags/duration and do not transition.
void uiSleepMenuSetUntilAwakened(uint32_t nowMs);
void uiSleepMenuSetUntilRested(uint32_t nowMs);
void uiSleepMenuSetForHours(uint32_t nowMs, uint32_t hours);