#include "menu_actions.h"

#include "app_state.h"
#include "death_state.h"
#include "graphics.h"
#include "input.h"
#include "mini_game_runtime.h"
#include "mini_games.h"
#include "pet.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_input_router.h"
#include "ui_invalidate.h"
#include "ui_runtime.h"
#include "ui_state_settings.h"

// ==================================================================
// MAIN DISPATCHER (thin wrapper)
// ==================================================================
bool handleMenuInput(InputState &in)
{
  const UIState oldState = g_app.uiState;
  uiHandleInput(in);
  return (oldState != g_app.uiState);
}

// ==================================================================
// RESURRECTION MINI-GAME RESULT (called by mini_games.cpp)
// ==================================================================
void onResurrectionMiniGameResult(bool success)
{
  if (success)
  {
    petResurrectFull();

    soundSetVolumeLevel(soundGetVolumeLevel());
    soundResetDeathDirgeLatch();

    currentMiniGame = MiniGame::NONE;
    g_app.inMiniGame = false;
    g_app.gameOver = false;

    requestUIRedraw();
    invalidateBackgroundCache();

    inputForceClear();
    clearInputLatch();
    requestUIRedraw();
    return;
  }

  // Failed: return to death screen
  resetDeathMenu();
  uiActionEnterState(UIState::DEATH, Tab::TAB_PET, true);

  requestUIRedraw();
  invalidateBackgroundCache();

  inputForceClear();
  clearInputLatch();
  requestUIRedraw();
}