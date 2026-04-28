#include "mini_game_runtime.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "app_state.h"
#include "currency.h"
#include "graphics.h"
#include "input.h"
#include "inventory.h"
#include "mg_pause_core.h"
#include "mg_pause_menu.h"
#include "mini_game_assets.h"
#include "mini_game_return_ui.h"
#include "pet.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_defs.h"
#include "ui_runtime.h"

// cleanup hooks implemented in mini_games.cpp
extern void freeImpWaveSprites();
extern void freeFlappyBgCache();
extern void freeFlappyFireballSprites();
extern void freeFlappyPipeSprites();

extern void freeDodgerBgCache();
extern void freeDodgerFireballSprites();
extern void freeDodgerCarSprite();
extern void freeDodgerGoalFrames();
extern void freeDodgerGoreSprite();

extern void freeCrossyZoneSprites();
extern void freeCrossyActorSprites();

extern void freeResRunSprites();

// game update/draw hooks implemented in mini_games.cpp
extern void updateFlappyFireball(const InputState &input);
extern void drawFlappyFireball();

extern void updateCrossyRoad(const InputState &input);
extern void drawCrossyRoad();

extern void updateInfernalDodger(const InputState &input);
extern void drawInfernalDodger();

extern void updateResurrectionRun(const InputState &input);
extern void drawResurrectionRun();

// timers owned by mini_games.cpp, synced on pause/resume
extern uint32_t s_lastStepMs;
extern uint32_t s_dodgerLastStepMs;
extern uint32_t s_dodgerMoveLastMs;
extern uint32_t s_crossyLastLaneMs;
extern uint32_t rr_lastMs;

// Shared runtime state
MiniGame currentMiniGame = MiniGame::NONE;
bool playerWon = false;

static bool s_mgExiting = false;

bool s_acceptArmed = false;
uint32_t s_gameOverMs = 0;

bool s_showReward = false;
char s_rewardMsg[64] = {0};
static uint32_t s_rewardShownAtMs = 0;

static bool s_prevSelectHeld = false;
static uint32_t s_mgInputLockoutUntilMs = 0;

// Repeated-game boredom tracking.
// This is intentionally runtime-only. It nudges players toward variety without
// making game choice a permanent/save-affecting system.
static MiniGame s_lastCompletedMiniGame = MiniGame::NONE;
static uint8_t s_sameMiniGameStreak = 0;
static bool s_showBoredomMessageOnExit = false;

static const uint32_t kRewardAcceptDelayMs = 180;
static const uint32_t kRewardAutoDismissMs = 15000;

void mgSetRewardMessage(const char *msg);

bool mgRewardShowing() { return s_showReward; }

void mgClearRewardState()
{
  s_showReward = false;
  s_rewardMsg[0] = 0;
  s_rewardShownAtMs = 0;
}

bool mgAcceptArmedNow(uint32_t now)
{
  if (!s_acceptArmed && s_gameOverMs != 0 && (int32_t)(now - s_gameOverMs) >= 0)
    s_acceptArmed = true;

  return s_acceptArmed;
}

bool mgRewardAutoDismissNow(uint32_t now)
{
  if (!s_showReward || s_rewardShownAtMs == 0)
    return false;

  return (int32_t)(now - (s_rewardShownAtMs + kRewardAutoDismissMs)) >= 0;
}

void mgResetAcceptState()
{
  s_acceptArmed = false;
  s_gameOverMs = 0;
}

const char *mgRewardMessage() { return s_rewardMsg; }

void mgSetRewardMessage(const char *msg)
{
  if (!msg)
  {
    s_rewardMsg[0] = 0;
    return;
  }

  strlcpy(s_rewardMsg, msg, sizeof(s_rewardMsg));
}

// simple access for per-game files if needed later
bool mgInputLockedOut() { return (int32_t)(millis() - s_mgInputLockoutUntilMs) < 0; }

void mgBeginInputLockout(uint32_t ms) { s_mgInputLockoutUntilMs = millis() + ms; }

bool miniGameEnterOnce(const InputState &input)
{
  const bool held = input.mgSelectHeld;

  if (mgInputLockedOut())
  {
    s_prevSelectHeld = held;
    return false;
  }

  const bool enterOnce = (held && !s_prevSelectHeld);
  s_prevSelectHeld = held;
  return enterOnce || input.mgSelectOnce;
}

static const char *mgItemName(ItemType t)
{
  const char *nm = g_app.inventory.getItemLabelForType(t);
  if (nm && nm[0])
    return nm;

  switch (t)
  {
  case ITEM_SOUL_FOOD:
    return "SOUL FOOD";
  case ITEM_CURSED_RELIC:
    return "CURSED RELIC";
  case ITEM_DEMON_BONE:
    return "DEMON BONE";
  case ITEM_RITUAL_CHALK:
    return "RITUAL CHALK";
  default:
    return "ITEM";
  }
}

static int rollMiniGameInfReward() { return 10; }

static bool tryAwardWinItem_1in4(ItemType *outType)
{
  if (outType)
    *outType = ITEM_NONE;

  if (random(4) != 0)
    return false;

  static const ItemType kRewards[] = {ITEM_SOUL_FOOD, ITEM_CURSED_RELIC, ITEM_DEMON_BONE, ITEM_RITUAL_CHALK};

  const int n = (int)(sizeof(kRewards) / sizeof(kRewards[0]));
  const ItemType t = kRewards[(int)random((long)n)];

  g_app.inventory.addItem(t, 1);
  if (outType)
    *outType = t;
  return true;
}

static const char *mgGameName(MiniGame game)
{
  switch (game)
  {
  case MiniGame::FLAPPY_FIREBALL:
    return "Flappy Fireball";
  case MiniGame::CROSSY_ROAD:
    return "Crossy Road";
  case MiniGame::INFERNAL_DODGER:
    return "Infernal Dodger";
  case MiniGame::RESURRECTION:
    return "Resurrection Run";
  default:
    return "Unknown";
  }
}

static bool mgCountsForRepeatBoredom(MiniGame game)
{
  switch (game)
  {
  case MiniGame::FLAPPY_FIREBALL:
  case MiniGame::CROSSY_ROAD:
  case MiniGame::INFERNAL_DODGER:
    return true;

  default:
    return false;
  }
}

static uint8_t mgRepeatBoredomPenaltyForStreak(uint8_t streak)
{
  if (streak >= 5)
    return 6;
  if (streak == 4)
    return 4;
  if (streak == 3)
    return 2;

  return 0;
}

static bool mgApplyRepeatGameBoredom(MiniGame completedGame, uint8_t *outStreak, uint8_t *outPenalty)
{
  if (outStreak)
    *outStreak = 0;
  if (outPenalty)
    *outPenalty = 0;

  if (!mgCountsForRepeatBoredom(completedGame))
    return false;

  if (completedGame == s_lastCompletedMiniGame)
  {
    if (s_sameMiniGameStreak < 255)
      ++s_sameMiniGameStreak;
  }
  else
  {
    s_lastCompletedMiniGame = completedGame;
    s_sameMiniGameStreak = 1;
  }

  const uint8_t penalty = mgRepeatBoredomPenaltyForStreak(s_sameMiniGameStreak);

  if (outStreak)
    *outStreak = s_sameMiniGameStreak;
  if (outPenalty)
    *outPenalty = penalty;

  if (penalty == 0)
    return false;

  const int oldHappy = pet.happiness;
  pet.happiness = constrain(pet.happiness - (int)penalty, 0, 100);

  Serial.printf("[PET] repeat game boredom game=%s streak=%u happiness %d->%d penalty=%u\n", mgGameName(completedGame),
                (unsigned)s_sameMiniGameStreak, oldHappy, pet.happiness, (unsigned)penalty);

  return true;
}

void mgApplyResultAndShowReward(bool won)
{
  if (currentMiniGame == MiniGame::RESURRECTION)
  {
    if (won)
    {
      if (pet.type == PET_ELDRITCH)
      {
        snprintf(s_rewardMsg, sizeof(s_rewardMsg), "The darkness has returned\nYou may return to life");
      }
      else
      {
        snprintf(s_rewardMsg, sizeof(s_rewardMsg), "Fall of Man has begun\nYou may return to life");
      }
    }
    else
    {
      snprintf(s_rewardMsg, sizeof(s_rewardMsg), "Resurrection failed\nDeath still holds you");
    }

    saveManagerMarkDirty();
    s_showReward = true;
    s_rewardShownAtMs = millis();
    g_app.gameOver = false;
    requestUIRedraw();
    return;
  }

  if (won)
  {
    pet.addXP(20);

    const int infReward = rollMiniGameInfReward();
    addInf(infReward);

    pet.happiness = constrain(pet.happiness + 20, 0, 100);

    ItemType rewardType = ITEM_NONE;
    const bool wonItem = tryAwardWinItem_1in4(&rewardType);

    if (wonItem)
    {
      const char *nm = mgItemName(rewardType);
      snprintf(s_rewardMsg, sizeof(s_rewardMsg), "You win! XP +20  INF +%d  MOOD +20\nRandom Reward: %s +1", infReward,
               (nm && nm[0]) ? nm : "ITEM");
    }
    else
    {
      snprintf(s_rewardMsg, sizeof(s_rewardMsg), "You win! XP +20  INF +%d  MOOD +20", infReward);
    }
  }
  else
  {
    pet.addXP(5);
    pet.happiness = constrain(pet.happiness + 10, 0, 100);
    snprintf(s_rewardMsg, sizeof(s_rewardMsg), "You lose! XP +5  MOOD +10");
  }

  uint8_t repeatStreak = 0;
  uint8_t boredomPenalty = 0;
  const bool boredomApplied = mgApplyRepeatGameBoredom(currentMiniGame, &repeatStreak, &boredomPenalty);

  if (boredomApplied && repeatStreak == 5)
  {
    s_showBoredomMessageOnExit = true;
  }

  saveManagerMarkDirty();

  s_showReward = true;
  s_rewardShownAtMs = millis();
  g_app.gameOver = false;

  requestUIRedraw();
}

void miniGameExitToReturnUi(bool beginLockout)
{
  s_mgExiting = true;

  s_showReward = false;
  s_rewardMsg[0] = 0;

  UIState target = miniGameGetReturnUiOrDefault(UIState::PET_SCREEN);
  Tab targetTab = miniGameGetReturnTabOrDefault(Tab::TAB_PET);

  if (target == UIState::MINI_GAME || target == UIState::MG_PAUSE)
  {
    target = UIState::PET_SCREEN;
    targetTab = Tab::TAB_PET;
  }

  miniGameClearReturnUi();

  uiActionEnterState(target, targetTab, true);

  g_app.inMiniGame = false;
  g_app.gameOver = false;
  playerWon = false;

  mgAssetsEndSession(currentMiniGame, "miniGameExitToReturnUi");
  mgmem::endSession();
  currentMiniGame = MiniGame::NONE;

  mgPauseReset();
  clearInputLatch();
  inputForceClear();
  s_prevSelectHeld = false;

  freeImpWaveSprites();

  freeFlappyBgCache();
  freeFlappyFireballSprites();
  freeFlappyPipeSprites();

  freeDodgerBgCache();
  freeDodgerFireballSprites();
  freeDodgerCarSprite();
  freeDodgerGoalFrames();
  freeDodgerGoreSprite();

  freeCrossyZoneSprites();
  freeCrossyActorSprites();

  freeResRunSprites();

  invalidateBackgroundCache();
  requestFullUIRedraw();

  if (s_showBoredomMessageOnExit)
  {
    s_showBoredomMessageOnExit = false;

    char msg[64];
    snprintf(msg, sizeof(msg), "%s is bored of this game.", pet.getName());

    ui_showTimedMessage(msg, 1800);
  }

  if (beginLockout)
    mgBeginInputLockout(220);
}

void mgSyncGameTimebases(uint32_t now)
{
  switch (currentMiniGame)
  {
  case MiniGame::FLAPPY_FIREBALL:
    s_lastStepMs = now;
    break;

  case MiniGame::INFERNAL_DODGER:
    s_dodgerLastStepMs = now;
    s_dodgerMoveLastMs = now;
    break;

  case MiniGame::CROSSY_ROAD:
    s_crossyLastLaneMs = now;
    break;

  case MiniGame::RESURRECTION:
    rr_lastMs = now;
    break;

  default:
    break;
  }
}

void updateMiniGame(const InputState &input)
{
  if (g_app.uiState != UIState::MINI_GAME)
    return;
  if (!g_app.inMiniGame)
    return;
  if (currentMiniGame == MiniGame::NONE)
    return;

  const uint32_t now = millis();

  const uint8_t p = mgPauseHandle(input);
  mgPauseUpdateClocks(now);

  if (p == MGPAUSE_EXIT)
  {
    miniGameExitToReturnUi(true);
    requestUIRedraw();
    return;
  }

  if (p == MGPAUSE_CONSUME)
  {
    if (mgPauseIsPaused())
      mgSyncGameTimebases(now);

    requestUIRedraw();
    return;
  }

  if (mgPauseIsPaused())
  {
    mgSyncGameTimebases(now);
    requestUIRedraw();
    return;
  }

  switch (currentMiniGame)
  {
  case MiniGame::FLAPPY_FIREBALL:
    updateFlappyFireball(input);
    break;

  case MiniGame::CROSSY_ROAD:
    updateCrossyRoad(input);
    break;

  case MiniGame::INFERNAL_DODGER:
    updateInfernalDodger(input);
    break;

  case MiniGame::RESURRECTION:
    updateResurrectionRun(input);
    break;

  default:
    break;
  }

  requestUIRedraw();
}

void drawMiniGame()
{
  if (g_app.uiState != UIState::MINI_GAME)
    return;
  if (!g_app.inMiniGame)
    return;
  if (currentMiniGame == MiniGame::NONE)
    return;

  const uint32_t now = millis();
  mgPauseUpdateClocks(now);

  switch (currentMiniGame)
  {
  case MiniGame::FLAPPY_FIREBALL:
    drawFlappyFireball();
    break;

  case MiniGame::CROSSY_ROAD:
    drawCrossyRoad();
    break;

  case MiniGame::INFERNAL_DODGER:
    drawInfernalDodger();
    break;

  case MiniGame::RESURRECTION:
    drawResurrectionRun();
    break;

  default:
    break;
  }

  if (mgPauseIsPaused())
    mgDrawPauseOverlay();
}