#include "ui_new_pet_flow.h"

#include <Arduino.h>
#include <string.h>

#include "app_state.h"
#include "boot_pipeline.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "name_entry_state.h"
#include "new_pet_flow_state.h"
#include "pet.h"
#include "save_manager.h"
#include "ui_actions.h"
#include "ui_runtime.h"

void beginNamePetFlow()
{
  memset(g_pendingPetName, 0, sizeof(g_pendingPetName));
  inputSetTextCapture(true);
  g_textCaptureMode = true;
  g_namePetJustOpened = true;

  uiActionEnterState(UIState::NAME_PET, Tab::TAB_PET, true);
  requestUIRedraw();
  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
}

void finalizeNewPetFromName(InputState &in)
{
  inputSetTextCapture(false); // restore normal UI mapping
  g_textCaptureMode = false;

  pet.setName(g_pendingPetName[0] ? g_pendingPetName : "PET");
  Serial.printf("[PET] named '%s'\n", pet.getName());

  // This is a truly brand-new pet lifecycle, so it must get a brand-new
  // identity even if some earlier runtime state accidentally survived.
  saveManagerAssignFreshPetId();

  // Commit chosen type into the final saved pet
  pet.type = g_pendingPetType;

  // New pet type is now final. Blow away any cached presentation from the
  // previous pet immediately so PET / clock / sleep screens rebuild using the
  // new type on the very first frame.
  graphicsReleaseUiCachesForMiniGame();
  resetClockModePetPresentation();
  sleepBgKickNow();
  invalidateBackgroundCache();

  // New pet must start with the canonical starter inventory, regardless of
  // what the previous pet had (death / factory reset / flow restart, etc.).
  g_app.inventory.resetToDefaults();

  // Birth time starts when the pet is actually named/finalized.
  saveManagerStampBirthNow();
  Serial.printf("[SAVE] first-finalize birthEpoch=%lu\n", (unsigned long)saveManagerGetBirthEpoch());

  // The pet is finalized now, so clear pending-flow flags BEFORE forcing the
  // first live save. Otherwise saveManagerForce() will skip save.bin creation.
  saveManagerClearNamePendingFlag();
  bootSetupClearPendingFlag();

  saveManagerForce();

  g_app.newPetFlowActive = false;

  // Enter PET_SCREEN, then start the scripted intro walk and fade.

  uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);

  startPetIntroWalkFromLeft();
  startPetScreenIntroFadeNow();

  requestFullUIRedraw();
  invalidateBackgroundCache();
  requestUIRedraw();

  // Swallow any stray edges/typing
  while (in.kbHasEvent())
    (void)in.kbPop();
  inputForceClear();
  clearInputLatch();
}