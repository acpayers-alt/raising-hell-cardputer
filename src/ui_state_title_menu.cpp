#include "ui_state_title_menu.h"

#include <Arduino.h>

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "pet.h"
#include "save_manager.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_input_common.h"
#include "ui_runtime.h"
#include "ui_state_choose_pet.h"
#include "ui_state_pet_sleeping.h"
#include "ui_state_import_pet_list.h"

int g_titleMenuIndex = 0;

namespace
{
constexpr int kTitleMenuCount = 3;

static void swallowTitleInput(InputState &in);

static bool s_titleHasSave = false;
static bool s_titleHasImport = false;

static bool titleHasLivePet()
{
  return saveManagerSaveFileExists() || (saveManagerGetBirthEpoch() != 0 && pet.getName()[0] != '\0');
}

static void refreshTitleMenuAvailability()
{
  s_titleHasSave = titleHasLivePet();
  s_titleHasImport = saveManagerHasImportableBubJson();
}

enum TitleMenuItem : int
{
  TITLE_CONTINUE = 0,
  TITLE_IMPORT = 1,
  TITLE_SETTINGS = 2,
};

static bool titleItemEnabled(int idx)
{
  switch (idx)
  {
  case TITLE_CONTINUE:
    return true; // Continue or New Pet
  case TITLE_IMPORT:
    return s_titleHasImport;
  case TITLE_SETTINGS:
    return true;
  default:
    return false;
  }
}

static int titleFirstEnabledItem()
{
  if (titleItemEnabled(TITLE_CONTINUE))
    return TITLE_CONTINUE;
  if (titleItemEnabled(TITLE_IMPORT))
    return TITLE_IMPORT;
  if (titleItemEnabled(TITLE_SETTINGS))
    return TITLE_SETTINGS;
  return TITLE_SETTINGS;
}

static int titleStepEnabled(int startIdx, int dir)
{
  int idx = startIdx;
  for (int i = 0; i < kTitleMenuCount; ++i)
  {
    idx += dir;
    if (idx < 0)
      idx = kTitleMenuCount - 1;
    if (idx >= kTitleMenuCount)
      idx = 0;

    if (titleItemEnabled(idx))
      return idx;
  }
  return startIdx;
}

static void titleActivateContinue(InputState &in)
{
  refreshTitleMenuAvailability();

  // No live save: row 0 means NEW PET, not Continue.
  if (!s_titleHasSave)
  {
    saveManagerDeletePetOnly();
    resetRuntimeToCleanNoSaveState(/*resetName=*/true);
    g_app.newPetFlowActive = false;
    saveManagerClearNamePendingFlag();
    saveManagerClearSleepPendingFlag();

    uiActionEnterState(UIState::CHOOSE_PET, Tab::TAB_PET, true);
    uiChoosePetOnEnter(in);
    requestFullUIRedraw();
    requestUIRedraw();

    inputForceClear();
    clearInputLatch();
    return;
  }

  const bool shouldEnterSleeping = pet.isSleeping || g_app.isSleeping || g_app.sleepingByTimer ||
                                   g_app.sleepUntilRested || g_app.sleepUntilAwakened ||
                                   saveManagerSleepPendingFlagExists();

  if (shouldEnterSleeping)
  {
    // Enter sleep screen, but when the pet wakes we should land on the live PET tab.
    enterSleepFlow(UIState::PET_SCREEN, Tab::TAB_PET, in, 120);
    forceRenderUIOnce();
  }
  else
  {
    uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
    requestUIRedraw();
  }

  inputForceClear();
  clearInputLatch();
  (void)in;
}

static void swallowTitleInput(InputState &in)
{
  while (in.kbHasEvent())
    (void)in.kbPop();

  in.clearEdges();
  inputForceClear();
  clearInputLatch();
}
} // namespace

void uiTitleMenuOnEnter(InputState &in)
{
  swallowTitleInput(in);
  refreshTitleMenuAvailability();

  // If no save exists, always focus "New Pet" (row 0)
  if (!s_titleHasSave)
  {
    g_titleMenuIndex = TITLE_CONTINUE;
  }
  else if (!titleItemEnabled(g_titleMenuIndex))
  {
    g_titleMenuIndex = titleFirstEnabledItem();
  }

  requestFullUIRedraw();
}

void uiTitleMenuHandle(InputState &in)
{
  refreshTitleMenuAvailability();

  if (!titleItemEnabled(g_titleMenuIndex))
    g_titleMenuIndex = titleFirstEnabledItem();

  int move = 0;
  if (in.upOnce || in.leftOnce || in.encoderDelta < 0)
    move = -1;
  if (in.downOnce || in.rightOnce || in.encoderDelta > 0)
    move = +1;

  if (move != 0)
  {
    const int nextIdx = titleStepEnabled(g_titleMenuIndex, (move > 0) ? +1 : -1);
    if (nextIdx != g_titleMenuIndex)
    {
      g_titleMenuIndex = nextIdx;
      playBeep();
      requestFullUIRedraw();
    }

    swallowTitleInput(in);
    return;
  }

  if (in.menuOnce)
  {
    titleActivateContinue(in);
    return;
  }

  if (in.escOnce)
  {
    refreshTitleMenuAvailability();

    if (s_titleHasSave)
    {
      playBeep();
      titleActivateContinue(in);
      return;
    }

    // No pet loaded: Esc is intentionally dead on the title menu.
    playBeep();
    swallowTitleInput(in);
    requestFullUIRedraw();
    return;
  }

  const bool activate = in.selectOnce || in.encoderPressOnce;
  if (!activate)
  {
    while (in.kbHasEvent())
      (void)in.kbPop();
    return;
  }

  switch (g_titleMenuIndex)
  {
  case TITLE_CONTINUE:
  {
    titleActivateContinue(in);
    return;
  }

  case TITLE_IMPORT:
  {
    playBeep();
    openImportPetListFromTitle(in);
    swallowTitleInput(in);
    return;
  }

  case TITLE_SETTINGS:
  {
    playBeep();
    openSettingsWithReturn(UIState::TITLE_MENU, Tab::TAB_PET, SettingsPage::TOP);
    swallowTitleInput(in);
    return;
  }
  }

  swallowTitleInput(in);

  Serial.printf("[TITLE] idx=%d row0=%s import=%d settings=%d\n", g_titleMenuIndex,
                s_titleHasSave ? "continue" : "newpet", s_titleHasImport ? 1 : 0, 1);
}

bool uiTitleMenuHasSave() { return s_titleHasSave; }

bool uiTitleMenuHasImport() { return s_titleHasImport; }