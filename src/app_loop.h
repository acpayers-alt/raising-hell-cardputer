#pragma once
#include <stdint.h>

// Runs one full frame/tick of the legacy application loop.
void appMainLoopTick();

// Runs the per-frame services that MUST happen no matter what state is active
// (keyboard update, sound tick, power button tick, etc).
void appServicesTick(uint32_t nowMs);

