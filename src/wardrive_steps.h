#pragma once

#include <Arduino.h>

void wardriveStepsTick(uint32_t nowMs);
void wardriveStepsResetRuntime();

uint32_t wardriveStepsTotal();
uint32_t wardriveStepsSession();
uint32_t wardriveHitsSession();