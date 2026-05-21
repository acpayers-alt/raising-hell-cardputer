#pragma once

#include <stdint.h>

#include "pet.h"

int uiPlayMenuCount();
const char *uiPlayMenuLabel(int idx);
PetType uiPlayMenuOwnerPetType(int idx);
bool uiPlayMenuActivate(int idx);