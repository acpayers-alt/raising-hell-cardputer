#include "ui_state_import_pet_list.h"

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include <SD.h>
#include <cstring>

namespace
{
constexpr int kMaxPetExports = 24;
constexpr int kVisibleRows = 5;

PetExportEntry s_entries[kMaxPetExports];
int s_entryCount = 0;
int s_importIndex = 0;
int s_windowStart = 0;

bool s_confirming = false;
int s_confirmIndex = 0; // legacy/dead state, kept for compatibility with existing getters

bool s_actionMenuActive = false;
int s_actionIndex = 0; // 0 Retrieve, 1 Delete, 2 Cancel

bool s_confirmDeleteActive = false;
int s_confirmDeleteIndex = 0; // 0 Yes, 1 No

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

static void clampImportSelection()
{
  if (s_entryCount <= 0)
  {
    s_importIndex = 0;
    s_windowStart = 0;
    return;
  }

  if (s_importIndex < 0)
    s_importIndex = 0;
  if (s_importIndex >= s_entryCount)
    s_importIndex = s_entryCount - 1;

  if (s_windowStart < 0)
    s_windowStart = 0;
  if (s_importIndex < s_windowStart)
    s_windowStart = s_importIndex;
  if (s_importIndex >= s_windowStart + kVisibleRows)
    s_windowStart = s_importIndex - kVisibleRows + 1;
}

static void reloadStoredPets()
{
  s_entryCount = saveManagerListPetExports(s_entries, kMaxPetExports);
  clampImportSelection();
}

static void leaveImportList()
{
  if (g_importPetListReturnToSettings)
  {
    g_settingsFlow.settingsPage = g_importPetListReturnPage;
    g_importPetListReturnToSettings = false;
    uiActionEnterState(UIState::SETTINGS, Tab::TAB_PET, true);
  }
  else
  {
    uiActionEnterState(UIState::TITLE_MENU, Tab::TAB_PET, true);
  }
}

} // namespace

void uiImportPetListOnEnter(InputState &in)
{
  reloadStoredPets();

  s_confirming = false;
  s_confirmIndex = 0;
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
          reloadStoredPets();
        }
        else
        {
          playBeep();
          ui_showMessage("Delete Failed");
          requestUIRedraw();
        }
      }

      s_confirmDeleteActive = false;
      s_confirmDeleteIndex = 0;
      s_actionMenuActive = false;
      s_actionIndex = 0;
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
        // Retrieve -> always store current pet first, no extra prompt.
        char importedPath[128];
        if (saveManagerImportBubAtPath(s_entries[s_importIndex].path,
                                       importedPath,
                                       sizeof(importedPath),
                                       true))
        {
          ui_showSuccessMessage("Pet Retrieved");
          s_actionMenuActive = false;
          s_actionIndex = 0;
          swallowImportInput(in);
          leaveImportList();
          return;
        }
        else
        {
          ui_showMessage("Retrieve Failed");
          s_actionMenuActive = false;
          s_actionIndex = 0;
          requestUIRedraw();
          swallowImportInput(in);
          return;
        }
      }
      else if (s_actionIndex == 1)
      {
        // Delete
        s_confirmDeleteActive = true;
        s_confirmDeleteIndex = 0;
        requestFullUIRedraw();
        swallowImportInput(in);
        return;
      }
      else
      {
        // Cancel
        s_actionMenuActive = false;
        s_actionIndex = 0;
        requestFullUIRedraw();
        swallowImportInput(in);
        return;
      }
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
    swallowImportInput(in);
    leaveImportList();
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

// Legacy/dead state kept so existing graphics/header references still compile cleanly.
bool uiImportPetListConfirming() { return s_confirming; }
int uiImportPetListConfirmIndex() { return s_confirmIndex; }

bool uiImportPetListActionMenuActive() { return s_actionMenuActive; }
int uiImportPetListActionIndex() { return s_actionIndex; }

bool uiImportPetListConfirmDeleteActive() { return s_confirmDeleteActive; }
int uiImportPetListConfirmDeleteIndex() { return s_confirmDeleteIndex; }