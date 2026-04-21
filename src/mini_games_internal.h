#pragma once

#include "input.h"

// Shared mini-game result state
extern bool s_resultShown;
void miniGameDrawRewardModal(int gW, int gH);

// Flappy
extern bool s_flappyShowIntro;
void freeFlappyBgCache();
void freeFlappyFireballSprites();
void freeFlappyPipeSprites();
void freeImpWaveSprites();

// Crossy
bool crossyIsShowingIntro();
void freeCrossyZoneSprites();
void freeCrossyActorSprites();

// Dodger
bool dodgerIsShowingIntro();
void freeDodgerSprites();

// Resurrection Run
bool resRunIsShowingIntro();
void freeResRunSprites();