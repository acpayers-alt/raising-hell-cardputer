#include "ui_state_title_menu.h"

#include <Arduino.h>

#include "settings_flow_state.h"
#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "settings_flow_state.h"

int g_titleMenuIndex = 0;

namespace
{
constexpr int kTitleMenuCount = 3;

enum TitleMenuItem : int {
  TITLE_CONTINUE = 0,
  TITLE_IMPORT   = 1,
  TITLE_SETTINGS = 2,
};

static bool titleItemEnabled(int idx)
{
  switch (idx)
  {
    case TITLE_CONTINUE: return saveManagerSaveFileExists();
    case TITLE_IMPORT:   return saveManagerHasImportableBubJson();
    case TITLE_SETTINGS: return true;
    default:             return false;
  }
}

static int titleFirstEnabledItem()
{
  if (titleItemEnabled(TITLE_CONTINUE)) return TITLE_CONTINUE;
  if (titleItemEnabled(TITLE_IMPORT))   return TITLE_IMPORT;
  if (titleItemEnabled(TITLE_SETTINGS)) return TITLE_SETTINGS;
  return TITLE_SETTINGS;
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

    case TITLE_IMPORT:
    {
      playBeep();
      uiActionEnterState(UIState::IMPORT_PET_LIST, Tab::TAB_PET, true);
      swallowTitleInput(in);
      return;
    }
        
    case TITLE_SETTINGS:
    {
      playBeep();
      openSettingsWithReturn(UIState::TITLE_MENU, Tab::TAB_PET);
      swallowTitleInput(in);
      return;
    }
  }

  swallowTitleInput(in);

  Serial.printf("[TITLE] idx=%d continue=%d import=%d settings=%d\n",
                g_titleMenuIndex,
                saveManagerSaveFileExists() ? 1 : 0,
                saveManagerHasImportableBubJson() ? 1 : 0,
                1);
}