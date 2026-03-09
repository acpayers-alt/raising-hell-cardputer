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

static inline void exitMiniGameToReturnUi(bool beginLockout = true)
{
  mgmem::endSession();
  miniGameExitToReturnUi(beginLockout);
}

// -----------------------------------------------------------------------------
// Mini-game input helpers / shared state
// -----------------------------------------------------------------------------
// Forward decls used by multiple mini-games / defined later in this file
void freeCrossyZoneSprites();
void freeCrossyActorSprites();
static const char *crossyStartZonePathForPet();
static const char *crossyGoalZonePathForPet();
static const char *crossyLavaZonePathForPet(uint8_t frame);
static const char *crossyStonePathForPet();
void freeFlappyBgCache();
static bool ensureFlappyBgCache(const char *path);
static void logMiniGameHeap(const char *tag);

// I should sort these better
void startCrossyRoad();
void updateCrossyRoad(const InputState &input);
void drawCrossyRoad();
void freeFlappyFireballSprites();
void freeFlappyPipeSprites();
void freeDodgerGoalFrames();
void freeDodgerGoreSprite();

// -----------------------------------------------------------------------------
// Mini-game global state
// -----------------------------------------------------------------------------

// Simple mini-game state
static bool s_resultShown = false;

static constexpr uint32_t kSurviveWinMs = 15000; // or whatever your old value was

static const uint16_t kSpriteKey = 0x0841; // very dark grey

static void logMiniGameHeap(const char *tag) { mgAssetsLogHeap(tag); }

static void logSpriteAllocOk(const char *tag, int w, int h, int depth)
{
  Serial.printf("[MG SPR OK] %s %dx%d depth=%d free=%u largest=%u\n", tag, w, h, depth, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void logSpriteAllocFail(const char *tag, int w, int h, int depth)
{
  Serial.printf("[MG SPR FAIL] %s %dx%d depth=%d free=%u largest=%u\n", tag, w, h, depth, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

static void logSpriteLoadFail(const char *tag, const char *path)
{
  Serial.printf("[MG SPR LOAD FAIL] %s path='%s' free=%u largest=%u\n", tag, path ? path : "(null)",
                (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

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
static M5Canvas s_flappyFireballSpr[3];
static bool s_flappyFireballReady = false;
static char s_flappyFireballDir[128] = {0};
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
static constexpr uint32_t kImpBurnHoldMs = 600;
static constexpr uint32_t kImpLastFrameHoldMs = 500;

static const char *const kImpWaveFrames[] = {
    "/raising_hell/graphics/mini_games/flappy/dev/imp_wave1.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_wave2.png",
};

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
static bool s_flappyCrashed = false;
static int s_flappyDistancePx = 0;
static bool s_flappyGoalActive = false;
static int s_flappyGoalX = 0;
static int s_flappyGoalY = 0;
static bool s_flappyGoalReached = false;
static int s_flappyGoalR = 10;
static uint32_t s_flappyStartMs = 0;
static int s_flappyBgW = 0;
static int s_flappyBgH = 0;
static int s_fbX = 0;
static int s_fbY = 0;
static int s_fbVY = 0;
uint32_t s_lastStepMs = 0;
static int s_flappyBgScrollX = 0;

// Survive timer
static const uint32_t s_flappyWinMs = kSurviveWinMs;

// Shared fullscreen background now lives in mini_game_assets.cpp
// Keep only width/height bookkeeping local for flappy scroll math.

// Flappy pipe sprites (8bpp, cached)
static M5Canvas s_flappyPipeUpSpr(&M5.Display);
static M5Canvas s_flappyPipeDownSpr(&M5.Display);
static bool s_flappyPipeSprReady = false;
static int s_flappyPipeW = 0;
static int s_flappyPipeH = 0;
static char s_flappyPipeDir[128] = {0}; // folder containing bg + spikes

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
  for (int i = 0; i < 3; ++i)
    mgAssetsReleaseSprite(s_flappyFireballSpr[i], "flappy-fireball-release");

  s_flappyFireballReady = false;
  s_flappyFireballDir[0] = 0;
  s_flappyFireballW = 0;
  s_flappyFireballH = 0;
}

static bool flappyDirFromPath(const char *path, char *out, size_t outSz)
{
  if (!path || !path[0] || !out || outSz == 0)
    return false;
  const char *last = strrchr(path, '/');
  if (!last)
    return false;

  const size_t len = (size_t)(last - path) + 1; // include trailing '/'
  if (len >= outSz)
    return false;

  memcpy(out, path, len);
  out[len] = 0;
  return true;
}

static bool sdExistsTrySlashLocal(const char *path, const char **outUse)
{
  if (!path || !path[0])
    return false;
  if (SD.exists(path))
  {
    if (outUse)
      *outUse = path;
    return true;
  }
  if (path[0] == '/' && SD.exists(path + 1))
  {
    if (outUse)
      *outUse = path + 1;
    return true;
  }
  return false;
}

// -----------------------------------------------------------------------------
// Flappy pipe sprites (stalagmites/stalactites)
// -----------------------------------------------------------------------------
// We keep this simple: read PNG dimensions once, then draw the PNGs directly.
// (This preserves transparency and avoids maintaining a separate alpha-capable cache.)
static FlappyPipe s_pipes[3];
static bool s_flappyPipeFsSet = false;
static bool s_flappyPipeLoadFailed = false;
static int s_flappyPipeImgW = 0;
static int s_flappyPipeImgH = 0;
static int s_flappyPipePetType = -1;

static inline void flappyEnsurePipeFileStorage()
{
  if (!s_flappyPipeFsSet)
  {
    spr.setFileStorage(SD);
    s_flappyPipeFsSet = true;
  }
}

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
  s_flappyStartMs = millis();
  s_flappyPlaying = true;
  s_flappyCrashed = false;

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
  if (s_flappyPipeSprReady)
  {
    mgAssetsReleaseSprite(s_flappyPipeUpSpr, "flappy-pipe-up-release");
    mgAssetsReleaseSprite(s_flappyPipeDownSpr, "flappy-pipe-down-release");
    s_flappyPipeSprReady = false;
  }

  s_flappyPipeW = 0;
  s_flappyPipeH = 0;
  s_flappyPipeDir[0] = 0;
  s_flappyPipeLoadFailed = false;
}

static bool ensureImpWaveSprites()
{
  return true;
}

static bool ensureFlappyFireballSprites(const char *bgPath)
{
  if (!bgPath || !bgPath[0] || !g_sdReady)
    return false;

  char dir[128];
  flappyDirFromBgPath(bgPath, dir, sizeof(dir));
  if (!dir[0])
    return false;

  if (s_flappyFireballReady && s_flappyFireballDir[0] && strcmp(s_flappyFireballDir, dir) == 0)
    return true;

  freeFlappyFireballSprites();

  char path[192];

  for (int i = 0; i < 3; ++i)
  {
    snprintf(path, sizeof(path), "%sfireball%d.png", dir, i + 1);

    if (!mgAssetsLoadSprite(s_flappyFireballSpr[i], path, 8, kFireKey, "flappy-fireball-load"))
    {
      freeFlappyFireballSprites();
      return false;
    }
  }

  s_flappyFireballW = (int)s_flappyFireballSpr[0].width();
  s_flappyFireballH = (int)s_flappyFireballSpr[0].height();

  strlcpy(s_flappyFireballDir, dir, sizeof(s_flappyFireballDir));
  s_flappyFireballReady = true;
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

  if (s_flappyPipeSprReady && s_flappyPipeDir[0] && strcmp(s_flappyPipeDir, dir) == 0)
    return true;

  char upPath[192];
  char downPath[192];
  snprintf(upPath, sizeof(upPath), "%srock_spike_up.png", dir);
  snprintf(downPath, sizeof(downPath), "%srock_spike_down.png", dir);

  freeFlappyPipeSprites();

  if (!mgAssetsLoadSprite(s_flappyPipeUpSpr, upPath, 8, kPipeKey, "flappy-pipe-up-load"))
  {
    s_flappyPipeLoadFailed = true;
    return false;
  }

  if (!mgAssetsLoadSprite(s_flappyPipeDownSpr, downPath, 8, kPipeKey, "flappy-pipe-down-load"))
  {
    mgAssetsReleaseSprite(s_flappyPipeUpSpr, "flappy-pipe-up-release-on-fail");
    s_flappyPipeLoadFailed = true;
    return false;
  }

  s_flappyPipeW = (int)s_flappyPipeUpSpr.width();
  s_flappyPipeH = (int)s_flappyPipeUpSpr.height();
  strlcpy(s_flappyPipeDir, dir, sizeof(s_flappyPipeDir));
  s_flappyPipeSprReady = true;
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

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  logMiniGameHeap("startFlappyFireball");

  miniGameSetReturnUi(retUi);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;
  (void)gW;
  (void)gH;

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
  freeFlappyBgCache();
  ensureFlappyBgCache(flappyBgPathForPet());
  
  freeFlappyPipeSprites();
  freeFlappyFireballSprites();
  
  const char *bgPath = flappyBgPathForPet();
  
  const bool pipeOk = ensureFlappyPipeSprites(bgPath);
  const bool fireballOk = ensureFlappyFireballSprites(bgPath);
  const bool impOk = ensureImpWaveSprites();
  
  Serial.printf("FLAPPY preload: pipes=%d fireball=%d imp=%d free=%u largest=%u\n",
                pipeOk ? 1 : 0,
                fireballOk ? 1 : 0,
                impOk ? 1 : 0,
                (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);
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

static bool s_flappyBgCacheDisabled = false;

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

static void drawFlappyScrollingBg(int scrollX)
{
  M5Canvas *bg = mgAssetsSharedBg();
  if (!bg)
  {
    spr.fillSprite(TFT_BLACK);
    return;
  }

  const int w = mgAssetsSharedBgW();
  const int h = mgAssetsSharedBgH();
  if (w <= 0 || h <= 0)
  {
    spr.fillSprite(TFT_BLACK);
    return;
  }

  // normalize scroll into [0..w-1]
  scrollX %= w;
  if (scrollX < 0)
    scrollX += w;

  int x = -scrollX;
  bg->pushSprite(&spr, x, 0);
  bg->pushSprite(&spr, x + w, 0);
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
      const char *impSprite = (s_impFrame == 0)
                                  ? flappyImpWave1PathForPet()
                                  : flappyImpWave2PathForPet();
    
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

    if (havePipes && s_flappyPipeSprReady)
    {
      const int drawX = x + (pipeW - s_flappyPipeW) / 2;

      // stalactite (top)
      s_flappyPipeDownSpr.pushSprite(&spr, drawX, gapTop - s_flappyPipeH, kPipeKey);

      // stalagmite (bottom)
      s_flappyPipeUpSpr.pushSprite(&spr, drawX, gapBot, kPipeKey);
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
        const char *impSprite = (s_impFrame == 0)
                                    ? flappyImpWave1PathForPet()
                                    : flappyImpWave2PathForPet();
    
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
    if (haveFireball && s_flappyFireballReady)
    {
      const int frame = (millis() / 80) % 3;

      const int w = s_flappyFireballSpr[frame].width();
      const int h = s_flappyFireballSpr[frame].height();

      const int drawX = s_fbX - w / 2;
      const int drawY = s_fbY - h / 2;

      spr.fillCircle(s_fbX, s_fbY, 6, TFT_RED);
      s_flappyFireballSpr[frame].pushSprite(&spr, drawX, drawY, kFireKey);
    }
    else
    {
      spr.fillCircle(s_fbX, s_fbY, 5, TFT_ORANGE);
    }
  }
}

// -----------------------------------------------------------------------------
// Resurrection Run (side-scroller runner)
// -----------------------------------------------------------------------------

static bool rr_active = false;
static bool rr_gameOver = false;
static bool rr_won = false;
static bool rr_ducking = false;

static float rr_y = 0.0f;
static float rr_vy = 0.0f;
static bool rr_onGround = true;

static int rr_distance = 0;
uint32_t rr_lastMs = 0;

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
    {260, RR_SPIKE, 0},  {520, RR_SPIKE, 0},  {780, RR_LOW_FIRE, 0},  {1020, RR_SPIKE, 0}, {1280, RR_LOW_FIRE, 0},
    {1520, RR_SPIKE, 0}, {1780, RR_SPIKE, 0}, {2040, RR_LOW_FIRE, 0}, {2280, RR_SPIKE, 0}, {2540, RR_LOW_FIRE, 0},
};

static const int rr_scriptCount = (int)(sizeof(rr_script) / sizeof(rr_script[0]));
static int rr_nextSpawn = 0;

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

  const int spawnScreenLead = 60;
  const int spawnWorldX = rr_distance + w + spawnScreenLead;

  const int groundY = h - 18;

  if (type == RR_SPIKE)
  {
    rr_obs[slot].x = spawnWorldX;
    rr_obs[slot].y = groundY - 14;
    rr_obs[slot].w = 16;
    rr_obs[slot].h = 14;
    rr_obs[slot].active = true;
  }
  else
  {
    rr_obs[slot].x = spawnWorldX;
    rr_obs[slot].y = groundY - 32;
    rr_obs[slot].w = 18;
    rr_obs[slot].h = 10;
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

  // Make Resurrection Run behave like all other mini-games (state + flags)
  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;
  s_resultShown = false;

  currentMiniGame = MiniGame::RESURRECTION;

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  // Hard reset any previous end-of-game modal state so retries don't instantly re-trigger.
  mgClearRewardState();
  mgResetAcceptState();

  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

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

  invalidateBackgroundCache();
  requestUIRedraw();
}

void updateResurrectionRun(const InputState &input)
{
  const uint32_t now = millis();

  if (rr_gameOver)
  {
    const bool enterOnce = miniGameEnterOnce(input);

    if (enterOnce && !mgInputLockedOut())
    {
      rr_active = false;
      rr_gameOver = false;

      playerWon = rr_won;

      currentMiniGame = MiniGame::NONE;
      g_app.inMiniGame = false;

      onResurrectionMiniGameResult(playerWon);

      miniGameClearReturnUi();

      mgPauseReset();
      clearInputLatch();
      inputForceClear();
      requestUIRedraw();
      mgBeginInputLockout(220);
    }
    return;
  }

  uint32_t dtMs = now - rr_lastMs;
  rr_lastMs = now;
  if (dtMs > 40)
    dtMs = 40;
  const float dt = dtMs / 1000.0f;

  rr_ducking = (input.mgDownHeld || input.mgSpaceHeld);

  const bool jumpOnce = input.mgSelectOnce || input.mgUpOnce;
  const bool jumpHeld = input.mgSelectHeld || input.mgUpHeld;

  if (jumpOnce && rr_onGround)
  {
    rr_vy = -220.0f;
    rr_onGround = false;
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

  const int speed = 290;
  rr_distance += (int)(speed * dt);

  while (rr_nextSpawn < rr_scriptCount && rr_distance >= rr_script[rr_nextSpawn].triggerDist)
  {
    rrSpawnObstacle(rr_script[rr_nextSpawn].type);
    rr_nextSpawn++;
  }

  if (rr_distance >= rr_courseLen)
  {
    rr_gameOver = true;
    rr_won = true;
    return;
  }

  const int w = (screenW > 0) ? screenW : 240;
  const int h = (screenH > 0) ? screenH : 135;

  const int groundY = h - 18;
  const int px = 48;
  int py = groundY - 18 + (int)rr_y;
  int pw = 16;
  int ph = rr_ducking ? 10 : 16;
  if (rr_ducking)
    py = groundY - ph + (int)rr_y;

  for (auto &o : rr_obs)
  {
    if (!o.active)
      continue;

    int ox = o.x - rr_distance;
    int oy = o.y;
    if (rrAabb(px, py, pw, ph, ox, oy, o.w, o.h))
    {
      rr_gameOver = true;
      rr_won = false;
      return;
    }

    if (ox < -40)
      o.active = false;
  }
}

void drawResurrectionRun()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  spr.fillSprite(TFT_BLACK);

  if (rr_gameOver)
  {
    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(rr_won ? TFT_GREEN : TFT_RED, TFT_BLACK);
    spr.drawCentreString(rr_won ? "RESURRECTED!" : "FALLEN...", gW / 2, gH / 2 - 10, 4);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Press ENTER", gW / 2, gH / 2 + 22, 2);
    return;
  }

  const int groundY = gH - 18;
  spr.drawLine(0, groundY, gW, groundY, TFT_DARKGREY);

  int px = 48;
  int py = groundY - 18 + (int)rr_y;
  int pw = 16;
  int ph = rr_ducking ? 10 : 16;
  if (rr_ducking)
    py = groundY - ph + (int)rr_y;
  spr.fillRect(px, py, pw, ph, TFT_GREEN);

  for (auto &o : rr_obs)
  {
    if (!o.active)
      continue;
    int ox = o.x - rr_distance;
    if (ox < -40 || ox > gW + 40)
      continue;
    spr.fillRect(ox, o.y, o.w, o.h, TFT_RED);
  }

  int barW = gW - 20;
  int barX = 10;
  int barY = 6;
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
static uint8_t s_crossyIntroImpFrame = 0;
static uint32_t s_crossyIntroImpAnimMs = 0;

static const int kCrossyCols = 15;
static const int kCrossyRows = 7;

static const int kCrossyTileW = 16;
static const int kCrossyTileH = 19;

static const int kCrossyOriginX = 0;
static const int kCrossyOriginY = 1;

static int s_crossyCarryPxAccum[kCrossyRows] = {0};
static uint8_t s_crossyLaneTick[kCrossyRows] = {0};

static int s_crossyCarryAnchor[kCrossyRows] = {0};
static uint32_t s_crossyAnimMs = 0;
static uint8_t s_crossyLavaFrame = 0;
static uint32_t s_crossyLavaAnimMs = 0;
static uint8_t s_crossyLandingGraceFrames = 0;

static M5Canvas s_crossyStoneSpr(&M5.Display);
static bool s_crossyStoneReady = false;
static char s_crossyStonePath[128] = {0};

static M5Canvas s_crossyImpSpr(&M5.Display);
static bool s_crossyImpReady = false;
static char s_crossyImpPath[128] = {0};

static M5Canvas s_crossyGoalZoneSpr(&M5.Display);
static bool s_crossyGoalZoneReady = false;
static char s_crossyGoalZonePath[128] = {0};

static M5Canvas s_crossyStartZoneSpr(&M5.Display);
static bool s_crossyStartZoneReady = false;
static char s_crossyStartZonePath[128] = {0};

static M5Canvas s_crossyIntroSpr;
static bool s_crossyIntroReady = false;
static char s_crossyIntroPath[96] = {0};

static M5Canvas s_crossyLavaZoneSpr[2] = {M5Canvas(&M5.Display), M5Canvas(&M5.Display)};
static bool s_crossyLavaZoneReady[2] = {false, false};
static char s_crossyLavaZonePath[2][128] = {{0}, {0}};

static M5Canvas s_crossyStoneSmallSpr(&M5.Display);
static bool s_crossyStoneSmallReady = false;
static char s_crossyStoneSmallPath[128] = {0};

static M5Canvas s_crossyStoneXSSpr(&M5.Display);
static bool s_crossyStoneXSReady = false;
static char s_crossyStoneXSPath[128] = {0};

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

static const char *crossyStartZonePathForPet();
static const char *crossyGoalZonePathForPet();
static const char *crossyLavaZonePathForPet(uint8_t frame);
static const char *crossyStonePathForPet();
static const char *crossyStoneSmallPathForPet();
static const char *crossyStoneXSPathForPet();
static const char *crossyImpPathForPet(CrossyFacing facing);

static bool ensureCrossyStartZoneSprite();
static bool ensureCrossyGoalZoneSprite();
static bool ensureCrossyLavaZoneSprite(uint8_t frame);
static bool ensureCrossyStoneSprite();
static bool ensureCrossyStoneSmallSprite();
static bool ensureCrossyStoneXSSprite();
static bool ensureCrossyImpSprite();

static bool crossyRowIsWater(int row) { return row >= 1 && row <= 5; }

static bool crossyRowIsRoad(int row) { return false; }

static bool crossyRowIsGoal(int row) { return row == 0; }

static bool crossyRowIsSafe(int row) { return row == 6; }

static bool crossyPlayerOverlapsMoverInRow(int row);

static bool ensureCrossyGoalZoneSprite()
{
  return mgAssetsLoadCachedSprite(
      s_crossyGoalZoneSpr,
      s_crossyGoalZoneReady,
      s_crossyGoalZonePath,
      sizeof(s_crossyGoalZonePath),
      crossyGoalZonePathForPet(),
      8,
      kSpriteKey,
      "crossy-goal-load",
      "crossy-goal-release");
}

static bool ensureCrossyStartZoneSprite()
{
  return mgAssetsLoadCachedSprite(
      s_crossyStartZoneSpr,
      s_crossyStartZoneReady,
      s_crossyStartZonePath,
      sizeof(s_crossyStartZonePath),
      crossyStartZonePathForPet(),
      8,
      kSpriteKey,
      "crossy-start-load",
      "crossy-start-release");
}

static bool ensureCrossyLavaZoneSprite(uint8_t frame)
{
  const uint8_t i = frame & 1;

  return mgAssetsLoadCachedSprite(
      s_crossyLavaZoneSpr[i],
      s_crossyLavaZoneReady[i],
      s_crossyLavaZonePath[i],
      sizeof(s_crossyLavaZonePath[i]),
      crossyLavaZonePathForPet(i),
      16,
      TFT_BLACK,
      (i == 0) ? "crossy-lava0-load" : "crossy-lava1-load",
      (i == 0) ? "crossy-lava0-release" : "crossy-lava1-release");
}

static bool ensureCrossyIntroSprite()
{
  return mgAssetsLoadCachedSprite(
      s_crossyIntroSpr,
      s_crossyIntroReady,
      s_crossyIntroPath,
      sizeof(s_crossyIntroPath),
      "/raising_hell/graphics/mini_games/crossy/dev/intro_goal.png",
      8,
      kSpriteKey,
      "crossy-intro-load",
      "crossy-intro-release");
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
  s_crossyGoalZoneSpr.deleteSprite();
  s_crossyGoalZoneReady = false;
  s_crossyGoalZonePath[0] = 0;

  s_crossyStartZoneSpr.deleteSprite();
  s_crossyStartZoneReady = false;
  s_crossyStartZonePath[0] = 0;

  for (int i = 0; i < 2; ++i)
  {
    mgAssetsReleaseSprite(s_crossyLavaZoneSpr[i], (i == 0) ? "crossy-lava0-release" : "crossy-lava1-release");
    s_crossyLavaZoneReady[i] = false;
    s_crossyLavaZonePath[i][0] = 0;
  }

  if (s_crossyIntroReady)
  {
    s_crossyIntroSpr.deleteSprite();
    s_crossyIntroReady = false;
    s_crossyIntroPath[0] = 0;
  }
}

static bool drawCrossyLavaZoneRow(int row, int y)
{
  const uint8_t lavaFrame = (s_crossyLavaFrame + row) & 1;
  const char *path = crossyLavaZonePathForPet(lavaFrame);

  if (!path || !path[0] || !g_sdReady)
    return false;

  const char *usePath = nullptr;
  if (!sdExistsTrySlash(path, &usePath))
    return false;

  return spr.drawPngFile(SD, usePath, 0, y);
}

static bool loadCrossyRowSprite(M5Canvas &dst, bool &ready, char *cachedPath, size_t cachedPathSize, const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  if (ready && strcmp(cachedPath, path) == 0)
  {
    if (dst.width() > 0 && dst.height() > 0)
      return true;

    // Cached state lied; force reload
    ready = false;
    cachedPath[0] = 0;
  }

  if (ready)
  {
    dst.deleteSprite();
    ready = false;
    cachedPath[0] = 0;
  }

  int w = 0;
  int h = 0;
  const char *usePath = nullptr;
  if (!mgAssetsReadPngDims(path, &w, &h, &usePath))
    return false;

  dst.deleteSprite();
  dst.setColorDepth(8);

  if (!dst.createSprite(w, h))
    return false;

  dst.fillSprite(kSpriteKey);

  if (!dst.drawPngFile(SD, usePath, 0, 0))
  {
    dst.deleteSprite();
    ready = false;
    cachedPath[0] = 0;
    return false;
  }

  strlcpy(cachedPath, path, cachedPathSize);
  ready = true;
  return true;
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
  if (s_crossyStoneReady)
  {
    mgAssetsReleaseSprite(s_crossyStoneSpr, "crossy-stone-release");
    s_crossyStoneReady = false;
    s_crossyStonePath[0] = 0;
  }

  if (s_crossyImpReady)
  {
    mgAssetsReleaseSprite(s_crossyImpSpr, "crossy-imp-release");
    s_crossyImpReady = false;
    s_crossyImpPath[0] = 0;
  }

  if (s_crossyStoneSmallReady)
  {
    mgAssetsReleaseSprite(s_crossyStoneSmallSpr, "crossy-stone-small-release");
    s_crossyStoneSmallReady = false;
    s_crossyStoneSmallPath[0] = 0;
  }

  if (s_crossyStoneXSReady)
  {
    mgAssetsReleaseSprite(s_crossyStoneXSSpr, "crossy-stone-xs-release");
    s_crossyStoneXSReady = false;
    s_crossyStoneXSPath[0] = 0;
  }
}

static bool ensureCrossyStoneSprite()
{
  return mgAssetsLoadCachedSprite(
      s_crossyStoneSpr,
      s_crossyStoneReady,
      s_crossyStonePath,
      sizeof(s_crossyStonePath),
      crossyStonePathForPet(),
      8,
      kSpriteKey,
      "crossy-stone-load",
      "crossy-stone-release");
}

static bool ensureCrossyStoneSmallSprite()
{
  return mgAssetsLoadCachedSprite(
      s_crossyStoneSmallSpr,
      s_crossyStoneSmallReady,
      s_crossyStoneSmallPath,
      sizeof(s_crossyStoneSmallPath),
      crossyStoneSmallPathForPet(),
      8,
      kSpriteKey,
      "crossy-stone-small-load",
      "crossy-stone-small-release");
}

static bool ensureCrossyStoneXSSprite()
{
  return mgAssetsLoadCachedSprite(
      s_crossyStoneXSSpr,
      s_crossyStoneXSReady,
      s_crossyStoneXSPath,
      sizeof(s_crossyStoneXSPath),
      crossyStoneXSPathForPet(),
      8,
      kSpriteKey,
      "crossy-stone-xs-load",
      "crossy-stone-xs-release");
}

static bool ensureCrossyImpSprite()
{
  return mgAssetsLoadCachedSprite(
      s_crossyImpSpr,
      s_crossyImpReady,
      s_crossyImpPath,
      sizeof(s_crossyImpPath),
      crossyImpPathForPet(s_crossyFacing),
      8,
      kSpriteKey,
      "crossy-imp-load",
      "crossy-imp-release");
}

static const char *crossyDirForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/";
  }
}

static const char *crossyBgFramePathForPet(uint8_t frame)
{
  const uint8_t f = (frame & 1) + 1;

  switch (pet.type)
  {
  case PET_ELDRITCH:
    switch (f)
    {
    case 1:
      return "/raising_hell/graphics/mini_games/crossy/eld/crossy_eld_bg1.jpg";
    default:
      return "/raising_hell/graphics/mini_games/crossy/eld/crossy_eld_bg2.jpg";
    }

  case PET_DEVIL:
  default:
    switch (f)
    {
    case 1:
      return "/raising_hell/graphics/mini_games/crossy/dev/crossy_dev_bg1.jpg";
    default:
      return "/raising_hell/graphics/mini_games/crossy/dev/crossy_dev_bg2.jpg";
    }
  }
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
  if (platSize == CROSSY_PLAT_XS)
  {
    if (ensureCrossyStoneXSSprite() && s_crossyStoneXSReady)
    {
      const int drawX = x + (w - (int)s_crossyStoneXSSpr.width()) / 2;
      const int drawY = y + (h - (int)s_crossyStoneXSSpr.height()) / 2;
      s_crossyStoneXSSpr.pushSprite(&spr, drawX, drawY, kSpriteKey);
      return;
    }
  }
  else if (platSize == CROSSY_PLAT_SMALL)
  {
    if (ensureCrossyStoneSmallSprite() && s_crossyStoneSmallReady)
    {
      const int drawX = x + (w - (int)s_crossyStoneSmallSpr.width()) / 2;
      const int drawY = y + (h - (int)s_crossyStoneSmallSpr.height()) / 2;
      s_crossyStoneSmallSpr.pushSprite(&spr, drawX, drawY, kSpriteKey);
      return;
    }
  }
  else
  {
    if (ensureCrossyStoneSprite() && s_crossyStoneReady)
    {
      const int drawX = x + (w - (int)s_crossyStoneSpr.width()) / 2;
      const int drawY = y + (h - (int)s_crossyStoneSpr.height()) / 2;
      s_crossyStoneSpr.pushSprite(&spr, drawX, drawY, kSpriteKey);
      return;
    }
  }

  spr.fillRoundRect(x, y + 2, w, h - 4, 3, TFT_DARKGREY);
  spr.drawFastHLine(x + 2, y + 4, w - 4, TFT_LIGHTGREY);
  spr.drawFastHLine(x + 3, y + h - 4, w - 6, TFT_BLACK);
}

static void drawCrossyImp(int x, int y, int w, int h, uint32_t now)
{
  if (ensureCrossyImpSprite() && s_crossyImpReady)
  {
    s_crossyImpSpr.pushSprite(&spr, x, y, kSpriteKey);
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

  s_crossyAnimMs = millis();

  s_crossyShowIntro = true;
  s_crossyDontShowAgain = false;
  s_crossyIntroImpFrame = 0;
  s_crossyIntroImpAnimMs = millis();

  mgClearRewardState();
  mgResetAcceptState();

  currentMiniGame = MiniGame::CROSSY_ROAD;
  mgAssetsBeginSession(currentMiniGame, "startCrossyRoad");
  mgmem::beginSession(currentMiniGame, pet.type);
  logMiniGameHeap("startCrossyRoad");

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_crossyInited = true;
  crossyReset();
  freeCrossyZoneSprites();
  freeCrossyActorSprites();

  ensureCrossyStartZoneSprite();
  ensureCrossyGoalZoneSprite();

  const bool lava0 = ensureCrossyLavaZoneSprite(0);
  const bool lava1 = ensureCrossyLavaZoneSprite(1);

  Serial.printf("Crossy lava reload: f0=%d f1=%d\n", lava0 ? 1 : 0, lava1 ? 1 : 0);

  ensureCrossyStoneSprite();
  ensureCrossyStoneSmallSprite();
  ensureCrossyStoneXSSprite();
  ensureCrossyImpSprite();

  s_crossyWinPoseActive = false;
  s_crossyWinPoseStart = 0;

  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  s_crossyWinPoseActive = false;
  s_crossyWinPoseStart = 0;
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

static void crossyCarryPlayerOnLog()
{
  if (s_crossyPy < 0 || s_crossyPy >= kCrossyRows)
    return;

  const CrossyLane &L = s_crossyLanes[s_crossyPy];
  if (L.type != CROSSY_LANE_WATER)
    return;

  // IMPORTANT:
  // Draw code uses x = x0 - offset, so positive offset means the platform
  // moves LEFT on screen. Frog must move with the platform, so carry is the
  // NEGATIVE of the offset direction.
  const int deltaPx = -(int)L.speed * (L.dir > 0 ? 1 : -1);

  s_crossyCarryPxAccum[s_crossyPy] += deltaPx;

  while (s_crossyCarryPxAccum[s_crossyPy] >= kCrossyTileW)
  {
    s_crossyPx += 1;
    s_crossyCarryPxAccum[s_crossyPy] -= kCrossyTileW;
  }

  while (s_crossyCarryPxAccum[s_crossyPy] <= -kCrossyTileW)
  {
    s_crossyPx -= 1;
    s_crossyCarryPxAccum[s_crossyPy] += kCrossyTileW;
  }
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
    {
      s_crossyIntroImpAnimMs = now;
      s_crossyIntroImpFrame ^= 1;
    }

    if (input.mgSpaceOnce)
      s_crossyDontShowAgain = !s_crossyDontShowAgain;

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

    ensureCrossyImpSprite();
    playBeep();
  }

  if (s_crossyPy == 0)
  {
    s_crossyFacing = CROSSY_FACE_DOWN;
    ensureCrossyImpSprite();

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

  const uint8_t animBaseFrame = s_crossyLavaFrame & 1;

  const bool haveGoal = ensureCrossyGoalZoneSprite();
  const bool haveStart = ensureCrossyStartZoneSprite();
  const bool haveLava0 = ensureCrossyLavaZoneSprite(0);
  const bool haveLava1 = ensureCrossyLavaZoneSprite(1);

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

    const char *introPath = "/raising_hell/graphics/mini_games/crossy/dev/intro_goal.png";

    int iw = 0, ih = 0;
    const char *useIntroPath = nullptr;

    if (mgAssetsReadPngDims(introPath, &iw, &ih, &useIntroPath))    {
      const int ix = (gW - iw) / 2;
      const int iy = 48;
      sprDrawPngFromSD(useIntroPath ? useIntroPath : introPath, ix, iy);
    }
    else
    {
      sprDrawPngFromSD(introPath, (gW - 68) / 2, 48);
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
      if (haveGoal && s_crossyGoalZoneReady)
        s_crossyGoalZoneSpr.pushSprite(&spr, 0, y);
      else
        spr.fillRect(0, y, gW, kCrossyTileH, TFT_RED);
      break;

    case CROSSY_LANE_SAFE:
      if (row == kCrossyRows - 1)
      {
        if (haveStart && s_crossyStartZoneReady)
          s_crossyStartZoneSpr.pushSprite(&spr, 0, y);
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
  
        if (ensureCrossyLavaZoneSprite(lavaFrame) && s_crossyLavaZoneReady[lavaFrame])
        {
          const int tileW = (int)s_crossyLavaZoneSpr[lavaFrame].width();
          const int tileH = (int)s_crossyLavaZoneSpr[lavaFrame].height();
  
          if (tileW > 0 && tileH > 0)
          {
            for (int x = 0; x < gW; x += tileW)
              s_crossyLavaZoneSpr[lavaFrame].pushSprite(&spr, x, y);
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

static M5Canvas s_dodgerFireballSpr[3] = {M5Canvas(&M5.Display), M5Canvas(&M5.Display), M5Canvas(&M5.Display)};

static bool s_dodgerFreezeScroll = false;

static bool s_dodgerGoalActive = false;
static bool s_dodgerGoalReached = false;
static int16_t s_dodgerGoalX = 0;
static int16_t s_dodgerGoalY = 0;

static constexpr uint32_t kDodgerGoalSpawnMs = 12000;

static constexpr uint32_t kDodgerCoastMs = 100;

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
static constexpr uint32_t kDodgerGoalHoldMs = 900;

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

static bool s_dodgerFireballReady = false;
static char s_dodgerFireballDir[128] = {0};
static int s_dodgerFireballW = 0;
static int s_dodgerFireballH = 0;

static M5Canvas s_dodgerCarSpr(&M5.Display);
static bool s_dodgerCarReady = false;
static char s_dodgerCarPath[128] = {0};
static int s_dodgerCarW = 0;
static int s_dodgerCarH = 0;

static bool ensureDodgerGoalFrames(const char *path0, const char *path1);
static bool ensureDodgerGoreSprite(const char *path);

void freeDodgerGoalFrames()
{
  for (int i = 0; i < 2; ++i)
  {
    if (s_dodgerGoalFrameReady[i])
      s_dodgerGoalSpr[i].deleteSprite();

    s_dodgerGoalFrameReady[i] = false;
    s_dodgerGoalFramePath[i][0] = 0;
  }

  s_dodgerGoalW = 0;
  s_dodgerGoalH = 0;
}

void freeDodgerGoreSprite()
{
  if (s_dodgerGoreReady)
    s_dodgerGoreSpr.deleteSprite();

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
    return "/raising_hell/graphics/mini_games/fbrun/eld/eld_fbrun_bg.jpg";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/dev_fbrun_bg.jpg";
  }
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

static bool ensureDodgerFireballSpriteFrame(uint8_t frame)
{
  if (!g_sdReady)
    return false;

  const uint8_t i = frame % 3;

  char dir[128];
  flappyDirFromBgPath(fireballRunBgPathForPet(), dir, sizeof(dir));
  if (!dir[0])
    return false;

  char path[192];
  snprintf(path, sizeof(path), "%sfireball%d.png", dir, (int)i + 1);

  static const char *kLoadTags[3] = {
      "dodger-fireball1-load",
      "dodger-fireball2-load",
      "dodger-fireball3-load"};

  static const char *kReleaseTags[3] = {
      "dodger-fireball1-release",
      "dodger-fireball2-release",
      "dodger-fireball3-release"};

  bool ok = mgAssetsLoadCachedSprite(
      s_dodgerFireballSpr[i],
      s_dodgerFireballReady,
      s_dodgerFireballDir,
      sizeof(s_dodgerFireballDir),
      path,
      8,
      kDodgerKey,
      kLoadTags[i],
      kReleaseTags[i]);

  if (ok)
  {
    s_dodgerFireballW = (int)s_dodgerFireballSpr[i].width();
    s_dodgerFireballH = (int)s_dodgerFireballSpr[i].height();
  }

  return ok;
}

static bool ensureDodgerCarSprite()
{
  return mgAssetsLoadCachedSprite(
      s_dodgerCarSpr,
      s_dodgerCarReady,
      s_dodgerCarPath,
      sizeof(s_dodgerCarPath),
      fireballRunCarPathForPet(),
      8,
      kDodgerKey,
      "dodger-car-load",
      "dodger-car-release");
}

static bool ensureDodgerGoalFrames()
{
  return ensureDodgerGoalFrames(
      dodgerGoalFrame1PathForPet(),
      dodgerGoalFrame2PathForPet());
}

static bool loadDodgerSprite(LGFX_Sprite &dst, const char *path, int &outW, int &outH)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  int w = 0, h = 0;
  const char *usePath = nullptr;

  if (!mgAssetsReadPngDims(path, &w, &h, &usePath) || w <= 0 || h <= 0)    return false;

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

  const bool cached = s_dodgerGoalFrameReady[0] && s_dodgerGoalFrameReady[1] &&
                      strcmp(s_dodgerGoalFramePath[0], path0) == 0 && strcmp(s_dodgerGoalFramePath[1], path1) == 0;

  if (cached)
    return true;

  freeDodgerGoalFrames();

  int w0 = 0, h0 = 0;
  if (!loadDodgerSprite(s_dodgerGoalSpr[0], path0, w0, h0))
  {
    freeDodgerGoalFrames();
    return false;
  }

  int w1 = 0, h1 = 0;
  if (!loadDodgerSprite(s_dodgerGoalSpr[1], path1, w1, h1))
  {
    freeDodgerGoalFrames();
    return false;
  }

  s_dodgerGoalFrameReady[0] = true;
  s_dodgerGoalFrameReady[1] = true;

  strlcpy(s_dodgerGoalFramePath[0], path0, sizeof(s_dodgerGoalFramePath[0]));
  strlcpy(s_dodgerGoalFramePath[1], path1, sizeof(s_dodgerGoalFramePath[1]));

  s_dodgerGoalW = w0;
  s_dodgerGoalH = h0;

  return true;
}

static bool ensureDodgerGoreSprite()
{
  return ensureDodgerGoreSprite(dodgerGoalGorePathForPet());
}

static bool ensureDodgerGoreSprite(const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  if (s_dodgerGoreReady && s_dodgerGorePath[0] && strcmp(s_dodgerGorePath, path) == 0)
    return true;

  freeDodgerGoreSprite();

  int w = 0;
  int h = 0;
  if (!loadDodgerSprite(s_dodgerGoreSpr, path, w, h))
  {
    freeDodgerGoreSprite();
    return false;
  }

  s_dodgerGoreReady = true;
  strlcpy(s_dodgerGorePath, path, sizeof(s_dodgerGorePath));
  return true;
}

void freeDodgerBgCache()
{
  mgAssetsReleaseSharedBgIfOwner(MiniGame::INFERNAL_DODGER);
  s_dodgerBgScrollY = 0;
}

void freeDodgerFireballSprites()
{
  for (int i = 0; i < 3; ++i)
    mgAssetsReleaseSprite(s_dodgerFireballSpr[i], "dodger-fireball-release");

  s_dodgerFireballReady = false;
  s_dodgerFireballDir[0] = 0;
  s_dodgerFireballW = 0;
  s_dodgerFireballH = 0;
}

void freeDodgerCarSprite()
{
  if (s_dodgerCarReady)
  {
    mgAssetsReleaseSprite(s_dodgerCarSpr, "dodger-car-release");
    s_dodgerCarReady = false;
  }

  s_dodgerCarPath[0] = 0;
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

  if (s_dodgerFireballReady && s_dodgerFireballDir[0] && strcmp(s_dodgerFireballDir, dir) == 0)
    return true;

  freeDodgerFireballSprites();

  char path[192];

  for (int i = 0; i < 3; ++i)
  {
    snprintf(path, sizeof(path), "%sfireball%d.png", dir, i + 1);

    if (!mgAssetsLoadSprite(s_dodgerFireballSpr[i], path, 8, kDodgerKey, "dodger-fireball-load"))
    {
      freeDodgerFireballSprites();
      return false;
    }
  }

  s_dodgerFireballW = (int)s_dodgerFireballSpr[0].width();
  s_dodgerFireballH = (int)s_dodgerFireballSpr[0].height();
  strlcpy(s_dodgerFireballDir, dir, sizeof(s_dodgerFireballDir));
  s_dodgerFireballReady = true;
  return true;
}

static bool ensureDodgerCarSprite(const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return false;

  if (s_dodgerCarReady && s_dodgerCarPath[0] && strcmp(s_dodgerCarPath, path) == 0)
    return true;

  freeDodgerCarSprite();

  if (!mgAssetsLoadSprite(s_dodgerCarSpr, path, 8, kDodgerKey, "dodger-car-load"))
  {
    s_dodgerCarReady = false;
    return false;
  }

  s_dodgerCarW = (int)s_dodgerCarSpr.width();
  s_dodgerCarH = (int)s_dodgerCarSpr.height();
  strlcpy(s_dodgerCarPath, path, sizeof(s_dodgerCarPath));
  s_dodgerCarReady = true;
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
  const int roadLeft = 54;
  const int roadRight = gW - 54;

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

  freeDodgerBgCache();
  freeDodgerFireballSprites();
  freeDodgerCarSprite();
  freeDodgerGoalFrames();
  freeDodgerGoreSprite();
  ensureDodgerBgCache(fireballRunBgPathForPet());
  ensureDodgerFireballSprites(fireballRunBgPathForPet());
  ensureDodgerCarSprite();
  s_dodgerInited = true;
  dodgerReset();

  invalidateBackgroundCache();
  s_dodgerShowIntro = true;
  s_dodgerDontShowAgain = false;
  s_dodgerIntroImpFrame = 0;
  s_dodgerIntroImpAnimMs = millis();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);
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

  int roadSpeed = 2 + (difficulty / 4);
  if (roadSpeed > 4)
    roadSpeed = 4;

  // prevent acceleration during coast/goal/impact/exit/offroad
  if (s_dodgerPhase != DODGER_PHASE_FIREBALLS)
    roadSpeed = 2;

  if (!s_dodgerFreezeScroll)
    s_dodgerBgScrollY -= roadSpeed;

  if (s_dodgerPhase == DODGER_PHASE_FIREBALLS && aliveMs >= kDodgerGoalSpawnMs)
  {
    s_dodgerPhase = DODGER_PHASE_COAST;
    s_dodgerPhaseStartMs = now;
    s_dodgerMoveDir = 0;

    for (auto &b : s_dodgerBalls)
      b.active = false;

      freeDodgerGoalFrames();
      freeDodgerGoreSprite();
  }

  if (s_dodgerPhase == DODGER_PHASE_COAST)
  {
    const int targetX = gW / 2;
    const int centerDriftPx = 2;

    if (s_dodgerPx < targetX)
      s_dodgerPx += centerDriftPx;
    else if (s_dodgerPx > targetX)
      s_dodgerPx -= centerDriftPx;

    if (abs(s_dodgerPx - targetX) < centerDriftPx)
      s_dodgerPx = targetX;

    s_dodgerPxF = (float)s_dodgerPx;

    if ((now - s_dodgerPhaseStartMs) >= kDodgerCoastMs)
    {
      s_dodgerPhase = DODGER_PHASE_GOAL;
      s_dodgerPhaseStartMs = now;
      s_dodgerGoalActive = true;
      s_dodgerGoalReached = false;
      s_dodgerGoalX = gW / 2;
      s_dodgerGoalY = -24;
      s_dodgerGoalAnimFrame = 0;
      s_dodgerGoalAnimMs = now;
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

  const int roadLeft = 54;
  const int roadRight = gW - 54;

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

    if (s_dodgerPhase == DODGER_PHASE_FIREBALLS)
    {
      s_dodgerSpawnAccMs += stepMs;
      if (s_dodgerSpawnAccMs >= (uint32_t)spawnEveryMs)
      {
        s_dodgerSpawnAccMs = 0;
        dodgerSpawnOne(difficulty);
      }

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

      s_dodgerGoalY += roadSpeed;

      if (s_dodgerGoalY >= targetY)
      {
        s_dodgerGoalY = targetY;
        s_dodgerGoalReached = true;
        s_dodgerFreezeScroll = true;
        s_dodgerPhase = DODGER_PHASE_CAR_EXIT;
        s_dodgerPhaseStartMs = now;
        soundConfirm();
      }
    }
    else if (s_dodgerPhase == DODGER_PHASE_IMPACT)
    {
      s_dodgerPhase = DODGER_PHASE_CAR_EXIT;
      s_dodgerPhaseStartMs = now;
    }
    else if (s_dodgerPhase == DODGER_PHASE_CAR_EXIT)
    {
      s_dodgerPy -= 3;

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

  const bool haveFireballs = s_dodgerFireballReady;
  const bool haveCar = s_dodgerCarReady;

  bool drewBg = false;

  if (haveBg && bg && bw > 0 && bh > 0)
  {
    int y = -(s_dodgerBgScrollY % bh);
    if (y > 0)
      y -= bh;

    bg->pushSprite(&spr, 0, y);
    bg->pushSprite(&spr, 0, y + bh);
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

    const char *introImp = (s_dodgerIntroImpFrame == 0)
                               ? dodgerGoalFrame1PathForPet()
                               : dodgerGoalFrame2PathForPet();

    sprDrawPngFromSD(introImp, impX, impY);

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

  if (s_dodgerPhase == DODGER_PHASE_FIREBALLS)
  {
    for (auto &b : s_dodgerBalls)
    {
      if (!b.active)
        continue;

      const int bx = (int)b.x;
      const int by = (int)b.y;

      if (haveFireballs)
      {
        const int frame = (millis() / 80) % 3;
        const int w = s_dodgerFireballSpr[frame].width();
        const int h = s_dodgerFireballSpr[frame].height();

        const int drawX = bx - w / 2;
        const int drawY = by - h / 2;

        s_dodgerFireballSpr[frame].pushSprite(&spr, drawX, drawY, kDodgerKey);
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
        (s_dodgerPhase == DODGER_PHASE_IMPACT) ||
        (s_dodgerPhase == DODGER_PHASE_CAR_EXIT) ||
        (s_dodgerPhase == DODGER_PHASE_HOLD);

    const char *goalPath = gorePhase
                               ? dodgerGoalGorePathForPet()
                               : ((s_dodgerGoalAnimFrame & 1)
                                      ? dodgerGoalFrame2PathForPet()
                                      : dodgerGoalFrame1PathForPet());

    int iw = 0;
    int ih = 0;
    const char *usePath = nullptr;

    if (goalPath && mgAssetsReadPngDims(goalPath, &iw, &ih, &usePath) && iw > 0 && ih > 0)
    {
      const int drawX = s_dodgerGoalX - (iw / 2);
      const int drawY = s_dodgerGoalY - (ih / 2);
      sprDrawPngFromSD(usePath ? usePath : goalPath, drawX, drawY);
    }
    else
    {
      spr.fillRect(s_dodgerGoalX - 24, s_dodgerGoalY - 8, 48, 16, TFT_RED);
    }
  }

  if (s_dodgerPhase != DODGER_PHASE_HOLD && s_dodgerPhase != DODGER_PHASE_OFFROAD_HOLD)
  {
    if (haveCar)
    {
      const int drawX = s_dodgerPx - (s_dodgerCarW / 2);
      const int drawY = s_dodgerPy - (s_dodgerCarH / 2);
      s_dodgerCarSpr.pushSprite(&spr, drawX, drawY, kDodgerKey);
    }
    else
    {
      spr.fillCircle(s_dodgerPx, s_dodgerPy, 6, TFT_GREEN);
      spr.drawCircle(s_dodgerPx, s_dodgerPy, 6, TFT_DARKGREEN);
    }
  }
}

#endif // RH_MINIGAMES_IMPL_IN_PAUSE_MENU