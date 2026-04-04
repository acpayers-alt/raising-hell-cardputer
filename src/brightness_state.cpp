#include "brightness_state.h"

#include "debug.h"
#include "display.h" // setBacklight(...)

uint8_t brightnessLevel = 1; // default mid
const uint16_t brightnessValues[3] = {10, 80, 255};

static inline int clampi(int v, int lo, int hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

void applyBrightnessLevel(int level)
{
  brightnessLevel = (uint8_t)clampi(level, 0, 2);
  displayRememberUserBrightness(brightnessLevel);
  setBacklight((uint8_t)brightnessValues[brightnessLevel]);
  // Optional:
  // DBG_ON("[BRIGHT] level=%u val=%u\n", (unsigned)brightnessLevel, (unsigned)brightnessValues[brightnessLevel]);
}
