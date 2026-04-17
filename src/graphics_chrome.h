#pragma once

#include "pet.h"
#include <Arduino.h>

struct PetUIColorScheme
{
  uint16_t topBg;
  uint16_t topOutline;
  uint16_t topText;

  uint16_t tabBg;
  uint16_t tabOutline;
  uint16_t tabFillSel;
  uint16_t tabTextOff;
  uint16_t tabTextOn;
};

PetUIColorScheme uiSchemeForPet(PetType t);

String formatTime();

void drawTopBar();
void drawTabBar();