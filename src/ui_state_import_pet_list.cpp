#include "ui_state_import_pet_list.h"

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "pet.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_state_pet_sleeping.h"
#include <SD.h>
#include <cstring>

namespace
{
constexpr int kMaxPetExports = 24;
PetExportEntry s_entries[kMaxPetExports];
int s_entryCount = 0;
int s_importIndex = 0;
bool s_confirming = false;
int s_confirmIndex = 0; // 0 = YES, 1 = NO
static int s_windowStart = 0;
constexpr int kVisibleRows = 5;
static bool s_actionMenuActive = false;
static int s_actionIndex = 0; // 0 Retrieve, 1 Delete, 2 Cancel
static bool s_confirmDeleteActive = false;
static int s_confirmDeleteIndex = 0; // 0 Yes, 1 No

static void swallowImportInput(InputState &in)
{
  while (in.kbHasEvent())
    (void)in.kbPop();
  in.clearEdges();
  inputForceClear();
  clearInputLatch();
}

static bool deleteStoredPetAtPath(const char *path)
{
  if (!g_sdReady || !path || !path[0])
    return false;

  // Stored pets live under the exports directory, not backup.
  static const char *kExportsDir = "/raising_hell/exports";
  if (strncmp(path, kExportsDir, strlen(kExportsDir)) != 0)
    return false;

  if (!SD.exists(path))
    return false;

  return SD.remove(path);
}

} // namespace

void uiImportPetListOnEnter(InputState &in)
{
  s_entryCount = saveManagerListPetExports(s_entries, kMaxPetExports);
  s_confirming = false;
  s_importIndex = 0;
  s_windowStart = 0;
  s_actionMenuActive = false;
  s_actionIndex = 0;
  s_confirmDeleteActive = false;
  s_confirmDeleteIndex = 0;
  swallowImportInput(in);
  requestFullUIRedraw();
}

void uiImportPetListHandle(InputState &in)
{
  if (s_confirmDeleteActive)
  {
    if (in.leftOnce || in.upOnce || in.encoderDelta < 0)
    {
      s_confirmDeleteIndex = 0; // YES
      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    if (in.rightOnce || in.downOnce || in.encoderDelta > 0)
    {
      s_confirmDeleteIndex = 1; // NO
      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    if (in.selectOnce || in.encoderPressOnce)
    {
      if (s_confirmDeleteIndex == 0)
      {
        if (deleteStoredPetAtPath(s_entries[s_importIndex].path))
        {
          playBeep();
          ui_showSuccessMessage("Stored Pet Deleted");

          s_entryCount = saveManagerListPetExports(s_entries, kMaxPetExports);
          if (s_entryCount <= 0)
          {
            s_importIndex = 0;
            s_windowStart = 0;
          }
          else
          {
            if (s_importIndex >= s_entryCount)
              s_importIndex = s_entryCount - 1;
            if (s_importIndex < s_windowStart)
              s_windowStart = s_importIndex;
            if (s_importIndex >= s_windowStart + kVisibleRows)
              s_windowStart = s_importIndex - kVisibleRows + 1;
          }
        }
        else
        {
          playBeep();
          ui_showMessage("Delete Failed");
          requestUIRedraw();
        }
      }

      s_confirmDeleteActive = false;
      s_actionMenuActive = false;
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    if (in.menuOnce || in.escOnce)
    {
      s_confirmDeleteActive = false;
      s_confirmDeleteIndex = 0;
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    clearInputLatch();
    return;
  }

  if (s_actionMenuActive)
  {
    int move = 0;
    if (in.upOnce || in.leftOnce || in.encoderDelta < 0)
      move = -1;
    if (in.downOnce || in.rightOnce || in.encoderDelta > 0)
      move = +1;

    if (move != 0)
    {
      s_actionIndex += move;
      if (s_actionIndex < 0)
        s_actionIndex = 2;
      if (s_actionIndex > 2)
        s_actionIndex = 0;

      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    if (in.selectOnce || in.encoderPressOnce)
    {
      playBeep();

      if (s_actionIndex == 0)
      {
        s_actionMenuActive = false;
        s_actionIndex = 0;

        if (g_importPetListReturnToSettings)
        {
          // Settings flow: keep the existing confirm prompt.
          s_confirming = true;
          s_confirmIndex = 0;
          requestFullUIRedraw();
        }
        else
        {
          // Title-menu flow: retrieve immediately.
          char importedPath[128];
          if (saveManagerImportBubAtPath(s_entries[s_importIndex].path, importedPath, sizeof(importedPath), false))
          {
            playBeep();
            g_importPetListReturnToSettings = false;
            ui_showSuccessMessage("Pet Resumed");

            const bool restoredSleeping = pet.isSleeping || g_app.isSleeping || saveManagerSleepPendingFlagExists();

            if (restoredSleeping)
            {
              uiActionEnterStateClean(UIState::PET_SLEEPING, Tab::TAB_PET, true, in, 200);
              uiPetSleepingBootEnter();
              requestFullUIRedraw();
              sleepBgKickNow();
              forceRenderUIOnce();
            }
            else
            {
              uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, true, in, 200);
            }

            return;
          }
          else
          {
            playBeep();
            ui_showMessage("Import Failed");
            requestUIRedraw();
          }
        }
      }
      else if (s_actionIndex == 1)
      {
        // Delete
        s_confirmDeleteActive = true;
        s_confirmDeleteIndex = 0;
        requestFullUIRedraw();
      }
      else
      {
        // Cancel
        s_actionMenuActive = false;
        s_actionIndex = 0;
        requestFullUIRedraw();
      }

      swallowImportInput(in);
      return;
    }

    if (in.menuOnce || in.escOnce)
    {
      s_actionMenuActive = false;
      s_actionIndex = 0;
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    clearInputLatch();
    return;
  }

  if (s_confirming)
  {
    if (in.leftOnce || in.upOnce || in.encoderDelta < 0)
    {
      s_confirmIndex = 0;
      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    if (in.rightOnce || in.downOnce || in.encoderDelta > 0)
    {
      s_confirmIndex = 1;
      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    const bool activateConfirm = in.selectOnce || in.encoderPressOnce;
    if (!activateConfirm)
    {
      clearInputLatch();
      return;
    }

    const bool exportCurrentPetFirst = (s_confirmIndex == 0);

    char importedPath[128];
    if (saveManagerImportBubAtPath(s_entries[s_importIndex].path, importedPath, sizeof(importedPath),
                                   exportCurrentPetFirst))
    {
      playBeep();

      s_confirming = false;
      s_confirmIndex = 0;
      s_actionMenuActive = false;
      s_actionIndex = 0;
      g_importPetListReturnToSettings = false;

      ui_showSuccessMessage("Pet Resumed");

      const bool restoredSleeping = pet.isSleeping || g_app.isSleeping || saveManagerSleepPendingFlagExists();

      if (restoredSleeping)
      {
        uiActionEnterStateClean(UIState::PET_SLEEPING, Tab::TAB_PET, true, in, 200);
        uiPetSleepingBootEnter();
        requestFullUIRedraw();
        sleepBgKickNow();
        forceRenderUIOnce();
      }
      else
      {
        uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, true, in, 200);
      }

      return;
    }
    else
    {
      playBeep();
      ui_showMessage(exportCurrentPetFirst ? "Resume Failed" : "Import Failed");

      s_confirming = false;
      s_confirmIndex = 0;
      s_actionMenuActive = false;
      s_actionIndex = 0;

      requestUIRedraw();
      swallowImportInput(in);
      return;
    }
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

    if (s_importIndex < s_windowStart)
      s_windowStart = s_importIndex;
    if (s_importIndex >= s_windowStart + kVisibleRows)
      s_windowStart = s_importIndex - kVisibleRows + 1;

    playBeep();
    requestFullUIRedraw();
    swallowImportInput(in);
    return;
  }

  if (in.menuOnce || in.escOnce)
  {
    playBeep();

    if (g_importPetListReturnToSettings)
    {
      const SettingsPage returnPage = g_importPetListReturnPage;
      g_importPetListReturnToSettings = false;
      returnToSettingsPage(returnPage, g_app.currentTab, in);
    }
    else
    {
      uiActionEnterStateClean(UIState::TITLE_MENU, Tab::TAB_PET, true, in, 120);
    }

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

  s_actionMenuActive = true;
  s_actionIndex = 0;
  requestFullUIRedraw();
  swallowImportInput(in);
}

int uiImportPetListCount() { return s_entryCount; }

int uiImportPetListVisibleCount()
{
  const int remaining = s_entryCount - s_windowStart;
  return (remaining <= 0) ? 0 : ((remaining < kVisibleRows) ? remaining : kVisibleRows);
}

int uiImportPetListWindowStart() { return s_windowStart; }

const PetExportEntry &uiImportPetListGetVisible(int idx) { return s_entries[s_windowStart + idx]; }

int uiImportPetListSelected() { return s_importIndex; }

bool uiImportPetListActionMenuActive() { return s_actionMenuActive; }
int uiImportPetListActionIndex() { return s_actionIndex; }

bool uiImportPetListConfirmDeleteActive() { return s_confirmDeleteActive; }
int uiImportPetListConfirmDeleteIndex() { return s_confirmDeleteIndex; }