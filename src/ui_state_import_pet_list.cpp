#include "ui_state_import_pet_list.h"

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

namespace
{
constexpr int kMaxPetExports = 24;
PetExportEntry s_entries[kMaxPetExports];
int s_entryCount = 0;
int s_importIndex = 0;
bool s_confirming = false;
int s_confirmIndex = 0; // 0 = YES, 1 = NO

static void swallowImportInput(InputState &in)
{
  while (in.kbHasEvent())
    (void)in.kbPop();
  in.clearEdges();
  inputForceClear();
  clearInputLatch();
}
} // namespace

void uiImportPetListOnEnter(InputState &in)
{
  s_entryCount = saveManagerListPetExports(s_entries, kMaxPetExports);
  s_confirming = false;
  s_importIndex = 0;
  s_confirming = false;
  swallowImportInput(in);
  requestFullUIRedraw();
}

void uiImportPetListHandle(InputState &in)
{
  if (s_confirming)
  {
    if (in.leftOnce || in.upOnce)
    {
      s_confirmIndex = 0;
      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    if (in.rightOnce || in.downOnce)
    {
      s_confirmIndex = 1;
      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    const bool activateConfirm = in.selectOnce || in.encoderPressOnce;
    if (activateConfirm)
    {
      const bool exportCurrentPetFirst = (s_confirmIndex == 0);

      char importedPath[128];
      if (saveManagerImportBubAtPath(s_entries[s_importIndex].path, importedPath, sizeof(importedPath),
                                     exportCurrentPetFirst))
      {
        playBeep();
        ui_showMessage("Pet Resumed");
        uiActionEnterState(UIState::TITLE_MENU, Tab::TAB_PET, true);
      }
      else
      {
        playBeep();
        ui_showMessage(exportCurrentPetFirst ? "Resume Failed" : "Import failed");
        requestUIRedraw();
      }

      s_confirming = false;
      swallowImportInput(in);
      return;
    }

    // Cancel confirm prompt
    if (in.menuOnce || in.escOnce)
    {
      s_confirming = false;
      s_confirmIndex = 0;
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    clearInputLatch();
    return;
  }

  int move = 0;
  if (in.upOnce || in.leftOnce || in.encoderDelta < 0)
    move = -1;
  if (in.downOnce || in.rightOnce || in.encoderDelta > 0)
    move = +1;

  if (move != 0 && s_entryCount > 0)
  {
    s_importIndex += move;
    if (s_importIndex < 0)
      s_importIndex = s_entryCount - 1;
    if (s_importIndex >= s_entryCount)
      s_importIndex = 0;
    playBeep();
    requestFullUIRedraw();
    swallowImportInput(in);
    return;
  }

  if (in.menuOnce || in.escOnce)
  {
    playBeep();
    uiActionEnterState(UIState::TITLE_MENU, Tab::TAB_PET, true);
    swallowImportInput(in);
    return;
  }

  const bool activate = in.selectOnce || in.encoderPressOnce;
  if (!activate)
  {
    clearInputLatch();
    return;
  }

  if (s_entryCount <= 0)
  {
    playBeep();
    ui_showMessage("No stored pets");
    requestUIRedraw();
    swallowImportInput(in);
    return;
  }

  s_confirming = true;
  s_confirmIndex = 0;
  requestFullUIRedraw();
  swallowImportInput(in);
}

int uiImportPetListCount() { return s_entryCount; }

int uiImportPetListVisibleCount() { return (s_entryCount < 5) ? s_entryCount : 5; }

const PetExportEntry &uiImportPetListGet(int idx) { return s_entries[idx]; }

int uiImportPetListSelected() { return s_importIndex; }

bool uiImportPetListConfirming() { return s_confirming; }

int uiImportPetListConfirmIndex() { return s_confirmIndex; }