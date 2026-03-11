// ---------------------------------------------------------------------------
// Mini-game implementation toggle
//
// Default: implementation lives in mini_games.cpp.
// If you ever move implementation back to the pause-menu module, set
// RH_MINIGAMES_IMPL_IN_PAUSE_MENU=1 (e.g. via build_flags.h) and this file
// becomes an intentional stub to avoid duplicate symbols.
// ---------------------------------------------------------------------------

#ifndef RH_MINIGAMES_IMPL_IN_PAUSE_MENU
#define RH_MINIGAMES_IMPL_IN_PAUSE_MENU 0
#endif

#if RH_MINIGAMES_IMPL_IN_PAUSE_MENU

#include "mini_games.h"
// Intentionally empty (implementation moved elsewhere).

#else

#include "esp_heap_caps.h"
#include <stdint.h>

#include "mini_game_assets.h"
#include "mini_game_runtime.h"
#include "mini_games.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>

#include "app_state.h" // g_app
#include "display.h"
#include "graphics.h"   // spr, screenW/screenH, invalidateBackgroundCache()
#include "sdcard.h"     // g_sdReady
#include "sound.h"      // soundFlap/soundConfirm/soundError/playBeep
#include "ui_defs.h"    // UIState
#include "ui_runtime.h" // requestUIRedraw()
#include <string.h>     // strrchr
#include <strings.h>    // strcasecmp (ESP32 toolchain usually has this)

#include "currency.h"
#include "input.h"
#include "inventory.h"     // ItemType
#include "mg_pause_core.h" // mgPause* + MGPAUSE_* constants
#include "mg_pause_menu.h"
#include "mini_game_return_ui.h" // miniGameSetReturnUi / miniGameGetReturnUiOrDefault / miniGameClearReturnUi
#include "pet.h"
#include "save_manager.h"
#include "ui_actions.h"

static inline void exitMiniGameToReturnUi(bool beginLockout = true);

// -----------------------------------------------------------------------------
// Mini-game input helpers / shared state
// -----------------------------------------------------------------------------
// Forward decls used by multiple mini-games / defined later in this file
static const char *crossyStartZonePathForPet();
static const char *crossyGoalZonePathForPet();
static const char *crossyLavaZonePathForPet(uint8_t frame);
static const char *crossyStonePathForPet();
void freeFlappyBgCache();
static bool ensureFlappyBgCache(const char *path);
static void logMiniGameHeap(const char *tag);

// I should sort these better
void freeCrossyZoneSprites();
void freeCrossyActorSprites();
void startCrossyRoad();
void updateCrossyRoad(const InputState &input);
void drawCrossyRoad();
void freeFlappyFireballSprites();
void freeFlappyPipeSprites();
void freeDodgerGoalFrames();
void freeDodgerGoreSprite();
void freeImpWaveSprites();
void freeDodgerBgCache();
void freeDodgerFireballSprites();
void freeDodgerCarSprite();
void freeResRunSprites();

static bool ensureDodgerGoalFrames(const char *path0, const char *path1);
static bool ensureDodgerGoreSprite(const char *path);

static const char *dodgerGoalFrame1ResolvedPath();
static const char *dodgerGoalFrame2ResolvedPath();
static const char *dodgerGoalGoreResolvedPath();

static void releaseMiniGameAssetsFor(MiniGame game)
{
  switch (game)
  {
  case MiniGame::FLAPPY_FIREBALL:
    freeFlappyPipeSprites();
    freeFlappyFireballSprites();
    freeImpWaveSprites();
    freeFlappyBgCache();
    break;

  case MiniGame::CROSSY_ROAD:
    freeCrossyZoneSprites();
    freeCrossyActorSprites();
    break;

  case MiniGame::INFERNAL_DODGER:
    freeDodgerBgCache();
    freeDodgerFireballSprites();
    freeDodgerCarSprite();
    freeDodgerGoalFrames();
    freeDodgerGoreSprite();
    break;

  case MiniGame::RESURRECTION:
    freeResRunSprites();
    break;

  default:
    break;
  }
}

static inline void exitMiniGameToReturnUi(bool beginLockout)
{
  releaseMiniGameAssetsFor(currentMiniGame);
  mgmem::endSession();
  miniGameExitToReturnUi(beginLockout);
}

// -----------------------------------------------------------------------------
// Mini-game global state
// -----------------------------------------------------------------------------

// Simple mini-game state
static bool s_resultShown = false;

static const uint16_t kSpriteKey = 0x0841; // very dark grey

static void logMiniGameHeap(const char *tag) { mgAssetsLogHeap(tag); }

// -----------------------------------------------------------------------------
// FLAPPY FIREBALL GLOBALS
// -----------------------------------------------------------------------------

// START SCREEN
static bool s_flappyShowIntro = true;
static bool s_flappyDontShowAgain = false; // visual only for now

// BACKGROUND
static bool s_flappyBgReady = false;
static bool s_flappyBgLoadFailed = false;
static char s_flappyBgPath[160] = {0};
static const char *flappyBgPathForPet();

// FIREBALL
static int s_flappyFireballW = 0;
static int s_flappyFireballH = 0;

// IMP
static bool s_impHit = false;
static bool s_impBurnDone = false;
static M5Canvas s_impWaveSpr[2];
static bool s_impWaveSprReady = false;
static int s_impFrame = 0;
static uint32_t s_impAnimMs = 0;
static uint32_t s_impHoldMs = 0;

static constexpr uint32_t kImpWaveFrameMs = 180;
static constexpr uint32_t kImpBurnFrameMs = 180;
static constexpr uint32_t kImpLastFrameHoldMs = 500;

static const char *const kImpBurnFrames[] = {
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn1.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn2.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn3.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn4.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn5.png",
};

static const char *flappyImpWave1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/imp_wave1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/imp_wave1.png";
  }
}

static const char *flappyImpWave2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/imp_wave2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/imp_wave2.png";
  }
}

// SPIKES
static const uint16_t kFireKey = kSpriteKey;

// GOALS
static bool s_flappyInited = false;
static bool s_flappyPlaying = false;
static int s_flappyDistancePx = 0;
static bool s_flappyGoalActive = false;
static int s_flappyGoalX = 0;
static int s_flappyGoalY = 0;
static bool s_flappyGoalReached = false;
static uint32_t s_flappyStartMs = 0;
static int s_flappyBgW = 0;
static int s_flappyBgH = 0;
static int s_fbX = 0;
static int s_fbY = 0;
static int s_fbVY = 0;
uint32_t s_lastStepMs = 0;
static int s_flappyBgScrollX = 0;

// Shared fullscreen background now lives in mini_game_assets.cpp
// Keep only width/height bookkeeping local for flappy scroll math.

// Flappy pipe sprites (8bpp, cached)
static int s_flappyPipeW = 0;
static int s_flappyPipeH = 0;

// Fireball Run background Cache
void freeDodgerBgCache();
void freeDodgerFireballSprites();
void freeDodgerCarSprite();
static bool ensureDodgerBgCache(const char *path);
static bool ensureDodgerFireballSprites(const char *dir);
static bool ensureDodgerCarSprite(const char *path);

struct FlappyPipe
{
  int x;
  int gapY; // center of gap
  bool passed;
};

void freeImpWaveSprites()
{
  for (int i = 0; i < 2; ++i)
    mgAssetsReleaseSprite(s_impWaveSpr[i], "flappy-imp-release");

  s_impWaveSprReady = false;
}

// -----------------------------------------------------------------------------
// Flappy fireball sprite (3-frame PNG animation) - cached per flappy folder
// -----------------------------------------------------------------------------
void freeFlappyFireballSprites()
{
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "fireball1");
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "fireball2");
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "fireball3");

  s_flappyFireballW = 0;
  s_flappyFireballH = 0;
}

// -----------------------------------------------------------------------------
// Flappy pipe sprites (stalagmites/stalactites)
// -----------------------------------------------------------------------------
// We keep this simple: read PNG dimensions once, then draw the PNGs directly.
// (This preserves transparency and avoids maintaining a separate alpha-capable cache.)
static FlappyPipe s_pipes[3];
static bool s_flappyPipeLoadFailed = false;

static int flappyRandGapY(int h)
{
  const int gapH = 64;
  const int margin = 14;
  const int lo = margin + gapH / 2;
  const int hi = (h - 1) - margin - gapH / 2;
  if (hi <= lo)
    return h / 2;
  return lo + (int)random((long)(hi - lo + 1));
}

static void flappyResetWorld(int w, int h)
{
  s_flappyPlaying = true;

  s_flappyDistancePx = 0;

  s_fbX = 52;
  s_fbY = h / 2;
  s_fbVY = 0;

  const int spacing = 140;
  const int startX = w + 30;

  for (int i = 0; i < 3; ++i)
  {
    s_pipes[i].x = startX + i * spacing;
    s_pipes[i].gapY = flappyRandGapY(h);
    s_pipes[i].passed = false;
  }

  s_flappyGoalActive = false;
  s_flappyGoalReached = false;
  s_flappyGoalX = 0;
  s_flappyGoalY = h - 42;

  s_impHit = false;
  s_impBurnDone = false;
  s_impFrame = 0;
  s_impAnimMs = 0;
  s_impHoldMs = 0;
  s_flappyGoalReached = false;
}

static void flappyDirFromBgPath(const char *bgPath, char *outDir, size_t outSz)
{
  if (!outDir || outSz == 0)
    return;
  outDir[0] = 0;

  if (!bgPath || !bgPath[0])
    return;

  const char *lastSlash = strrchr(bgPath, '/');
  if (!lastSlash)
  {
    // no slash; treat as current dir
    strlcpy(outDir, "", outSz);
    return;
  }

  const size_t len = (size_t)(lastSlash - bgPath + 1); // include trailing slash
  if (len >= outSz)
    return;

  memcpy(outDir, bgPath, len);
  outDir[len] = 0;
}

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

void freeFlappyPipeSprites()
{
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "pipe_up");
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "pipe_down");

  s_flappyPipeW = 0;
  s_flappyPipeH = 0;
  s_flappyPipeLoadFailed = false;
}

static bool ensureImpWaveSprites() { return true; }

static bool ensureFlappyFireballSprites(const char *bgPath)
{
  if (!bgPath || !bgPath[0] || !g_sdReady)
    return false;

  char dir[128];
  flappyDirFromBgPath(bgPath, dir, sizeof(dir));
  if (!dir[0])
    return false;

  char path1[192];
  char path2[192];
  char path3[192];
  snprintf(path1, sizeof(path1), "%sfireball1.png", dir);
  snprintf(path2, sizeof(path2), "%sfireball2.png", dir);
  snprintf(path3, sizeof(path3), "%sfireball3.png", dir);

  M5Canvas *fb1 = nullptr;
  M5Canvas *fb2 = nullptr;
  M5Canvas *fb3 = nullptr;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball1", path1, 8, kFireKey, fb1))
    return false;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball2", path2, 8, kFireKey, fb2))
    return false;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball3", path3, 8, kFireKey, fb3))
    return false;

  if (!fb1 || fb1->width() <= 0 || fb1->height() <= 0)
    return false;

  s_flappyFireballW = (int)fb1->width();
  s_flappyFireballH = (int)fb1->height();
  return true;
}

static const uint16_t kPipeKey = kSpriteKey;

static bool ensureFlappyPipeSprites(const char *bgPath)
{
  if (s_flappyPipeLoadFailed)
    return false;

  if (!bgPath || !bgPath[0] || !g_sdReady)
    return false;

  char dir[128];
  flappyDirFromBgPath(bgPath, dir, sizeof(dir));
  if (!dir[0])
    return false;

  char upPath[192];
  char downPath[192];
  snprintf(upPath, sizeof(upPath), "%srock_spike_up.png", dir);
  snprintf(downPath, sizeof(downPath), "%srock_spike_down.png", dir);

  M5Canvas *pipeUp = nullptr;
  M5Canvas *pipeDown = nullptr;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "pipe_up", upPath, 8, kPipeKey, pipeUp))
  {
    s_flappyPipeLoadFailed = true;
    return false;
  }

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "pipe_down", downPath, 8, kPipeKey, pipeDown))
  {
    s_flappyPipeLoadFailed = true;
    return false;
  }

  if (!pipeUp || pipeUp->width() <= 0 || pipeUp->height() <= 0)
  {
    s_flappyPipeLoadFailed = true;
    return false;
  }

  s_flappyPipeW = (int)pipeUp->width();
  s_flappyPipeH = (int)pipeUp->height();
  s_flappyPipeLoadFailed = false;
  return true;
}

void startFlappyFireball()
{
  mgPauseReset();

  // Ensure keyboard nav works (ENTER/W) even if console/text mode was left on.
  inputSetTextCapture(false);

  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;
  s_resultShown = false;

  mgClearRewardState();
  mgResetAcceptState();

  currentMiniGame = MiniGame::FLAPPY_FIREBALL;
  mgAssetsBeginSession(currentMiniGame, "startFlappyFireball");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("flappy-start-beginSession");

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  logMiniGameHeap("startFlappyFireball");

  miniGameSetReturnUi(retUi);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_flappyInited = true;
  s_flappyShowIntro = true;
  s_flappyDontShowAgain = false;
  s_impHit = false;
  s_impBurnDone = false;
  s_impFrame = 0;
  s_impAnimMs = 0;
  s_impHoldMs = 0;
  s_lastStepMs = millis();

  s_flappyBgScrollX = 0;

  mgmem::logUsage("flappy-before-bg-reset");
  freeFlappyBgCache();
  mgmem::logUsage("flappy-after-bg-reset");

  const char *bgPath = flappyBgPathForPet();

  const bool bgOk = ensureFlappyBgCache(bgPath);
  mgmem::logUsage("flappy-after-bg-ensure");

  freeFlappyPipeSprites();
  freeFlappyFireballSprites();
  mgmem::logUsage("flappy-after-sprite-free");

  const bool pipeOk = ensureFlappyPipeSprites(bgPath);
  const bool fireballOk = ensureFlappyFireballSprites(bgPath);
  const bool impOk = ensureImpWaveSprites();

  mgmem::logUsage("flappy-after-preload");

  Serial.printf("FLAPPY preload: bg=%d pipes=%d fireball=%d imp=%d free=%u largest=%u\n", bgOk ? 1 : 0, pipeOk ? 1 : 0,
                fireballOk ? 1 : 0, impOk ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  mgmem::logUsage("flappy-start-complete");
}

static bool flappyCollides(int fbX, int fbY, int r, const FlappyPipe &p, int w, int h)
{
  const int pipeW = 26;
  const int gapH = 64;

  const int kPipeInsetPx = 2;
  const int kGapBonusPx = 4;
  const int kScreenForgive = 2;

  if (fbY - r < -kScreenForgive)
    return true;
  if (fbY + r >= h + kScreenForgive)
    return true;

  int pipeL = p.x + kPipeInsetPx;
  int pipeR = p.x + pipeW - kPipeInsetPx;

  if (fbX + r < pipeL)
    return false;
  if (fbX - r > pipeR)
    return false;

  int gapTop = (p.gapY - gapH / 2) - kGapBonusPx;
  int gapBot = (p.gapY + gapH / 2) + kGapBonusPx;

  if (gapTop < 0)
    gapTop = 0;
  if (gapBot > h)
    gapBot = h;

  if (fbY - r < gapTop)
    return true;
  if (fbY + r > gapBot)
    return true;

  return false;
}

static void flappyStep(int w, int h, bool flap)
{
  const int pipeW = 26;
  const int speedX = 1;

  s_flappyBgScrollX += speedX;
  s_flappyDistancePx += speedX;

  const int fbR = 4;
  const int gravity = 1;
  const int flapVY = -3;

  static uint8_t s_gravCounter = 0;

  if (flap)
  {
    s_fbVY = flapVY;
    soundFlap();
  }

  s_gravCounter++;
  if (s_gravCounter >= 3)
  {
    s_gravCounter = 0;
    s_fbVY += gravity;
  }

  if (s_fbVY > 4)
    s_fbVY = 4;
  if (s_fbVY < -6)
    s_fbVY = -6;

  s_fbY += s_fbVY;

  int rightMost = s_pipes[0].x;
  for (int i = 1; i < 3; ++i)
    if (s_pipes[i].x > rightMost)
      rightMost = s_pipes[i].x;

  for (int i = 0; i < 3; ++i)
  {
    s_pipes[i].x -= speedX;

    if (s_pipes[i].x < -pipeW)
    {
      s_pipes[i].x = rightMost + 140;
      s_pipes[i].gapY = flappyRandGapY(h);
      s_pipes[i].passed = false;
      rightMost = s_pipes[i].x;
    }
  }

  if (!s_flappyGoalActive && s_flappyDistancePx >= 520)
  {
    s_flappyGoalActive = true;
    s_flappyGoalX = w + 120;
    s_flappyGoalY = h - 26;
  }

  if (s_flappyGoalActive)
  {
    s_flappyGoalX -= speedX;
  }

  for (int i = 0; i < 3; ++i)
  {
    if (flappyCollides(s_fbX, s_fbY, fbR, s_pipes[i], w, h))
    {
      playerWon = false;
      g_app.gameOver = true;
      requestUIRedraw();
      s_resultShown = true;
      s_flappyPlaying = false;
      soundError();
      return;
    }
  }

  if (s_flappyGoalActive && !s_flappyGoalReached)
  {
    const int impW = 48;
    const int impH = 48;
    const int islandH = 8;

    const int goalLeft = s_flappyGoalX;
    const int goalTop = s_flappyGoalY - impH;
    const int goalRight = goalLeft + impW;
    const int goalBottom = s_flappyGoalY + islandH;

    if ((s_fbX + fbR) >= goalLeft && (s_fbX - fbR) <= goalRight && (s_fbY + fbR) >= goalTop &&
        (s_fbY - fbR) <= goalBottom)
    {
      if (!s_impHit)
      {
        s_impHit = true;
        s_flappyGoalReached = true;
        s_impFrame = 0;
        s_impAnimMs = 0;
        s_impHoldMs = 0;
        soundConfirm();
      }
    }
  }
}

static inline uint32_t flappyAliveMsNow(uint32_t now)
{
  uint32_t elapsed = now - s_flappyStartMs;

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

void updateFlappyFireball(const InputState &input)
{
  const bool enterOnce = miniGameEnterOnce(input);
  const uint32_t now = millis();

  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  // ---------------------------------------------------------------------------
  // Reward modal
  // ---------------------------------------------------------------------------
  if (mgRewardShowing())
  {
    if (enterOnce && !mgInputLockedOut())
    {
      mgClearRewardState();
      mgResetAcceptState();
      exitMiniGameToReturnUi(true);
    }
    return;
  }

  // ---------------------------------------------------------------------------
  // Game over: transition directly into the reward modal
  // ---------------------------------------------------------------------------
  if (g_app.gameOver)
  {
    mgApplyResultAndShowReward(playerWon);
    mgResetAcceptState();
    mgBeginInputLockout(180);
    clearInputLatch();
    inputForceClear();
    return;
  }

  if (s_flappyShowIntro)
  {
    const uint32_t dt = (s_lastStepMs == 0) ? 0 : (now - s_lastStepMs);
    s_lastStepMs = now;

    s_impAnimMs += dt;
    while (s_impAnimMs >= kImpWaveFrameMs)
    {
      s_impAnimMs -= kImpWaveFrameMs;
      s_impFrame = (s_impFrame + 1) % 2;
    }

    if (input.mgQuitOnce && !mgInputLockedOut())
    {
      miniGameCancelFromIntro();
            return;
    }

    const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;

    if (startPressed && !mgInputLockedOut())
    {
      s_flappyShowIntro = false;
      flappyResetWorld(gW, gH);
      s_lastStepMs = now;
      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  // INIT
  if (!s_flappyInited)
  {
    s_flappyInited = true;
    flappyResetWorld(gW, gH);
    s_lastStepMs = millis();
  }

  if (mgPauseIsPaused())
  {
    s_lastStepMs = now;
    return;
  }

  if (mgPauseJustResumedConsume())
  {
    s_lastStepMs = now;
  }

  const bool flap = input.mgSelectOnce || input.mgSelectHeld || input.mgUpOnce || input.mgUpHeld;

  const uint32_t stepMs = 16;

  if ((int32_t)(now - s_lastStepMs) >= (int32_t)stepMs)
  {
    bool flapUsed = false;
    int steps = 0;
    const int kMaxStepsPerFrame = 5;

    while ((int32_t)(now - s_lastStepMs) >= (int32_t)stepMs && steps < kMaxStepsPerFrame)
    {
      const bool flapThisStep = (flap && !flapUsed);

      if (!s_impHit)
      {
        flappyStep(gW, gH, flapThisStep);
      }

      const uint32_t dt = stepMs;

      if (!s_impHit)
      {
        s_impAnimMs += dt;
        if (s_impAnimMs >= kImpWaveFrameMs)
        {
          s_impAnimMs -= kImpWaveFrameMs;
          s_impFrame = (s_impFrame + 1) % 2;
        }
      }
      else
      {
        s_impAnimMs += dt;

        // Advance through burn frames 0..4
        if (s_impFrame < 4)
        {
          if (s_impAnimMs >= kImpBurnFrameMs)
          {
            s_impAnimMs = 0;
            s_impFrame++;

            if (s_impFrame > 4)
              s_impFrame = 4;
          }
        }
        else
        {
          // Hold on the final burn frame before winning
          if (s_impAnimMs >= kImpLastFrameHoldMs)
          {
            playerWon = true;
            g_app.gameOver = true;
            requestUIRedraw();
            s_resultShown = true;
            s_flappyPlaying = false;
          }
        }
      }

      if (flapThisStep)
        flapUsed = true;

      s_lastStepMs += stepMs;
      steps++;

      if (g_app.gameOver)
        break;
    }

    // If we fell WAY behind (SD hitch), snap forward so we don't "fast-forward death".
    if ((int32_t)(now - s_lastStepMs) >= (int32_t)stepMs)
      s_lastStepMs = now;
  }
}

static void drawRewardModal(int gW, int gH)
{
  spr.fillSprite(TFT_BLACK);
  spr.setTextDatum(CC_DATUM);

  // ---------------------------------------------------------
  // BIG WIN / LOSE HEADER
  // ---------------------------------------------------------
  spr.setTextColor(playerWon ? TFT_GREEN : TFT_RED, TFT_BLACK);
  spr.drawCentreString(playerWon ? "YOU WIN!" : "YOU LOSE!", gW / 2, gH / 2 - 36,
                       4 // big font
  );

  // ---------------------------------------------------------
  // Reward body text (supports 1 or 2 lines)
  // ---------------------------------------------------------
  const char *msg = mgRewardMessage();
  const char *nl = strchr(msg, '\n');

  if (nl)
  {
    char line1[64];
    size_t len = (size_t)(nl - msg);
    if (len > sizeof(line1) - 1)
      len = sizeof(line1) - 1;

    memcpy(line1, msg, len);
    line1[len] = 0;

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(line1, gW / 2, gH / 2 - 4, 2);

    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawCentreString(nl + 1, gW / 2, gH / 2 + 16, 2);
  }
  else
  {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(msg, gW / 2, gH / 2, 2);
  }

  // ---------------------------------------------------------
  // Footer
  // ---------------------------------------------------------
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawCentreString("Press ENTER", gW / 2, gH / 2 + 40, 2);
}

// -----------------------------------------------------------------------------
// Flappy scrolling background (cached RGB565 + per-pet theme)
// -----------------------------------------------------------------------------
static const char *flappyBgPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/eld_flap_bg.jpg";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/dev_flap_bg.jpg";
  }
}

void freeFlappyBgCache()
{
  mgAssetsReleaseSharedBgIfOwner(MiniGame::FLAPPY_FIREBALL);
  s_flappyBgW = 0;
  s_flappyBgH = 0;
  s_flappyBgReady = false;
  s_flappyBgLoadFailed = false;
  s_flappyBgPath[0] = 0;
}

static bool ensureFlappyBgCache(const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  if (s_flappyBgReady && strcmp(s_flappyBgPath, path) == 0)
    return true;

  if (s_flappyBgLoadFailed && strcmp(s_flappyBgPath, path) == 0)
    return false;

  s_flappyBgReady = false;
  s_flappyBgLoadFailed = false;
  strlcpy(s_flappyBgPath, path, sizeof(s_flappyBgPath));

  if (!mgAssetsEnsureSharedBg(MiniGame::FLAPPY_FIREBALL, path))
  {
    s_flappyBgLoadFailed = true;
    s_flappyBgW = 0;
    s_flappyBgH = 0;
    return false;
  }

  s_flappyBgW = mgAssetsSharedBgW();
  s_flappyBgH = mgAssetsSharedBgH();

  if (s_flappyBgW <= 0 || s_flappyBgH <= 0 || mgAssetsSharedBg() == nullptr)
  {
    s_flappyBgLoadFailed = true;
    s_flappyBgW = 0;
    s_flappyBgH = 0;
    return false;
  }

  s_flappyBgReady = true;
  return true;
}

void drawFlappyFireball()
{
  const int gW = (int)spr.width();
  const int gH = (int)spr.height();

  if (mgRewardShowing())
  {
    drawRewardModal(gW, gH);
    return;
  }

  if (g_app.gameOver)
  {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(playerWon ? TFT_GREEN : TFT_RED, TFT_BLACK);
    spr.drawCentreString(playerWon ? "YOU WIN!" : "YOU LOSE!", gW / 2, gH / 2 - 10, 4);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Press ENTER", gW / 2, gH / 2 + 22, 2);
    return;
  }

  if (s_flappyShowIntro)
  {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(CC_DATUM);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Press Enter or G to boost", gW / 2, 4, 2);
    spr.drawCentreString("Torch the Imp!", gW / 2, 22, 2);

    const int impX = (gW - 48) / 2;
    const int impY = 40;

    if (ensureImpWaveSprites())
    {
      const char *impSprite = (s_impFrame == 0) ? flappyImpWave1PathForPet() : flappyImpWave2PathForPet();

      if (impSprite)
        sprDrawPngFromSD(impSprite, impX, impY);
    }

    const int cbY = 100;
    const int cbSize = 10;
    const int textOffset = 16;
    const int lineWidth = 150;
    const int cbX = (gW - lineWidth) / 2;

    spr.drawRect(cbX, cbY, cbSize, cbSize, TFT_WHITE);

    if (s_flappyDontShowAgain)
    {
      spr.drawLine(cbX + 2, cbY + 5, cbX + 4, cbY + 7, TFT_WHITE);
      spr.drawLine(cbX + 4, cbY + 7, cbX + 8, cbY + 2, TFT_WHITE);
    }

    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString("Don't show again (Space)", cbX + textOffset, cbY + 5, 2);

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 116, 2);
    return;
  }

  const char *bgPath = flappyBgPathForPet();
  const bool haveFireball = ensureFlappyFireballSprites(bgPath);
  const bool havePipes = ensureFlappyPipeSprites(bgPath);

  M5Canvas *pipeUp = nullptr;
  M5Canvas *pipeDown = nullptr;
  M5Canvas *fbFrame0 = nullptr;
  M5Canvas *fbFrame1 = nullptr;
  M5Canvas *fbFrame2 = nullptr;

  if (havePipes)
  {
    char dir[128];
    flappyDirFromBgPath(bgPath, dir, sizeof(dir));

    char upPath[192];
    char downPath[192];
    snprintf(upPath, sizeof(upPath), "%srock_spike_up.png", dir);
    snprintf(downPath, sizeof(downPath), "%srock_spike_down.png", dir);

    mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "pipe_up", upPath, 8, kPipeKey, pipeUp);
    mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "pipe_down", downPath, 8, kPipeKey, pipeDown);
  }

  if (haveFireball)
  {
    char dir[128];
    flappyDirFromBgPath(bgPath, dir, sizeof(dir));

    char path1[192];
    char path2[192];
    char path3[192];
    snprintf(path1, sizeof(path1), "%sfireball1.png", dir);
    snprintf(path2, sizeof(path2), "%sfireball2.png", dir);
    snprintf(path3, sizeof(path3), "%sfireball3.png", dir);

    mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball1", path1, 8, kFireKey, fbFrame0);
    mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball2", path2, 8, kFireKey, fbFrame1);
    mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball3", path3, 8, kFireKey, fbFrame2);
  }

  M5Canvas *bg = nullptr;
  int bw = 0;
  int bh = 0;
  const bool haveBg = mgmem::ensureSharedBg(flappyBgPathForPet(), bg, bw, bh);

  bool drewBg = false;

  if (haveBg && bg && bw > 0 && bh > 0)
  {
    int x = -(s_flappyBgScrollX % bw);
    if (x > 0)
      x -= bw;

    bg->pushSprite(&spr, x, 0);
    bg->pushSprite(&spr, x + bw, 0);
    drewBg = true;
  }

  if (!drewBg)
    spr.fillSprite(TFT_BLACK);

  const int gapH = 64;
  const int pipeW = 26;

  for (int i = 0; i < 3; ++i)
  {
    const int x = s_pipes[i].x;
    const int gapTop = s_pipes[i].gapY - gapH / 2;
    const int gapBot = s_pipes[i].gapY + gapH / 2;

    if (havePipes && pipeUp && pipeDown)
    {
      const int drawX = x + (pipeW - s_flappyPipeW) / 2;

      pipeDown->pushSprite(&spr, drawX, gapTop - s_flappyPipeH, kPipeKey);
      pipeUp->pushSprite(&spr, drawX, gapBot, kPipeKey);
    }
    else
    {
      // fallback pipes
      spr.fillRect(x, 0, pipeW, gapTop, TFT_DARKGREY);
      spr.fillRect(x, gapBot, pipeW, gH - gapBot, TFT_DARKGREY);
    }
  }

  if (s_flappyGoalActive && !s_impBurnDone)
  {
    const int islandW = 48;
    const int islandH = 8;
    const int impH = 48;

    const int islandX = s_flappyGoalX - 4;
    const int islandY = s_flappyGoalY;

    const int impX = s_flappyGoalX;
    const int impY = s_flappyGoalY - impH;

    if (!s_impHit)
    {
      if (ensureImpWaveSprites())
      {
        const char *impSprite = (s_impFrame == 0) ? flappyImpWave1PathForPet() : flappyImpWave2PathForPet();

        if (impSprite)
          sprDrawPngFromSD(impSprite, impX, impY);
      }
    }
    else
    {
      const char *impSprite = kImpBurnFrames[s_impFrame];
      if (impSprite)
        sprDrawPngFromSD(impSprite, impX, impY);
    }
  }

  if (!s_impHit)
  {
    M5Canvas *fbFrames[3] = {fbFrame0, fbFrame1, fbFrame2};

    if (haveFireball && fbFrames[0] && fbFrames[1] && fbFrames[2])
    {
      const int frame = (millis() / 80) % 3;
      M5Canvas *fb = fbFrames[frame];

      const int w = fb->width();
      const int h = fb->height();

      const int drawX = s_fbX - w / 2;
      const int drawY = s_fbY - h / 2;

      spr.fillCircle(s_fbX, s_fbY, 6, TFT_RED);
      fb->pushSprite(&spr, drawX, drawY, kFireKey);
    }
    else
    {
      spr.fillCircle(s_fbX, s_fbY, 5, TFT_ORANGE);
    }
  }
}

// -----------------------------------------------------------------------------
// Resurrection Run (side-scroller runner) GLOBALS
// -----------------------------------------------------------------------------
static bool s_rrShowIntro = true;
static bool s_rrDontShowAgain = false; // visual only for now
static uint32_t s_rrIntroAnimMs = 0;

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

static int s_rrSkyW = 0;
static int s_rrSkyH = 0;

static void rrResetObstacles();

static const char *resRunSkyTilePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/sky_tile.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/sky_tile.png";
  }
}

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

static constexpr int kRrHandTriggerDist = 2200;
static constexpr int kRrHandEnterSpeed = 2;
static constexpr int kRrHandExitSpeed = 3;
static constexpr uint32_t kRrHandHoldMs = 250;
static constexpr uint32_t kRrHandContactHoldMs = 350;
static constexpr uint32_t kRrWinHoldMs = 500;

static bool ensureResRunSkySprite()
{
  M5Canvas *sky = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "sky_tile", resRunSkyTilePathForPet(), 8, kSpriteKey, sky))
    return false;

  if (!sky || sky->width() <= 0 || sky->height() <= 0)
    return false;

  s_rrSkyW = (int)sky->width();
  s_rrSkyH = (int)sky->height();
  return true;
}

static const char *resRunSnakeCrouchPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/snake_crouch.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/snake_jump.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/snake_jump.png";
  }
}

struct RRObs
{
  int x;
  int y;
  int w;
  int h;
  bool active;
};

static RRObs rr_obs[8];
static int rr_courseLen = 2600;

struct RRSpawn
{
  int triggerDist;
  uint8_t type;
  uint8_t param;
};

static const uint8_t RR_SPIKE = 0;
static const uint8_t RR_LOW_FIRE = 1;

static const RRSpawn rr_script[] = {
    {520, RR_SPIKE, 0},     {860, RR_LOW_FIRE, 0}, {1200, RR_SPIKE, 0},
    {1540, RR_LOW_FIRE, 0}, {1880, RR_SPIKE, 0},   {2220, RR_LOW_FIRE, 0},
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

static const uint16_t kResRunKey = kSpriteKey;

static const char *resRunHand1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/resrun/eld/hand_1.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/hand_2.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/ladybug_ground.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/ladybug_fly1.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/ladybug_fly2.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/snake_run1.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/snake_run2.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/snake_win1.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/snake_win2.png";
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
    return "/raising_hell/graphics/mini_games/resrun/eld/branch_ground.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/resrun/dev/branch_ground.png";
  }
}

static bool ensureResRunHandSprites()
{
  M5Canvas *hand1 = nullptr;
  M5Canvas *hand2 = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "hand_1", resRunHand1PathForPet(), 8, kResRunKey, hand1))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "hand_2", resRunHand2PathForPet(), 8, kResRunKey, hand2))
    return false;

  if (!hand1 || hand1->width() <= 0 || hand1->height() <= 0)
    return false;

  s_rrHandW = (int)hand1->width();
  s_rrHandH = (int)hand1->height();
  return true;
}

static bool ensureResRunLadybugSprites()
{
  M5Canvas *groundBug = nullptr;
  M5Canvas *fly1 = nullptr;
  M5Canvas *fly2 = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_ground", resRunLadybugGroundPathForPet(), 8, kResRunKey,
                           groundBug))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_fly1", resRunLadybugFly1PathForPet(), 8, kResRunKey, fly1))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "ladybug_fly2", resRunLadybugFly2PathForPet(), 8, kResRunKey, fly2))
    return false;

  if (!groundBug || groundBug->width() <= 0 || groundBug->height() <= 0)
    return false;

  s_rrLadybugW = (int)groundBug->width();
  s_rrLadybugH = (int)groundBug->height();
  return true;
}

static bool ensureResRunSnakeSprites()
{
  M5Canvas *run1 = nullptr;
  M5Canvas *run2 = nullptr;
  M5Canvas *crouch = nullptr;
  M5Canvas *jump = nullptr;
  M5Canvas *win1 = nullptr;
  M5Canvas *win2 = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run1", resRunSnakeRun1PathForPet(), 8, kResRunKey, run1))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run2", resRunSnakeRun2PathForPet(), 8, kResRunKey, run2))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_crouch", resRunSnakeCrouchPathForPet(), 8, kResRunKey,
                           crouch))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_jump", resRunSnakeJumpPathForPet(), 8, kResRunKey, jump))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_win1", resRunSnakeWin1PathForPet(), 8, kResRunKey, win1))
    return false;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_win2", resRunSnakeWin2PathForPet(), 8, kResRunKey, win2))
    return false;

  if (!run1 || run1->width() <= 0 || run1->height() <= 0)
    return false;

  s_rrSnakeW = (int)run1->width();
  s_rrSnakeH = (int)run1->height();

  if (crouch && crouch->width() > 0 && crouch->height() > 0)
  {
    s_rrSnakeCrouchW = (int)crouch->width();
    s_rrSnakeCrouchH = (int)crouch->height();
  }

  if (jump && jump->width() > 0 && jump->height() > 0)
  {
    s_rrSnakeJumpW = (int)jump->width();
    s_rrSnakeJumpH = (int)jump->height();
  }

  return true;
}

static bool ensureResRunGroundSprite()
{
  M5Canvas *ground = nullptr;

  if (!mgmem::ensureSprite(MiniGame::RESURRECTION, "branch_ground", resRunBranchGroundPathForPet(), 8, kResRunKey,
                           ground))
    return false;

  if (!ground || ground->width() <= 0 || ground->height() <= 0)
    return false;

  s_rrGroundW = (int)ground->width();
  s_rrGroundH = (int)ground->height();
  return true;
}

void freeResRunSprites()
{
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_run1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_run2");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_crouch");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_jump");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "branch_ground");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "sky_tile");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "hand_1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "hand_2");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "ladybug_ground");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "ladybug_fly1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "ladybug_fly2");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_win1");
  mgmem::releaseSprite(MiniGame::RESURRECTION, "snake_win2");

  s_rrSnakeW = 0;
  s_rrSnakeH = 0;
  s_rrSnakeCrouchW = 0;
  s_rrSnakeCrouchH = 0;
  s_rrSnakeJumpW = 0;
  s_rrSnakeJumpH = 0;
  s_rrGroundW = 0;
  s_rrGroundH = 0;
  s_rrSkyW = 0;
  s_rrSkyH = 0;
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

  rr_courseLen = 2600;
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
  s_resultShown = true;

  mgmem::logUsage(won ? "rr finish win" : "rr finish loss");
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

  if (type == RR_SPIKE)
  {
    rr_obs[slot].x = spawnWorldX;
    rr_obs[slot].y = groundY - kRrJumpObsH;
    rr_obs[slot].w = kRrJumpObsW;
    rr_obs[slot].h = kRrJumpObsH;
    rr_obs[slot].active = true;
  }
  else
  {
    rr_obs[slot].x = spawnWorldX;
    rr_obs[slot].y = groundY - kRrPlayerH - kRrDuckObsClearance;
    rr_obs[slot].w = kRrDuckObsW;
    rr_obs[slot].h = kRrDuckObsH;
    rr_obs[slot].active = true;
  }
}

static void rrResetObstacles()
{
  for (auto &o : rr_obs)
    o = {0, 0, 0, 0, false};
}

static bool rrAabb(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
  return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}

void startResurrectionRun()
{
  mgPauseReset();
  inputSetTextCapture(false);

  currentMiniGame = MiniGame::RESURRECTION;
  mgAssetsBeginSession(currentMiniGame, "startResurrectionRun");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("rr beginSession");
  freeResRunSprites();

  miniGameSetReturnUi(UIState::DEATH);
  
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  const bool snakeOk = ensureResRunSnakeSprites();
  const bool groundOk = ensureResRunGroundSprite();
  const bool skyOk = ensureResRunSkySprite();
  const bool handOk = ensureResRunHandSprites();
  const bool ladybugOk = ensureResRunLadybugSprites();

  Serial.printf("RESRUN preload: snake=%d ground=%d sky=%d hand=%d ladybug=%d run=%dx%d crouch=%dx%d jump=%dx%d "
                "sky=%dx%d hand=%dx%d bug=%dx%d free=%u largest=%u\n",
                snakeOk ? 1 : 0, groundOk ? 1 : 0, skyOk ? 1 : 0, handOk ? 1 : 0, ladybugOk ? 1 : 0, s_rrSnakeW,
                s_rrSnakeH, s_rrSnakeCrouchW, s_rrSnakeCrouchH, s_rrSnakeJumpW, s_rrSnakeJumpH, s_rrSkyW, s_rrSkyH,
                s_rrHandW, s_rrHandH, s_rrLadybugW, s_rrLadybugH, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

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

  invalidateBackgroundCache();
  s_rrShowIntro = true;
  s_rrDontShowAgain = false;
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
    if (enterOnce && !mgInputLockedOut())
    {
      mgClearRewardState();
      mgResetAcceptState();

      mgmem::logUsage(rr_won ? "rr accept win" : "rr accept loss");
      mgmem::endSession();

      rr_active = false;
      currentMiniGame = MiniGame::NONE;
      g_app.inMiniGame = false;
      g_app.gameOver = false;

      onResurrectionMiniGameResult(rr_won);

      clearInputLatch();
      inputForceClear();
      mgPauseReset();
      mgBeginInputLockout(220);
      requestUIRedraw();
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
      if (input.mgSpaceOnce)
        s_rrDontShowAgain = !s_rrDontShowAgain;
  
      const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;
      
    if (startPressed && !mgInputLockedOut())
    {
      s_rrShowIntro = false;
      rrResetRunState();
      rr_lastMs = now;
      s_rrAnimMs = now;
      s_rrHandAnimMs = now;
      s_rrLadybugAnimMs = now;

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
      s_rrHandX = gW;
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

    // Snake stays locked in the contact position for the rest of the sequence.
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
    drawRewardModal(gW, gH);
    return;
  }

  const int groundY = gH - kRrGroundH;

  const bool haveSnake = ensureResRunSnakeSprites();
  const bool haveGround = ensureResRunGroundSprite();
  const bool haveSky = ensureResRunSkySprite();
  const bool haveHand = ensureResRunHandSprites();
  const bool haveLadybug = ensureResRunLadybugSprites();

  M5Canvas *snake1 = nullptr;
  M5Canvas *snake2 = nullptr;
  M5Canvas *snakeCrouch = nullptr;
  M5Canvas *snakeJump = nullptr;
  M5Canvas *snakeWin1 = nullptr;
  M5Canvas *snakeWin2 = nullptr;
  M5Canvas *groundSpr = nullptr;
  M5Canvas *skySpr = nullptr;
  M5Canvas *hand1 = nullptr;
  M5Canvas *hand2 = nullptr;
  M5Canvas *ladybugGround = nullptr;
  M5Canvas *ladybugFly1 = nullptr;
  M5Canvas *ladybugFly2 = nullptr;

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

  if (haveSky)
  {
    mgmem::ensureSprite(MiniGame::RESURRECTION, "sky_tile", resRunSkyTilePathForPet(), 8, kResRunKey, skySpr);
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
    spr.drawCentreString("Up/Down to Jump/Duck G to boost", gW / 2, 8, 2);
    spr.drawCentreString("Deliver the apple to our ally", gW / 2, 26, 2);

    M5Canvas *introSnake1 = nullptr;
    M5Canvas *introSnake2 = nullptr;

    if (haveSnake)
    {
      mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run1", resRunSnakeRun1PathForPet(), 8, kResRunKey,
                          introSnake1);
      mgmem::ensureSprite(MiniGame::RESURRECTION, "snake_run2", resRunSnakeRun2PathForPet(), 8, kResRunKey,
                          introSnake2);
    }

    M5Canvas *introSnake = (s_rrAnimFrame == 0) ? introSnake1 : introSnake2;

    if (introSnake && introSnake->width() > 0 && introSnake->height() > 0)
    {
      const int sx = (gW - (int)introSnake->width()) / 2;
      const int sy = (gH / 2) - ((int)introSnake->height() / 2) + 6;
      introSnake->pushSprite(&spr, sx, sy, kResRunKey);
    }

    const int cbY = 102;
    const int cbSize = 10;
    const int textOffset = 16;
    const int lineWidth = 150;
    const int cbX = (gW - lineWidth) / 2;

    spr.drawRect(cbX, cbY, cbSize, cbSize, TFT_WHITE);

    if (s_rrDontShowAgain)
    {
      spr.drawLine(cbX + 2, cbY + 5, cbX + 4, cbY + 7, TFT_WHITE);
      spr.drawLine(cbX + 4, cbY + 7, cbX + 8, cbY + 2, TFT_WHITE);
    }

    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString("Don't show again (Space)", cbX + textOffset, cbY + 5, 2);

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

  if (skySpr && skySpr->width() > 0 && skySpr->height() > 0)
  {
    const int tileW = (int)skySpr->width();
    const int tileH = (int)skySpr->height();
    const int skyScrollX = (rr_distance / 6) % tileW;

    for (int y = 0; y < groundY; y += tileH)
    {
      for (int x = -skyScrollX; x < gW; x += tileW)
        skySpr->pushSprite(&spr, x, y, kResRunKey);
    }
  }
  else
  {
    spr.fillRect(0, 0, gW, groundY, TFT_CYAN);
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

  spr.drawFastHLine(0, groundY, gW, TFT_DARKGREY);

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

    if (ladybugGround && ladybugGround->width() > 0 && ladybugGround->height() > 0)
    {
      const int drawW = o.w;
      const int drawH = o.h;
      const int drawX = ox;
      const int drawY = o.y;

      ladybugGround->pushRotateZoom(&spr, drawX + drawW / 2, drawY + drawH / 2, 0.0f,
                                    (float)drawW / (float)ladybugGround->width(),
                                    (float)drawH / (float)ladybugGround->height(), kResRunKey);
    }
    else
    {
      spr.fillRoundRect(ox, o.y, o.w, o.h, 4, TFT_GREEN);
    }
  }

  int barW = gW - 20;
  int barX = 10;
  int barY = 8;
  spr.drawRect(barX, barY, barW, 6, TFT_DARKGREY);

  int fill = (rr_distance * (barW - 2)) / rr_courseLen;
  if (fill < 0)
    fill = 0;
  if (fill > barW - 2)
    fill = barW - 2;

  spr.fillRect(barX + 1, barY + 1, fill, 4, TFT_YELLOW);
}

// -----------------------------------------------------------------------------
// CROSSY HELL GLOBALS (Frogger-style)
// -----------------------------------------------------------------------------
static bool s_crossyShowIntro = true;
static bool s_crossyDontShowAgain = false; // visual only for now
static uint32_t s_crossyIntroImpAnimMs = 0;

static const int kCrossyCols = 15;
static const int kCrossyRows = 7;

static const int kCrossyTileW = 16;
static const int kCrossyTileH = 19;

static const int kCrossyOriginX = 0;
static const int kCrossyOriginY = 1;

static int s_crossyCarryPxAccum[kCrossyRows] = {0};
static uint8_t s_crossyLaneTick[kCrossyRows] = {0};

static uint8_t s_crossyLavaFrame = 0;
static uint32_t s_crossyLavaAnimMs = 0;
static uint8_t s_crossyLandingGraceFrames = 0;

static uint32_t s_crossyWinPoseStart = 0;
static bool s_crossyWinPoseActive = false;

enum CrossyLaneType : uint8_t
{
  CROSSY_LANE_SAFE = 0,
  CROSSY_LANE_ROAD,
  CROSSY_LANE_WATER,
  CROSSY_LANE_GOAL
};

struct CrossyLane
{
  CrossyLaneType type;
  int8_t dir;
  uint8_t speed;
  uint8_t moverLen;
  uint16_t gapPx;
  int32_t offsetPx;
  uint8_t platSize;
};

enum CrossyFacing : uint8_t
{
  CROSSY_FACE_DOWN = 0,
  CROSSY_FACE_UP,
  CROSSY_FACE_LEFT,
  CROSSY_FACE_RIGHT
};

enum CrossyPlatformSize
{
  CROSSY_PLAT_LARGE = 0,
  CROSSY_PLAT_SMALL,
  CROSSY_PLAT_XS
};

static CrossyLane s_crossyLanes[kCrossyRows];

static int s_crossyPx = 0;
static int s_crossyPy = 0;
static int s_crossyVisualOffsetPx = 0;
static CrossyFacing s_crossyFacing = CROSSY_FACE_DOWN;

static bool s_crossyInited = false;
uint32_t s_crossyLastLaneMs = 0;

static inline int crossyClamp(int v, int lo, int hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static const char *crossyStoneSmallPathForPet();
static const char *crossyStoneXSPathForPet();
static const char *crossyImpPathForPet(CrossyFacing facing);

static bool ensureCrossyStartZoneSprite();
static bool ensureCrossyGoalZoneSprite();
static bool ensureCrossyLavaZoneSprite(uint8_t frame);
static bool ensureCrossyStoneSprite();
static bool ensureCrossyStoneSmallSprite();
static bool ensureCrossyStoneXSSprite();

static bool crossyRowIsWater(int row) { return row >= 1 && row <= 5; }

static bool crossyRowIsGoal(int row) { return row == 0; }

static bool crossyPlayerOverlapsMoverInRow(int row);

static bool ensureCrossyGoalZoneSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "goal_zone", crossyGoalZonePathForPet(), 8, kSpriteKey, sprPtr);
}
static bool ensureCrossyStartZoneSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "start_zone", crossyStartZonePathForPet(), 8, kSpriteKey, sprPtr);
}

static bool ensureCrossyLavaZoneSprite(uint8_t frame)
{
  const uint8_t i = frame & 1;
  const char *assetId = (i == 0) ? "lava_zone_0" : "lava_zone_1";

  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, assetId, crossyLavaZonePathForPet(i), 16, TFT_BLACK, sprPtr);
}

static bool ensureCrossyIntroSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "intro_goal",
                             "/raising_hell/graphics/mini_games/crossy/dev/intro_goal.png", 8, kSpriteKey, sprPtr);
}

static void crossyInitLanes()
{
  memset(s_crossyLanes, 0, sizeof(s_crossyLanes));

  for (int r = 0; r < kCrossyRows; ++r)
  {
    CrossyLane &L = s_crossyLanes[r];

    if (crossyRowIsGoal(r))
    {
      L.type = CROSSY_LANE_GOAL;
      L.dir = 0;
      L.speed = 0;
      L.moverLen = 0;
      L.gapPx = 0;
      L.offsetPx = 0;
      L.platSize = CROSSY_PLAT_LARGE;
      continue;
    }

    if (crossyRowIsWater(r))
    {
      L.type = CROSSY_LANE_WATER;
      L.dir = (r % 2 == 1) ? -1 : +1;

      switch (r)
      {
      case 1: // top water row
        L.speed = 4;
        L.platSize = CROSSY_PLAT_XS;
        L.moverLen = 2;
        L.gapPx = kCrossyTileW * 5;
        break;

      case 2:
        L.speed = 3;
        L.platSize = CROSSY_PLAT_LARGE;
        L.moverLen = 3;
        L.gapPx = kCrossyTileW * 4;
        break;

      case 3:
        L.speed = 4;
        L.platSize = CROSSY_PLAT_SMALL;
        L.moverLen = 2;
        L.gapPx = kCrossyTileW * 5;
        break;

      case 4:
        L.speed = 3;
        L.platSize = CROSSY_PLAT_LARGE;
        L.moverLen = 3;
        L.gapPx = kCrossyTileW * 3;
        break;

      case 5: // bottom water row
      default:
        L.speed = 2;
        L.platSize = CROSSY_PLAT_SMALL;
        L.moverLen = 2;
        L.gapPx = kCrossyTileW * 4;
        break;
      }

      const int moverLenPx = (int)L.moverLen * kCrossyTileW;
      const int periodPx = moverLenPx + (int)L.gapPx;
      L.offsetPx = (periodPx > 0) ? (int32_t)random((long)periodPx) : 0;
      continue;
    }

    L.type = CROSSY_LANE_SAFE;
    L.dir = 0;
    L.speed = 0;
    L.moverLen = 0;
    L.gapPx = 0;
    L.offsetPx = 0;
    L.platSize = CROSSY_PLAT_LARGE;
  }
}

void freeCrossyZoneSprites()
{
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "goal_zone");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "start_zone");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "intro_goal");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "lava_zone_0");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "lava_zone_1");
}

static const char *crossyStartZonePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/eld_start_zone.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/dev_start_zone.png";
  }
}

static const char *crossyGoalZonePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/eld_goal_zone.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/dev_goal_zone.png";
  }
}

static const char *crossyLavaZonePathForPet(uint8_t frame)
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return (frame & 1) ? "/raising_hell/graphics/mini_games/crossy/eld/eld_lava_zone2.png"
                       : "/raising_hell/graphics/mini_games/crossy/eld/eld_lava_zone1.png";
  case PET_DEVIL:
  default:
    return (frame & 1) ? "/raising_hell/graphics/mini_games/crossy/dev/dev_lava_zone2.png"
                       : "/raising_hell/graphics/mini_games/crossy/dev/dev_lava_zone1.png";
  }
}

void freeCrossyActorSprites()
{
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "stone_lg");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "stone_sm");
  mgmem::releaseSprite(MiniGame::CROSSY_ROAD, "stone_xs");
}

static bool ensureCrossyStoneSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_lg", crossyStonePathForPet(), 8, kSpriteKey, sprPtr);
}

static bool ensureCrossyStoneSmallSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_sm", crossyStoneSmallPathForPet(), 8, kSpriteKey, sprPtr);
}

static bool ensureCrossyStoneXSSprite()
{
  M5Canvas *sprPtr = nullptr;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_xs", crossyStoneXSPathForPet(), 8, kSpriteKey, sprPtr);
}

static const char *crossyImpPathForPet(CrossyFacing facing)
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    switch (facing)
    {
    case CROSSY_FACE_UP:
      return "/raising_hell/graphics/mini_games/crossy/eld/imp_up.png";
    case CROSSY_FACE_LEFT:
      return "/raising_hell/graphics/mini_games/crossy/eld/imp_left.png";
    case CROSSY_FACE_RIGHT:
      return "/raising_hell/graphics/mini_games/crossy/eld/imp_right.png";
    case CROSSY_FACE_DOWN:
    default:
      return "/raising_hell/graphics/mini_games/crossy/eld/imp_down.png";
    }

  case PET_DEVIL:
  default:
    switch (facing)
    {
    case CROSSY_FACE_UP:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_up.png";
    case CROSSY_FACE_LEFT:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_left.png";
    case CROSSY_FACE_RIGHT:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_right.png";
    case CROSSY_FACE_DOWN:
    default:
      return "/raising_hell/graphics/mini_games/crossy/dev/imp_down.png";
    }
  }
}

static const char *crossyStoneSmallPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/stone_chunk_sm.png";

  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/stone_chunk_sm.png";
  }
}

static const char *crossyStoneXSPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/stone_chunk_xs.png";

  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/stone_chunk_xs.png";
  }
}

static const char *crossyStonePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/stone_chunk.png";

  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/stone_chunk.png";
  }
}

static void drawCrossyStoneChunk(int x, int y, int w, int h, uint8_t platSize)
{
  M5Canvas *stone = nullptr;

  if (platSize == CROSSY_PLAT_XS)
  {
    if (ensureCrossyStoneXSSprite())
      mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_xs", crossyStoneXSPathForPet(), 8, kSpriteKey, stone);
  }
  else if (platSize == CROSSY_PLAT_SMALL)
  {
    if (ensureCrossyStoneSmallSprite())
      mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_sm", crossyStoneSmallPathForPet(), 8, kSpriteKey, stone);
  }
  else
  {
    if (ensureCrossyStoneSprite())
      mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "stone_lg", crossyStonePathForPet(), 8, kSpriteKey, stone);
  }

  if (stone && stone->width() > 0 && stone->height() > 0)
  {
    const int drawX = x + (w - (int)stone->width()) / 2;
    const int drawY = y + (h - (int)stone->height()) / 2;
    stone->pushSprite(&spr, drawX, drawY, kSpriteKey);
    return;
  }

  spr.fillRoundRect(x, y + 2, w, h - 4, 3, TFT_DARKGREY);
  spr.drawFastHLine(x + 2, y + 4, w - 4, TFT_LIGHTGREY);
  spr.drawFastHLine(x + 3, y + h - 4, w - 6, TFT_BLACK);
}

static void drawCrossyImp(int x, int y, int w, int h, uint32_t now)
{
  const char *path = crossyImpPathForPet(s_crossyFacing);
  const char *usePath = nullptr;

  if (sdExistsTrySlash(path, &usePath))
  {
    sprDrawPngFromSD(usePath ? usePath : path, x, y);
    return;
  }

  const int bob = ((now / 120) % 2 == 0) ? 0 : 1;

  spr.fillRect(x + 5, y + 5 + bob, w - 10, h - 9, TFT_RED);
  spr.fillTriangle(x + 6, y + 6 + bob, x + 8, y + 1 + bob, x + 10, y + 6 + bob, TFT_RED);
  spr.fillTriangle(x + w - 10, y + 6 + bob, x + w - 8, y + 1 + bob, x + w - 6, y + 6 + bob, TFT_RED);

  spr.fillRect(x + 7, y + 8 + bob, 2, 2, TFT_BLACK);
  spr.fillRect(x + w - 9, y + 8 + bob, 2, 2, TFT_BLACK);

  spr.drawFastHLine(x + 8, y + h - 6 + bob, w - 16, TFT_YELLOW);
}

static bool crossyGoalEntryAllowed(int px)
{
  const int playerCenterX = kCrossyOriginX + px * kCrossyTileW + (kCrossyTileW / 2);

  const int leftTorchX = 85;
  const int rightTorchX = 85 + 68;

  return (playerCenterX >= leftTorchX && playerCenterX <= rightTorchX);
}

static void crossyReset()
{
  s_crossyPx = kCrossyCols / 2;
  s_crossyPy = kCrossyRows - 1;
  s_crossyVisualOffsetPx = 0;

  s_crossyFacing = CROSSY_FACE_DOWN;

  s_crossyLastLaneMs = millis();
  s_crossyLavaFrame = 0;
  s_crossyLavaAnimMs = millis();
  s_crossyLandingGraceFrames = 0;

  memset(s_crossyCarryPxAccum, 0, sizeof(s_crossyCarryPxAccum));
  memset(s_crossyLaneTick, 0, sizeof(s_crossyLaneTick));
  crossyInitLanes();
}

void startCrossyRoad()
{
  mgPauseReset();
  inputSetTextCapture(false);

  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;
  s_resultShown = false;

  s_crossyShowIntro = true;
  s_crossyDontShowAgain = false;
  s_crossyIntroImpAnimMs = millis();

  mgClearRewardState();
  mgResetAcceptState();

  currentMiniGame = MiniGame::CROSSY_ROAD;
  mgAssetsBeginSession(currentMiniGame, "startCrossyRoad");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("crossy-start-beginSession");
  logMiniGameHeap("startCrossyRoad");

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_crossyInited = true;
  crossyReset();

  mgmem::logUsage("crossy-before-asset-free");

  freeCrossyZoneSprites();
  freeCrossyActorSprites();

  mgmem::logUsage("crossy-after-asset-free");

  const bool startOk = ensureCrossyStartZoneSprite();
  mgmem::logUsage("crossy-after-start-zone");

  const bool goalOk = ensureCrossyGoalZoneSprite();
  mgmem::logUsage("crossy-after-goal-zone");

  const bool lava0 = ensureCrossyLavaZoneSprite(0);
  mgmem::logUsage("crossy-after-lava0");

  const bool lava1 = ensureCrossyLavaZoneSprite(1);
  mgmem::logUsage("crossy-after-lava1");

  const bool stoneOk = ensureCrossyStoneSprite();
  mgmem::logUsage("crossy-after-stone-lg");

  const bool stoneSmOk = ensureCrossyStoneSmallSprite();
  mgmem::logUsage("crossy-after-stone-sm");

  const bool stoneXsOk = ensureCrossyStoneXSSprite();
  mgmem::logUsage("crossy-after-stone-xs");

  const bool impOk = true;
  mgmem::logUsage("crossy-after-imp");

  Serial.printf(
      "CROSSY preload: start=%d goal=%d lava0=%d lava1=%d stone=%d stoneSm=%d stoneXs=%d imp=%d free=%u largest=%u\n",
      startOk ? 1 : 0, goalOk ? 1 : 0, lava0 ? 1 : 0, lava1 ? 1 : 0, stoneOk ? 1 : 0, stoneSmOk ? 1 : 0,
      stoneXsOk ? 1 : 0, impOk ? 1 : 0, (unsigned)ESP.getFreeHeap(),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  s_crossyWinPoseActive = false;
  s_crossyWinPoseStart = 0;

  mgmem::logUsage("crossy-start-complete");
}

static void crossyStepLanes(uint32_t now)
{
  const uint32_t kLaneStepMs = 16;

  while ((uint32_t)(now - s_crossyLastLaneMs) >= kLaneStepMs)
  {
    s_crossyLastLaneMs += kLaneStepMs;

    for (int r = 0; r < kCrossyRows; ++r)
    {
      CrossyLane &L = s_crossyLanes[r];
      if (L.type != CROSSY_LANE_WATER)
        continue;

      // Faster platform movement.
      // speed 1 = 1 px every 3 ticks
      // speed 2 = 1 px every 2 ticks
      // speed 3 = 2 px every 3 ticks
      uint8_t ticksPerPixel = 3;
      int movePx = 1;

      if (L.speed <= 1)
      {
        ticksPerPixel = 3;
        movePx = 1;
      }
      else if (L.speed == 2)
      {
        ticksPerPixel = 2;
        movePx = 1;
      }
      else if (L.speed == 3)
      {
        ticksPerPixel = 2;
        movePx = 2;
      }
      else
      {
        ticksPerPixel = 1;
        movePx = 2;
      }

      s_crossyLaneTick[r]++;
      if (s_crossyLaneTick[r] < ticksPerPixel)
        continue;

      s_crossyLaneTick[r] = 0;

      const bool carryFrog = (s_crossyPy == r) && crossyPlayerOverlapsMoverInRow(r);

      const int deltaPx = (L.dir > 0) ? movePx : -movePx;
      L.offsetPx += deltaPx;
      const int moverLenPx = L.moverLen * kCrossyTileW;
      const int periodPx = moverLenPx + L.gapPx;

      L.offsetPx %= periodPx;
      if (L.offsetPx < 0)
        L.offsetPx += periodPx;

      if (carryFrog)
      {
        s_crossyVisualOffsetPx -= deltaPx;

        while (s_crossyVisualOffsetPx <= -kCrossyTileW)
        {
          s_crossyPx -= 1;
          s_crossyVisualOffsetPx += kCrossyTileW;
        }

        while (s_crossyVisualOffsetPx >= kCrossyTileW)
        {
          s_crossyPx += 1;
          s_crossyVisualOffsetPx -= kCrossyTileW;
        }
      }
    }
  }
}

static bool crossyPlayerOverlapsMoverInRow(int row)
{
  if (row < 0 || row >= kCrossyRows)
    return false;

  const CrossyLane &L = s_crossyLanes[row];
  if (L.type != CROSSY_LANE_WATER)
    return false;

  const int moverLenPx = (int)L.moverLen * kCrossyTileW;
  const int periodPx = moverLenPx + (int)L.gapPx;
  const int laneW = kCrossyCols * kCrossyTileW;

  if (moverLenPx <= 0 || periodPx <= 0)
    return false;

  // Give landings a little forgiveness so edge hops don't randomly fail.
  const int kEdgeForgivePx = 3;

  const int playerLeft = s_crossyPx * kCrossyTileW + s_crossyVisualOffsetPx;
  const int playerRight = playerLeft + kCrossyTileW - 1;

  for (int x = -periodPx * 2; x < laneW + periodPx * 2; x += periodPx)
  {
    const int platLeft = x - (int)L.offsetPx;
    const int platRight = platLeft + moverLenPx - 1;

    if ((playerRight >= platLeft - kEdgeForgivePx) && (playerLeft <= platRight + kEdgeForgivePx))
    {
      return true;
    }
  }

  return false;
}

static bool crossyOnLog()
{
  if (s_crossyPy < 0 || s_crossyPy >= kCrossyRows)
    return false;

  if (s_crossyLanes[s_crossyPy].type != CROSSY_LANE_WATER)
    return false;

  return crossyPlayerOverlapsMoverInRow(s_crossyPy);
}

void updateCrossyRoad(const InputState &input)
{
  const bool enterOnce = miniGameEnterOnce(input);
  const uint32_t now = millis();

  if ((uint32_t)(now - s_crossyLavaAnimMs) >= 180)
  {
    s_crossyLavaAnimMs = now;
    s_crossyLavaFrame ^= 1;
  }

  // ---------------------------------------------------------------------------
  // Reward modal
  // ---------------------------------------------------------------------------
  if (mgRewardShowing())
  {
    if (enterOnce && !mgInputLockedOut())
    {
      mgClearRewardState();
      mgResetAcceptState();
      exitMiniGameToReturnUi(true);
    }
    return;
  }

  // ---------------------------------------------------------------------------
  // Game over -> reward modal
  // ---------------------------------------------------------------------------
  if (g_app.gameOver)
  {
    mgApplyResultAndShowReward(playerWon);
    mgResetAcceptState();
    mgBeginInputLockout(180);
    clearInputLatch();
    inputForceClear();
    return;
  }

  if (!s_crossyInited)
  {
    s_crossyInited = true;
    crossyReset();
  }

  if (s_crossyWinPoseActive)
  {
    // tiny victory bounce while posing
    s_crossyVisualOffsetPx = ((millis() / 120) % 2) ? -2 : 0;

    if ((uint32_t)(now - s_crossyWinPoseStart) >= 800)
    {
      s_crossyVisualOffsetPx = 0; // reset after pose
      s_crossyWinPoseActive = false;
      playerWon = true;
      g_app.gameOver = true;
      requestUIRedraw();
      s_resultShown = true;
    }
    return;
  }

  if (s_crossyShowIntro)
  {
    const uint32_t dt = now - s_crossyIntroImpAnimMs;

    if (dt >= 180)
      s_crossyIntroImpAnimMs = now;

    if (input.mgSpaceOnce)
      s_crossyDontShowAgain = !s_crossyDontShowAgain;

      if (input.mgQuitOnce && !mgInputLockedOut())
      {
        miniGameCancelFromIntro();
                return;
      }

    const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;

    if (startPressed && !mgInputLockedOut())
    {
      s_crossyShowIntro = false;
      crossyReset();
      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  crossyStepLanes(now);

  int dx = 0;
  int dy = 0;

  if (input.mgLeftOnce)
    dx = -1;
  if (input.mgRightOnce)
    dx = +1;
  if (input.mgUpOnce)
    dy = -1;
  if (input.mgDownOnce)
    dy = +1;

  if (input.encoderDelta < 0)
    dy = -1;
  if (input.encoderDelta > 0)
    dy = +1;

  if (dx || dy)
  {
    if (dx < 0)
      s_crossyFacing = CROSSY_FACE_LEFT;
    else if (dx > 0)
      s_crossyFacing = CROSSY_FACE_RIGHT;
    else if (dy < 0)
      s_crossyFacing = CROSSY_FACE_UP;
    else if (dy > 0)
      s_crossyFacing = CROSSY_FACE_DOWN;

    const int oldPx = s_crossyPx;
    const int oldPy = s_crossyPy;

    const int newPx = crossyClamp(s_crossyPx + dx, 0, kCrossyCols - 1);
    const int newPy = crossyClamp(s_crossyPy + dy, 0, kCrossyRows - 1);

    // Block entry into the goal row unless the imp is between the two torches.
    if (newPy == 0 && !crossyGoalEntryAllowed(newPx))
    {
      playBeep();
      return;
    }

    s_crossyPx = newPx;
    s_crossyPy = newPy;
    s_crossyVisualOffsetPx = 0;

    // If we just stepped onto a water row, allow one update tick of landing grace
    // before declaring a miss. This prevents false deaths on edge-timed hops.
    if (s_crossyPy != oldPy && s_crossyLanes[s_crossyPy].type == CROSSY_LANE_WATER)
      s_crossyLandingGraceFrames = 1;

    playBeep();
  }

  if (s_crossyPy == 0)
  {
    s_crossyFacing = CROSSY_FACE_DOWN;

    s_crossyWinPoseActive = true;
    s_crossyWinPoseStart = now;

    soundConfirm();
    requestUIRedraw();
    return;
  }

  if (s_crossyLanes[s_crossyPy].type == CROSSY_LANE_WATER)
  {
    // If the lane step carried us off-screen, we lose.
    if (s_crossyPx < 0 || s_crossyPx >= kCrossyCols)
    {
      playerWon = false;
      g_app.gameOver = true;
      requestUIRedraw();
      s_resultShown = true;
      soundError();
      return;
    }

    const bool onPlatform = crossyOnLog();

    if (!onPlatform)
    {
      if (s_crossyLandingGraceFrames > 0)
      {
        s_crossyLandingGraceFrames--;
      }
      else
      {
        playerWon = false;
        g_app.gameOver = true;
        requestUIRedraw();
        s_resultShown = true;
        soundError();
        return;
      }
    }
    else
    {
      s_crossyLandingGraceFrames = 0;
    }
  }
  else
  {
    s_crossyLandingGraceFrames = 0;
  }
}

void drawCrossyRoad()
{
  if (!s_crossyInited)
  {
    s_crossyInited = true;
    crossyReset();
  }

  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  const bool haveGoal = ensureCrossyGoalZoneSprite();
  const bool haveStart = ensureCrossyStartZoneSprite();

  M5Canvas *goalZoneSpr = nullptr;
  M5Canvas *startZoneSpr = nullptr;

  if (haveGoal)
  {
    mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "goal_zone", crossyGoalZonePathForPet(), 8, kSpriteKey, goalZoneSpr);
  }

  if (haveStart)
  {
    mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "start_zone", crossyStartZonePathForPet(), 8, kSpriteKey, startZoneSpr);
  }

  const bool haveLava0 = ensureCrossyLavaZoneSprite(0);
  const bool haveLava1 = ensureCrossyLavaZoneSprite(1);

  M5Canvas *lavaZoneSpr0 = nullptr;
  M5Canvas *lavaZoneSpr1 = nullptr;

  if (haveLava0)
  {
    mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "lava_zone_0", crossyLavaZonePathForPet(0), 16, TFT_BLACK, lavaZoneSpr0);
  }

  if (haveLava1)
  {
    mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "lava_zone_1", crossyLavaZonePathForPet(1), 16, TFT_BLACK, lavaZoneSpr1);
  }

  (void)haveLava0;
  (void)haveLava1;

  spr.fillSprite(TFT_BLACK);

  if (mgRewardShowing())
  {
    drawRewardModal(gW, gH);
    return;
  }

  if (g_app.gameOver)
  {
    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(playerWon ? TFT_GREEN : TFT_RED, TFT_BLACK);
    spr.drawCentreString(playerWon ? "YOU WIN!" : "YOU BURN!", gW / 2, gH / 2 - 10, 4);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Press ENTER", gW / 2, gH / 2 + 22, 2);
    return;
  }

  if (s_crossyShowIntro)
  {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(CC_DATUM);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Escape Hell! Use Arrow Keys", gW / 2, 8, 2);
    spr.drawCentreString("Avoid Lava, Exit between Torches!", gW / 2, 26, 2);

    M5Canvas *introSpr = nullptr;
    const char *introPath = "/raising_hell/graphics/mini_games/crossy/dev/intro_goal.png";

    if (ensureCrossyIntroSprite() &&
        mgmem::ensureSprite(MiniGame::CROSSY_ROAD, "intro_goal", introPath, 8, kSpriteKey, introSpr) && introSpr &&
        introSpr->width() > 0 && introSpr->height() > 0)
    {
      const int ix = (gW - (int)introSpr->width()) / 2;
      const int iy = 48;
      introSpr->pushSprite(&spr, ix, iy, kSpriteKey);
    }
    else
    {
      int iw = 0, ih = 0;
      const char *useIntroPath = nullptr;

      if (mgAssetsReadPngDims(introPath, &iw, &ih, &useIntroPath))
      {
        const int ix = (gW - iw) / 2;
        const int iy = 48;
        sprDrawPngFromSD(useIntroPath ? useIntroPath : introPath, ix, iy);
      }
      else
      {
        sprDrawPngFromSD(introPath, (gW - 68) / 2, 48);
      }
    }

    const int cbY = 102;
    const int cbSize = 10;
    const int textOffset = 16;
    const int lineWidth = 150;
    const int cbX = (gW - lineWidth) / 2;

    spr.drawRect(cbX, cbY, cbSize, cbSize, TFT_WHITE);

    if (s_crossyDontShowAgain)
    {
      spr.drawLine(cbX + 2, cbY + 5, cbX + 4, cbY + 7, TFT_WHITE);
      spr.drawLine(cbX + 4, cbY + 7, cbX + 8, cbY + 2, TFT_WHITE);
    }

    spr.setTextDatum(ML_DATUM);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString("Don't show again (Space)", cbX + textOffset, cbY + 5, 2);

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 120, 2);
    return;
  }

  const uint32_t now = millis();

  for (int row = 0; row < kCrossyRows; ++row)
  {
    const int y = kCrossyOriginY + row * kCrossyTileH;

    switch (s_crossyLanes[row].type)
    {
    case CROSSY_LANE_GOAL:
      if (haveGoal && goalZoneSpr)
        goalZoneSpr->pushSprite(&spr, 0, y);
      else
        spr.fillRect(0, y, gW, kCrossyTileH, TFT_RED);
      break;

    case CROSSY_LANE_SAFE:
      if (row == kCrossyRows - 1)
      {
        if (haveStart && startZoneSpr)
          startZoneSpr->pushSprite(&spr, 0, y);
        else
          spr.fillRect(0, y, 240, kCrossyTileH, TFT_DARKGREY);
      }
      else
      {
        spr.fillRect(0, y, 240, kCrossyTileH, TFT_BLACK);
      }
      break;

    case CROSSY_LANE_WATER:
    {
      const uint8_t lavaFrame = (s_crossyLavaFrame + row) & 1;

      M5Canvas *lavaSpr = (lavaFrame == 0) ? lavaZoneSpr0 : lavaZoneSpr1;

      if (lavaSpr && lavaSpr->width() > 0 && lavaSpr->height() > 0)
      {
        const int tileW = (int)lavaSpr->width();
        const int tileH = (int)lavaSpr->height();

        if (tileW > 0 && tileH > 0)
        {
          for (int x = 0; x < gW; x += tileW)
            lavaSpr->pushSprite(&spr, x, y);
        }
        else
        {
          spr.fillRect(0, y, gW, kCrossyTileH, TFT_RED);
        }
      }
      else
      {
        spr.fillRect(0, y, gW, kCrossyTileH, TFT_RED);
      }

      break;
    }
    }
  }

  for (int r = 0; r < kCrossyRows; ++r)
  {
    const CrossyLane &L = s_crossyLanes[r];
    if (L.type != CROSSY_LANE_WATER)
      continue;

    const int moverLenPx = (int)L.moverLen * kCrossyTileW;
    const int periodPx = moverLenPx + (int)L.gapPx;
    const int laneW = kCrossyCols * kCrossyTileW;

    if (moverLenPx <= 0 || periodPx <= 0)
      continue;

    const int y = kCrossyOriginY + r * kCrossyTileH;

    int offset = (int)(L.offsetPx % periodPx);
    if (offset < 0)
      offset += periodPx;

    for (int x0 = -periodPx * 2; x0 < laneW + periodPx * 2; x0 += periodPx)
    {
      const int x = x0 - offset;
      const int drawX = kCrossyOriginX + x;

      if (drawX + moverLenPx < kCrossyOriginX)
        continue;
      if (drawX > kCrossyOriginX + laneW)
        continue;

      drawCrossyStoneChunk(drawX, y + 2, moverLenPx - 1, kCrossyTileH - 4, L.platSize);
    }
  }

  const int fx = kCrossyOriginX + s_crossyPx * kCrossyTileW + s_crossyVisualOffsetPx;
  const int fy = kCrossyOriginY + s_crossyPy * kCrossyTileH - 8;

  drawCrossyImp(fx, fy, kCrossyTileW, kCrossyTileH, now);
}

// -----------------------------------------------------------------------------
// FIREBALL RUN GLOBALS
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
static bool s_dodgerDontShowAgain = false; // visual only for now
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

static LGFX_Sprite s_dodgerGoalSpr[2];
static bool s_dodgerGoalFrameReady[2] = {false, false};
static char s_dodgerGoalFramePath[2][160] = {{0}, {0}};
static int s_dodgerGoalW = 0;
static int s_dodgerGoalH = 0;

static LGFX_Sprite s_dodgerGoreSpr;
static bool s_dodgerGoreReady = false;
static char s_dodgerGorePath[160] = {0};

static uint8_t s_dodgerGoalAnimFrame = 0;
static uint32_t s_dodgerGoalAnimMs = 0;

static DodgerPhase s_dodgerPhase = DODGER_PHASE_FIREBALLS;
static uint32_t s_dodgerPhaseStartMs = 0;

static constexpr uint32_t kDodgerGoalSpawnMs = 12000;
static constexpr uint32_t kDodgerCoastMs = 1400;
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
static int16_t s_dodgerSpeed = 3;
static float s_dodgerPxF = 0.0f;
uint32_t s_dodgerMoveLastMs = 0;

static int8_t s_dodgerMoveDir = 0;
static uint32_t s_dodgerDirHoldMs = 0;

static DodgerBall s_dodgerBalls[8];

static const uint16_t kDodgerKey = kSpriteKey;

static int s_dodgerBgScrollY = 0;

static int s_dodgerFireballW = 0;
static int s_dodgerFireballH = 0;

static int s_dodgerCarW = 0;
static int s_dodgerCarH = 0;

void freeDodgerGoalFrames()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "goal_frame_1");
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "goal_frame_2");

  s_dodgerGoalFrameReady[0] = false;
  s_dodgerGoalFrameReady[1] = false;
  s_dodgerGoalFramePath[0][0] = 0;
  s_dodgerGoalFramePath[1][0] = 0;
  s_dodgerGoalW = 0;
  s_dodgerGoalH = 0;
}

void freeDodgerGoreSprite()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "goal_gore");

  s_dodgerGoreReady = false;
  s_dodgerGorePath[0] = 0;
}

static const char *dodgerGoalFrame1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/imp_stack1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/imp_stack1.png";
  }
}

static const char *dodgerGoalFrame2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/imp_stack2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/imp_stack2.png";
  }
}

static const char *dodgerGoalGorePathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/imp_gore.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/imp_gore.png";
  }
}

static const char *fireballRunBgPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/eld_fbrun_bg.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/dev_fbrun_bg.png";
  }
}

static const char *dodgerGoalFrame1ResolvedPath()
{
  const char *usePath = nullptr;
  const char *petPath = dodgerGoalFrame1PathForPet();
  if (sdExistsTrySlash(petPath, &usePath))
    return usePath ? usePath : petPath;

  const char *fallback = "/raising_hell/graphics/mini_games/fbrun/dev/imp_stack1.png";
  if (sdExistsTrySlash(fallback, &usePath))
    return usePath ? usePath : fallback;

  return nullptr;
}

static const char *dodgerGoalFrame2ResolvedPath()
{
  const char *usePath = nullptr;
  const char *petPath = dodgerGoalFrame2PathForPet();
  if (sdExistsTrySlash(petPath, &usePath))
    return usePath ? usePath : petPath;

  const char *fallback = "/raising_hell/graphics/mini_games/fbrun/dev/imp_stack2.png";
  if (sdExistsTrySlash(fallback, &usePath))
    return usePath ? usePath : fallback;

  return nullptr;
}

static const char *dodgerGoalGoreResolvedPath()
{
  const char *usePath = nullptr;
  const char *petPath = dodgerGoalGorePathForPet();
  if (sdExistsTrySlash(petPath, &usePath))
    return usePath ? usePath : petPath;

  const char *fallback = "/raising_hell/graphics/mini_games/fbrun/dev/imp_gore.png";
  if (sdExistsTrySlash(fallback, &usePath))
    return usePath ? usePath : fallback;

  return nullptr;
}

static const char *fireballRunCarPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/car.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/car.png";
  }
}

static bool loadDodgerSprite(LGFX_Sprite &dst, const char *path, int &outW, int &outH)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  int w = 0, h = 0;
  const char *usePath = nullptr;

  if (!mgAssetsReadPngDims(path, &w, &h, &usePath) || w <= 0 || h <= 0)
    return false;

  dst.setColorDepth(8);

  if (!dst.createSprite(w, h))
    return false;

  dst.fillSprite(kDodgerKey);

  if (!dst.drawPngFile(SD, usePath, 0, 0))
  {
    dst.deleteSprite();
    return false;
  }

  outW = w;
  outH = h;
  return true;
}

static bool ensureDodgerGoalFrames(const char *path0, const char *path1)
{
  if (!path0 || !path1 || !path0[0] || !path1[0] || !g_sdReady)
    return false;

  M5Canvas *goal1 = nullptr;
  M5Canvas *goal2 = nullptr;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_frame_1", path0, 8, kDodgerKey, goal1))
    return false;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_frame_2", path1, 8, kDodgerKey, goal2))
    return false;

  if (!goal1 || !goal2 || goal1->width() <= 0 || goal1->height() <= 0)
    return false;

  s_dodgerGoalFrameReady[0] = true;
  s_dodgerGoalFrameReady[1] = true;
  strlcpy(s_dodgerGoalFramePath[0], path0, sizeof(s_dodgerGoalFramePath[0]));
  strlcpy(s_dodgerGoalFramePath[1], path1, sizeof(s_dodgerGoalFramePath[1]));
  s_dodgerGoalW = (int)goal1->width();
  s_dodgerGoalH = (int)goal1->height();
  return true;
}

static bool ensureDodgerGoreSprite(const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  M5Canvas *gore = nullptr;
  const bool ok =
      mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_gore", path, 8, kDodgerKey, gore) &&
      gore && gore->width() > 0 && gore->height() > 0;

  s_dodgerGoreReady = ok;

  if (ok)
    strlcpy(s_dodgerGorePath, path, sizeof(s_dodgerGorePath));
  else
    s_dodgerGorePath[0] = 0;

  return ok;
}

void freeDodgerBgCache()
{
  mgAssetsReleaseSharedBgIfOwner(MiniGame::INFERNAL_DODGER);
  s_dodgerBgScrollY = 0;
}

void freeDodgerFireballSprites()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "fireball1");
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "fireball2");
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "fireball3");

  s_dodgerFireballW = 0;
  s_dodgerFireballH = 0;
}

void freeDodgerCarSprite()
{
  mgmem::releaseSprite(MiniGame::INFERNAL_DODGER, "car");

  s_dodgerCarW = 0;
  s_dodgerCarH = 0;
}

static bool ensureDodgerBgCache(const char *path) { return mgAssetsEnsureSharedBg(MiniGame::INFERNAL_DODGER, path); }

static bool ensureDodgerFireballSprites(const char *bgPath)
{
  if (!bgPath || !bgPath[0] || !g_sdReady)
    return false;

  char dir[128];
  flappyDirFromBgPath(bgPath, dir, sizeof(dir));
  if (!dir[0])
    return false;

  char path1[192];
  char path2[192];
  char path3[192];
  snprintf(path1, sizeof(path1), "%sfireball1.png", dir);
  snprintf(path2, sizeof(path2), "%sfireball2.png", dir);
  snprintf(path3, sizeof(path3), "%sfireball3.png", dir);

  M5Canvas *fb1 = nullptr;
  M5Canvas *fb2 = nullptr;
  M5Canvas *fb3 = nullptr;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball1", path1, 8, kDodgerKey, fb1))
    return false;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball2", path2, 8, kDodgerKey, fb2))
    return false;

  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball3", path3, 8, kDodgerKey, fb3))
    return false;

  if (!fb1 || fb1->width() <= 0 || fb1->height() <= 0)
    return false;

  s_dodgerFireballW = (int)fb1->width();
  s_dodgerFireballH = (int)fb1->height();
  return true;
}

static bool ensureDodgerCarSprite(const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  M5Canvas *car = nullptr;
  if (!mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "car", path, 8, kDodgerKey, car))
    return false;

  if (!car || car->width() <= 0 || car->height() <= 0)
    return false;

  s_dodgerCarW = (int)car->width();
  s_dodgerCarH = (int)car->height();
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
  s_dodgerSpeed = 3;
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

//SHARED MINI GAME INTRO INTERRUPT
bool miniGameIsShowingIntro()
{
  switch (currentMiniGame)
  {
  case MiniGame::FLAPPY_FIREBALL:
    return s_flappyShowIntro;

  case MiniGame::CROSSY_ROAD:
    return s_crossyShowIntro;

  case MiniGame::INFERNAL_DODGER:
    return s_dodgerShowIntro;

  case MiniGame::RESURRECTION:
    return s_rrShowIntro;

  default:
    return false;
  }
}

void miniGameCancelFromIntro()
{
  bool refundEnergy = false;

  switch (currentMiniGame)
  {
  case MiniGame::FLAPPY_FIREBALL:
  case MiniGame::CROSSY_ROAD:
  case MiniGame::INFERNAL_DODGER:
    refundEnergy = true;
    break;

  case MiniGame::RESURRECTION:
  default:
    refundEnergy = false;
    break;
  }

  if (refundEnergy)
  {
    pet.energy = constrain(pet.energy + 10, 0, 100);
    saveManagerMarkDirty();
  }

  clearInputLatch();
  inputForceClear();
  mgPauseReset();
  exitMiniGameToReturnUi(true);
  requestFullUIRedraw();
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

  mgAssetsBeginSession(currentMiniGame, "startInfernalDodger");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("dodger-start-beginSession");
  logMiniGameHeap("startInfernalDodger");

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_dodgerInited = false;
  s_dodgerBgScrollY = 0;
  s_dodgerFreezeScroll = false;

  mgmem::logUsage("dodger-before-asset-free");

  freeDodgerBgCache();
  freeDodgerFireballSprites();
  freeDodgerCarSprite();
  freeDodgerGoalFrames();
  freeDodgerGoreSprite();

  mgmem::logUsage("dodger-after-asset-free");

  const char *bgPath = fireballRunBgPathForPet();

  const bool bgOk = ensureDodgerBgCache(bgPath);
  mgmem::logUsage("dodger-after-bg-ensure");

  const bool fireballOk = ensureDodgerFireballSprites(bgPath);
  mgmem::logUsage("dodger-after-fireballs-ensure");

  const bool carOk = ensureDodgerCarSprite(fireballRunCarPathForPet());
  mgmem::logUsage("dodger-after-car-ensure");

  const char *goalFrame1Path = dodgerGoalFrame1ResolvedPath();
  const char *goalFrame2Path = dodgerGoalFrame2ResolvedPath();
  const char *goalGorePath = dodgerGoalGoreResolvedPath();

  const bool goalOk =
  (goalFrame1Path && goalFrame2Path) &&
  ensureDodgerGoalFrames(goalFrame1Path, goalFrame2Path);
  
  const bool goreOk = ensureDodgerGoreSprite(goalGorePath);

  mgmem::logUsage("dodger-after-goal-ensure");
mgmem::logUsage("dodger-after-gore-ensure");

  s_dodgerInited = true;
  dodgerReset();

  Serial.printf("DODGER preload: bg=%d fireballs=%d car=%d goal=%d gore=%d free=%u largest=%u\n",
    bgOk ? 1 : 0, fireballOk ? 1 : 0, carOk ? 1 : 0, goalOk ? 1 : 0, goreOk ? 1 : 0,
    (unsigned)ESP.getFreeHeap(),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  invalidateBackgroundCache();
  s_dodgerShowIntro = true;
  s_dodgerDontShowAgain = false;
  s_dodgerIntroImpFrame = 0;
  s_dodgerIntroImpAnimMs = millis();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  mgmem::logUsage("dodger-start-complete");
}

void updateInfernalDodger(const InputState &input)
{
  const bool enterOnce = miniGameEnterOnce(input);

  if (mgRewardShowing())
  {
    if (enterOnce && !mgInputLockedOut())
    {
      mgClearRewardState();
      mgResetAcceptState();
      exitMiniGameToReturnUi(true);
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

    if (input.mgSpaceOnce)
      s_dodgerDontShowAgain = !s_dodgerDontShowAgain;

      if (input.mgQuitOnce && !mgInputLockedOut())
      {
        miniGameCancelFromIntro();
                return;
      }

    const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;

    if (startPressed && !mgInputLockedOut())
    {
      s_dodgerShowIntro = false;
      dodgerReset();
      s_dodgerMoveLastMs = now;
      s_dodgerLastStepMs = now;
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
      soundError();
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
        soundConfirm();
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

  const char *bgPath = fireballRunBgPathForPet();

  M5Canvas *bg = nullptr;
  int bw = 0;
  int bh = 0;
  const bool haveBg = mgAssetsEnsureSharedBg(MiniGame::INFERNAL_DODGER, bgPath);

  if (haveBg)
  {
    bg = mgAssetsSharedBg();
    bw = mgAssetsSharedBgW();
    bh = mgAssetsSharedBgH();
  }

  const bool needFireballs = (s_dodgerPhase == DODGER_PHASE_FIREBALLS);
  const bool haveFireballs = needFireballs && ensureDodgerFireballSprites(bgPath);
  const bool haveCar = ensureDodgerCarSprite(fireballRunCarPathForPet());

  M5Canvas *fbFrame0 = nullptr;
  M5Canvas *fbFrame1 = nullptr;
  M5Canvas *fbFrame2 = nullptr;
  M5Canvas *carSpr = nullptr;

  if (needFireballs && haveFireballs)
  {
    char dir[128];
    flappyDirFromBgPath(bgPath, dir, sizeof(dir));
  
    char path1[192];
    char path2[192];
    char path3[192];
    snprintf(path1, sizeof(path1), "%sfireball1.png", dir);
    snprintf(path2, sizeof(path2), "%sfireball2.png", dir);
    snprintf(path3, sizeof(path3), "%sfireball3.png", dir);
  
    mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball1", path1, 8, kDodgerKey, fbFrame0);
    mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball2", path2, 8, kDodgerKey, fbFrame1);
    mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "fireball3", path3, 8, kDodgerKey, fbFrame2);
  }

  if (haveCar)
  {
    mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "car", fireballRunCarPathForPet(), 8, kDodgerKey, carSpr);
  }

  bool drewBg = false;

  if (haveBg && bg && bw > 0 && bh > 0)
  {
    int y = -(s_dodgerBgScrollY % bh);
    if (y > 0)
      y -= bh;

    for (int drawY = y; drawY < gH; drawY += bh)
      bg->pushSprite(&spr, 0, drawY);

    drewBg = true;
  }

  if (!drewBg)
    spr.fillSprite(TFT_BLACK);

  if (mgRewardShowing())
  {
    drawRewardModal(gW, gH);
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
    spr.drawCentreString("Arrow keys or A/L to dodge", gW / 2, 8, 2);
    spr.drawCentreString("Stay on the road, smash the Imp!", gW / 2, 26, 2);

    const int impX = (gW - 48) / 2;
    const int impY = 44;

    const char *introImp =
    (s_dodgerIntroImpFrame == 0) ? dodgerGoalFrame1ResolvedPath() : dodgerGoalFrame2ResolvedPath();

    if (introImp && introImp[0])
    sprDrawPngFromSD(introImp, impX, impY);
  else
    spr.fillRect(impX, impY, 48, 48, TFT_RED);
  
  const int cbY = 102;
  const int cbSize = 10;
  const int textOffset = 16;
  const int lineWidth = 150;
  const int cbX = (gW - lineWidth) / 2;
  
  spr.drawRect(cbX, cbY, cbSize, cbSize, TFT_WHITE);
  
  if (s_dodgerDontShowAgain)
  {
    spr.drawLine(cbX + 2, cbY + 5, cbX + 4, cbY + 7, TFT_WHITE);
    spr.drawLine(cbX + 4, cbY + 7, cbX + 8, cbY + 2, TFT_WHITE);
  }
  
  spr.setTextDatum(ML_DATUM);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("Don't show again (Space)", cbX + textOffset, cbY + 5, 2);
  
  spr.setTextDatum(CC_DATUM);
  spr.setTextColor(TFT_GREEN, TFT_BLACK);
  spr.drawCentreString("ENTER to begin", gW / 2, 120, 2);
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
  
      M5Canvas *fbFrames[3] = {fbFrame0, fbFrame1, fbFrame2};
  
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
    const bool gorePhase =
        (s_dodgerPhase == DODGER_PHASE_CAR_EXIT) ||
        (s_dodgerPhase == DODGER_PHASE_HOLD);
  
    if (gorePhase)
    {
      M5Canvas *goreSpr = nullptr;
      const char *gorePath = dodgerGoalGoreResolvedPath();
  
      if (gorePath &&
          ensureDodgerGoreSprite(gorePath) &&
          mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_gore", gorePath, 8, kDodgerKey, goreSpr) &&
          goreSpr && goreSpr->width() > 0 && goreSpr->height() > 0)
      {
        const int drawX = s_dodgerGoalX - ((int)goreSpr->width() / 2);
        const int drawY = s_dodgerGoalY - ((int)goreSpr->height() / 2);
        goreSpr->pushSprite(&spr, drawX, drawY, kDodgerKey);
      }
      else
      {
        spr.fillRect(s_dodgerGoalX - 24, s_dodgerGoalY - 8, 48, 16, TFT_RED);
      }
    }
    else
    {
      M5Canvas *goal1 = nullptr;
      M5Canvas *goal2 = nullptr;
  
      const char *goalPath1 = dodgerGoalFrame1ResolvedPath();
      const char *goalPath2 = dodgerGoalFrame2ResolvedPath();
  
      if (goalPath1 && goalPath2 &&
          ensureDodgerGoalFrames(goalPath1, goalPath2) &&
          mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_frame_1", goalPath1, 8, kDodgerKey, goal1) &&
          mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_frame_2", goalPath2, 8, kDodgerKey, goal2))
      {
        M5Canvas *goalSpr = (s_dodgerGoalAnimFrame & 1) ? goal2 : goal1;
  
        if (goalSpr && goalSpr->width() > 0 && goalSpr->height() > 0)
        {
          const int drawX = s_dodgerGoalX - ((int)goalSpr->width() / 2);
          const int drawY = s_dodgerGoalY - ((int)goalSpr->height() / 2);
          goalSpr->pushSprite(&spr, drawX, drawY, kDodgerKey);
        }
        else
        {
          spr.fillRect(s_dodgerGoalX - 24, s_dodgerGoalY - 8, 48, 16, TFT_RED);
        }
      }
      else
      {
        spr.fillRect(s_dodgerGoalX - 24, s_dodgerGoalY - 8, 48, 16, TFT_RED);
      }
    }
  }

  if (s_dodgerPhase != DODGER_PHASE_HOLD && s_dodgerPhase != DODGER_PHASE_OFFROAD_HOLD)
  {
    if (haveCar && carSpr)
    {
      const int drawX = s_dodgerPx - (s_dodgerCarW / 2);
      const int drawY = s_dodgerPy - (s_dodgerCarH / 2);
      carSpr->pushSprite(&spr, drawX, drawY, kDodgerKey);
    }
    else
    {
      spr.fillCircle(s_dodgerPx, s_dodgerPy, 6, TFT_GREEN);
      spr.drawCircle(s_dodgerPx, s_dodgerPy, 6, TFT_DARKGREEN);
    }
  }
}

#endif // RH_MINIGAMES_IMPL_IN_PAUSE_MENU