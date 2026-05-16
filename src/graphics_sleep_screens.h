#pragma once

#include <Arduino.h>

void drawSleepScreen();
void drawSleepScreenSceneOnly();
void sleepAnimHeartbeat(uint32_t now);
void sleepBgNotifyScreenWake();