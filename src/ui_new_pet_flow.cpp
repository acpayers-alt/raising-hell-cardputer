#include "ui_new_pet_flow.h"

#include <string.h>

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "name_entry_state.h"
#include "new_pet_flow_state.h"
#include "pet.h"
#include "ui_runtime.h"
#include "save_manager.h"

void beginNamePetFlow()
{
  memset(g_pendingPetName, 0, sizeof(g_pendingPetName));
  inputSetTextCapture(true);
  g_textCaptureMode   = true;
  g_namePetJustOpened = true;

  g_app.uiState = UIState::NAME_PET;
  requestUIRedraw();
  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
}

void finalizeNewPetFromName(InputState& in)
{
  inputSetTextCapture(false); // restore normal UI mapping
  g_textCaptureMode = false;

  pet.setName(g_pendingPetName[0] ? g_pendingPetName : "PET");

  // Commit chosen type into the final saved pet
  pet.type = g_pendingPetType;

  // New pet must start with the canonical starter inventory, regardless of
  // what the previous pet had (death / factory reset / flow restart, etc.).
  g_app.inventory.resetToDefaults();

  // Birth time starts when the pet is actually named/finalized.
  saveManagerStampBirthNow();

  saveManagerForce();
  
  g_app.newPetFlowActive = false;

  // Leave NAME flow and go back to pet screen
  g_app.uiState    = UIState::PET_SCREEN;
  g_app.currentTab = Tab::TAB_PET;

  requestUIRedraw();
  invalidateBackgroundCache();
  requestUIRedraw();

  // Swallow any stray edges/typing
  while (in.kbHasEvent()) (void)in.kbPop();
  inputForceClear();
  clearInputLatch();
}