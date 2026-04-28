#pragma once

#include <Arduino.h>

void petAutonomyTick(uint32_t nowMs);
void petAutonomyNotifyIfPending(uint32_t nowMs);
void petAutonomyReset();

void petAutonomySuppressAutoSleepUntil(uint32_t untilMs);

// Dev/test helpers. These trigger the same action bodies used by autonomy.
void petAutonomyDebugTriggerPizza();
void petAutonomyDebugTriggerAutoSleep();
void petAutonomyDebugTriggerMischief();
void petAutonomyDebugNotifyNow(uint32_t nowMs);
bool petAutonomyPassOutNoticePending();
void petAutonomyClearPassOutNotice();