#pragma once
#include <stdint.h>
#include "anim_clips.h"

struct PetPerfStats
{
  uint16_t petFrameMs = 0;
  uint16_t petSpriteDrawMs = 0;
  uint16_t animStepMs = 0;
};

const PetPerfStats &petPerfStats();
void petPerfResetStats();

void animTick();

// Returns true once per frame change; clears the internal “changed” flag.
bool animConsumeFrameChanged();

// Render the current PET SCREEN frame (full sprite). Safe to call even if ANIM_NONE.
void animDrawPetFrame(int x, int y);

void animForceStop();

// Draw frame anchored so the sprite's bottom touches bottomY.
// centerX is the horizontal center point for the sprite.
void animDrawPetFrameAnchoredBottom(int centerX, int bottomY);
void animDrawPetFrameAnchoredNominalBottom(int centerX, int nominalBottomY, int nominalH, int yAdjust);

void animNotifyScreenWake();
