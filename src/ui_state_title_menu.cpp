#include "ui_state_title_menu.h"

#include <Arduino.h>

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

int g_titleMenuIndex = 0;

namespace
{
constexpr int kTitleMenuCount = 3;

enum TitleMenuItem : int {
  TITLE_CONTINUE = 0,
  TITLE_NEW_PET  = 1,
  TITLE_IMPORT   = 2,
};

static bool titleItemEnabled(int idx)
{
  switch (idx)
  {
    case TITLE_CONTINUE: return saveManagerSaveFileExists();
    case TITLE_NEW_PET:  return true;
    case TITLE_IMPORT:   return saveManagerHasImportableBubJson();
    default:             return false;
  }
}

static int titleFirstEnabledItem()
{
  if (titleItemEnabled(TITLE_CONTINUE)) return TITLE_CONTINUE;
  if (titleItemEnabled(TITLE_NEW_PET))  return TITLE_NEW_PET;
  if (titleItemEnabled(TITLE_IMPORT))   return TITLE_IMPORT;
  return TITLE_NEW_PET;
}

static int titleStepEnabled(int startIdx, int dir)
{
  int idx = startIdx;
  for (int i = 0; i < kTitleMenuCount; ++i)
  {
    idx += dir;
    if (idx < 0) idx = kTitleMenuCount - 1;
    if (idx >= kTitleMenuCount) idx = 0;

    if (titleItemEnabled(idx))
      return idx;
  }
  return startIdx;
}

static void swallowTitleInput(InputState& in)
{
  while (in.kbHasEvent())
    (void)in.kbPop();

  in.clearEdges();
  inputForceClear();
  clearInputLatch();
}
} // namespace

void uiTitleMenuOnEnter(InputState& in)
{
  swallowTitleInput(in);

  if (!titleItemEnabled(g_titleMenuIndex))
    g_titleMenuIndex = titleFirstEnabledItem();
}

void uiTitleMenuHandle(InputState& in)
{
  if (!titleItemEnabled(g_titleMenuIndex))
    g_titleMenuIndex = titleFirstEnabledItem();

  int move = 0;
  if (in.upOnce || in.leftOnce || in.encoderDelta < 0) move = -1;
  if (in.downOnce || in.rightOnce || in.encoderDelta > 0) move = +1;

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

  const bool activate = in.selectOnce || in.encoderPressOnce;
  if (!activate)
  {
    while (in.kbHasEvent())
      (void)in.kbPop();
    clearInputLatch();
    return;
  }

  switch (g_titleMenuIndex)
  {
    case TITLE_CONTINUE:
    {
      if (saveManagerSaveFileExists())
      {
        playBeep();
        uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
        swallowTitleInput(in);
        return;
      }
      break;
    }

    case TITLE_NEW_PET:
    {
      playBeep();
      saveManagerStartFreshPetFlow();
      swallowTitleInput(in);
      return;
    }

    case TITLE_IMPORT:
    {
      char path[128];
      if (saveManagerImportLatestBubJson(path, sizeof(path)))
      {
        playBeep();
        ui_showMessage("Bub imported");
        g_titleMenuIndex = TITLE_CONTINUE;
        requestFullUIRedraw();
      }
      else
      {
        playBeep();
        ui_showMessage("No valid export");
        requestUIRedraw();
      }

      swallowTitleInput(in);
      return;
    }
  }

  swallowTitleInput(in);
}