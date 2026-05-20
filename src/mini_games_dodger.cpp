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
#include "graphics_pet_bg_paths.h"
#include "input.h"
#include "pet.h"
#include "save_manager.h"
#include "sdcard.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

void freeDodgerBgCache();
void freeDodgerFireballSprites();
void freeDodgerCarSprite();
void freeDodgerGoalFrames();
void freeDodgerGoreSprite();
static bool sdExistsTrySlash(const char *path, const char **outUsePath);

static bool sdExistsTrySlash(const char *path, const char **outUsePath)
{
  if (outUsePath)
    *outUsePath = path;

  if (!path || !path[0])
    return false;

  if (SD.exists(path))
  {
    if (outUsePath)
      *outUsePath = path;
    return true;
  }

  if (path[0] == '/')
  {
    const char *p2 = path + 1;
    if (SD.exists(p2))
    {
      if (outUsePath)
        *outUsePath = p2;
      return true;
    }
  }

  return false;
}

static const char *dodgerBgLeftPathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/dev_fbrun_bgl.png"; }

static const char *dodgerBgRightPathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/dev_fbrun_bgr.png"; }

static void dodgerLogState(const char *tag);

static inline void dodgerExitMiniGameToReturnUi(bool beginLockout = true)
{
  dodgerLogState("exit");
  mgmem::endSession();
  miniGameExitToReturnUi(beginLockout);
}

// -----------------------------------------------------------------------------
// (Infernal Dodger) FIREBALL RUN GLOBALS
// -----------------------------------------------------------------------------

struct DodgerBall
{
  int16_t x;
  int16_t y;
  int16_t vy;
  uint8_t r;
  bool active;
};

enum DodgerPhase
{
  DODGER_PHASE_FIREBALLS = 0,
  DODGER_PHASE_COAST,
  DODGER_PHASE_GOAL,
  DODGER_PHASE_IMPACT,
  DODGER_PHASE_CAR_EXIT,
  DODGER_PHASE_HOLD,
  DODGER_PHASE_OFFROAD_CRASH,
  DODGER_PHASE_OFFROAD_HOLD
};

static bool s_dodgerShowIntro = true;
static bool s_dodgerIntroDrawnOnce = false;
static bool s_dodgerAssetsPreloaded = false;
bool dodgerIsShowingIntro() { return s_dodgerShowIntro; }
static uint8_t s_dodgerIntroImpFrame = 0;
static uint32_t s_dodgerIntroImpAnimMs = 0;

static int8_t s_dodgerCrashDir = 0;
static constexpr uint32_t kDodgerOffroadHoldMs = 500;

static bool s_dodgerFreezeScroll = false;

static bool s_dodgerGoalActive = false;
static bool s_dodgerGoalReached = false;
static int16_t s_dodgerGoalX = 0;
static int16_t s_dodgerGoalY = 0;
static int s_dodgerGoalSpawnScrollY = 0;

static int s_dodgerGoalW = 0;
static int s_dodgerGoalH = 0;

static uint8_t s_dodgerGoalAnimFrame = 0;
static uint32_t s_dodgerGoalAnimMs = 0;

static DodgerPhase s_dodgerPhase = DODGER_PHASE_FIREBALLS;
static uint32_t s_dodgerPhaseStartMs = 0;

static constexpr uint32_t kDodgerGoalSpawnMs = 12000;
static constexpr uint32_t kDodgerCoastMs = 700;
static constexpr uint32_t kDodgerImpactHoldMs = 120;
static int s_dodgerCarExitVy = 2;
static constexpr uint32_t kDodgerGoalHoldMs = 900;
static constexpr uint32_t kDodgerGoalPauseMs = 180;
static constexpr uint32_t kDodgerCoastDelayMs = 450;
static constexpr int kDodgerRoadScrollSpeed = 3;

static bool s_dodgerInited = false;
uint32_t s_dodgerLastStepMs = 0;
static uint32_t s_dodgerStartMs = 0;
static uint32_t s_dodgerSpawnAccMs = 0;

static int16_t s_dodgerPx = 0;
static int16_t s_dodgerPy = 0;
static float s_dodgerPxF = 0.0f;
uint32_t s_dodgerMoveLastMs = 0;

static int8_t s_dodgerMoveDir = 0;
static uint32_t s_dodgerDirHoldMs = 0;

static DodgerBall s_dodgerBalls[8];

static const uint16_t kDodgerKey = 0x0841;
static const uint16_t kDodgerCarKey = 0xF81F;
static int s_dodgerBgScrollY = 0;

static int s_dodgerFireballW = 0;
static int s_dodgerFireballH = 0;

static int s_dodgerCarW = 0;
static int s_dodgerCarH = 0;

static void dodgerLogState(const char *tag)
{
  Serial.printf("[DODGER] %s intro=%d phase=%d gameOver=%d won=%d px=%d py=%d goalY=%d free=%u largest=%u\n",
                tag ? tag : "state", s_dodgerShowIntro ? 1 : 0, (int)s_dodgerPhase, g_app.gameOver ? 1 : 0,
                playerWon ? 1 : 0, (int)s_dodgerPx, (int)s_dodgerPy, (int)s_dodgerGoalY, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static M5Canvas *s_dodgerFireball1Spr = nullptr;
static M5Canvas *s_dodgerFireball2Spr = nullptr;
static M5Canvas *s_dodgerFireball3Spr = nullptr;
static M5Canvas *s_dodgerCarSpr = nullptr;
static M5Canvas *s_dodgerGoalFrame1Spr = nullptr;
static M5Canvas *s_dodgerGoalFrame2Spr = nullptr;
static M5Canvas *s_dodgerGoreSpr = nullptr;
static M5Canvas *s_dodgerBgLeftSpr = nullptr;
static M5Canvas *s_dodgerBgRightSpr = nullptr;
static int s_dodgerBgHalfW = 0;
static int s_dodgerBgHalfH = 0;

static const char *dodgerIntroLine1() { return "Arrow keys or A/L to dodge"; }

static const char *dodgerIntroLine2() { return "Stay on the road, smash the Imp!"; }

void freeDodgerGoalFrames()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "goal_frame_1");
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "goal_frame_2");

  s_dodgerGoalFrame1Spr = nullptr;
  s_dodgerGoalFrame2Spr = nullptr;
  s_dodgerGoalW = 0;
  s_dodgerGoalH = 0;
}

void freeDodgerGoreSprite()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "goal_gore");
  s_dodgerGoreSpr = nullptr;
}

void freeDodgerSprites()
{
  freeDodgerBgCache();
  freeDodgerFireballSprites();
  freeDodgerCarSprite();
  freeDodgerGoalFrames();
  freeDodgerGoreSprite();
}

static const char *dodgerGoalFrame1PathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/imp_stack1.png"; }

static const char *dodgerGoalFrame2PathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/imp_stack2.png"; }

static const char *dodgerGoalGorePathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/imp_gore.png"; }

static const char *dodgerProjectile1PathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/fireball1.png"; }

static const char *dodgerProjectile2PathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/fireball2.png"; }

static const char *dodgerProjectile3PathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/fireball3.png"; }

static const char *fireballRunCarPathForPet() { return "/raising_hell/graphics/mini_games/fbrun/dev/car.png"; }

static const char *dodgerGoalFrame1ResolvedPath() { return dodgerGoalFrame1PathForPet(); }

static const char *dodgerGoalFrame2ResolvedPath() { return dodgerGoalFrame2PathForPet(); }

static const char *dodgerGoalGoreResolvedPath() { return dodgerGoalGorePathForPet(); }

static bool ensureDodgerGoalFrames(const char *path0, const char *path1)
{
  if (!path0 || !path1 || !path0[0] || !path1[0] || !g_sdReady)
    return false;

  s_dodgerGoalFrame1Spr = nullptr;
  s_dodgerGoalFrame2Spr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_frame_1", path0, 8, kDodgerKey, s_dodgerGoalFrame1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_frame_2", path1, 8, kDodgerKey, s_dodgerGoalFrame2Spr))
    return false;

  if (!s_dodgerGoalFrame1Spr || s_dodgerGoalFrame1Spr->width() <= 0 || s_dodgerGoalFrame1Spr->height() <= 0)
    return false;

  s_dodgerGoalW = (int)s_dodgerGoalFrame1Spr->width();
  s_dodgerGoalH = (int)s_dodgerGoalFrame1Spr->height();
  return true;
}

static bool ensureDodgerGoreSprite(const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  s_dodgerGoreSpr = nullptr;

  return mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_gore", path, 8, kDodgerKey, s_dodgerGoreSpr) &&
         s_dodgerGoreSpr && s_dodgerGoreSpr->width() > 0 && s_dodgerGoreSpr->height() > 0;
}

void freeDodgerBgCache()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "bg_left");
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "bg_right");

  s_dodgerBgLeftSpr = nullptr;
  s_dodgerBgRightSpr = nullptr;
  s_dodgerBgHalfW = 0;
  s_dodgerBgHalfH = 0;
  s_dodgerBgScrollY = 0;
}

void freeDodgerFireballSprites()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "fireball1");
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "fireball2");
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "fireball3");

  s_dodgerFireball1Spr = nullptr;
  s_dodgerFireball2Spr = nullptr;
  s_dodgerFireball3Spr = nullptr;
  s_dodgerFireballW = 0;
  s_dodgerFireballH = 0;
}

void freeDodgerCarSprite()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "car");

  s_dodgerCarSpr = nullptr;
  s_dodgerCarW = 0;
  s_dodgerCarH = 0;
}

static bool ensureDodgerBgCache()
{
  if (!g_sdReady)
    return false;

  const char *leftPath = dodgerBgLeftPathForPet();
  const char *rightPath = dodgerBgRightPathForPet();

  s_dodgerBgLeftSpr = nullptr;
  s_dodgerBgRightSpr = nullptr;
  s_dodgerBgHalfW = 0;
  s_dodgerBgHalfH = 0;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "bg_left", leftPath, 8, kDodgerKey, s_dodgerBgLeftSpr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "bg_right", rightPath, 8, kDodgerKey, s_dodgerBgRightSpr))
    return false;

  if (!s_dodgerBgLeftSpr || !s_dodgerBgRightSpr)
    return false;

  if (s_dodgerBgLeftSpr->width() <= 0 || s_dodgerBgLeftSpr->height() <= 0)
    return false;

  if (s_dodgerBgRightSpr->width() <= 0 || s_dodgerBgRightSpr->height() <= 0)
    return false;

  s_dodgerBgHalfW = (int)s_dodgerBgLeftSpr->width();
  s_dodgerBgHalfH = (int)s_dodgerBgLeftSpr->height();
  return true;
}

static bool ensureDodgerFireballSprites()
{
  if (!g_sdReady)
    return false;

  const char *path1 = dodgerProjectile1PathForPet();
  const char *path2 = dodgerProjectile2PathForPet();
  const char *path3 = dodgerProjectile3PathForPet();

  if (!path1 || !path2 || !path3 || !path1[0] || !path2[0] || !path3[0])
    return false;

  s_dodgerFireball1Spr = nullptr;
  s_dodgerFireball2Spr = nullptr;
  s_dodgerFireball3Spr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball1", path1, 8, kDodgerKey, s_dodgerFireball1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball2", path2, 8, kDodgerKey, s_dodgerFireball2Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball3", path3, 8, kDodgerKey, s_dodgerFireball3Spr))
    return false;

  if (!s_dodgerFireball1Spr || s_dodgerFireball1Spr->width() <= 0 || s_dodgerFireball1Spr->height() <= 0)
    return false;

  s_dodgerFireballW = (int)s_dodgerFireball1Spr->width();
  s_dodgerFireballH = (int)s_dodgerFireball1Spr->height();
  return true;
}

static bool ensureDodgerCarSprite(const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  s_dodgerCarSpr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "car", path, 8, kDodgerCarKey, s_dodgerCarSpr))
    return false;

  if (!s_dodgerCarSpr || s_dodgerCarSpr->width() <= 0 || s_dodgerCarSpr->height() <= 0)
    return false;

  s_dodgerCarW = (int)s_dodgerCarSpr->width();
  s_dodgerCarH = (int)s_dodgerCarSpr->height();
  return true;
}

static void dodgerReset()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  s_dodgerPx = gW / 2;
  s_dodgerPxF = (float)s_dodgerPx;
  s_dodgerMoveLastMs = millis();
  s_dodgerPy = gH - 42;
  s_dodgerMoveDir = 0;
  s_dodgerDirHoldMs = 0;

  for (auto &b : s_dodgerBalls)
  {
    b = {0, -200, 2, 4, false};
  }

  s_dodgerStartMs = millis();
  s_dodgerLastStepMs = millis();
  s_dodgerSpawnAccMs = 0;

  s_dodgerGoalActive = false;
  s_dodgerGoalReached = false;
  s_dodgerGoalX = gW / 2;
  s_dodgerGoalY = gH / 2 - 20;
  s_dodgerGoalSpawnScrollY = 0;
  s_dodgerCarExitVy = 2;

  s_dodgerPhase = DODGER_PHASE_FIREBALLS;
  s_dodgerPhaseStartMs = millis();
  s_dodgerGoalAnimFrame = 0;
  s_dodgerGoalAnimMs = millis();
  s_dodgerCrashDir = 0;

  s_dodgerFreezeScroll = false;
}

static void dodgerSpawnOne(int difficulty)
{
  const int gW = (screenW > 0) ? screenW : 240;

  int slot = -1;
  for (int i = 0; i < (int)(sizeof(s_dodgerBalls) / sizeof(s_dodgerBalls[0])); ++i)
  {
    if (!s_dodgerBalls[i].active)
    {
      slot = i;
      break;
    }
  }
  if (slot < 0)
    return;

  DodgerBall &b = s_dodgerBalls[slot];

  const int margin = 6;
  const int roadLeft = 32;
  const int roadRight = gW - 32;

  b.r = (uint8_t)(3 + (difficulty % 3));
  b.x = (int16_t)random((long)(roadLeft + margin), (long)(roadRight - margin));
  b.y = (int16_t)(-(int)(10 + random(40)));

  b.vy = (int16_t)(2 + (difficulty / 5));
  if (b.vy > 7)
    b.vy = 7;

  b.active = true;
}

static inline bool dodgerHit(int ax, int ay, int ar, int bx, int by, int br)
{
  const int dx = ax - bx;
  const int dy = ay - by;
  const int rr = ar + br;
  return (dx * dx + dy * dy) <= (rr * rr);
}

static inline uint32_t dodgerAliveMsNow(uint32_t now)
{
  uint32_t elapsed = now - s_dodgerStartMs;

  const uint32_t pausedAccum = mgPauseAccumMs();
  if (elapsed > pausedAccum)
    elapsed -= pausedAccum;
  else
    elapsed = 0;

  if (mgPauseIsPaused() && mgPauseStartMs() != 0)
  {
    uint32_t pausedSoFar = now - mgPauseStartMs();
    if (elapsed > pausedSoFar)
      elapsed -= pausedSoFar;
    else
      elapsed = 0;
  }

  return elapsed;
}

static void dodgerPreloadAssetsForIntro()
{
  if (s_dodgerAssetsPreloaded)
    return;

  mgmem::logUsage("dodger-deferred-preload-begin");

  freeDodgerBgCache();
  freeDodgerFireballSprites();
  freeDodgerCarSprite();
  freeDodgerGoalFrames();
  freeDodgerGoreSprite();

  const bool bgOk = ensureDodgerBgCache();
  const bool fireballOk = ensureDodgerFireballSprites();
  const bool carOk = ensureDodgerCarSprite(fireballRunCarPathForPet());

  s_dodgerGoreSpr = nullptr;

  const char *goalFrame1Path = dodgerGoalFrame1ResolvedPath();
  const char *goalFrame2Path = dodgerGoalFrame2ResolvedPath();

  const bool goalOk = (goalFrame1Path && goalFrame2Path) && ensureDodgerGoalFrames(goalFrame1Path, goalFrame2Path);

  Serial.printf("[DODGER] deferred preload bg=%d fireballs=%d car=%d goal=%d free=%u largest=%u\n", bgOk ? 1 : 0,
                fireballOk ? 1 : 0, carOk ? 1 : 0, goalOk ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  s_dodgerAssetsPreloaded = true;
  s_dodgerInited = true;
  requestUIRedraw();
}

void startInfernalDodger()
{
  inputSetTextCapture(false);
  mgPauseReset();

  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;
  s_resultShown = false;

  mgClearRewardState();
  mgResetAcceptState();

  currentMiniGame = MiniGame::INFERNAL_DODGER;

  graphicsReleaseUiCachesForMiniGame();
  mgAssetsBeginSession(currentMiniGame, "startInfernalDodger");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("dodger-start-beginSession");

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi, g_app.currentTab);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_dodgerInited = false;
  s_dodgerBgScrollY = 0;
  s_dodgerFreezeScroll = false;

  invalidateBackgroundCache();
  s_dodgerShowIntro = true;
  s_dodgerIntroDrawnOnce = false;
  s_dodgerAssetsPreloaded = false;
  s_dodgerIntroImpFrame = 0;
  s_dodgerIntroImpAnimMs = millis();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  mgmem::logUsage("dodger-start-complete");
  dodgerLogState("start-complete");
}

void updateInfernalDodger(const InputState &input)
{
  const bool enterOnce = miniGameEnterOnce(input);

  if (mgRewardShowing())
  {
    const uint32_t now = millis();
    if ((enterOnce && !mgInputLockedOut()) || mgRewardAutoDismissNow(now))
    {
      mgClearRewardState();
      mgResetAcceptState();
      dodgerExitMiniGameToReturnUi(true);
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
    return;
  }

  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  const uint32_t now = millis();
  const uint32_t aliveMs = dodgerAliveMsNow(now);
  const int difficulty = (int)(aliveMs / 3000);

  if (s_dodgerShowIntro)
  {
    const uint32_t dt = now - s_dodgerIntroImpAnimMs;

    if (dt >= 180)
    {
      s_dodgerIntroImpAnimMs = now;
      s_dodgerIntroImpFrame ^= 1;
    }

    if (s_dodgerIntroDrawnOnce && !s_dodgerAssetsPreloaded)
    {
      dodgerPreloadAssetsForIntro();
      return;
    }

    if (input.mgQuitOnce && !mgInputLockedOut())
    {
      miniGameCancelFromIntro();
      return;
    }

    const bool startPressed = s_dodgerAssetsPreloaded && (enterOnce || input.mgSelectOnce || input.mgUpOnce);

    if (startPressed && !mgInputLockedOut())
    {
      s_dodgerShowIntro = false;
      dodgerReset();
      s_dodgerMoveLastMs = now;
      s_dodgerLastStepMs = now;

      dodgerLogState("intro-dismissed");

      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  const int roadSpeed = kDodgerRoadScrollSpeed;

  if (s_dodgerPhase == DODGER_PHASE_FIREBALLS && aliveMs >= kDodgerGoalSpawnMs)
  {
    s_dodgerPhase = DODGER_PHASE_COAST;
    s_dodgerPhaseStartMs = now;
    s_dodgerMoveDir = 0;

    for (auto &b : s_dodgerBalls)
      b.active = false;

    freeDodgerFireballSprites();
    mgmem::logUsage("dodger-after-fireball-unload");

    // Load goal frames FIRST (bigger, more important)

    // THEN load gore
    if (!s_dodgerGoreSpr)
    {
      const char *g = dodgerGoalGoreResolvedPath();
      if (g)
      {
        ensureDodgerGoreSprite(g);
        mgmem::logUsage("dodger-after-gore-late-ensure");
        dodgerLogState("gore-loaded");
      }
    }
  }

  if (s_dodgerPhase == DODGER_PHASE_COAST)
  {
    const uint32_t coastElapsed = now - s_dodgerPhaseStartMs;

    if (coastElapsed >= kDodgerCoastDelayMs)
    {
      const int targetX = gW / 2;
      const int centerDriftPx = 1;

      if (s_dodgerPx < targetX)
        s_dodgerPx += centerDriftPx;
      else if (s_dodgerPx > targetX)
        s_dodgerPx -= centerDriftPx;

      if (abs(s_dodgerPx - targetX) < centerDriftPx)
        s_dodgerPx = targetX;

      s_dodgerPxF = (float)s_dodgerPx;
    }

    const bool coastDone = (coastElapsed >= (kDodgerCoastDelayMs + kDodgerCoastMs)) && (s_dodgerPx == (gW / 2));

    if (coastDone)
    {
      s_dodgerPhase = DODGER_PHASE_GOAL;
      s_dodgerPhaseStartMs = now;
      s_dodgerGoalActive = true;
      s_dodgerGoalReached = false;
      s_dodgerGoalX = gW / 2;
      s_dodgerGoalY = -24;
      s_dodgerGoalSpawnScrollY = s_dodgerBgScrollY;
      s_dodgerGoalAnimFrame = 0;
      s_dodgerGoalAnimMs = now;
      s_dodgerFreezeScroll = false;
    }
  }

  if ((s_dodgerPhase == DODGER_PHASE_GOAL) && ((now - s_dodgerGoalAnimMs) >= 180))
  {
    s_dodgerGoalAnimMs = now;
    s_dodgerGoalAnimFrame ^= 1;
  }

  if (s_dodgerPhase == DODGER_PHASE_FIREBALLS)
  {
    const bool leftHeld = input.mgLeftHeld;
    const bool rightHeld = input.mgRightHeld;

    if (input.mgLeftOnce)
    {
      s_dodgerMoveDir = -1;
      s_dodgerDirHoldMs = now + 140;
    }
    if (input.mgRightOnce)
    {
      s_dodgerMoveDir = +1;
      s_dodgerDirHoldMs = now + 140;
    }

    if (leftHeld && !rightHeld)
    {
      s_dodgerMoveDir = -1;
      s_dodgerDirHoldMs = now + 140;
    }
    if (rightHeld && !leftHeld)
    {
      s_dodgerMoveDir = +1;
      s_dodgerDirHoldMs = now + 140;
    }

    if (!leftHeld && !rightHeld)
    {
      if ((int32_t)(now - s_dodgerDirHoldMs) >= 0)
        s_dodgerMoveDir = 0;
    }

    if (input.encoderDelta < 0)
    {
      s_dodgerMoveDir = -1;
      s_dodgerDirHoldMs = now + 140;
    }
    if (input.encoderDelta > 0)
    {
      s_dodgerMoveDir = +1;
      s_dodgerDirHoldMs = now + 140;
    }
  }
  else
  {
    s_dodgerMoveDir = 0;
  }

  // Normal horizontal motion should NOT run during offroad crash/hold,
  // or it will clamp the car back onto the screen.
  if (s_dodgerPhase != DODGER_PHASE_OFFROAD_CRASH && s_dodgerPhase != DODGER_PHASE_OFFROAD_HOLD)
  {
    uint32_t mvDtMs = now - s_dodgerMoveLastMs;
    s_dodgerMoveLastMs = now;
    if (mvDtMs > 40)
      mvDtMs = 40;

    float pxPerSec = 120.0f + (float)(difficulty * 6);
    if (pxPerSec > 170.0f)
      pxPerSec = 170.0f;

    const float dt = (float)mvDtMs / 1000.0f;
    s_dodgerPxF += (float)s_dodgerMoveDir * pxPerSec * dt;

    const float marginF = 6.0f;
    if (s_dodgerPxF < marginF)
      s_dodgerPxF = marginF;
    if (s_dodgerPxF > (float)gW - marginF)
      s_dodgerPxF = (float)gW - marginF;

    s_dodgerPx = (int16_t)(s_dodgerPxF + 0.5f);
  }
  else
  {
    s_dodgerMoveLastMs = now;
  }

  const int roadLeft = 32;
  const int roadRight = gW - 32;

  if (s_dodgerPhase == DODGER_PHASE_FIREBALLS)
  {
    if (s_dodgerPx < roadLeft || s_dodgerPx > roadRight)
    {
      s_dodgerCrashDir = (s_dodgerPx < roadLeft) ? -1 : +1;

      if (s_dodgerMoveDir < 0)
        s_dodgerCrashDir = -1;
      else if (s_dodgerMoveDir > 0)
        s_dodgerCrashDir = +1;

      s_dodgerPhase = DODGER_PHASE_OFFROAD_CRASH;
      s_dodgerPhaseStartMs = now;
      s_dodgerMoveDir = 0;

      dodgerLogState("offroad-crash");
    }
  }

  const uint32_t stepMs = 16;
  int steps = 0;
  const int kMaxStepsPerFrame = 4;

  while ((int32_t)(now - s_dodgerLastStepMs) >= (int32_t)stepMs && steps < kMaxStepsPerFrame)
  {
    int spawnEveryMs = 520 - difficulty * 24;
    if (spawnEveryMs < 220)
      spawnEveryMs = 220;

    if (!s_dodgerFreezeScroll)
      s_dodgerBgScrollY -= roadSpeed;

    if (s_dodgerPhase == DODGER_PHASE_FIREBALLS)
    {
      s_dodgerSpawnAccMs += stepMs;
      if (s_dodgerSpawnAccMs >= (uint32_t)spawnEveryMs)
      {
        s_dodgerSpawnAccMs = 0;
        dodgerSpawnOne(difficulty);
      }
    }

    if (s_dodgerPhase == DODGER_PHASE_FIREBALLS || s_dodgerPhase == DODGER_PHASE_COAST)
    {
      for (auto &b : s_dodgerBalls)
      {
        if (!b.active)
          continue;

        b.y += b.vy;

        if (b.y > gH + 12)
        {
          b.active = false;
          continue;
        }

        const int pr = 6;
        if (dodgerHit((int)s_dodgerPx, (int)s_dodgerPy, pr, (int)b.x, (int)b.y, (int)b.r))
        {
          playerWon = false;
          g_app.gameOver = true;
          requestUIRedraw();
          s_resultShown = true;

          dodgerLogState("lose");

          soundError();
          return;
        }
      }
    }
    else if (s_dodgerPhase == DODGER_PHASE_GOAL)
    {
      const int targetY = gH / 2;

      const int scrollDelta = s_dodgerGoalSpawnScrollY - s_dodgerBgScrollY;
      s_dodgerGoalY = -24 + scrollDelta;

      if (s_dodgerGoalY >= targetY)
      {
        s_dodgerGoalY = targetY;
        s_dodgerGoalReached = true;
        s_dodgerFreezeScroll = true;
        s_dodgerCarExitVy = roadSpeed;
        s_dodgerPhase = DODGER_PHASE_IMPACT;
        s_dodgerPhaseStartMs = now;

        dodgerLogState("goal-reached");
      }
    }
    else if (s_dodgerPhase == DODGER_PHASE_IMPACT)
    {
      s_dodgerPy -= s_dodgerCarExitVy;

      const int impactY = s_dodgerGoalY + 4;
      if (s_dodgerPy <= impactY)
      {
        s_dodgerPy = impactY;
        s_dodgerPhase = DODGER_PHASE_CAR_EXIT;
        s_dodgerPhaseStartMs = now;
        soundWin();
      }
    }
    else if (s_dodgerPhase == DODGER_PHASE_CAR_EXIT)
    {
      s_dodgerPy -= s_dodgerCarExitVy;

      if (s_dodgerPy < -(s_dodgerCarH > 0 ? s_dodgerCarH : 32))
      {
        s_dodgerPhase = DODGER_PHASE_HOLD;
        s_dodgerPhaseStartMs = now;
      }
    }
    else if (s_dodgerPhase == DODGER_PHASE_HOLD)
    {
      if ((now - s_dodgerPhaseStartMs) >= kDodgerGoalHoldMs)
      {
        playerWon = true;
        g_app.gameOver = true;
        requestUIRedraw();
        s_resultShown = true;

        dodgerLogState("win");

        return;
      }
    }
    else if (s_dodgerPhase == DODGER_PHASE_OFFROAD_CRASH)
    {
      s_dodgerPx += s_dodgerCrashDir * 4;
      s_dodgerPxF = (float)s_dodgerPx;

      if ((s_dodgerCrashDir < 0 && s_dodgerPx < -(s_dodgerCarW > 0 ? s_dodgerCarW : 32)) ||
          (s_dodgerCrashDir > 0 && s_dodgerPx > gW + (s_dodgerCarW > 0 ? s_dodgerCarW : 32)))
      {
        s_dodgerPhase = DODGER_PHASE_OFFROAD_HOLD;
        s_dodgerPhaseStartMs = now;
      }
    }
    else if (s_dodgerPhase == DODGER_PHASE_OFFROAD_HOLD)
    {
      if ((now - s_dodgerPhaseStartMs) >= kDodgerOffroadHoldMs)
      {
        playerWon = false;
        g_app.gameOver = true;
        requestUIRedraw();
        s_resultShown = true;

        dodgerLogState("offroad-lose");
        soundError();

        return;
      }
    }

    s_dodgerLastStepMs += stepMs;
    steps++;
  }

  if ((int32_t)(now - s_dodgerLastStepMs) >= (int32_t)stepMs)
    s_dodgerLastStepMs = now;
}

void drawInfernalDodger()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  M5Canvas *bgL = s_dodgerBgLeftSpr;
  M5Canvas *bgR = s_dodgerBgRightSpr;
  const int bw = s_dodgerBgHalfW;
  const int bh = s_dodgerBgHalfH;

  const bool haveBg =
      bgL && bgR && bw > 0 && bh > 0 && bgL->width() > 0 && bgL->height() > 0 && bgR->width() > 0 && bgR->height() > 0;

  M5Canvas *fbFrame0 = s_dodgerFireball1Spr;
  M5Canvas *fbFrame1 = s_dodgerFireball2Spr;
  M5Canvas *fbFrame2 = s_dodgerFireball3Spr;

  const bool haveFireballs = fbFrame0 && fbFrame1 && fbFrame2 && fbFrame0->width() > 0 && fbFrame0->height() > 0;

  M5Canvas *carSpr = s_dodgerCarSpr;

  const bool haveCar = carSpr && carSpr->width() > 0 && carSpr->height() > 0;

  bool drewBg = false;

  if (haveBg)
  {
    int y = -(s_dodgerBgScrollY % bh);
    if (y > 0)
      y -= bh;

    while (y > -bh)
      y -= bh;

    for (int drawY = y; drawY < gH; drawY += bh)
    {
      bgL->pushSprite(&spr, 0, drawY, kDodgerKey);
      bgR->pushSprite(&spr, bw, drawY, kDodgerKey);
    }

    drewBg = true;
  }

  if (!drewBg)
    spr.fillSprite(TFT_BLACK);

  if (mgRewardShowing())
  {
    miniGameDrawRewardModal(gW, gH);
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

  if (s_dodgerShowIntro)
  {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(CC_DATUM);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(dodgerIntroLine1(), gW / 2, 8, 2);
    spr.drawCentreString(dodgerIntroLine2(), gW / 2, 26, 2);

    const int impX = (gW - 48) / 2;
    const int impY = 56;

    const char *introImp =
        (s_dodgerIntroImpFrame == 0) ? dodgerGoalFrame1ResolvedPath() : dodgerGoalFrame2ResolvedPath();

    if (introImp && introImp[0])
      sprDrawPngFromSD(introImp, impX, impY);
    else
      spr.fillRect(impX, impY, 48, 48, TFT_RED);

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(s_dodgerAssetsPreloaded ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);

    spr.drawCentreString(s_dodgerAssetsPreloaded ? "ENTER to begin" : "Loading...", gW / 2, 120, 2);

    // Mark that the intro has been shown once so updateInfernalDodger()
    // can perform the deferred asset preload.
    s_dodgerIntroDrawnOnce = true;
    return;
  }

  if (s_dodgerPhase == DODGER_PHASE_FIREBALLS || s_dodgerPhase == DODGER_PHASE_COAST)
  {
    for (auto &b : s_dodgerBalls)
    {
      if (!b.active)
        continue;

      const int bx = (int)b.x;
      const int by = (int)b.y;

      M5Canvas *fbFrames[3] = {
          s_dodgerFireball1Spr,
          s_dodgerFireball2Spr,
          s_dodgerFireball3Spr,
      };

      if (haveFireballs && fbFrames[0] && fbFrames[1] && fbFrames[2])
      {
        const int frame = (millis() / 80) % 3;
        M5Canvas *fb = fbFrames[frame];

        const int w = fb->width();
        const int h = fb->height();

        const int drawX = bx - w / 2;
        const int drawY = by - h / 2;

        fb->pushSprite(&spr, drawX, drawY, kDodgerKey);
      }
      else
      {
        spr.fillCircle(bx, by, 4, TFT_ORANGE);
        spr.drawCircle(bx, by, 4, TFT_RED);
      }
    }
  }

  if (s_dodgerGoalActive)
  {
    const bool gorePhase = (s_dodgerPhase == DODGER_PHASE_CAR_EXIT) || (s_dodgerPhase == DODGER_PHASE_HOLD);

    M5Canvas *goal1 = s_dodgerGoalFrame1Spr;
    M5Canvas *goal2 = s_dodgerGoalFrame2Spr;

    if (!gorePhase)
    {
      M5Canvas *goalSpr = nullptr;

      if (goal1 && goal2)
        goalSpr = (s_dodgerGoalAnimFrame & 1) ? goal2 : goal1;
      else if (goal1)
        goalSpr = goal1;
      else if (goal2)
        goalSpr = goal2;

      if (goalSpr && goalSpr->width() > 0 && goalSpr->height() > 0)
      {
        const int drawX = s_dodgerGoalX - ((int)goalSpr->width() / 2);
        const int drawY = s_dodgerGoalY - ((int)goalSpr->height() / 2);
        goalSpr->pushSprite(&spr, drawX, drawY, kDodgerKey);
      }
    }
    else
    {
      if (s_dodgerGoreSpr && s_dodgerGoreSpr->width() > 0 && s_dodgerGoreSpr->height() > 0)
      {
        const int drawX = s_dodgerGoalX - ((int)s_dodgerGoreSpr->width() / 2);
        const int drawY = s_dodgerGoalY - ((int)s_dodgerGoreSpr->height() / 2);
        s_dodgerGoreSpr->pushSprite(&spr, drawX, drawY, kDodgerKey);
      }
    }
  }

  if (s_dodgerPhase != DODGER_PHASE_HOLD && s_dodgerPhase != DODGER_PHASE_OFFROAD_HOLD)
  {
    if (haveCar && carSpr)
    {
      const int drawX = s_dodgerPx - (s_dodgerCarW / 2);
      const int drawY = s_dodgerPy - (s_dodgerCarH / 2);
      carSpr->pushSprite(&spr, drawX, drawY, kDodgerCarKey);
    }
    else
    {
      spr.fillCircle(s_dodgerPx, s_dodgerPy, 6, TFT_GREEN);
      spr.drawCircle(s_dodgerPx, s_dodgerPy, 6, TFT_DARKGREEN);
    }
  }
}
