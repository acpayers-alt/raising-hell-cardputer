#pragma once

#include <stdint.h>

void freeSleepAnimFrameCache();
bool ensureSleepAnimFrameCache(uint8_t mode, const char *const *frames, uint8_t frameCount, int drawX, int drawY);