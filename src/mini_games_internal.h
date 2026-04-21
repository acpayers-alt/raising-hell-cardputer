#pragma once

#include "mini_games.h"

void miniGameDrawRewardModal(int gW, int gH);

extern bool s_resultShown;

// Ressurection Run
void freeResRunSprites();
bool resRunIsShowingIntro();

// Dodger
void freeDodgerSprites();
bool dodgerIsShowingIntro();