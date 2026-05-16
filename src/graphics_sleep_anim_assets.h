#pragma once

#include <stdint.h>

#include "pet.h"

struct SleepAnimSelection
{
  uint8_t mode = 0;
  const char *bgPath = nullptr;

  const char *const *frames = nullptr;
  uint8_t frameCount = 0;
  uint32_t frameMs = 0;

  const char *const *triggerFrames = nullptr;
  uint8_t triggerFrameCount = 0;
  uint32_t triggerFrameMs = 0;
  uint32_t triggerMinMs = 0;
  uint32_t triggerMaxMs = 0;
};

SleepAnimSelection selectSleepAnimForPet(PetType type, int evoStage);