#pragma once

#include <stdint.h>
struct InputState;

// Power menu
void openPowerMenuFromHere(uint32_t now);
void uiPowerMenuHandle(InputState& in);

void powerMenuClose(InputState* in = nullptr, uint32_t suppressMs = 0);

void powerMenuActSleep();
void powerMenuActReboot();
void powerMenuActShutdown();
void emergencyBatteryShutdown();