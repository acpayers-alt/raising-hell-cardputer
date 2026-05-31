#include "ui_state_mini_game.h"

#include <Arduino.h>

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "mg_pause_core.h"
#include "mini_games.h"
#include "ui_actions.h"
#include "ui_runtime.h"

void uiMiniGameHandle(InputState &in)
{
  if (!g_app.inMiniGame)
  {
    uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, false, in, 150);
    requestFullUIRedraw();
    return;
  }

  // ESC during a mini-game intro cancels the mini-game instead of pausing.
  if (miniGameIsShowingIntro() && currentMiniGame != MiniGame::RESURRECTION &&
      currentMiniGame != MiniGame::SIGNAL_RECOVERY && currentMiniGame != MiniGame::VOID_RITUAL && in.mgQuitOnce)
  {
    miniGameCancelFromIntro();
    requestFullUIRedraw();
    return;
  }

  // Pause-gate handles ESC toggle and pause menu interaction.
  const MgPauseGateResult gate = mgPauseGateHandle(in);

  if (gate == MgPauseGateResult::MG_GATE_EXIT)
  {
    miniGameExitToReturnUi(true);
    requestFullUIRedraw();
    return;
  }

  if (gate == MgPauseGateResult::MG_GATE_SKIP)
  {
    if (mgPauseIsPaused())
    {
      uiActionEnterStateClean(UIState::MG_PAUSE, g_app.currentTab, false, in, 150);
      requestFullUIRedraw();
    }
    return;
  }

  updateMiniGame(in);
  // drawMiniGame();  // Do not draw directly here.
  // updateMiniGame() requests redraw; renderUI() owns drawing.

  if (mgPauseIsPaused())
  {
    uiActionEnterStateClean(UIState::MG_PAUSE, g_app.currentTab, false, in, 150);
    requestFullUIRedraw();
  }
}