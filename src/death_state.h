#pragma once

#include <stdint.h>

// Death screen menu state (selected option)
extern int deathMenuIndex;

// Reset death screen menu selection to default
void resetDeathMenu();

// Death transition state
void beginDeathTransition();
void tickDeathTransition(uint32_t now);
bool deathTransitionActive();
bool consumeDeathScreenFadeInStart();