#include "mini_games_internal.h"

#include "graphics_sd_draw.h"

#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "app_state.h"
#include "currency.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "inventory.h"
#include "mg_pause_core.h"
#include "mg_pause_menu.h"
#include "mini_game_assets.h"
#include "mini_game_return_ui.h"
#include "mini_game_runtime.h"
#include "mini_games.h"
#include "pet.h"
#include "save_manager.h"
#include "sdcard.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_defs.h"
#include "ui_runtime.h"

// Flappy-local forward decls / helpers
static const uint16_t kSpriteKey = 0x0841;

static void flappyLogState(const char *tag);

static inline void flappyExitMiniGameToReturnUi(bool beginLockout = true)
{
  flappyLogState("exit");
  mgmem::endSession();
  miniGameExitToReturnUi(beginLockout);
}

static void logMiniGameHeap(const char *tag) { mgAssetsLogHeap(tag); }

void freeFlappyBgCache();
static bool ensureFlappyBgCache(const char *path);

// -----------------------------------------------------------------------------
// FLAPPY FIREBALL GLOBALS
// -----------------------------------------------------------------------------

// START SCREEN
bool s_flappyShowIntro = true;

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

static const char *const kDevBurnFrames[] = {
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn1.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn2.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn3.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn4.png",
    "/raising_hell/graphics/mini_games/flappy/dev/imp_burn5.png",
};

static const char *const kEldBurnFrames[] = {
    "/raising_hell/graphics/mini_games/flappy/eld/mer_curse1.png",
    "/raising_hell/graphics/mini_games/flappy/eld/mer_curse2.png",
    "/raising_hell/graphics/mini_games/flappy/eld/mer_curse3.png",
    "/raising_hell/graphics/mini_games/flappy/eld/mer_curse4.png",
    "/raising_hell/graphics/mini_games/flappy/eld/mer_curse5.png",
};

static const char *miniGameFlappyNameForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Flappy Curse";
  case PET_DEVIL:
  default:
    return "Flappy Fireball";
  }
}

static const char *const *flappyBurnFramesForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return kEldBurnFrames;
  case PET_DEVIL:
  default:
    return kDevBurnFrames;
  }
}

static const char *fireballRunBgLeftPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/eld_fbrun_bgl.png";
  case PET_DEVIL:
  default:
    // Prep for future devil split; safe to point both halves at the old asset for now.
    return "/raising_hell/graphics/mini_games/fbrun/dev/dev_fbrun_bgl.png";
  }
}

static const char *fireballRunBgRightPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/eld_fbrun_bgr.png";
  case PET_DEVIL:
  default:
    // Prep for future devil split; safe to point both halves at the old asset for now.
    return "/raising_hell/graphics/mini_games/fbrun/dev/dev_fbrun_bgr.png";
  }
}

static const char *flappyImpWave1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/mer_taunt1.png";
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
    return "/raising_hell/graphics/mini_games/flappy/eld/mer_taunt2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/imp_wave2.png";
  }
}

static const char *flappyProjectile1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/starfish1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/fireball1.png";
  }
}

static const char *flappyProjectile2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/starfish2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/fireball2.png";
  }
}

static const char *flappyProjectile3PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/starfish3.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/fireball3.png";
  }
}

static const char *flappyObstacleUpPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/column_up.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/rock_spike_up.png";
  }
}

static const char *flappyObstacleDownPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/flappy/eld/column_down.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/flappy/dev/rock_spike_down.png";
  }
}

static const char *flappyIntroTargetTextForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Curse the Merman!";
  case PET_DEVIL:
  default:
    return "Torch the Imp!";
  }
}

static M5Canvas *s_flappyFireball1Spr = nullptr;
static M5Canvas *s_flappyFireball2Spr = nullptr;
static M5Canvas *s_flappyFireball3Spr = nullptr;

static M5Canvas *s_flappyPipeUpSpr = nullptr;
static M5Canvas *s_flappyPipeDownSpr = nullptr;

static M5Canvas *s_impWave1Spr = nullptr;
static M5Canvas *s_impWave2Spr = nullptr;
static M5Canvas *s_impBurn1Spr = nullptr;
static M5Canvas *s_impBurn2Spr = nullptr;
static M5Canvas *s_impBurn3Spr = nullptr;
static M5Canvas *s_impBurn4Spr = nullptr;
static M5Canvas *s_impBurn5Spr = nullptr;

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

static void flappyLogState(const char *tag)
{
  Serial.printf("[FLAPPY] %s intro=%d playing=%d hit=%d burnDone=%d gameOver=%d won=%d fb=(%d,%d) vy=%d dist=%d "
                "free=%u largest=%u\n",
                tag ? tag : "state", s_flappyShowIntro ? 1 : 0, s_flappyPlaying ? 1 : 0, s_impHit ? 1 : 0,
                s_impBurnDone ? 1 : 0, g_app.gameOver ? 1 : 0, playerWon ? 1 : 0, s_fbX, s_fbY, s_fbVY,
                s_flappyDistancePx, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// Shared fullscreen background now lives in mini_game_assets.cpp
// Keep only width/height bookkeeping local for flappy scroll math.

// Flappy pipe sprites (8bpp, cached)
static int s_flappyPipeW = 0;
static int s_flappyPipeH = 0;
static bool s_flappyPipeLoadFailed = false;

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
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "imp_wave1");
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "imp_wave2");

  s_impWave1Spr = nullptr;
  s_impWave2Spr = nullptr;
  s_impBurn1Spr = nullptr;
  s_impBurn2Spr = nullptr;
  s_impBurn3Spr = nullptr;
  s_impBurn4Spr = nullptr;
  s_impBurn5Spr = nullptr;

  s_impWaveSprReady = false;
}

void freeFlappyPipeSprites()
{
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "pipe_up");
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "pipe_down");

  s_flappyPipeUpSpr = nullptr;
  s_flappyPipeDownSpr = nullptr;

  s_flappyPipeW = 0;
  s_flappyPipeH = 0;
  s_flappyPipeLoadFailed = false;
}

// -----------------------------------------------------------------------------
// Flappy pipe sprites (stalagmites/stalactites)
// -----------------------------------------------------------------------------
// We keep this simple: read PNG dimensions once, then draw the PNGs directly.
// (This preserves transparency and avoids maintaining a separate alpha-capable cache.)
static FlappyPipe s_pipes[3];

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

void freeFlappyFireballSprites()
{
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "fireball1");
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "fireball2");
  mgmem::releaseSprite(MiniGame::FLAPPY_FIREBALL, "fireball3");

  s_flappyFireball1Spr = nullptr;
  s_flappyFireball2Spr = nullptr;
  s_flappyFireball3Spr = nullptr;

  s_flappyFireballW = 0;
  s_flappyFireballH = 0;
}

static bool ensureImpWaveSprites()
{
  s_impWave1Spr = nullptr;
  s_impWave2Spr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "imp_wave1", flappyImpWave1PathForPet(), 8, kSpriteKey,
                           s_impWave1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "imp_wave2", flappyImpWave2PathForPet(), 8, kSpriteKey,
                           s_impWave2Spr))
    return false;

  s_impWaveSprReady = s_impWave1Spr && s_impWave2Spr;
  return s_impWaveSprReady;
}

static bool ensureFlappyFireballSprites(const char *bgPath)
{
  (void)bgPath;

  if (!g_sdReady)
    return false;

  s_flappyFireball1Spr = nullptr;
  s_flappyFireball2Spr = nullptr;
  s_flappyFireball3Spr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball1", flappyProjectile1PathForPet(), 8, kFireKey,
                           s_flappyFireball1Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball2", flappyProjectile2PathForPet(), 8, kFireKey,
                           s_flappyFireball2Spr))
    return false;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "fireball3", flappyProjectile3PathForPet(), 8, kFireKey,
                           s_flappyFireball3Spr))
    return false;

  if (!s_flappyFireball1Spr || s_flappyFireball1Spr->width() <= 0 || s_flappyFireball1Spr->height() <= 0)
    return false;

  s_flappyFireballW = (int)s_flappyFireball1Spr->width();
  s_flappyFireballH = (int)s_flappyFireball1Spr->height();
  return true;
}

static const uint16_t kPipeKey = kSpriteKey;

static bool ensureFlappyPipeSprites(const char *bgPath)
{
  (void)bgPath;

  if (!g_sdReady)
    return false;

  s_flappyPipeUpSpr = nullptr;
  s_flappyPipeDownSpr = nullptr;

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "pipe_up", flappyObstacleUpPathForPet(), 8, kPipeKey,
                           s_flappyPipeUpSpr))
  {
    s_flappyPipeLoadFailed = true;
    s_flappyPipeW = 0;
    s_flappyPipeH = 0;
    return false;
  }

  if (!mgmem::ensureSprite(MiniGame::FLAPPY_FIREBALL, "pipe_down", flappyObstacleDownPathForPet(), 8, kPipeKey,
                           s_flappyPipeDownSpr))
  {
    s_flappyPipeLoadFailed = true;
    s_flappyPipeW = 0;
    s_flappyPipeH = 0;
    return false;
  }

  if (!s_flappyPipeUpSpr || s_flappyPipeUpSpr->width() <= 0 || s_flappyPipeUpSpr->height() <= 0)
  {
    s_flappyPipeLoadFailed = true;
    s_flappyPipeW = 0;
    s_flappyPipeH = 0;
    return false;
  }

  s_flappyPipeW = (int)s_flappyPipeUpSpr->width();
  s_flappyPipeH = (int)s_flappyPipeUpSpr->height();
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
  graphicsReleaseUiCachesForMiniGame();
  mgAssetsBeginSession(currentMiniGame, "startFlappyFireball");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("flappy-start-beginSession");

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  logMiniGameHeap("startFlappyFireball");

  miniGameSetReturnUi(retUi, g_app.currentTab);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_flappyInited = true;
  s_flappyShowIntro = true;
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
  freeImpWaveSprites();
  mgmem::logUsage("flappy-after-sprite-free");

  const bool pipeOk = ensureFlappyPipeSprites(bgPath);
  const bool fireballOk = ensureFlappyFireballSprites(bgPath);
  const bool impOk = ensureImpWaveSprites();

  mgmem::logUsage("flappy-after-preload");

  Serial.printf("[FLAPPY] preload bg=%d pipes=%d fireball=%d imp=%d free=%u largest=%u\n", bgOk ? 1 : 0, pipeOk ? 1 : 0,
                fireballOk ? 1 : 0, impOk ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  mgmem::logUsage("flappy-start-complete");
  flappyLogState("start-complete");
}

static bool flappyCollides(int fbX, int fbY, int r, const FlappyPipe &p, int w, int h)
{
  const int pipeVisualW = (s_flappyPipeW > 0) ? s_flappyPipeW : 26;
  const int gapH = 64;

  // General forgiveness
  const int kScreenForgive = 2;
  const int kGapBonusPx = 8;

  // Make the pipe collision narrower than the art so the tapered spike mouths
  // feel fair instead of like invisible flat walls.
  const int kPipeSideInset = 6;

  // Additional forgiveness near the mouth of the gap.
  // As the fireball gets closer to the left/right edge of the pipe column,
  // widen the safe opening a bit.
  const int kMouthForgiveMax = 8;

  if (fbY - r < -kScreenForgive)
    return true;
  if (fbY + r >= h + kScreenForgive)
    return true;

  const int drawX = p.x + (26 - pipeVisualW) / 2;

  int pipeL = drawX + kPipeSideInset;
  int pipeR = drawX + pipeVisualW - kPipeSideInset;

  if (pipeR <= pipeL)
  {
    pipeL = drawX;
    pipeR = drawX + pipeVisualW;
  }

  if (fbX + r < pipeL)
    return false;
  if (fbX - r > pipeR)
    return false;

  int gapTop = p.gapY - gapH / 2 - kGapBonusPx;
  int gapBot = p.gapY + gapH / 2 + kGapBonusPx;

  if (gapTop < 0)
    gapTop = 0;
  if (gapBot > h)
    gapBot = h;

  // Mouth forgiveness:
  // near the left/right edge of the pipe band, allow a little extra gap
  // because the spike art is tapered there.
  const int pipeMid = (pipeL + pipeR) / 2;
  const int halfW = (pipeR - pipeL) / 2;

  int dx = fbX - pipeMid;
  if (dx < 0)
    dx = -dx;

  int mouthForgive = 0;
  if (halfW > 0)
  {
    mouthForgive = (dx * kMouthForgiveMax) / halfW;
    if (mouthForgive > kMouthForgiveMax)
      mouthForgive = kMouthForgiveMax;
  }

  gapTop -= mouthForgive;
  gapBot += mouthForgive;

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

      flappyLogState("lose");

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

        flappyLogState("goal-reached");

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
    const uint32_t now = millis();
    if ((enterOnce && !mgInputLockedOut()) || mgRewardAutoDismissNow(now))
    {
      mgClearRewardState();
      mgResetAcceptState();
      flappyExitMiniGameToReturnUi(true);
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

      flappyLogState("intro-dismissed");

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

            flappyLogState("win");
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

  if (s_flappyBgReady && strcmp(s_flappyBgPath, path) == 0 && mgAssetsSharedBg() != nullptr)
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
    miniGameDrawRewardModal(gW, gH);
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
    spr.drawCentreString(flappyIntroTargetTextForPet(), gW / 2, 22, 2);

    const int impX = (gW - 48) / 2;
    const int impY = 52;

    M5Canvas *impSpr = nullptr;

    if (s_impWave1Spr && s_impWave2Spr)
      impSpr = (s_impFrame == 0) ? s_impWave1Spr : s_impWave2Spr;
    else if (s_impWave1Spr)
      impSpr = s_impWave1Spr;
    else if (s_impWave2Spr)
      impSpr = s_impWave2Spr;

    if (impSpr && impSpr->width() > 0 && impSpr->height() > 0)
      impSpr->pushSprite(&spr, impX, impY, kSpriteKey);

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 116, 2);
    return;
  }

  const bool haveFireball = s_flappyFireball1Spr && s_flappyFireball2Spr && s_flappyFireball3Spr;

  const bool havePipes = s_flappyPipeUpSpr && s_flappyPipeDownSpr;

  M5Canvas *fbFrame0 = s_flappyFireball1Spr;
  M5Canvas *fbFrame1 = s_flappyFireball2Spr;
  M5Canvas *fbFrame2 = s_flappyFireball3Spr;

  M5Canvas *bg = nullptr;
  int bw = 0;
  int bh = 0;
  const bool haveBg = s_flappyBgReady && (mgAssetsSharedBg() != nullptr);

  if (haveBg)
  {
    bg = mgAssetsSharedBg();
    bw = s_flappyBgW;
    bh = s_flappyBgH;
  }

  bool drewBg = false;

  if (haveBg && bg && bw > 0 && bh > 0)
  {
    int x = -(s_flappyBgScrollX % bw);
    if (x > 0)
      x -= bw;

    while (x > -bw)
      x -= bw;

    for (int drawX = x; drawX < gW; drawX += bw)
      bg->pushSprite(&spr, drawX, 0);

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

    if (havePipes)
    {
      const int drawX = x + (pipeW - s_flappyPipeW) / 2;

      s_flappyPipeDownSpr->pushSprite(&spr, drawX, gapTop - s_flappyPipeH, kPipeKey);
      s_flappyPipeUpSpr->pushSprite(&spr, drawX, gapBot, kPipeKey);
    }
    else
    {
      spr.fillRect(x, 0, pipeW, gapTop, TFT_DARKGREY);
      spr.fillRect(x, gapBot, pipeW, gH - gapBot, TFT_DARKGREY);
    }
  }

  if (s_flappyGoalActive && !s_impBurnDone)
  {
    const int impH = 48;
    const int impX = s_flappyGoalX;
    const int impY = s_flappyGoalY - impH;

    if (!s_impHit)
    {
      M5Canvas *impSpr = nullptr;

      if (s_impWave1Spr && s_impWave2Spr)
        impSpr = (s_impFrame == 0) ? s_impWave1Spr : s_impWave2Spr;
      else if (s_impWave1Spr)
        impSpr = s_impWave1Spr;
      else if (s_impWave2Spr)
        impSpr = s_impWave2Spr;

      if (impSpr && impSpr->width() > 0 && impSpr->height() > 0)
        impSpr->pushSprite(&spr, impX, impY, kSpriteKey);
    }
    else
    {
      const char *const *burnFrames = flappyBurnFramesForPet();
      const char *burnPath = nullptr;

      switch (s_impFrame)
      {
      case 0:
        burnPath = burnFrames[0];
        break;
      case 1:
        burnPath = burnFrames[1];
        break;
      case 2:
        burnPath = burnFrames[2];
        break;
      case 3:
        burnPath = burnFrames[3];
        break;
      default:
        burnPath = burnFrames[4];
        break;
      }

      if (burnPath && burnPath[0])
        sprDrawPngFromSD(burnPath, impX, impY);
    }
  }

  if (!s_impHit)
  {
    if ((!s_flappyFireball1Spr || !s_flappyFireball2Spr || !s_flappyFireball3Spr) && s_flappyBgPath[0])
      ensureFlappyFireballSprites(s_flappyBgPath);

    M5Canvas *fbFrames[3] = {s_flappyFireball1Spr, s_flappyFireball2Spr, s_flappyFireball3Spr};

    if (fbFrames[0] && fbFrames[1] && fbFrames[2])
    {
      const int frame = (millis() / 80) % 3;
      M5Canvas *fb = fbFrames[frame];

      const int w = fb->width();
      const int h = fb->height();

      const int drawX = s_fbX - w / 2;
      const int drawY = s_fbY - h / 2;

      fb->pushSprite(&spr, drawX, drawY, kFireKey);
    }
    else
    {
      spr.fillCircle(s_fbX, s_fbY, 5, TFT_ORANGE);
    }
  }
}
