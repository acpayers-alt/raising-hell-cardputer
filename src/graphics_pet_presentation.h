#pragma once
#include "pet.h"

struct PetRenderProfile
{
  int w;
  int h;
  int xOff;
  int yOff;
};

const PetRenderProfile &getPetProfile(PetType t);

void getPetHomeScreenPosition(int &outX, int &outY);
void resetPetWanderToHome();
bool petWalkOverrideActive();
bool drawIntroWalkingPetOverride();
int petPresentationX();
int petPresentationY();
bool petPresentationHasIntroHandoff();
void clearPetPresentationIntroHandoff();

void drawPetScreen(bool redrawBg);

void resetPetScreenPositionToHome();
void resetClockModePetPresentation();
void startPetIntroWalkFromLeft();

void tickPetIntroWalk();
void tickPetWander();

bool petPresentationAnimating();
bool petPresentationScriptedIntroActive();