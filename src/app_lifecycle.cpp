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
  (void)saveFileExists;

  // We now normalize post-boot handoff through a single title/menu state.
  // The title screen decides whether Continue/New Pet/Import are available.
  return UIState::TITLE_MENU;
}

bool appLifecycleLoadedSaveRequiresChoosePet(bool namePending, bool blankPetName)
{
  if (namePending)
    return true;

  if (blankPetName)
    return true;

  return false;
}