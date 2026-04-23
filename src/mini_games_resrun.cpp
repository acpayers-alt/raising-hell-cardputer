#include "mini_games_internal.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "mg_pause_core.h"
#include "mini_game_assets.h"
#include "mini_game_return_ui.h"
#include "mini_game_runtime.h"

#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "pet.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

static const uint16_t kResRunKey = 0x0841;

// -----------------------------------------------------------------------------
// Resurrection Run (side-scroller runner) GLOBALS
// -----------------------------------------------------------------------------
static bool s_rrShowIntro = true;
static uint32_t s_rrIntroAnimMs = 0;
bool resRunIsShowingIntro() { return s_rrShowIntro; }

static bool rr_active = false;
static bool rr_gameOver = false;
static bool rr_won = false;
static bool rr_ducking = false;

static float rr_y = 0.0f;
static float rr_vy = 0.0f;
static bool rr_onGround = true;

static int rr_distance = 0;
uint32_t rr_lastMs = 0;

static bool rr_boosting = false;
static uint32_t rr_boostEndMs = 0;
static uint32_t rr_boostCooldownEndMs = 0;

static constexpr int kRrBaseSpeed = 290;
static constexpr int kRrBoostSpeed = 430;
static constexpr uint32_t kRrBoostMs = 180;
static constexpr uint32_t kRrBoostCooldownMs = 600;

static int s_rrSnakeCrouchW = 0;
static int s_rrSnakeCrouchH = 0;
static int s_rrSnakeJumpW = 0;
static int s_rrSnakeJumpH = 0;
static const int kRrFlyingBugVisualLift = 16;
static const int kRrFlyingBugBob = 2;

static M5Canvas *s_rrSnakeRun1Spr = nullptr;
static M5Canvas *s_rrSnakeRun2Spr = nullptr;
static M5Canvas *s_rrSnakeCrouchSpr = nullptr;
static M5Canvas *s_rrSnakeJumpSpr = nullptr;
static M5Canvas *s_rrSnakeWin1Spr = nullptr;
static M5Canvas *s_rrSnakeWin2Spr = nullptr;

static M5Canvas *s_rrGroundSpr = nullptr;
static M5Canvas *s_rrHand1Spr = nullptr;
static M5Canvas *s_rrHand2Spr = nullptr;
static M5Canvas *s_rrLadybugGroundSpr = nullptr;
static M5Canvas *s_rrLadybugFly1Spr = nullptr;
static M5Canvas *s_rrLadybugFly2Spr = nullptr;

static void rrInitStars();
static void rrResetObstacles();

static inline void rrExitMiniGameToReturnUi(bool beginLockout = true)
{
  freeResRunSprites();
  mgmem::endSession();

  rr_active = false;
  currentMiniGame = MiniGame::NONE;
  g_app.inMiniGame = false;
  g_app.gameOver = false;

  miniGameExitToReturnUi(beginLockout);
}

struct RrStar
{
  int16_t x;
  int16_t y;
  uint8_t phase;
  uint8_t kind;
};

static constexpr int kRrStarCount = 18;
static RrStar s_rrStars[kRrStarCount];

enum RRPhase : uint8_t
{
  RR_PHASE_RUN = 0,
  RR_PHASE_HAND_ENTER,
  RR_PHASE_HAND_HOLD,
  RR_PHASE_HAND_CONTACT,
  RR_PHASE_HAND_EXIT,
  RR_PHASE_WIN_HOLD
};

static RRPhase s_rrPhase = RR_PHASE_RUN;
static uint32_t s_rrPhaseStartMs = 0;

static bool s_rrHandActive = false;
static bool s_rrHandTouched = false;
static int s_rrHandX = 0;
static int s_rrHandY = 0;

static constexpr int kRrHandTriggerDist = 7200;
static constexpr int kRrHandEnterSpeed = 2;
static constexpr int kRrHandExitSpeed = 3;
static constexpr uint32_t kRrHandHoldMs = 250;
static constexpr uint32_t kRrHandContactHoldMs = 350;
static constexpr uint32_t kRrWinHoldMs = 500;

static void resRunLogState(const char *tag)
{
  Serial.printf("[RESRUN] %s intro=%d phase=%d gameOver=%d won=%d dist=%d free=%u largest=%u\n", tag ? tag : "state",
                s_rrShowIntro ? 1 : 0, (int)s_rrPhase, g_app.gameOver ? 1 : 0, playerWon ? 1 : 0, rr_distance,
                (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static const char *resRunSnakeCrouchPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/worm_crouch.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/snake_crouch.png";
  }
}

static const char *resRunSnakeJumpPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/worm_jump.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/snake_jump.png";
  }
}

static const char *resRunIntroLine1ForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Up/Down to Jump/Duck G to boost";
  case PET_DEVIL:
  default:
    return "Up/Down to Jump/Duck G to boost";
  }
}

static const char *resRunIntroLine2ForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Deliver the tome to the cult";
  case PET_DEVIL:
  default:
    return "Deliver the apple to our ally";
  }
}

struct RRObs
{
  int x;
  int y;
  int w;
  int h;
  bool active;
  bool flying;
};

static RRObs rr_obs[8];
static int rr_courseLen = 7800;

struct RRSpawn
{
  int triggerDist;
  uint8_t type;
  uint8_t param;
};

static const uint8_t RR_SPIKE = 0;
static const uint8_t RR_LOW_FIRE = 1;
static const uint8_t RR_FLY_BUG = 2;

static const RRSpawn rr_script[] = {
    // --- pass 1 ---
    {520, RR_SPIKE, 0},
    {860, RR_LOW_FIRE, 0},
    {1080, RR_FLY_BUG, 0},
    {1320, RR_SPIKE, 0},
    {1540, RR_LOW_FIRE, 0},
    {1760, RR_FLY_BUG, 0},
    {1980, RR_SPIKE, 0},
    {2220, RR_LOW_FIRE, 0},
    {2380, RR_FLY_BUG, 0},

    // --- pass 2 ---
    {520 + 2600, RR_SPIKE, 0},
    {860 + 2600, RR_LOW_FIRE, 0},
    {1080 + 2600, RR_FLY_BUG, 0},
    {1320 + 2600, RR_SPIKE, 0},
    {1540 + 2600, RR_LOW_FIRE, 0},
    {1760 + 2600, RR_FLY_BUG, 0},
    {1980 + 2600, RR_SPIKE, 0},
    {2220 + 2600, RR_LOW_FIRE, 0},
    {2380 + 2600, RR_FLY_BUG, 0},

    // --- pass 3 ---
    {520 + 5200, RR_SPIKE, 0},
    {860 + 5200, RR_LOW_FIRE, 0},
    {1080 + 5200, RR_FLY_BUG, 0},
    {1320 + 5200, RR_SPIKE, 0},
    {1540 + 5200, RR_LOW_FIRE, 0},
    {1760 + 5200, RR_FLY_BUG, 0},
    {1980 + 5200, RR_SPIKE, 0},
    {2220 + 5200, RR_LOW_FIRE, 0},
    {2380 + 5200, RR_FLY_BUG, 0},
};

static const int rr_scriptCount = (int)(sizeof(rr_script) / sizeof(rr_script[0]));
static int rr_nextSpawn = 0;

// -----------------------------------------------------------------------------
// Resurrection Run visual/layout tuning
// -----------------------------------------------------------------------------
static constexpr int kRrSkyH = 64;
static constexpr int kRrGroundH = 28;
static constexpr int kRrGroundInset = 6;

static constexpr int kRrPlayerX = 44;
static constexpr int kRrPlayerW = 48;
static constexpr int kRrPlayerH = 24;
static constexpr int kRrPlayerDuckH = 16;

static constexpr int kRrJumpObsW = 44;
static constexpr int kRrJumpObsH = 30;

static constexpr int kRrDuckObsW = 52;
static constexpr int kRrDuckObsH = 24;
static constexpr int kRrDuckObsClearance = 8;

// Collision tuning (smaller than drawn sprites)
static constexpr int kRrPlayerHitInsetX = 10;
static constexpr int kRrPlayerHitInsetY = 4;

static constexpr int kRrJumpObsHitInsetX = 8;
static constexpr int kRrJumpObsHitInsetY = 8;

static constexpr int kRrDuckObsHitInsetX = 10;
static constexpr int kRrDuckObsHitInsetY = 6;

static int s_rrSnakeW = 0;
static int s_rrSnakeH = 0;
static int s_rrGroundW = 0;
static int s_rrGroundH = 0;

static int s_rrHandW = 0;
static int s_rrHandH = 0;
static int s_rrLadybugW = 0;
static int s_rrLadybugH = 0;

static uint32_t s_rrHandAnimMs = 0;
static uint8_t s_rrHandAnimFrame = 0;

static uint32_t s_rrLadybugAnimMs = 0;
static uint8_t s_rrLadybugAnimFrame = 0;

static uint32_t s_rrAnimMs = 0;
static uint8_t s_rrAnimFrame = 0;

static int s_rrPlayerGoalOffsetX = 0;
static constexpr int kRrGoalWalkSpeed = 2;

static const char *resRunHand1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/cult_hand1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/hand_1.png";
  }
}

static const char *resRunHand2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/cult_hand2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/hand_2.png";
  }
}

static const char *resRunLadybugGroundPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/rat_ground.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/ladybug_ground.png";
  }
}

static const char *resRunLadybugFly1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/bat_fly1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/ladybug_fly1.png";
  }
}

static const char *resRunLadybugFly2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/bat_fly2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/ladybug_fly2.png";
  }
}

static const char *resRunSnakeRun1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/worm_run1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/snake_run1.png";
  }
}

static const char *resRunSnakeRun2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/worm_run2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/snake_run2.png";
  }
}

static const char *resRunSnakeWin1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/worm_win1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/snake_win1.png";
  }
}

static const char *resRunSnakeWin2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/worm_win2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/snake_win2.png";
  }
}

static const char *resRunBranchGroundPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/dock_ground.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/branch_ground.png";
  }
}

static bool ensureResRunHandSprites()
{
  s_rrHand1Spr = nullptr;
  s_rrHand2Spr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "hand_1", resRunHand1PathForPet(), 8, kResRunKey, s_rrHand1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "hand_2", resRunHand2PathForPet(), 8, kResRunKey, s_rrHand2Spr))
    return false;

  if (!s_rrHand1Spr || s_rrHand1Spr->width() <= 0 || s_rrHand1Spr->height() <= 0)
    return false;

  s_rrHandW = (int)s_rrHand1Spr->width();
  s_rrHandH = (int)s_rrHand1Spr->height();
  return true;
}

static bool ensureResRunLadybugSprites()
{
  s_rrLadybugGroundSpr = nullptr;
  s_rrLadybugFly1Spr = nullptr;
  s_rrLadybugFly2Spr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_ground", resRunLadybugGroundPathForPet(), 8, kResRunKey,
                           s_rrLadybugGroundSpr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_fly1", resRunLadybugFly1PathForPet(), 8, kResRunKey,
                           s_rrLadybugFly1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_fly2", resRunLadybugFly2PathForPet(), 8, kResRunKey,
                           s_rrLadybugFly2Spr))
    return false;

  if (!s_rrLadybugGroundSpr || s_rrLadybugGroundSpr->width() <= 0 || s_rrLadybugGroundSpr->height() <= 0)
    return false;

  s_rrLadybugW = (int)s_rrLadybugGroundSpr->width();
  s_rrLadybugH = (int)s_rrLadybugGroundSpr->height();
  return true;
}

static bool ensureResRunSnakeSprites()
{
  s_rrSnakeRun1Spr = nullptr;
  s_rrSnakeRun2Spr = nullptr;
  s_rrSnakeCrouchSpr = nullptr;
  s_rrSnakeJumpSpr = nullptr;
  s_rrSnakeWin1Spr = nullptr;
  s_rrSnakeWin2Spr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run1", resRunSnakeRun1PathForPet(), 8, kResRunKey,
                           s_rrSnakeRun1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run2", resRunSnakeRun2PathForPet(), 8, kResRunKey,
                           s_rrSnakeRun2Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_crouch", resRunSnakeCrouchPathForPet(), 8, kResRunKey,
                           s_rrSnakeCrouchSpr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_jump", resRunSnakeJumpPathForPet(), 8, kResRunKey,
                           s_rrSnakeJumpSpr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_win1", resRunSnakeWin1PathForPet(), 8, kResRunKey,
                           s_rrSnakeWin1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_win2", resRunSnakeWin2PathForPet(), 8, kResRunKey,
                           s_rrSnakeWin2Spr))
    return false;

  if (!s_rrSnakeRun1Spr || s_rrSnakeRun1Spr->width() <= 0 || s_rrSnakeRun1Spr->height() <= 0)
    return false;

  s_rrSnakeW = (int)s_rrSnakeRun1Spr->width();
  s_rrSnakeH = (int)s_rrSnakeRun1Spr->height();

  if (s_rrSnakeCrouchSpr && s_rrSnakeCrouchSpr->width() > 0 && s_rrSnakeCrouchSpr->height() > 0)
  {
    s_rrSnakeCrouchW = (int)s_rrSnakeCrouchSpr->width();
    s_rrSnakeCrouchH = (int)s_rrSnakeCrouchSpr->height();
  }

  if (s_rrSnakeJumpSpr && s_rrSnakeJumpSpr->width() > 0 && s_rrSnakeJumpSpr->height() > 0)
  {
    s_rrSnakeJumpW = (int)s_rrSnakeJumpSpr->width();
    s_rrSnakeJumpH = (int)s_rrSnakeJumpSpr->height();
  }

  return true;
}

static bool ensureResRunGroundSprite()
{
  s_rrGroundSpr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "branch_ground", resRunBranchGroundPathForPet(), 8, kResRunKey,
                           s_rrGroundSpr))
    return false;

  if (!s_rrGroundSpr || s_rrGroundSpr->width() <= 0 || s_rrGroundSpr->height() <= 0)
    return false;

  s_rrGroundW = (int)s_rrGroundSpr->width();
  s_rrGroundH = (int)s_rrGroundSpr->height();
  return true;
}

void freeResRunSprites()
{
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_run1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_run2");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_crouch");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_jump");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "branch_ground");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "hand_1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "hand_2");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "ladybug_ground");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "ladybug_fly1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "ladybug_fly2");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_win1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_win2");

  s_rrSnakeRun1Spr = nullptr;
  s_rrSnakeRun2Spr = nullptr;
  s_rrSnakeCrouchSpr = nullptr;
  s_rrSnakeJumpSpr = nullptr;
  s_rrSnakeWin1Spr = nullptr;
  s_rrSnakeWin2Spr = nullptr;
  s_rrGroundSpr = nullptr;
  s_rrHand1Spr = nullptr;
  s_rrHand2Spr = nullptr;
  s_rrLadybugGroundSpr = nullptr;
  s_rrLadybugFly1Spr = nullptr;
  s_rrLadybugFly2Spr = nullptr;

  s_rrSnakeW = 0;
  s_rrSnakeH = 0;
  s_rrSnakeCrouchW = 0;
  s_rrSnakeCrouchH = 0;
  s_rrSnakeJumpW = 0;
  s_rrSnakeJumpH = 0;
  s_rrGroundW = 0;
  s_rrGroundH = 0;
  s_rrHandW = 0;
  s_rrHandH = 0;
  s_rrLadybugW = 0;
  s_rrLadybugH = 0;
}

static void rrResetRunState()
{
  rr_active = true;
  rr_gameOver = false;
  rr_won = false;
  rr_ducking = false;

  rr_distance = 0;
  rr_lastMs = millis();

  rr_y = 0.0f;
  rr_vy = 0.0f;
  rr_onGround = true;

  rr_courseLen = 7800;
  rrResetObstacles();
  rr_nextSpawn = 0;

  rr_boosting = false;
  rr_boostEndMs = 0;
  rr_boostCooldownEndMs = 0;

  s_rrPhase = RR_PHASE_RUN;
  s_rrPhaseStartMs = millis();

  s_rrHandActive = false;
  s_rrHandTouched = false;
  s_rrHandX = (screenW > 0) ? screenW : 240;
  s_rrHandY = ((screenH > 0) ? screenH : 135) - kRrGroundH - s_rrHandH + 8;
  s_rrPlayerGoalOffsetX = 0;

  rrInitStars();
}

static void rrFinishRun(bool won)
{
  if (rr_gameOver)
    return;

  rr_gameOver = true;
  rr_won = won;
  rr_active = false;

  g_app.gameOver = true;
  playerWon = won;
  if (won)
  soundWin();
  s_resultShown = true;

  if (!won)
    soundError();

  mgmem::logUsage(won ? "rr finish win" : "rr finish loss");
  resRunLogState(won ? "win" : "lose");
  requestUIRedraw();
}

static void rrSpawnObstacle(uint8_t type)
{
  const int w = (screenW > 0) ? screenW : 240;
  const int h = (screenH > 0) ? screenH : 135;

  int slot = -1;
  for (int i = 0; i < (int)(sizeof(rr_obs) / sizeof(rr_obs[0])); i++)
  {
    if (!rr_obs[i].active)
    {
      slot = i;
      break;
    }
  }
  if (slot < 0)
    return;

  const int spawnScreenLead = 72;
  const int spawnWorldX = rr_distance + w + spawnScreenLead;
  const int groundY = h - kRrGroundH;

  RRObs &o = rr_obs[slot];
  o.x = spawnWorldX;
  o.active = true;
  o.flying = false;

  if (type == RR_SPIKE)
  {
    o.y = groundY - kRrJumpObsH;
    o.w = kRrJumpObsW;
    o.h = kRrJumpObsH;
    o.flying = false;
  }
  else if (type == RR_FLY_BUG)
  {
    o.y = groundY - kRrPlayerH - kRrDuckObsClearance;
    o.w = kRrDuckObsW;
    o.h = kRrDuckObsH;
    o.flying = true;
  }
  else
  {
    o.y = groundY - kRrPlayerH - kRrDuckObsClearance;
    o.w = kRrDuckObsW;
    o.h = kRrDuckObsH;
    o.flying = false;
  }
}

static void rrResetObstacles()
{
  for (auto &o : rr_obs)
    o = {0, 0, 0, 0, false, false};
}

static void rrInitStars()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;
  const int groundY = gH - kRrGroundH;

  for (int i = 0; i < kRrStarCount; ++i)
  {
    s_rrStars[i].x = (int16_t)random(0, gW);
    s_rrStars[i].y = (int16_t)random(4, groundY - 6);
    s_rrStars[i].phase = (uint8_t)random(0, 64);
    s_rrStars[i].kind = (uint8_t)random(0, 4);
  }
}

static void rrDrawEldritchStars(int groundY, uint32_t now)
{
  if (pet.type != PET_ELDRITCH)
    return;

  const uint32_t tick = now / 90U;

  for (int i = 0; i < kRrStarCount; ++i)
  {
    const RrStar &s = s_rrStars[i];

    if (s.y < 0 || s.y >= groundY)
      continue;

    const uint8_t t = (uint8_t)((tick + s.phase) & 31U);
    const uint8_t glow = (t < 16U) ? t : (31U - t);

    if (glow < 2)
      continue;

    uint16_t c;
    if (glow < 5)
      c = spr.color565(90, 90, 110);
    else if (glow < 9)
      c = spr.color565(150, 150, 185);
    else
      c = spr.color565(230, 230, 255);

    if (s.kind == 0)
    {
      spr.drawPixel(s.x, s.y, c);
    }
    else if (s.kind == 1)
    {
      spr.drawPixel(s.x, s.y, c);
      if (glow >= 8)
      {
        if (s.x > 0)
          spr.drawPixel(s.x - 1, s.y, c);
        if (s.x + 1 < ((screenW > 0) ? screenW : 240))
          spr.drawPixel(s.x + 1, s.y, c);
      }
    }
    else if (s.kind == 2)
    {
      spr.drawPixel(s.x, s.y, c);
      if (glow >= 8)
      {
        if (s.y > 0)
          spr.drawPixel(s.x, s.y - 1, c);
        if (s.y + 1 < groundY)
          spr.drawPixel(s.x, s.y + 1, c);
      }
    }
    else
    {
      spr.drawPixel(s.x, s.y, c);
      if (glow >= 10)
      {
        if (s.x > 0)
          spr.drawPixel(s.x - 1, s.y, c);
        if (s.x + 1 < ((screenW > 0) ? screenW : 240))
          spr.drawPixel(s.x + 1, s.y, c);
        if (s.y > 0)
          spr.drawPixel(s.x, s.y - 1, c);
        if (s.y + 1 < groundY)
          spr.drawPixel(s.x, s.y + 1, c);
      }
    }
  }
}

static bool rrAabb(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
  return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}

void startResurrectionRun()
{
  mgPauseReset();
  inputSetTextCapture(false);
  soundSetVolumeLevel(soundGetVolumeLevel());

  currentMiniGame = MiniGame::RESURRECTION;
  graphicsReleaseUiCachesForMiniGame();
  mgAssetsBeginSession(currentMiniGame, "startResurrectionRun");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("rr beginSession");
  freeResRunSprites();

  miniGameSetReturnUi(UIState::DEATH, Tab::TAB_PET);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  const bool snakeOk = ensureResRunSnakeSprites();
  const bool groundOk = ensureResRunGroundSprite();
  const bool handOk = ensureResRunHandSprites();
  const bool ladybugOk = ensureResRunLadybugSprites();

  Serial.printf("[RESRUN] preload: snake=%d ground=%d hand=%d ladybug=%d run=%dx%d crouch=%dx%d jump=%dx%d "
                "hand=%dx%d bug=%dx%d free=%u largest=%u\n",
                snakeOk ? 1 : 0, groundOk ? 1 : 0, handOk ? 1 : 0, ladybugOk ? 1 : 0, s_rrSnakeW, s_rrSnakeH,
                s_rrSnakeCrouchW, s_rrSnakeCrouchH, s_rrSnakeJumpW, s_rrSnakeJumpH, s_rrHandW, s_rrHandH, s_rrLadybugW,
                s_rrLadybugH, (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;
  s_resultShown = false;

  mgClearRewardState();
  mgResetAcceptState();

  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  rrResetRunState();

  mgmem::logUsage("rr ready");
  resRunLogState("start-complete");

  invalidateBackgroundCache();
  s_rrShowIntro = true;
  s_rrIntroAnimMs = millis();
  s_rrAnimMs = millis();
  s_rrAnimFrame = 0;
  s_rrHandAnimMs = millis();
  s_rrHandAnimFrame = 0;
  s_rrLadybugAnimMs = millis();
  s_rrLadybugAnimFrame = 0;
  requestUIRedraw();
}

void updateResurrectionRun(const InputState &input)
{
  const uint32_t now = millis();

  if ((uint32_t)(now - s_rrHandAnimMs) >= 220)
  {
    s_rrHandAnimMs = now;
    s_rrHandAnimFrame ^= 1;
  }

  if ((uint32_t)(now - s_rrLadybugAnimMs) >= 160)
  {
    s_rrLadybugAnimMs = now;
    s_rrLadybugAnimFrame ^= 1;
  }

  if ((uint32_t)(now - s_rrAnimMs) >= 120)
  {
    s_rrAnimMs = now;
    s_rrAnimFrame ^= 1;
  }

  if (rr_boosting && (int32_t)(now - rr_boostEndMs) >= 0)
    rr_boosting = false;

  const bool enterOnce = miniGameEnterOnce(input);

  if (mgRewardShowing())
  {
    const uint32_t now = millis();
    if ((enterOnce && !mgInputLockedOut()) || mgRewardAutoDismissNow(now))
    {
      mgClearRewardState();
      mgResetAcceptState();

      mgmem::logUsage(rr_won ? "rr accept win" : "rr accept loss");
      resRunLogState(rr_won ? "accept-win" : "accept-loss");

      onResurrectionMiniGameResult(rr_won);
      rrExitMiniGameToReturnUi(true);
    }
    return;
  }

  if (g_app.gameOver)
  {
    mgApplyResultAndShowReward(playerWon);
    mgResetAcceptState();
    mgBeginInputLockout(180);
    clearInputLatch();
    inputForceClear();
    requestUIRedraw();
    return;
  }

  if (rr_gameOver)
    return;

  if (s_rrShowIntro)
  {
    if (input.mgQuitOnce && !mgInputLockedOut())
    {
      miniGameCancelFromIntro();
      return;
    }

    const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;

    if (startPressed && !mgInputLockedOut())
    {
      s_rrShowIntro = false;
      rr_lastMs = now;

      resRunLogState("intro-dismissed");

      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  uint32_t dtMs = now - rr_lastMs;
  rr_lastMs = now;
  if (dtMs > 40)
    dtMs = 40;
  const float dt = dtMs / 1000.0f;

  rr_ducking = input.mgDownHeld;

  const bool jumpOnce = input.mgUpOnce;
  const bool jumpHeld = input.mgUpHeld;
  const bool boostOnce = input.mgSelectOnce;

  if (jumpOnce && rr_onGround && s_rrPhase == RR_PHASE_RUN)
  {
    rr_vy = -220.0f;
    rr_onGround = false;
  }

  if (boostOnce && !rr_boosting && (int32_t)(now - rr_boostCooldownEndMs) >= 0 && s_rrPhase == RR_PHASE_RUN)
  {
    rr_boosting = true;
    rr_boostEndMs = now + kRrBoostMs;
    rr_boostCooldownEndMs = now + kRrBoostCooldownMs;
    soundConfirm();
  }

  const float gravity = (jumpHeld && rr_vy < 0.0f) ? 520.0f : 720.0f;

  rr_vy += gravity * dt;
  rr_y += rr_vy * dt;

  if (rr_y >= 0.0f)
  {
    rr_y = 0.0f;
    rr_vy = 0.0f;
    rr_onGround = true;
  }

  const int speed = rr_boosting ? kRrBoostSpeed : kRrBaseSpeed;
  const bool scrollWorld = (s_rrPhase == RR_PHASE_RUN) || (s_rrPhase == RR_PHASE_HAND_ENTER);

  const int h = (screenH > 0) ? screenH : 135;
  const int gW = (screenW > 0) ? screenW : 240;
  const int groundY = h - kRrGroundH;
  const int px = kRrPlayerX;
  int py = groundY - kRrPlayerH + (int)rr_y;
  const int pw = kRrPlayerW;
  int ph = rr_ducking ? kRrPlayerDuckH : kRrPlayerH;

  if (rr_ducking)
    py = groundY - ph + (int)rr_y;

  if (scrollWorld)
    rr_distance += (int)(speed * dt);

  if (s_rrPhase == RR_PHASE_RUN)
  {
    while (rr_nextSpawn < rr_scriptCount && rr_distance >= rr_script[rr_nextSpawn].triggerDist)
    {
      rrSpawnObstacle(rr_script[rr_nextSpawn].type);
      rr_nextSpawn++;
    }

    if (rr_distance >= kRrHandTriggerDist)
    {
      s_rrPhase = RR_PHASE_HAND_ENTER;
      s_rrPhaseStartMs = now;
      s_rrHandActive = true;
      s_rrHandTouched = false;

      s_rrHandX = rr_distance + gW;
      s_rrHandY = groundY - s_rrHandH + 8;

      for (auto &o : rr_obs)
        o.active = false;
    }
  }

  if (s_rrPhase == RR_PHASE_RUN)
  {
    const int playerHitX = px + kRrPlayerHitInsetX;
    const int playerHitY = py + kRrPlayerHitInsetY;
    const int playerHitW = pw - (kRrPlayerHitInsetX * 2);
    const int playerHitH = ph - (kRrPlayerHitInsetY * 2);

    for (auto &o : rr_obs)
    {
      if (!o.active)
        continue;

      const int ox = o.x - rr_distance;
      const int oy = o.y;

      int obsHitInsetX = 0;
      int obsHitInsetY = 0;

      if (o.h >= kRrJumpObsH)
      {
        obsHitInsetX = kRrJumpObsHitInsetX;
        obsHitInsetY = kRrJumpObsHitInsetY;
      }
      else
      {
        obsHitInsetX = kRrDuckObsHitInsetX;
        obsHitInsetY = kRrDuckObsHitInsetY;
      }

      const int obsHitX = ox + obsHitInsetX;
      const int obsHitY = oy + obsHitInsetY;
      const int obsHitW = o.w - (obsHitInsetX * 2);
      const int obsHitH = o.h - (obsHitInsetY * 2);

      if (obsHitW > 0 && obsHitH > 0 &&
          rrAabb(playerHitX, playerHitY, playerHitW, playerHitH, obsHitX, obsHitY, obsHitW, obsHitH))
      {
        rrFinishRun(false);
        return;
      }

      if (ox < -40)
        o.active = false;
    }
  }

  if (s_rrPhase == RR_PHASE_HAND_ENTER)
  {
    s_rrHandY = groundY - s_rrHandH + 8;

    const int handScreenX = s_rrHandX - rr_distance;
    const int targetScreenX = gW - s_rrHandW;

    if (handScreenX <= targetScreenX)
    {
      s_rrPhase = RR_PHASE_HAND_HOLD;
      s_rrPhaseStartMs = now;
      s_rrHandX = targetScreenX;
    }
    return;
  }

  if (s_rrPhase == RR_PHASE_HAND_HOLD)
  {
    s_rrHandY = groundY - s_rrHandH + 8;
    s_rrHandX = gW - s_rrHandW;

    const int touchX = s_rrHandX + 12;
    const int snakeFrontX = kRrPlayerX + s_rrPlayerGoalOffsetX + pw;

    if (!s_rrHandTouched)
    {
      if (snakeFrontX < touchX)
      {
        s_rrPlayerGoalOffsetX += kRrGoalWalkSpeed;

        const int newFrontX = kRrPlayerX + s_rrPlayerGoalOffsetX + pw;
        if (newFrontX >= touchX)
        {
          s_rrPlayerGoalOffsetX -= (newFrontX - touchX);
          s_rrHandTouched = true;
          s_rrPhase = RR_PHASE_HAND_CONTACT;
          s_rrPhaseStartMs = now;
        }
      }
      else
      {
        s_rrHandTouched = true;
        s_rrPhase = RR_PHASE_HAND_CONTACT;
        s_rrPhaseStartMs = now;
      }
    }
    return;
  }

  if (s_rrPhase == RR_PHASE_HAND_CONTACT)
  {
    s_rrHandY = groundY - s_rrHandH + 8;

    const int targetFrontX = s_rrHandX + 12;
    s_rrPlayerGoalOffsetX = targetFrontX - kRrPlayerX - pw;

    if ((now - s_rrPhaseStartMs) >= kRrHandContactHoldMs)
    {
      s_rrPhase = RR_PHASE_HAND_EXIT;
      s_rrPhaseStartMs = now;
    }
    return;
  }

  if (s_rrPhase == RR_PHASE_HAND_EXIT)
  {
    s_rrHandY = groundY - s_rrHandH + 8;
    s_rrHandX += kRrHandExitSpeed;

    if (s_rrHandX >= gW + 4)
    {
      s_rrHandActive = false;
      s_rrPhase = RR_PHASE_WIN_HOLD;
      s_rrPhaseStartMs = now;
    }
    return;
  }

  if (s_rrPhase == RR_PHASE_WIN_HOLD)
  {
    if ((now - s_rrPhaseStartMs) >= kRrWinHoldMs)
    {
      rrFinishRun(true);
      return;
    }
  }

  if (rr_distance >= rr_courseLen && s_rrPhase == RR_PHASE_RUN)
  {
    rrFinishRun(true);
    return;
  }
}

void drawResurrectionRun()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  spr.fillSprite(TFT_BLACK);

  if (mgRewardShowing())
  {
    miniGameDrawRewardModal(gW, gH);
    return;
  }

  const int groundY = gH - kRrGroundH;

  const bool haveSnake = s_rrSnakeRun1Spr && s_rrSnakeRun2Spr && s_rrSnakeCrouchSpr && s_rrSnakeJumpSpr &&
                         s_rrSnakeWin1Spr && s_rrSnakeWin2Spr;

  const bool haveGround = (s_rrGroundSpr != nullptr);
  const bool haveHand = (s_rrHand1Spr != nullptr && s_rrHand2Spr != nullptr);
  const bool haveLadybug = s_rrLadybugGroundSpr && s_rrLadybugFly1Spr && s_rrLadybugFly2Spr;

  M5Canvas *snake1 = s_rrSnakeRun1Spr;
  M5Canvas *snake2 = s_rrSnakeRun2Spr;
  M5Canvas *snakeCrouch = s_rrSnakeCrouchSpr;
  M5Canvas *snakeJump = s_rrSnakeJumpSpr;
  M5Canvas *snakeWin1 = s_rrSnakeWin1Spr;
  M5Canvas *snakeWin2 = s_rrSnakeWin2Spr;
  M5Canvas *groundSpr = s_rrGroundSpr;
  M5Canvas *hand1 = s_rrHand1Spr;
  M5Canvas *hand2 = s_rrHand2Spr;
  M5Canvas *ladybugGround = s_rrLadybugGroundSpr;
  M5Canvas *ladybugFly1 = s_rrLadybugFly1Spr;
  M5Canvas *ladybugFly2 = s_rrLadybugFly2Spr;

  if (haveSnake)
  {
    mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run1", resRunSnakeRun1PathForPet(), 8, kResRunKey, snake1);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run2", resRunSnakeRun2PathForPet(), 8, kResRunKey, snake2);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_crouch", resRunSnakeCrouchPathForPet(), 8, kResRunKey,
                        snakeCrouch);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_jump", resRunSnakeJumpPathForPet(), 8, kResRunKey, snakeJump);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_win1", resRunSnakeWin1PathForPet(), 8, kResRunKey, snakeWin1);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_win2", resRunSnakeWin2PathForPet(), 8, kResRunKey, snakeWin2);
  }

  if (haveGround)
  {
    mgmem::ensureSprite(MiniGame::RESURRECTION, "branch_ground", resRunBranchGroundPathForPet(), 8, kResRunKey,
                        groundSpr);
  }

  if (haveHand)
  {
    mgmem::ensureSprite(MiniGame::RESURRECTION, "hand_1", resRunHand1PathForPet(), 8, kResRunKey, hand1);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "hand_2", resRunHand2PathForPet(), 8, kResRunKey, hand2);
  }

  if (haveLadybug)
  {
    mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_ground", resRunLadybugGroundPathForPet(), 8, kResRunKey,
                        ladybugGround);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_fly1", resRunLadybugFly1PathForPet(), 8, kResRunKey,
                        ladybugFly1);
    mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_fly2", resRunLadybugFly2PathForPet(), 8, kResRunKey,
                        ladybugFly2);
  }

  if (s_rrShowIntro)
  {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(CC_DATUM);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(resRunIntroLine1ForPet(), gW / 2, 8, 2);
    spr.drawCentreString(resRunIntroLine2ForPet(), gW / 2, 26, 2);

    M5Canvas *introSnake = (s_rrAnimFrame == 0) ? s_rrSnakeRun1Spr : s_rrSnakeRun2Spr;

    if (introSnake && introSnake->width() > 0 && introSnake->height() > 0)
    {
      const int sx = (gW - (int)introSnake->width()) / 2;
      const int sy = (gH / 2) - ((int)introSnake->height() / 2) + 18;
      introSnake->pushSprite(&spr, sx, sy, kResRunKey);
    }

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 120, 2);
    return;
  }

  if (g_app.gameOver)
  {
    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(playerWon ? TFT_GREEN : TFT_RED, TFT_BLACK);
    spr.drawCentreString(playerWon ? "YOU WIN!" : "YOU LOSE!", gW / 2, gH / 2 - 10, 4);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Press ENTER", gW / 2, gH / 2 + 22, 2);
    return;
  }

  uint16_t skyColor = TFT_CYAN;

  if (pet.type == PET_ELDRITCH)
  {
    skyColor = spr.color565(12, 0, 20);
    spr.fillRect(0, 0, gW, groundY, skyColor);

    for (int y = 0; y < groundY; y += 4)
    {
      uint8_t shade = 8 + (y / 8);
      uint16_t c = spr.color565(shade, 0, shade * 2);
      spr.drawFastHLine(0, y, gW, c);
    }

    rrDrawEldritchStars(groundY, millis());
  }
  else
  {
    spr.fillRect(0, 0, gW, groundY, skyColor);
  }

  if (groundSpr && groundSpr->width() > 0 && groundSpr->height() > 0)
  {
    const int tileW = (int)groundSpr->width();
    const int tileH = (int)groundSpr->height();
    const int scrollX = (rr_distance / 2) % tileW;
    const int groundDrawY = gH - tileH;

    for (int x = -scrollX; x < gW; x += tileW)
      groundSpr->pushSprite(&spr, x, groundDrawY, kResRunKey);

    if (groundDrawY > groundY)
      spr.fillRect(0, groundY, gW, groundDrawY - groundY, TFT_BROWN);
  }
  else
  {
    spr.fillRect(0, groundY, gW, kRrGroundH, TFT_BROWN);
  }

  const int px = kRrPlayerX;
  int py = groundY - kRrPlayerH + (int)rr_y;
  const int pw = kRrPlayerW;
  int ph = rr_ducking ? kRrPlayerDuckH : kRrPlayerH;

  int snakeDrawX = px;
  if (s_rrPhase != RR_PHASE_RUN)
    snakeDrawX += s_rrPlayerGoalOffsetX;

  if (rr_ducking)
    py = groundY - ph + (int)rr_y;

  M5Canvas *snakeSpr = nullptr;

  if (s_rrHandTouched && snakeWin1 && snakeWin2 && snakeWin1->width() > 0 && snakeWin1->height() > 0 &&
      snakeWin2->width() > 0 && snakeWin2->height() > 0)
  {
    snakeSpr = (s_rrAnimFrame == 0) ? snakeWin1 : snakeWin2;
  }
  else if (!rr_onGround && snakeJump && snakeJump->width() > 0 && snakeJump->height() > 0)
  {
    snakeSpr = snakeJump;
  }
  else if (rr_ducking && snakeCrouch && snakeCrouch->width() > 0 && snakeCrouch->height() > 0)
  {
    snakeSpr = snakeCrouch;
  }
  else
  {
    snakeSpr = (s_rrAnimFrame == 0) ? snake1 : snake2;
  }

  if (snakeSpr && snakeSpr->width() > 0 && snakeSpr->height() > 0)
  {
    const int drawY = groundY - (int)snakeSpr->height() + 4 + (int)rr_y;
    snakeSpr->pushSprite(&spr, snakeDrawX, drawY, kResRunKey);
  }
  else
  {
    spr.fillRoundRect(px, py, pw, ph, 6, TFT_GREEN);
    spr.fillCircle(px + pw - 8, py + ph / 2, 5, TFT_RED);
  }

  if (s_rrHandActive)
  {
    M5Canvas *goalHand = s_rrHandTouched ? hand2 : hand1;

    if (goalHand && goalHand->width() > 0 && goalHand->height() > 0)
    {
      const int handDrawX = (s_rrPhase == RR_PHASE_HAND_ENTER) ? (s_rrHandX - rr_distance) : s_rrHandX;
      goalHand->pushSprite(&spr, handDrawX, s_rrHandY, kResRunKey);
    }
  }

  for (auto &o : rr_obs)
  {
    if (!o.active)
      continue;

    const int ox = o.x - rr_distance;
    if (ox < -80 || ox > gW + 80)
      continue;

    M5Canvas *bugSpr = nullptr;

    if (o.flying)
      bugSpr = (s_rrLadybugAnimFrame == 0) ? ladybugFly1 : ladybugFly2;
    else
      bugSpr = ladybugGround;

    if (bugSpr && bugSpr->width() > 0 && bugSpr->height() > 0)
    {
      const int drawW = o.w;
      const int drawH = o.h;
      const int drawX = ox;

      int drawY = o.y;

      if (o.flying)
      {
        const int bob = ((millis() / 120) % 2 == 0) ? 0 : kRrFlyingBugBob;
        drawY = o.y - kRrFlyingBugVisualLift - bob;
      }
      else
      {
        drawY = groundY - drawH + 4;
      }

      bugSpr->pushRotateZoom(&spr, drawX + drawW / 2, drawY + drawH / 2, 0.0f, (float)drawW / (float)bugSpr->width(),
                             (float)drawH / (float)bugSpr->height(), kResRunKey);
    }
    else
    {
      spr.fillRoundRect(ox, o.y, o.w, o.h, 4, TFT_GREEN);
    }
  }
}