// system_status_state.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

extern int      batteryPercent;
extern bool     usbPowered;
extern uint32_t bootTime;

extern int batteryPercent;
extern int batteryVoltageMv;
extern bool usbPowered;
extern bool batteryLow;
extern bool batteryCritical;