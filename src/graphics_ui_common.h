#pragma once

#include "pet.h"
#include <Arduino.h>

uint16_t uiPillOutline(PetType t);
uint16_t uiPillFillSelected(PetType t);
uint16_t uiModalOutline(PetType t);

void drawButton(int x, int y, int w, int h, const char *label, bool selected);