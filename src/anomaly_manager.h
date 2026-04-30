#pragma once

#include <Arduino.h>

void anomalyTick(uint32_t nowMs);
void anomalyDrawOverlay();
bool anomalyActive();
void anomalyNotifyUserActivity(uint32_t nowMs);

// Console/dev helper: queue one forced anomaly after console returns to a safe UI.
void anomalyRequestForceAfterReturn();