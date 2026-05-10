#include "ui_state_backup_pet_list.h"

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "pet.h"
#include "save_manager.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_state_pet_sleeping.h"

namespace
{
constexpr int kMaxPetBackups = 48;
constexpr int kVisibleRows = 5;

PetExportEntry s_entries[kMaxPetBackups];
int s_entryCount = 0;
int s_selected = 0;
int s_windowStart = 0;

bool s_actionMenuActive = false;
int s_actionIndex = 0; // 0 Restore, 1 Delete Backup, 2 Cancel

bool s_confirmDeleteActive = false;
int s_confirmDeleteIndex = 0; // 0 Yes, 1 No

bool s_confirmRestoreActive = false;
int s_confirmRestoreIndex = 0; // 0 Yes, 1 Cancel

static void swallowBackupInput(InputState &in, bool deferredClear = false)
{
  while (in.kbHasEvent())
    (void)in.kbPop();

  in.clearEdges();

  // Same-screen cleanup should not schedule a deferred clear. Otherwise a fast
  // Enter after scrolling/restoring/deleting can be eaten next frame.
  if (deferredClear)
    inputForceClear();

  clearInputLatch();
}

static void clampSelectionAndWindow()
{
  if (s_entryCount <= 0)
  {
    s_selected = 0;
    s_windowStart = 0;
    return;
  }

  if (s_selected < 0)
    s_selected = 0;
  if (s_selected >= s_entryCount)
    s_selected = s_entryCount - 1;

  if (s_windowStart < 0)
    s_windowStart = 0;
  if (s_windowStart > s_selected)
    s_windowStart = s_selected;
  if (s_selected >= s_windowStart + kVisibleRows)
    s_windowStart = s_selected - kVisibleRows + 1;
}

static void reloadBackups()
{
  s_entryCount = saveManagerListPetBackups(s_entries, kMaxPetBackups);
  clampSelectionAndWindow();
}

static void leaveBackupBrowser(InputState &in)
{
  returnToSettingsPage(g_settingsFlow.settingsReturnPage, g_app.currentTab, in);
}

static void beginRestoreConfirm()
{
  if (s_entryCount <= 0)
  {
    ui_showMessage("No backups found");
    requestUIRedraw();
    return;
  }

  // Block corrupt entries (same behavior as import list)
  if (!s_entries[s_selected].valid)
  {
    playBeep();
    ui_showMessage("Bad .bub file");
    requestUIRedraw();
    return;
  }

  s_confirmRestoreActive = true;
  s_confirmRestoreIndex = 0;
  requestFullUIRedraw();
}

static void performRestore(bool storeCurrentFirst, InputState &in)
{
  if (!s_entries[s_selected].valid)
  {
    playBeep();
    ui_showMessage("Bad .bub file");

    s_confirmRestoreActive = false;
    s_confirmRestoreIndex = 0;
    s_actionMenuActive = false;
    s_actionIndex = 0;

    requestUIRedraw();
    swallowBackupInput(in);
    return;
  }

  char importedPath[128];
  if (saveManagerImportBubAtPath(s_entries[s_selected].path, importedPath, sizeof(importedPath), storeCurrentFirst))
  {
    playBeep();

    s_confirmRestoreActive = false;
    s_confirmRestoreIndex = 0;
    s_actionMenuActive = false;
    s_actionIndex = 0;

    ui_showSuccessMessage("Pet Restored!");

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
    ui_showMessage("Restore Failed");
    s_confirmRestoreActive = false;
    s_confirmRestoreIndex = 0;
    s_actionMenuActive = false;
    s_actionIndex = 0;
    requestUIRedraw();
    swallowBackupInput(in);
  }
}

static void performDelete(InputState &in)
{
  if (s_entryCount <= 0)
  {
    s_confirmDeleteActive = false;
    requestUIRedraw();
    swallowBackupInput(in);
    return;
  }

  if (saveManagerDeletePetBackupAtPath(s_entries[s_selected].path))
  {
    playBeep();
    reloadBackups();
    ui_showSuccessMessage("Backup Deleted");
  }
  else
  {
    playBeep();
    ui_showMessage("Delete Failed");
  }

  s_confirmDeleteActive = false;
  s_actionMenuActive = false;
  requestFullUIRedraw();
  swallowBackupInput(in);
}

} // namespace

void openBackupPetListFromSettings(SettingsPage returnPage, InputState &in)
{
  g_settingsFlow.settingsPage = returnPage;
  g_settingsFlow.settingsReturnPage = returnPage;
  uiActionEnterStateClean(UIState::BACKUP_PET_LIST, g_app.currentTab, true, in, 120);
  requestFullUIRedraw();
}

void uiBackupPetListOnEnter(InputState &in)
{
  s_selected = 0;
  s_windowStart = 0;
  s_actionMenuActive = false;
  s_actionIndex = 0;
  s_confirmDeleteActive = false;
  s_confirmDeleteIndex = 0;
  s_confirmRestoreActive = false;
  s_confirmRestoreIndex = 0;

  reloadBackups();

  const int invalidCount = saveManagerLastExportScanInvalidCount();

  if (s_entryCount <= 0)
  {
    if (invalidCount > 0)
    {
      Serial.printf("[BACKUP LIST] no valid backups; corrupt files=%d\n", invalidCount);
      ui_showMessage("Bad .bub file");
    }
    else
    {
      Serial.println("[BACKUP LIST] no backups found");
      ui_showMessage("No backups found");
    }
  }

  swallowBackupInput(in, true);
  requestFullUIRedraw();
}

void uiBackupPetListHandle(InputState &in)
{
  if (s_confirmDeleteActive)
  {
    if (in.leftOnce || in.upOnce || in.encoderDelta < 0)
    {
      s_confirmDeleteIndex = 0; // YES
      playBeep();
      requestFullUIRedraw();
      swallowBackupInput(in);
      return;
    }

    if (in.rightOnce || in.downOnce || in.encoderDelta > 0)
    {
      s_confirmDeleteIndex = 1; // NO
      playBeep();
      requestFullUIRedraw();
      swallowBackupInput(in);
      return;
    }

    if (in.selectOnce || in.encoderPressOnce)
    {
      if (s_confirmDeleteIndex == 0)
        performDelete(in);
      else
      {
        s_confirmDeleteActive = false;
        s_confirmDeleteIndex = 0;
        requestFullUIRedraw();
        swallowBackupInput(in);
      }
      return;
    }

    if (in.menuOnce || in.escOnce)
    {
      s_confirmDeleteActive = false;
      s_confirmDeleteIndex = 0;
      requestFullUIRedraw();
      swallowBackupInput(in);
      return;
    }

    clearInputLatch();
    return;
  }

  if (s_confirmRestoreActive)
  {
    if (in.leftOnce || in.upOnce || in.encoderDelta < 0)
    {
      s_confirmRestoreIndex = 0; // YES
      playBeep();
      requestFullUIRedraw();
      swallowBackupInput(in);
      return;
    }

    if (in.rightOnce || in.downOnce || in.encoderDelta > 0)
    {
      s_confirmRestoreIndex = 1; // CANCEL
      playBeep();
      requestFullUIRedraw();
      swallowBackupInput(in);
      return;
    }

    if (in.selectOnce || in.encoderPressOnce)
    {
      if (s_confirmRestoreIndex == 0)
      {
        performRestore(true, in);
      }
      else
      {
        s_confirmRestoreActive = false;
        s_confirmRestoreIndex = 0;
        s_actionMenuActive = false;
        s_actionIndex = 0;
        requestFullUIRedraw();
        swallowBackupInput(in);
      }
      return;
    }

    if (in.menuOnce || in.escOnce)
    {
      s_confirmRestoreActive = false;
      s_confirmRestoreIndex = 0;
      s_actionMenuActive = false;
      s_actionIndex = 0;
      requestFullUIRedraw();
      swallowBackupInput(in);
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
      swallowBackupInput(in);
      return;
    }

    if (in.selectOnce || in.encoderPressOnce)
    {
      playBeep();

      if (s_actionIndex == 0)
      {
        beginRestoreConfirm();
      }
      else if (s_actionIndex == 1)
      {
        s_actionMenuActive = false;
        s_actionIndex = 0;
        s_confirmDeleteActive = true;
        s_confirmDeleteIndex = 0;
        requestFullUIRedraw();
      }
      else
      {
        s_actionMenuActive = false;
        requestFullUIRedraw();
      }

      swallowBackupInput(in);
      return;
    }

    if (in.menuOnce || in.escOnce)
    {
      s_actionMenuActive = false;
      s_actionIndex = 0;
      requestFullUIRedraw();
      swallowBackupInput(in);
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
    s_selected += move;
    if (s_selected < 0)
      s_selected = s_entryCount - 1;
    if (s_selected >= s_entryCount)
      s_selected = 0;

    if (s_selected < s_windowStart)
      s_windowStart = s_selected;
    if (s_selected >= s_windowStart + kVisibleRows)
      s_windowStart = s_selected - kVisibleRows + 1;

    playBeep();
    requestFullUIRedraw();
    swallowBackupInput(in);
    return;
  }

  if (in.menuOnce || in.escOnce)
  {
    playBeep();
    leaveBackupBrowser(in);
    swallowBackupInput(in);
    return;
  }

  if (!(in.selectOnce || in.encoderPressOnce))
  {
    clearInputLatch();
    return;
  }

  if (s_entryCount <= 0)
  {
    playBeep();
    swallowBackupInput(in);
    return;
  }

  s_actionMenuActive = true;
  s_actionIndex = 0;
  requestFullUIRedraw();
  swallowBackupInput(in);
}

int uiBackupPetListCount() { return s_entryCount; }

int uiBackupPetListVisibleCount()
{
  const int remaining = s_entryCount - s_windowStart;
  return (remaining <= 0) ? 0 : ((remaining < kVisibleRows) ? remaining : kVisibleRows);
}

int uiBackupPetListWindowStart() { return s_windowStart; }

const PetExportEntry &uiBackupPetListGetVisible(int idx) { return s_entries[s_windowStart + idx]; }

int uiBackupPetListSelected() { return s_selected; }

bool uiBackupPetListActionMenuActive() { return s_actionMenuActive; }

int uiBackupPetListActionIndex() { return s_actionIndex; }

bool uiBackupPetListConfirmRestoreActive() { return s_confirmRestoreActive; }

int uiBackupPetListConfirmRestoreIndex() { return s_confirmRestoreIndex; }

bool uiBackupPetListConfirmDeleteActive() { return s_confirmDeleteActive; }

int uiBackupPetListConfirmDeleteIndex() { return s_confirmDeleteIndex; }