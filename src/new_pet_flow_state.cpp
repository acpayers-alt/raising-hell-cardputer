#include "new_pet_flow_state.h"

bool     g_choosePetBlockHatchUntilRelease = false;
uint32_t g_choosePetInputUnlockMs          = 0;
PetType  g_pendingPetType                  = PET_DEVIL;
bool     g_namePetJustOpened               = false;