#pragma once

#include "app_state.h"
#include "input.h"

void uiPetSleepingHandle(InputState &in);
void uiPetSleepingOnEnter(const InputState &in);
void uiPetSleepingBootEnter();
void uiPetSleepingSetReturnState(UIState state, Tab tab);