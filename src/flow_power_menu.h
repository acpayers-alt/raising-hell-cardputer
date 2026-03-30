#pragma once

#include <stdint.h>
struct InputState;

// Power menu
void openPowerMenuFromHere(uint32_t now);
void uiPowerMenuHandle(InputState& in);

void powerMenuClose();

void powerMenuActSleep();
void powerMenuActReboot();
void powerMenuActShutdown();
void emergencyBatteryShutdown();