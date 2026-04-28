#pragma once

#include <Arduino.h>

void petAutonomyTick(uint32_t nowMs);
void petAutonomyNotifyIfPending(uint32_t nowMs);
void petAutonomyReset();