#pragma once
#include "input.h"

enum class MiniGame {
  NONE = 0,
  FLAPPY_FIREBALL,
  RESURRECTION,
  CROSSY_ROAD,
  INFERNAL_DODGER,
};

void miniGameExitToReturnUi(bool beginLockout);
void updateMiniGame(const InputState& input);
void drawMiniGame();

void startCrossyRoad();
void updateCrossyRoad(const InputState& input);

// Global state
extern MiniGame currentMiniGame;
extern bool playerWon;

// -----------------------------------------------------------------------------
// Mini-game entry points (implemented in mini_games.cpp)
// -----------------------------------------------------------------------------
// Resurrection Run (side-scroller)
void startResurrectionRun();
void updateResurrectionRun(const InputState& input);
void drawResurrectionRun();

// Flappy Fireball
void startFlappyFireball();
void updateFlappyFireball(const InputState& input);
void drawFlappyFireball();

// Infernal Dodger
void startInfernalDodger();
void updateInfernalDodger(const InputState& input);
void drawInfernalDodger();

// Implemented in menu_actions.cpp (mini_games.cpp calls this)
void onResurrectionMiniGameResult(bool success);

//Pause Menu Helpers
bool miniGameIsShowingIntro();
void miniGameCancelFromIntro();