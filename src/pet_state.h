#pragma once
#include "app_state.h"

class Pet;
extern Pet pet;

// If you want a helper, DECLARE it here:
void petResurrectFull();

bool petResurrectGraceActive();

// Resets Pet::update() dt/accumulators so long pauses (death screen / mini-game)
// don't immediately decay stats on return.
void petResetUpdateTimers();
