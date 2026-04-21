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

#else

#include "graphics_sd_draw.h"

#include "esp_heap_caps.h"
#include <stdint.h>

#include "mini_game_assets.h"
#include "mini_game_runtime.h"
#include "mini_games.h"
#include "mini_games_internal.h"

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
bool s_resultShown = false;

static const uint16_t kSpriteKey = 0x0841; // very dark grey

static void logMiniGameHeap(const char *tag) { mgAssetsLogHeap(tag); }

// -----------------------------------------------------------------------------
// FLAPPY FIREBALL GLOBALS
// -----------------------------------------------------------------------------

// START SCREEN
static bool s_flappyShowIntro = true;

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

void miniGameDrawRewardModal(int gW, int gH)
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

// -----------------------------------------------------------------------------
// CROSSY HELL GLOBALS (Frogger-style)
// -----------------------------------------------------------------------------
static bool s_crossyShowIntro = true;
static uint32_t s_crossyIntroImpAnimMs = 0;

static const int kCrossyCols = 15;
static const int kCrossyRows = 7;

static const int kCrossyTileW = 16;
static const int kCrossyTileH = 19;

static const int kCrossyOriginX = 0;
static const int kCrossyOriginY = 1;

static uint8_t s_crossyLavaFrame = 0;
static uint32_t s_crossyLavaAnimMs = 0;
static uint8_t s_crossyLandingGraceFrames = 0;

static uint32_t s_crossyWinPoseStart = 0;
static bool s_crossyWinPoseActive = false;

struct CrossyStar
{
  int16_t x;
  int16_t y;
  uint8_t phase;
  uint8_t kind;
};

static constexpr int kCrossyStarCount = 28;
static CrossyStar s_crossyStars[kCrossyStarCount];

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

static M5Canvas *s_crossyLava0 = nullptr;
static M5Canvas *s_crossyLava1 = nullptr;
static M5Canvas *s_crossyEldBg = nullptr;

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

static bool crossyPlayerOverlapsMoverInRow(int row)
{
  if (row < 0 || row >= kCrossyRows)
    return false;

  const CrossyLane &L = s_crossyLanes[row];
  if (L.type != CROSSY_LANE_WATER)
    return false;

  const int playerLeft = s_crossyPx * kCrossyTileW + s_crossyVisualOffsetPx;
  const int playerRight = playerLeft + kCrossyTileW - 1;
  const int playerCenter = playerLeft + (kCrossyTileW / 2);

  const int moverLenPx = (int)L.moverLen * kCrossyTileW;
  const int periodPx = moverLenPx + (int)L.gapPx;
  const int laneW = kCrossyCols * kCrossyTileW;

  if (moverLenPx <= 0 || periodPx <= 0)
    return false;

  int offset = (int)(L.offsetPx % periodPx);
  if (offset < 0)
    offset += periodPx;

  for (int x0 = -periodPx * 2; x0 < laneW + periodPx * 2; x0 += periodPx)
  {
    const int start = x0 - offset;
    const int end = start + moverLenPx - 1;

    if (playerCenter >= start && playerCenter <= end)
      return true;

    if (playerLeft >= start && playerLeft <= end)
      return true;

    if (playerRight >= start && playerRight <= end)
      return true;

    if (playerLeft <= start && playerRight >= end)
      return true;
  }

  return false;
}

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

  M5Canvas *&target = (i == 0) ? s_crossyLava0 : s_crossyLava1;
  return mgmem::ensureSprite(MiniGame::CROSSY_ROAD, assetId, crossyLavaZonePathForPet(i), 16, TFT_BLACK, target);
}

static void crossyInitStars()
{
  const int gW = kCrossyCols * kCrossyTileW;
  const int gH = kCrossyRows * kCrossyTileH;

  for (int i = 0; i < kCrossyStarCount; ++i)
  {
    s_crossyStars[i].x = (int16_t)random(0, gW);
    s_crossyStars[i].y = (int16_t)random(0, gH);
    s_crossyStars[i].phase = (uint8_t)random(0, 64);
    s_crossyStars[i].kind = (uint8_t)random(0, 4);
  }
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

  s_crossyLava0 = nullptr;
  s_crossyLava1 = nullptr;
}

static const char *crossyIntroLine1ForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Escape the Moon! Use Arrow Keys";
  case PET_DEVIL:
  default:
    return "Escape Hell! Use Arrow Keys";
  }
}

static const char *crossyIntroLine2ForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Cross the cosmos, enter the portal!";
  case PET_DEVIL:
  default:
    return "Avoid Lava, Exit between Torches!";
  }
}

static const char *crossyIntroGoalPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/crossy/eld/intro_goal.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/crossy/dev/intro_goal.png";
  }
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
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_up.png";
    case CROSSY_FACE_LEFT:
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_left.png";
    case CROSSY_FACE_RIGHT:
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_right.png";
    case CROSSY_FACE_DOWN:
    default:
      return "/raising_hell/graphics/mini_games/crossy/eld/cult_down.png";
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

static void drawCrossyEldritchStarsForRow(int y, int rowH, uint32_t now)
{
  const int laneTop = y;
  const int laneBottom = y + rowH;
  const uint32_t tick = now / 90U;
  const int gW = kCrossyCols * kCrossyTileW;

  for (int i = 0; i < kCrossyStarCount; ++i)
  {
    const CrossyStar &s = s_crossyStars[i];

    if (s.y < laneTop || s.y >= laneBottom)
      continue;

    const uint8_t t = (uint8_t)((tick + s.phase) & 31U);
    const uint8_t glow = (t < 16U) ? t : (31U - t);

    if (glow < 2)
      continue;

    uint16_t c;
    if (glow < 5)
      c = spr.color565(80, 80, 96);
    else if (glow < 9)
      c = spr.color565(150, 150, 180);
    else
      c = spr.color565(235, 235, 255);

    const int sx = s.x;
    const int sy = s.y;

    if (sx < 0 || sx >= gW)
      continue;

    spr.drawPixel(sx, sy, c);

    if (s.kind >= 1 && glow >= 8)
    {
      if (sx > 0)
        spr.drawPixel(sx - 1, sy, c);
      if (sx + 1 < gW)
        spr.drawPixel(sx + 1, sy, c);
    }

    if (s.kind >= 2 && glow >= 8)
    {
      if (sy > laneTop)
        spr.drawPixel(sx, sy - 1, c);
      if (sy + 1 < laneBottom)
        spr.drawPixel(sx, sy + 1, c);
    }
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

  crossyInitLanes();
  crossyInitStars();
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
  s_crossyIntroImpAnimMs = millis();

  mgClearRewardState();
  mgResetAcceptState();

  currentMiniGame = MiniGame::CROSSY_ROAD;
  graphicsReleaseUiCachesForMiniGame();
  mgAssetsBeginSession(currentMiniGame, "startCrossyRoad");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("crossy-start-beginSession");
  logMiniGameHeap("startCrossyRoad");

  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi, g_app.currentTab);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_crossyInited = true;
  crossyReset();

  mgmem::logUsage("crossy-before-asset-free");

  freeCrossyZoneSprites();
  freeCrossyActorSprites();

  s_crossyLava0 = nullptr;
  s_crossyLava1 = nullptr;
  s_crossyEldBg = nullptr;

  mgmem::logUsage("crossy-after-asset-free");

  const bool startOk = ensureCrossyStartZoneSprite();
  mgmem::logUsage("crossy-after-start-zone");

  const bool goalOk = ensureCrossyGoalZoneSprite();
  mgmem::logUsage("crossy-after-goal-zone");

  bool lava0 = false;
  bool lava1 = false;
  bool eldBgOk = false;

  lava0 = ensureCrossyLavaZoneSprite(0);
  mgmem::logUsage("crossy-after-lava0");

  lava1 = ensureCrossyLavaZoneSprite(1);
  mgmem::logUsage("crossy-after-lava1");

  const bool stoneOk = ensureCrossyStoneSprite();
  mgmem::logUsage("crossy-after-stone-lg");

  const bool stoneSmOk = ensureCrossyStoneSmallSprite();
  mgmem::logUsage("crossy-after-stone-sm");

  const bool stoneXsOk = ensureCrossyStoneXSSprite();
  mgmem::logUsage("crossy-after-stone-xs");

  const bool impOk = true;
  mgmem::logUsage("crossy-after-imp");

  Serial.printf("CROSSY preload: start=%d goal=%d eldbg=%d lava0=%d lava1=%d stone=%d stoneSm=%d stoneXs=%d imp=%d "
                "free=%u largest=%u\n",
                startOk ? 1 : 0, goalOk ? 1 : 0, eldBgOk ? 1 : 0, lava0 ? 1 : 0, lava1 ? 1 : 0, stoneOk ? 1 : 0,
                stoneSmOk ? 1 : 0, stoneXsOk ? 1 : 0, impOk ? 1 : 0, (unsigned)ESP.getFreeHeap(),
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
  int steps = 0;
  const int kMaxStepsPerCall = 2;

  while ((uint32_t)(now - s_crossyLastLaneMs) >= kLaneStepMs && steps < kMaxStepsPerCall)
  {
    s_crossyLastLaneMs += kLaneStepMs;
    steps++;

    for (int r = 0; r < kCrossyRows; ++r)
    {
      CrossyLane &L = s_crossyLanes[r];
      if (L.type != CROSSY_LANE_WATER)
        continue;

      const int movePx = (int)L.speed;
      if (movePx <= 0)
        continue;

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

  if ((uint32_t)(now - s_crossyLastLaneMs) >= (kLaneStepMs * 8))
    s_crossyLastLaneMs = now;
}

void updateCrossyRoad(const InputState &input)
{
  const bool enterOnce = miniGameEnterOnce(input);
  const uint32_t now = millis();

  if (pet.type != PET_ELDRITCH)
  {
    if ((uint32_t)(now - s_crossyLavaAnimMs) >= 180)
    {
      s_crossyLavaAnimMs = now;
      s_crossyLavaFrame ^= 1;
    }
  }

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

  if (!s_crossyInited)
  {
    s_crossyInited = true;
    crossyReset();
  }

  if (s_crossyWinPoseActive)
  {
    s_crossyVisualOffsetPx = ((millis() / 120) % 2) ? -2 : 0;

    if ((uint32_t)(now - s_crossyWinPoseStart) >= 800)
    {
      s_crossyVisualOffsetPx = 0;
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

    if (newPy == 0 && !crossyGoalEntryAllowed(newPx))
    {
      playBeep();
      return;
    }

    s_crossyPx = newPx;
    s_crossyPy = newPy;
    s_crossyVisualOffsetPx = 0;

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
    if (s_crossyPx < 0 || s_crossyPx >= kCrossyCols)
    {
      playerWon = false;
      g_app.gameOver = true;
      requestUIRedraw();
      s_resultShown = true;
      soundError();
      return;
    }

    const bool onPlatform = crossyPlayerOverlapsMoverInRow(s_crossyPy);

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

  M5Canvas *lavaZoneSpr0 = s_crossyLava0;
  M5Canvas *lavaZoneSpr1 = s_crossyLava1;

  spr.fillSprite(TFT_BLACK);

  if (mgRewardShowing())
  {
    miniGameDrawRewardModal(gW, gH);
    return;
  }

  if (s_crossyShowIntro)
  {
    spr.fillSprite(TFT_BLACK);
    spr.setTextDatum(CC_DATUM);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(crossyIntroLine1ForPet(), gW / 2, 8, 2);
    spr.drawCentreString(crossyIntroLine2ForPet(), gW / 2, 26, 2);

    const char *introPath = crossyIntroGoalPathForPet();

    int iw = 0, ih = 0;
    const char *useIntroPath = nullptr;

    if (mgAssetsReadPngDims(introPath, &iw, &ih, &useIntroPath))
    {
      const int ix = (gW - iw) / 2;
      const int iy = 60;
      sprDrawPngFromSD(useIntroPath ? useIntroPath : introPath, ix, iy);
    }
    else
    {
      sprDrawPngFromSD(introPath, (gW - 68) / 2, 60);
    }

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
      if (pet.type == PET_ELDRITCH)
      {
        spr.fillRect(0, y, gW, kCrossyTileH, TFT_BLACK);
        drawCrossyEldritchStarsForRow(y, kCrossyTileH, millis());
      }
      else
      {
        M5Canvas *bgSpr = nullptr;
        const uint8_t lavaFrame = (s_crossyLavaFrame + row) & 1;
        bgSpr = (lavaFrame == 0) ? s_crossyLava0 : s_crossyLava1;

        if (bgSpr && bgSpr->width() > 0 && bgSpr->height() > 0)
        {
          const int tileW = (int)bgSpr->width();
          for (int x = 0; x < gW; x += tileW)
            bgSpr->pushSprite(&spr, x, y);
        }
        else
        {
          spr.fillRect(0, y, gW, kCrossyTileH, TFT_BLACK);
        }
      }

      break;
    }
    }
  }

  // draw movers ONCE, after all lane backgrounds
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
  const int playerX = kCrossyOriginX + s_crossyPx * kCrossyTileW + s_crossyVisualOffsetPx;
  const int playerY = kCrossyOriginY + s_crossyPy * kCrossyTileH - 8;

  drawCrossyImp(playerX, playerY, kCrossyTileW, kCrossyTileH, now);
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

static bool s_dodgerGoalFrameReady[2] = {false, false};
static char s_dodgerGoalFramePath[2][160] = {{0}, {0}};
static int s_dodgerGoalW = 0;
static int s_dodgerGoalH = 0;

static bool s_dodgerGoreReady = false;
static char s_dodgerGorePath[160] = {0};

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
static int16_t s_dodgerSpeed = 3;
static float s_dodgerPxF = 0.0f;
uint32_t s_dodgerMoveLastMs = 0;

static int8_t s_dodgerMoveDir = 0;
static uint32_t s_dodgerDirHoldMs = 0;

static DodgerBall s_dodgerBalls[8];

static const uint16_t kDodgerKey = kSpriteKey;
static const uint16_t kDodgerCarKey = 0xF81F;
static int s_dodgerBgScrollY = 0;

static int s_dodgerFireballW = 0;
static int s_dodgerFireballH = 0;

static int s_dodgerCarW = 0;
static int s_dodgerCarH = 0;

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

static bool ensureDodgerGoalFrame1Only(const char *f1)
{
  if (!f1)
    return false;

  return mgmem::ensureSprite(currentMiniGame, "goal_frame_1", f1, 8, kDodgerKey,
                             s_dodgerGoalFrame1Spr // ← FIXED
  );
}

static const char *dodgerIntroLine1()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Arrow keys or A/L to dodge";
  default:
    return "Arrow keys or A/L to dodge";
  }
}

static const char *dodgerIntroLine2()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Pilot the sub, smash the Merman!!";
  case PET_DEVIL:
  default:
    return "Stay on the road, smash the Imp!";
  }
}

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

static const char *dodgerGoalFrame1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/mertaunt1.png";
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
    return "/raising_hell/graphics/mini_games/fbrun/eld/mertaunt2.png";
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
    return "/raising_hell/graphics/mini_games/fbrun/eld/mer_gore.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/imp_gore.png";
  }
}

static const char *dodgerProjectile1PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/trident1.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/fireball1.png";
  }
}

static const char *dodgerProjectile2PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/trident2.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/fireball2.png";
  }
}

static const char *dodgerProjectile3PathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/trident3.png";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/mini_games/fbrun/dev/fireball3.png";
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

  Serial.printf("DODGER gore petPath=%s\n", petPath ? petPath : "(null)");

  if (sdExistsTrySlash(petPath, &usePath))
  {
    Serial.printf("DODGER gore found pet path: %s\n", usePath ? usePath : petPath);
    return usePath ? usePath : petPath;
  }

  const char *fallback = "/raising_hell/graphics/mini_games/fbrun/dev/imp_gore.png";

  if (sdExistsTrySlash(fallback, &usePath))
  {
    Serial.printf("DODGER gore found fallback path: %s\n", usePath ? usePath : fallback);
    return usePath ? usePath : fallback;
  }

  Serial.println("DODGER gore path resolve FAILED");
  return nullptr;
}

static const char *fireballRunCarPathForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/mini_games/fbrun/eld/sub_sprite.png";
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

  if (!canvasDrawPngFromSD(dst, usePath, 0, 0))
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
  {
    Serial.printf("DODGER gore load skipped path=%s g_sdReady=%d\n", path ? path : "(null)", g_sdReady ? 1 : 0);
    return false;
  }

  Serial.printf("DODGER gore load try: %s\n", path);

  s_dodgerGoreSpr = nullptr;

  const bool ok = mgmem::ensureSprite(MiniGame::INFERNAL_DODGER, "goal_gore", path, 8, kDodgerKey, s_dodgerGoreSpr) &&
                  s_dodgerGoreSpr && s_dodgerGoreSpr->width() > 0 && s_dodgerGoreSpr->height() > 0;

  Serial.printf("DODGER gore load result ok=%d spr=%p w=%d h=%d\n", ok ? 1 : 0, (void *)s_dodgerGoreSpr,
                s_dodgerGoreSpr ? (int)s_dodgerGoreSpr->width() : 0,
                s_dodgerGoreSpr ? (int)s_dodgerGoreSpr->height() : 0);

  return ok;
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

  const char *leftPath = fireballRunBgLeftPathForPet();
  const char *rightPath = fireballRunBgRightPathForPet();

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

// SHARED MINI GAME INTRO INTERRUPT
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
    return resRunIsShowingIntro();

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

  graphicsReleaseUiCachesForMiniGame();
  mgAssetsBeginSession(currentMiniGame, "startInfernalDodger");
  mgmem::beginSession(currentMiniGame, pet.type);
  mgmem::logUsage("dodger-start-beginSession");
  logMiniGameHeap("startInfernalDodger");

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi, g_app.currentTab);
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

  const bool bgOk = ensureDodgerBgCache();
  mgmem::logUsage("dodger-after-bg-ensure");

  const bool fireballOk = ensureDodgerFireballSprites();
  mgmem::logUsage("dodger-after-fireballs-ensure");

  const bool carOk = ensureDodgerCarSprite(fireballRunCarPathForPet());
  mgmem::logUsage("dodger-after-car-ensure");

  s_dodgerGoreSpr = nullptr;
  const char *goalFrame1Path = dodgerGoalFrame1ResolvedPath();
  const char *goalFrame2Path = dodgerGoalFrame2ResolvedPath();
  const char *goalGorePath = dodgerGoalGoreResolvedPath();

  Serial.printf("DODGER paths: frame1=%s frame2=%s gore=%s\n", goalFrame1Path ? goalFrame1Path : "(null)",
                goalFrame2Path ? goalFrame2Path : "(null)", goalGorePath ? goalGorePath : "(null)");

  const bool goalOk = (goalFrame1Path && goalFrame2Path) && ensureDodgerGoalFrames(goalFrame1Path, goalFrame2Path);

  // Keep gore deferred.
  const bool goreOk = false;

  mgmem::logUsage("dodger-after-goal-ensure");
  mgmem::logUsage("dodger-after-gore-defer");

  s_dodgerInited = true;
  dodgerReset();

  Serial.printf("DODGER preload: bg=%d fireballs=%d car=%d goal=%d gore=%d free=%u largest=%u\n", bgOk ? 1 : 0,
                fireballOk ? 1 : 0, carOk ? 1 : 0, goalOk ? 1 : 0, goreOk ? 1 : 0, (unsigned)ESP.getFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  invalidateBackgroundCache();
  s_dodgerShowIntro = true;
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

    // Load goal frames FIRST (bigger, more important)

    // THEN load gore
    if (!s_dodgerGoreSpr)
    {
      const char *g = dodgerGoalGoreResolvedPath();
      if (g)
      {
        ensureDodgerGoreSprite(g);
        mgmem::logUsage("dodger-after-gore-late-ensure");
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
    const bool gorePhase = (s_dodgerPhase == DODGER_PHASE_CAR_EXIT) || (s_dodgerPhase == DODGER_PHASE_HOLD);

    M5Canvas *goal1 = s_dodgerGoalFrame1Spr;
    M5Canvas *goal2 = s_dodgerGoalFrame2Spr;

    // ===== IMP / MERMAN (COAST + GOAL phases) =====
    if (!gorePhase)
    {
      M5Canvas *goalSpr = nullptr;

      if (goal1 && goal2)
      {
        goalSpr = (s_dodgerGoalAnimFrame & 1) ? goal2 : goal1;
      }
      else if (goal1)
      {
        goalSpr = goal1;
      }
      else if (goal2)
      {
        goalSpr = goal2;
      }

      if (goalSpr && goalSpr->width() > 0 && goalSpr->height() > 0)
      {
        const int drawX = s_dodgerGoalX - ((int)goalSpr->width() / 2);
        const int drawY = s_dodgerGoalY - ((int)goalSpr->height() / 2);
        goalSpr->pushSprite(&spr, drawX, drawY, kDodgerKey);
      }
    }

    // ===== GORE (IMPACT phases) =====
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

#endif // RH_MINIGAMES_IMPL_IN_PAUSE_MENU