#pragma once

#include <Arduino.h>
#include <stdint.h>

void wardriveStepsTick(uint32_t nowMs);
void wardriveStepsNotifyUserActivity();
void wardriveStepsResetRuntime();

uint32_t wardriveStepsToday();
uint32_t wardriveHitsToday();