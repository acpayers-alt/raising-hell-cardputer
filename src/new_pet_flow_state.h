#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pet.h" // PetType

// Gate to prevent instant hatch when entering CHOOSE_PET while ENTER is held
// or stale selectOnce leaks in from the prior screen.
extern bool g_choosePetBlockHatchUntilRelease;

// Time gate to prevent instant hatch from carried input right after entering
// CHOOSE_PET, even if the key has already been released.
extern uint32_t g_choosePetInputUnlockMs;

// Pending selected pet type while in CHOOSE/NAME flow.
extern PetType g_pendingPetType;

// Used by NAME_PET to clear stale keyboard state once after entering.
extern bool g_namePetJustOpened;