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
static bool s_returnToSettings = false;
static SettingsPage s_returnPage = SettingsPage::TOP;

static PetExportEntry s_storeCurrentEntry{};

static bool shouldShowStoreCurrentRow() { return saveManagerSaveFileExists(); }

static int totalImportRows() { return s_entryCount + (shouldShowStoreCurrentRow() ? 1 : 0); }

static bool isStoreCurrentRow(int index) { return shouldShowStoreCurrentRow() && index == s_entryCount; }

static void currentPetIdHex(char *out, size_t outSize)
{
  if (!out || outSize == 0)
    return;

  out[0] = '\0';

  if (pet.petId == 0)
    return;

  snprintf(out, outSize, "%llx", (unsigned long long)pet.petId);
}

static void filterLoadedPetFromEntries()
{
  if (!saveManagerSaveFileExists())
    return;

  char liveId[24];
  currentPetIdHex(liveId, sizeof(liveId));

  if (!liveId[0])
    return;

  int write = 0;
  for (int read = 0; read < s_entryCount; ++read)
  {
    // Only filter valid entries with matching pet IDs. Corrupt files should
    // remain visible so the user can delete them.
    if (s_entries[read].valid && s_entries[read].petId[0] && strcasecmp(s_entries[read].petId, liveId) == 0)
    {
      continue;
    }

    if (write != read)
      s_entries[write] = s_entries[read];

    ++write;
  }

  s_entryCount = write;
}

static void refreshImportEntries()
{
  s_entryCount = saveManagerListPetExports(s_entries, kMaxPetExports);
  filterLoadedPetFromEntries();

  memset(&s_storeCurrentEntry, 0, sizeof(s_storeCurrentEntry));
  strncpy(s_storeCurrentEntry.name, "Store Current Pet", sizeof(s_storeCurrentEntry.name) - 1);
  strncpy(s_storeCurrentEntry.petType, "CURRENT", sizeof(s_storeCurrentEntry.petType) - 1);
  s_storeCurrentEntry.valid = true;
}

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

void openImportPetListFromTitle(InputState &in)
{
  s_returnToSettings = false;
  s_returnPage = SettingsPage::TOP;

  uiActionEnterStateClean(UIState::IMPORT_PET_LIST, Tab::TAB_PET, true, in, 120);
  requestFullUIRedraw();
}

void uiImportPetListOnEnter(InputState &in)
{
  refreshImportEntries();

  const int invalidCount = saveManagerLastExportScanInvalidCount();

  s_confirming = false;
  s_importIndex = 0;
  s_windowStart = 0;
  s_actionMenuActive = false;
  s_actionIndex = 0;
  s_confirmDeleteActive = false;
  s_confirmDeleteIndex = 0;

  if (!s_returnToSettings)
    s_returnPage = SettingsPage::TOP;

  if (totalImportRows() <= 0)
  {
    if (invalidCount > 0)
    {
      Serial.printf("[IMPORT LIST] no valid pets; corrupt files=%d\n", invalidCount);
      ui_showMessage("Bad .bub file");
    }
    else
    {
      Serial.println("[IMPORT LIST] no stored pets found");
      ui_showMessage("No stored pets");
    }
  }

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

          refreshImportEntries();
          if (totalImportRows() <= 0)
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
      const int actionMax = isStoreCurrentRow(s_importIndex) ? 1 : 2;

      if (s_actionIndex < 0)
        s_actionIndex = actionMax;
      if (s_actionIndex > actionMax)
        s_actionIndex = 0;

      playBeep();
      requestFullUIRedraw();
      swallowImportInput(in);
      return;
    }

    if (in.selectOnce || in.encoderPressOnce)
    {
      playBeep();

      if (isStoreCurrentRow(s_importIndex))
      {
        if (s_actionIndex == 0)
        {
          char boxedPath[128] = {0};

          if (!saveManagerBoxCurrentPet(boxedPath, sizeof(boxedPath)))
          {
            soundError();
            ui_showMessage("Store failed");
            Serial.println("[IMPORT LIST] Store Current Pet FAILED");
            s_actionMenuActive = false;
            s_actionIndex = 0;
            requestUIRedraw();
            swallowImportInput(in);
            return;
          }

          Serial.printf("[IMPORT LIST] Store Current Pet OK path=%s\n", boxedPath);

          resetRuntimeToCleanNoSaveState(/*resetName=*/true);
          g_app.newPetFlowActive = false;
          saveManagerClearNamePendingFlag();
          saveManagerClearSleepPendingFlag();

          s_actionMenuActive = false;
          s_actionIndex = 0;
          refreshImportEntries();

          ui_showSuccessMessage("Pet Stored");
          uiActionEnterStateClean(UIState::TITLE_MENU, Tab::TAB_PET, true, in, 120);
          requestFullUIRedraw();
          swallowImportInput(in);
          return;
        }

        // Cancel
        s_actionMenuActive = false;
        s_actionIndex = 0;
        requestFullUIRedraw();
        swallowImportInput(in);
        return;
      }

      if (s_actionIndex == 0)
      {
        s_actionMenuActive = false;
        s_actionIndex = 0;

        if (s_returnToSettings)
        {
          // Settings flow: only allow valid entries.
          if (!s_entries[s_importIndex].valid)
          {
            playBeep();
            ui_showMessage("Bad .bub file");
            requestUIRedraw();
          }
          else
          {
            s_confirming = true;
            s_confirmIndex = 0;
            requestFullUIRedraw();
          }
        }
        else
        {
          // Title-menu flow: retrieve immediately (valid entries only).
          if (!s_entries[s_importIndex].valid)
          {
            playBeep();
            ui_showMessage("Bad .bub file");
            requestUIRedraw();
          }
          else
          {
            char importedPath[128];
            if (saveManagerImportBubAtPath(s_entries[s_importIndex].path, importedPath, sizeof(importedPath), true))
            {
              playBeep();
              s_returnToSettings = false;
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

    if (!s_entries[s_importIndex].valid)
    {
      playBeep();
      ui_showMessage("Bad .bub file");

      s_confirming = false;
      s_confirmIndex = 0;
      s_actionMenuActive = false;
      s_actionIndex = 0;

      requestUIRedraw();
      swallowImportInput(in);
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
      s_returnToSettings = false;

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

  const int rowCount = totalImportRows();

  if (move != 0 && rowCount > 0)
  {
    s_importIndex += move;
    if (s_importIndex < 0)
      s_importIndex = rowCount - 1;
    if (s_importIndex >= rowCount)
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

    if (s_returnToSettings)
    {
      const SettingsPage returnPage = g_settingsFlow.settingsReturnPage;

      s_returnToSettings = false;

      g_settingsFlow.settingsReturnPage = SettingsPage::TOP;

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

  if (totalImportRows() <= 0)
  {
    playBeep();
    swallowImportInput(in);
    return;
  }

  s_actionMenuActive = true;
  s_actionIndex = 0;
  requestFullUIRedraw();
  swallowImportInput(in);
}

int uiImportPetListCount() { return totalImportRows(); }

int uiImportPetListVisibleCount()
{
  const int remaining = totalImportRows() - s_windowStart;

  return (remaining <= 0) ? 0 : ((remaining < kVisibleRows) ? remaining : kVisibleRows);
}

int uiImportPetListWindowStart() { return s_windowStart; }

const PetExportEntry &uiImportPetListGetVisible(int idx)
{
  const int absolute = s_windowStart + idx;

  if (isStoreCurrentRow(absolute))
    return s_storeCurrentEntry;

  return s_entries[absolute];
}

int uiImportPetListSelected() { return s_importIndex; }

bool uiImportPetListActionMenuActive() { return s_actionMenuActive; }
int uiImportPetListActionIndex() { return s_actionIndex; }

bool uiImportPetListSelectedIsStoreCurrent()
{
  return isStoreCurrentRow(s_importIndex);
}

bool uiImportPetListVisibleIsStoreCurrent(int visibleIndex)
{
  const int absolute = s_windowStart + visibleIndex;
  return isStoreCurrentRow(absolute);
}

bool uiImportPetListConfirmDeleteActive() { return s_confirmDeleteActive; }
int uiImportPetListConfirmDeleteIndex() { return s_confirmDeleteIndex; }