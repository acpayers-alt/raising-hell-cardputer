#pragma once
#include "input.h"

enum class MiniGame
{
  NONE = 0,
  FLAPPY_FIREBALL,
  RESURRECTION,
  SIGNAL_RECOVERY,
  VOID_RITUAL,
  CROSSY_ROAD,
  INFERNAL_DODGER,
  ABDUCTION_BEAM,
};

void miniGameExitToReturnUi(bool beginLockout);
void updateMiniGame(const InputState &input);
void drawMiniGame();

void startCrossyRoad();
void updateCrossyRoad(const InputState &input);

// Global state
extern MiniGame currentMiniGame;
extern bool playerWon;

// -----------------------------------------------------------------------------
// Mini-game entry points (implemented in mini_games.cpp)
// -----------------------------------------------------------------------------
// Resurrection Run (side-scroller)
void startResurrectionRun();
void updateResurrectionRun(const InputState &input);
void drawResurrectionRun();

// Signal Recovery — Alien resurrection shooter
void startSignalRecovery();
void updateSignalRecovery(const InputState &input);
void drawSignalRecovery();
bool signalRecoveryIsShowingIntro();
void freeSignalRecoverySprites();

// Void Ritual — Eldritch resurrection timing game
void startVoidRitual();
void updateVoidRitual(const InputState &input);
void drawVoidRitual();
bool voidRitualIsShowingIntro();
void freeVoidRitualSprites();

// Flappy Fireball
void startFlappyFireball();
void updateFlappyFireball(const InputState &input);
void drawFlappyFireball();

// Infernal Dodger
void startInfernalDodger();
void updateInfernalDodger(const InputState &input);
void drawInfernalDodger();

// Abduction Beam
void startAbductionBeam();
void updateAbductionBeam(const InputState &input);
void drawAbductionBeam();
bool abductionBeamIsShowingIntro();

// Implemented in menu_actions.cpp (mini_games.cpp calls this)
void onResurrectionMiniGameResult(bool success);

// Pause Menu Helpers
bool miniGameIsShowingIntro();
void miniGameCancelFromIntro();