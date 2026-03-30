// system_status_state.cpp
#include <Arduino.h>
#include "system_status_state.h"

uint32_t bootTime = 0;

int batteryPercent = -1;
int batteryVoltageMv = 0;
bool usbPowered = false;
bool batteryLow = false;
bool batteryCritical = false;