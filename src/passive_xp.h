#pragma once

#include <Arduino.h>

void passiveXpTick(uint32_t nowMs);
void passiveXpResetTimer(uint32_t nowMs = millis());