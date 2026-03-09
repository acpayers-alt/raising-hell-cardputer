#pragma once

#include <stdint.h>

#include "mini_games.h"
#include "input.h"

extern MiniGame currentMiniGame;
extern bool playerWon;

// Shared mini-game runtime helpers
void updateMiniGame(const InputState& input);
void drawMiniGame();

void miniGameExitToReturnUi(bool beginLockout = true);
void mgSyncGameTimebases(uint32_t now);

bool mgInputLockedOut();
void mgBeginInputLockout(uint32_t ms);
bool miniGameEnterOnce(const InputState& input);

bool mgRewardShowing();
void mgClearRewardState();
bool mgAcceptArmedNow(uint32_t now);
void mgResetAcceptState();
const char* mgRewardMessage();

bool mgRewardShowing();
void mgClearRewardState();
bool mgAcceptArmedNow(uint32_t now);
void mgResetAcceptState();
const char* mgRewardMessage();
void mgApplyResultAndShowReward(bool won);