// system_status_state.h
#pragma once

#include <stdint.h>
#include <stdbool.h>

extern int batteryPercent;
extern int batteryVoltageMv;
extern bool usbPowered;
extern bool batteryLow;
extern bool batteryCritical;
extern uint32_t bootTime;

struct SystemStatusState
{
  int scrollOffset;
};

extern SystemStatusState g_systemStatus;