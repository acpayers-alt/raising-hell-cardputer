#include "app_lifecycle.h"

#include "boot_pipeline.h"   // bootSetupPendingFlagExists()

// ------------------------------------------------------------
// Lifecycle: onboarding state
// ------------------------------------------------------------

bool appLifecycleHasPendingOnboarding()
{
  // For now, onboarding is defined by this flag
  return bootSetupPendingFlagExists();
}

// ------------------------------------------------------------
// Lifecycle: can we enter normal UI?
// ------------------------------------------------------------

bool appLifecycleCanEnterNormalUi()
{
  return !appLifecycleHasPendingOnboarding();
}

// ------------------------------------------------------------
// Lifecycle: resolve post-boot landing
// ------------------------------------------------------------

UIState appLifecycleResolveBootAfterOkState(bool saveFileExists)
{
  // If onboarding/setup is still pending → must go to CHOOSE_PET
  if (appLifecycleHasPendingOnboarding())
    return UIState::CHOOSE_PET;

  // Otherwise normal behavior:
  // - if save exists → go to pet
  // - else → choose pet
  return saveFileExists ? UIState::PET_SCREEN : UIState::CHOOSE_PET;
}

bool appLifecycleLoadedSaveRequiresChoosePet(bool namePending, bool blankPetName)
{
  if (namePending)
    return true;

  if (blankPetName)
    return true;

  return false;
}