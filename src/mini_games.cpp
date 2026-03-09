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

// -----------------------------------------------------------------------------
// Mini-game input helpers / shared state
// -----------------------------------------------------------------------------
static bool s_mgExiting = false;

static bool s_acceptArmed = false;
static uint32_t s_gameOverMs = 0;

static bool s_showReward = false;
static char s_rewardMsg[64] = {0};

// Forward decls used by multiple mini-games / defined later in this file
static int rollMiniGameInfReward();
static void exitMiniGameToReturnUi(bool beginLockout = true);
static void mgApplyResultAndShowReward(bool won);
static bool tryAwardWinItem_1in4(ItemType *outType);
static void mgSyncGameTimebases(uint32_t now);
static inline bool mgInputLockedOut();
static void freeCrossyZoneSprites();
static bool loadCrossyRowSprite(M5Canvas &dst, bool &ready, char *cachedPath, size_t cachedPathSize, const char *path);
static void freeCrossyActorSprites();
static const char *crossyStartZonePathForPet();
static const char *crossyGoalZonePathForPet();
static const char *crossyLavaZonePathForPet(uint8_t frame);
static const char *crossyStonePathForPet();

// Crossy Road
void startCrossyRoad();
void updateCrossyRoad(const InputState &input);
void drawCrossyRoad();

// IMPORTANT:
// We synthesize "Enter once" from selectHeld edge. Do NOT reset this to false
// during transitions, or holding Enter will instantly auto-dismiss the next screen.
static bool s_prevSelectHeld = false;

static bool miniGameEnterOnce(const InputState &input)
{
  const bool held = input.mgSelectHeld;

  // During launch/transition lockout, do NOT generate an enterOnce edge.
  // Still track held state so we don't synthesize a fake edge when lockout ends.
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
  // Preferred: use inventory’s label function (old reference behavior).
  // If this doesn't compile, see fallback note below.
  const char *nm = g_app.inventory.getItemLabelForType(t);
  if (nm && nm[0])
    return nm;

  // Fallback: keep something readable even if labels aren’t available.
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

// -----------------------------------------------------------------------------
// Mini-game global state
// -----------------------------------------------------------------------------
MiniGame currentMiniGame = MiniGame::NONE;
bool playerWon = false;

// Simple mini-game state
static bool s_resultShown = false;

static uint32_t s_mgInputLockoutUntilMs = 0;

static inline void mgBeginInputLockout(uint32_t ms) { s_mgInputLockoutUntilMs = millis() + ms; }

static inline bool mgInputLockedOut() { return (int32_t)(millis() - s_mgInputLockoutUntilMs) < 0; }

static constexpr uint32_t kSurviveWinMs = 15000; // or whatever your old value was

static void mgArmAccept(uint32_t now, uint32_t delayMs = 180)
{
  s_acceptArmed = false;
  s_gameOverMs = now + delayMs;
}

static bool mgAcceptArmedNow(uint32_t now)
{
  if (!s_acceptArmed && (int32_t)(now - s_gameOverMs) >= 0)
    s_acceptArmed = true;
  return s_acceptArmed && !mgInputLockedOut();
}

static const uint16_t kSpriteKey = 0x0841; // very dark grey

// -----------------------------------------------------------------------------
// FLAPPY FIREBALL GLOBALS
// -----------------------------------------------------------------------------

// START SCREEN
static bool s_flappyShowIntro = true;
static bool s_flappyDontShowAgain = false; // visual only for now

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

// SPIKES
static const uint16_t kFireKey = kSpriteKey;
static const char *flappyBgPathForPet();

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
static uint32_t s_lastStepMs = 0;
static int s_flappyBgScrollX = 0;

// Survive timer
static const uint32_t s_flappyWinMs = kSurviveWinMs;

// Cache: decoded background image (assumed SCREEN_W x SCREEN_H)
static uint16_t *s_flappyBgCache = nullptr;
static char s_flappyBgCachePath[128] = {0};

// Flappy pipe sprites (8bpp, cached)
static M5Canvas s_flappyPipeUpSpr(&M5.Display);
static M5Canvas s_flappyPipeDownSpr(&M5.Display);
static bool s_flappyPipeSprReady = false;
static int s_flappyPipeW = 0;
static int s_flappyPipeH = 0;
static char s_flappyPipeDir[128] = {0}; // folder containing bg + spikes

// Flappy background cache (8bpp to fit non-PSRAM heaps)
static M5Canvas s_flappyBgSpr(&M5.Display);
static bool s_flappyBgSprReady = false;
static void freeFlappyBgCache();

// Fireball Run background Cache
static void freeDodgerBgCache();
static void freeDodgerFireballSprites();
static void freeDodgerCarSprite();
static bool ensureDodgerBgCache(const char *path);
static bool ensureDodgerFireballSprites(const char *dir);
static bool ensureDodgerCarSprite(const char *path);

struct FlappyPipe
{
  int x;
  int gapY; // center of gap
  bool passed;
};

static void freeImpWaveSprites()
{
  for (int i = 0; i < 2; ++i)
    s_impWaveSpr[i].deleteSprite();
  s_impWaveSprReady = false;
}

// -----------------------------------------------------------------------------
// Flappy fireball sprite (3-frame PNG animation) - cached per flappy folder
// -----------------------------------------------------------------------------
static void freeFlappyFireballSprites()
{
  for (int i = 0; i < 3; ++i)
    s_flappyFireballSpr[i].deleteSprite();
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

static void freeFlappyPipeSprites()
{
  if (s_flappyPipeSprReady)
  {
    s_flappyPipeUpSpr.deleteSprite();
    s_flappyPipeDownSpr.deleteSprite();
    s_flappyPipeSprReady = false;
  }
  s_flappyPipeW = 0;
  s_flappyPipeH = 0;
  s_flappyPipeDir[0] = 0;
}

static bool flappyReadPngDimsTrySlash(const char *path, int *outW, int *outH, const char **outUsePath)
{
  if (outW)
    *outW = 0;
  if (outH)
    *outH = 0;
  if (outUsePath)
    *outUsePath = nullptr;

  if (!g_sdReady || !path || !path[0])
    return false;

  const char *usePath = path;
  if (!sdExistsTrySlash(path, &usePath))
    return false;

  File f = SD.open(usePath, "r");
  if (!f)
    return false;

  // PNG signature (8) + IHDR chunk header (8) + width/height (8) = 24 bytes
  uint8_t b[24];
  const int n = f.read(b, sizeof(b));
  f.close();
  if (n != (int)sizeof(b))
    return false;

  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (memcmp(b, sig, 8) != 0)
    return false;

  // Expect "IHDR" at bytes 12..15
  if (!(b[12] == 'I' && b[13] == 'H' && b[14] == 'D' && b[15] == 'R'))
    return false;

  const int w = (int)((uint32_t)b[16] << 24 | (uint32_t)b[17] << 16 | (uint32_t)b[18] << 8 | (uint32_t)b[19]);
  const int h = (int)((uint32_t)b[20] << 24 | (uint32_t)b[21] << 16 | (uint32_t)b[22] << 8 | (uint32_t)b[23]);

  if (w <= 0 || h <= 0)
    return false;

  if (outW)
    *outW = w;
  if (outH)
    *outH = h;
  if (outUsePath)
    *outUsePath = usePath;
  return true;
}

static bool ensureImpWaveSprites()
{
  if (s_impWaveSprReady)
    return true;

  const char *paths[2] = {
      "/raising_hell/graphics/mini_games/flappy/dev/imp_wave1.png",
      "/raising_hell/graphics/mini_games/flappy/dev/imp_wave2.png",
  };

  for (int i = 0; i < 2; ++i)
  {
    int w = 0, h = 0;
    const char *usePath = nullptr;

    if (!flappyReadPngDimsTrySlash(paths[i], &w, &h, &usePath))
    {
      freeImpWaveSprites();
      return false;
    }

    s_impWaveSpr[i].setColorDepth(8);

    if (!s_impWaveSpr[i].createSprite(w, h))
    {
      freeImpWaveSprites();
      return false;
    }

    s_impWaveSpr[i].fillSprite(kFireKey);

    if (!s_impWaveSpr[i].drawPngFile(SD, usePath, 0, 0))
    {
      freeImpWaveSprites();
      return false;
    }
  }

  s_impWaveSprReady = true;
  return true;
}

static bool ensureFlappyFireballSprites(const char *bgPath)
{
  if (!bgPath || !bgPath[0])
    return false;
  if (!g_sdReady)
    return false;

  char dir[128];
  flappyDirFromBgPath(bgPath, dir, sizeof(dir));
  if (!dir[0])
    return false;

  // Already loaded for this folder?
  if (s_flappyFireballReady && s_flappyFireballDir[0] && strcmp(s_flappyFireballDir, dir) == 0)
    return true;

  freeFlappyFireballSprites();

  char path[192];

  for (int i = 0; i < 3; ++i)
  {
    snprintf(path, sizeof(path), "%sfireball%d.png", dir, i + 1);

    const char *usePath = path;
    if (!sdExistsTrySlash(path, &usePath))
    {
      freeFlappyFireballSprites();
      return false;
    }

    int w = 0, h = 0;
    const char *pngUse = nullptr;

    if (!flappyReadPngDimsTrySlash(usePath, &w, &h, &pngUse) || w <= 0 || h <= 0)
    {
      freeFlappyFireballSprites();
      return false;
    }

    s_flappyFireballSpr[i].setColorDepth(8);

    if (!s_flappyFireballSpr[i].createSprite(w, h))
    {
      freeFlappyFireballSprites();
      return false;
    }

    // Fill with colorkey so pushSprite(..., kFireballKey) works
    s_flappyFireballSpr[i].fillSprite(kFireKey);

    if (!s_flappyFireballSpr[i].drawPngFile(SD, pngUse, 0, 0))
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
  if (!bgPath || !bgPath[0])
    return false;
  if (!g_sdReady)
    return false;

  char dir[128];
  flappyDirFromBgPath(bgPath, dir, sizeof(dir));
  if (!dir[0])
    return false;

  // If already loaded for this folder, we're good.
  if (s_flappyPipeSprReady && s_flappyPipeDir[0] && strcmp(s_flappyPipeDir, dir) == 0)
    return true;

  // Build spike paths (same folder as bg image)
  char upPath[192];
  char downPath[192];
  snprintf(upPath, sizeof(upPath), "%srock_spike_up.png", dir);
  snprintf(downPath, sizeof(downPath), "%srock_spike_down.png", dir);

  const char *useUp = upPath;
  const char *useDown = downPath;

  if (!sdExistsTrySlash(upPath, &useUp))
    return false;
  if (!sdExistsTrySlash(downPath, &useDown))
    return false;

  // Reload
  freeFlappyPipeSprites();

  // Read PNG sizes so our sprites are big enough (prevents drawPngFile() failing)
  const char *useUpPath = nullptr;
  const char *useDnPath = nullptr;
  int upW = 0, upH = 0;
  int dnW = 0, dnH = 0;

  // Read actual PNG dimensions (handles leading slash variants via outUsePath)
  if (!flappyReadPngDimsTrySlash(useUp, &upW, &upH, &useUpPath))
    return false;
  if (!flappyReadPngDimsTrySlash(useDown, &dnW, &dnH, &useDnPath))
    return false;

  const int w = (upW > dnW) ? upW : dnW;
  const int h = (upH > dnH) ? upH : dnH;

  s_flappyPipeUpSpr.setColorDepth(8);
  s_flappyPipeDownSpr.setColorDepth(8);

  if (!s_flappyPipeUpSpr.createSprite(w, h))
    return false;
  if (!s_flappyPipeDownSpr.createSprite(w, h))
  {
    s_flappyPipeUpSpr.deleteSprite();
    return false;
  }

  s_flappyPipeUpSpr.fillSprite(kPipeKey);
  s_flappyPipeDownSpr.fillSprite(kPipeKey);

  const bool okUp = s_flappyPipeUpSpr.drawPngFile(SD, useUpPath, 0, 0);
  const bool okDn = s_flappyPipeDownSpr.drawPngFile(SD, useDnPath, 0, 0);

  if (!okUp || !okDn)
  {
    freeFlappyPipeSprites();
    return false;
  }

  s_flappyPipeW = w;
  s_flappyPipeH = h;
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

  s_showReward = false;
  s_rewardMsg[0] = 0;

  s_acceptArmed = false;
  s_gameOverMs = 0;

  s_prevSelectHeld = false;

  currentMiniGame = MiniGame::FLAPPY_FIREBALL;

  // Never allow "return UI" to be MINI_GAME / MG_PAUSE (causes exit->bounce/lock).
  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

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

  freeFlappyPipeSprites();
  ensureFlappyFireballSprites(flappyBgPathForPet());
  ensureImpWaveSprites();
  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
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

  // If we're actively playing (not reward, not game over), clear accept-arming state.
  // This prevents stale arming timers from carrying across rounds.
  if (!s_showReward && !g_app.gameOver)
  {
    s_acceptArmed = false;
    s_gameOverMs = 0;
  }

  // ---------------------------------------------------------------------------
  // Reward modal: require an "armed" ENTER (prevents instant skip)
  // ---------------------------------------------------------------------------
  if (s_showReward)
  {
    // If we just entered the reward modal, arm acceptance after a short delay.
    if (s_gameOverMs == 0)
    {
      s_acceptArmed = false;
      s_gameOverMs = now + 180;
      mgBeginInputLockout(180);
      clearInputLatch();
      inputForceClear();
      return;
    }

    if (!s_acceptArmed && (int32_t)(now - s_gameOverMs) >= 0)
      s_acceptArmed = true;

    if (enterOnce && s_acceptArmed && !mgInputLockedOut())
    {
      // Reset arming state for the next transition
      s_acceptArmed = false;
      s_gameOverMs = 0;

      exitMiniGameToReturnUi(true);
    }
    return;
  }

  // ---------------------------------------------------------------------------
  // Game over: transition directly into the reward modal (legacy behavior)
  // ---------------------------------------------------------------------------
  if (g_app.gameOver)
  {
    // Apply result + show reward immediately; the reward modal itself is armed
    // (so holding ENTER won't instantly dismiss it).
    mgApplyResultAndShowReward(playerWon);

    // Arm acceptance for the reward modal and swallow any lingering input.
    s_acceptArmed = false;
    s_gameOverMs = 0;
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
  const char *nl = strchr(s_rewardMsg, '\n');

  if (nl)
  {
    char line1[64];
    size_t len = (size_t)(nl - s_rewardMsg);
    if (len > sizeof(line1) - 1)
      len = sizeof(line1) - 1;

    memcpy(line1, s_rewardMsg, len);
    line1[len] = 0;

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(line1, gW / 2, gH / 2 - 4, 2);

    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawCentreString(nl + 1, gW / 2, gH / 2 + 16, 2);
  }
  else
  {
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString(s_rewardMsg, gW / 2, gH / 2, 2);
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

static void freeFlappyBgCache()
{
  // If you’ve moved to sprite-cache only, keep this harmless (or delete the malloc cache entirely)
  if (s_flappyBgCache)
  {
    free(s_flappyBgCache);
    s_flappyBgCache = nullptr;
  }

  s_flappyBgCachePath[0] = 0;
  s_flappyBgW = 0;
  s_flappyBgH = 0;

  // also free the sprite cache
  if (s_flappyBgSprReady)
  {
    s_flappyBgSpr.deleteSprite();
    s_flappyBgSprReady = false;
  }
}

static bool s_flappyBgCacheDisabled = false;

static bool ensureFlappyBgCache(const char *path)
{
  if (!path || !path[0])
    return false;

  // already cached for this path?
  if (s_flappyBgSprReady && s_flappyBgCachePath[0] && strcmp(s_flappyBgCachePath, path) == 0)
    return true;

  const int w = (int)spr.width();
  const int h = (int)spr.height();
  if (w <= 0 || h <= 0)
    return false;

  if (!g_sdReady)
  {
    Serial.println("[FLAPPY] SD not ready (g_sdReady=false)");
    return false;
  }

  // detect png vs jpg
  bool isPng = false;
  if (const char *ext = strrchr(path, '.'))
  {
    isPng = (strcasecmp(ext, ".png") == 0);
  }

  // Try both with and without leading slash
  const char *usePath = path;
  bool exists = SD.exists(usePath);
  if (!exists && usePath[0] == '/')
  {
    usePath = usePath + 1;
    exists = SD.exists(usePath);
  }

  Serial.printf("[FLAPPY] bg path='%s' try='%s' exists=%d isPng=%d\n", path, usePath, (int)exists, (int)isPng);

  if (!exists)
    return false;

  // (Re)create an 8bpp cache sprite if needed (fits heap without PSRAM)
  if (!s_flappyBgSprReady || s_flappyBgW != w || s_flappyBgH != h)
  {
    s_flappyBgSpr.deleteSprite();
    s_flappyBgSprReady = false;

    s_flappyBgSpr.setColorDepth(8);
    if (!s_flappyBgSpr.createSprite(w, h))
    {
      Serial.printf("[FLAPPY] bg cache sprite create FAIL w=%d h=%d free=%u largest=%u\n", w, h,
                    (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
      return false;
    }

    s_flappyBgW = w;
    s_flappyBgH = h;
    s_flappyBgSprReady = true;
  }

  s_flappyBgSpr.fillSprite(TFT_BLACK);

  bool ok = false;
  if (isPng)
    ok = s_flappyBgSpr.drawPngFile(SD, usePath, 0, 0);
  else
    ok = s_flappyBgSpr.drawJpgFile(SD, usePath, 0, 0);

  Serial.printf("[FLAPPY] bg cache draw -> %s\n", ok ? "OK" : "FAIL");
  if (!ok)
    return false;

  strncpy(s_flappyBgCachePath, path, sizeof(s_flappyBgCachePath) - 1);
  s_flappyBgCachePath[sizeof(s_flappyBgCachePath) - 1] = 0;
  return true;
}

static void drawFlappyScrollingBg(int scrollX)
{
  // If cache missing, fall back to black.
  if (!s_flappyBgSprReady)
  {
    spr.fillSprite(TFT_BLACK);
    return;
  }

  const int w = s_flappyBgW;
  const int h = s_flappyBgH;
  if (w <= 0 || h <= 0)
  {
    spr.fillSprite(TFT_BLACK);
    return;
  }

  // normalize scroll into [0..w-1]
  scrollX %= w;
  if (scrollX < 0)
    scrollX += w;

  uint16_t *dst = (uint16_t *)spr.getBuffer();
  if (!dst)
  {
    spr.fillSprite(TFT_BLACK);
    return;
  }

  // Copy each scanline in two chunks (wrap-around).
  for (int y = 0; y < h; ++y)
  {
    const uint16_t *srcRow = s_flappyBgCache + (size_t)y * (size_t)w;
    uint16_t *dstRow = dst + (size_t)y * (size_t)w;

    const int leftW = w - scrollX;
    const int rightW = scrollX;

    memcpy(dstRow, srcRow + scrollX, (size_t)leftW * sizeof(uint16_t));
    if (rightW > 0)
      memcpy(dstRow + leftW, srcRow, (size_t)rightW * sizeof(uint16_t));
  }
}

void drawFlappyFireball()
{
  const int gW = (int)spr.width();
  const int gH = (int)spr.height();

  const char *bgPath = flappyBgPathForPet();
  const bool haveFireball = ensureFlappyFireballSprites(bgPath);

  bool drewBg = false;

  if (bgPath && bgPath[0] && ensureFlappyBgCache(bgPath))
  {
    const int bw = (int)s_flappyBgSpr.width();
    if (bw > 0)
    {
      // Wrap scroll into [0, bw)
      int x = -(s_flappyBgScrollX % bw);
      if (x > 0)
        x -= bw;

      s_flappyBgSpr.pushSprite(&spr, x, 0);
      s_flappyBgSpr.pushSprite(&spr, x + bw, 0);
      drewBg = true;
    }
  }

  if (!drewBg)
  {
    spr.fillSprite(TFT_BLACK);
  }

  if (s_showReward)
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

  const int gapH = 64;

  const bool havePipes = ensureFlappyPipeSprites(bgPath);

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
      s_impWaveSpr[s_impFrame].pushSprite(&spr, impX, impY, kFireKey);
    }

    if (s_flappyShowIntro)
    {
      spr.fillSprite(TFT_BLACK);
      spr.setTextDatum(CC_DATUM);

      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawCentreString("Press Enter or G to boost", gW / 2, 4, 2);
      spr.drawCentreString("Torch the Imp!", gW / 2, 22, 2);

      const int impX = (gW - 48) / 2;
      const int impY = 42;

      const char *impSprite = kImpWaveFrames[s_impFrame];
      sprDrawPngFromSD(impSprite, impX, impY);

      const int cbY = 100;
      const int cbSize = 10;
      const int textOffset = 16;
      const int lineWidth = 150; // approximate width of checkbox + text

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

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 116, 2);
    return;
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
        s_impWaveSpr[s_impFrame].pushSprite(&spr, impX, impY, kFireKey);
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
// Random INF reward (WIN only)
// -----------------------------------------------------------------------------
static int rollMiniGameInfReward()
{
  const int r = (int)random(100);

  if (r < 50)
    return 25;
  if (r < 80)
    return 50;
  if (r < 95)
    return 75;
  return 100;
}

// -----------------------------------------------------------------------------
// Mini-game reward (WIN only): 25% chance to win ONE random item
// -----------------------------------------------------------------------------
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

static void mgApplyResultAndShowReward(bool won)
{
  // Old rules (from your reference):
  // WIN  => XP +25, INF +roll, MOOD +20, 25% chance random item +1 (also show name)
  // LOSE => XP +5,  MOOD +10

  if (won)
  {
    pet.addXP(25);

    const int infReward = rollMiniGameInfReward();
    addInf(infReward);

    pet.happiness = constrain(pet.happiness + 20, 0, 100);

    ItemType rewardType = ITEM_NONE;
    const bool wonItem = tryAwardWinItem_1in4(&rewardType);

    if (wonItem)
    {
      const char *nm = mgItemName(rewardType);
      snprintf(s_rewardMsg, sizeof(s_rewardMsg), "You win! XP +25  INF +%d  MOOD +20\nRandom Reward: %s +1", infReward,
               (nm && nm[0]) ? nm : "ITEM");
    }
    else
    {
      snprintf(s_rewardMsg, sizeof(s_rewardMsg), "You win! XP +25  INF +%d  MOOD +20", infReward);
    }
  }
  else
  {
    pet.addXP(5);
    pet.happiness = constrain(pet.happiness + 10, 0, 100);

    snprintf(s_rewardMsg, sizeof(s_rewardMsg), "You lose! XP +5  MOOD +10");
  }

  saveManagerMarkDirty();

  // Show modal (and clear gameOver so we don’t re-enter result logic)
  s_showReward = true;
  g_app.gameOver = false;

  requestUIRedraw();
}

void miniGameExitToReturnUi(bool beginLockout)
{
  s_mgExiting = true;

  s_showReward = false;
  s_rewardMsg[0] = 0;

  // Decide where we're going FIRST, and switch away from MINI_GAME / MG_PAUSE.
  UIState target = miniGameGetReturnUiOrDefault(UIState::PET_SCREEN);
  if (target == UIState::MINI_GAME || target == UIState::MG_PAUSE)
    target = UIState::PET_SCREEN;

  miniGameClearReturnUi();

  // IMPORTANT: switch UI state BEFORE clearing currentMiniGame,
  // so MG_PAUSE doesn't render one frame with "NO MINI GAME".
  g_app.uiState = target;

  // Now tear down the mini-game
  g_app.inMiniGame = false;
  g_app.gameOver = false;
  playerWon = false;
  currentMiniGame = MiniGame::NONE;

  mgPauseReset();
  clearInputLatch();

  // Prevent Exit-confirm ENTER from triggering other UI confirms
  inputForceClear();
  s_prevSelectHeld = false;
  freeImpWaveSprites();
  freeDodgerBgCache();
  freeDodgerFireballSprites();
  freeDodgerCarSprite();
  freeCrossyActorSprites();
  invalidateBackgroundCache();
  requestFullUIRedraw();

  if (beginLockout)
    mgBeginInputLockout(220);
}

// Back-compat: older call sites used this name.
static void exitMiniGameToReturnUi(bool beginLockout) { miniGameExitToReturnUi(beginLockout); }

static void mgSyncGameTimebases(uint32_t now);

// -----------------------------------------------------------------------------
// Universal mini-game update/draw dispatch + pause overlay
// -----------------------------------------------------------------------------

static void mgSyncGameTimebases(uint32_t now);

void updateMiniGame(const InputState &input)
{
  // If we are not on the MINI_GAME UI anymore, never run mini-game logic.
  if (g_app.uiState != UIState::MINI_GAME)
    return;

  if (!g_app.inMiniGame)
    return;

  if (currentMiniGame == MiniGame::NONE)
    return;

  const uint32_t now = millis();

  // Pause/menu logic first (ESC/menu must always work).
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

  // Gameplay update dispatch
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
  {
    mgDrawPauseOverlay();
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
static uint32_t rr_lastMs = 0;

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
  s_showReward = false;
  s_rewardMsg[0] = 0;

  s_prevSelectHeld = false;
  clearInputLatch();
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

  // Clear accept-arming state while actively playing
  if (!rr_gameOver)
  {
    s_acceptArmed = false;
    s_gameOverMs = 0;
  }

  if (rr_gameOver)
  {
    const bool enterOnce = miniGameEnterOnce(input);

    // First frame of game-over: arm acceptance after a short delay and swallow input
    if (s_gameOverMs == 0)
    {
      s_acceptArmed = false;
      s_gameOverMs = now + 180;
      mgBeginInputLockout(180);
      clearInputLatch();
      inputForceClear();
      return;
    }

    if (!s_acceptArmed && (int32_t)(now - s_gameOverMs) >= 0)
      s_acceptArmed = true;

    if (enterOnce && s_acceptArmed && !mgInputLockedOut())
    {
      // Consume accept so it can't double-trigger
      s_acceptArmed = false;
      s_gameOverMs = 0;

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
static uint32_t s_crossyLastLaneMs = 0;

static inline int crossyClamp(int v, int lo, int hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static const char *crossyStoneXSPathForPet();
static bool ensureCrossyStoneXSSprite();
static const char *crossyStoneSmallPathForPet();
static void freeCrossyZoneSprites();
static bool ensureCrossyStartZoneSprite();
static bool ensureCrossyGoalZoneSprite();
static bool ensureCrossyLavaZoneSprite(uint8_t frame);
static const char *crossyImpPathForPet(CrossyFacing facing);

static bool crossyRowIsWater(int row) { return row >= 1 && row <= 5; }

static bool crossyRowIsRoad(int row) { return false; }

static bool crossyRowIsGoal(int row) { return row == 0; }

static bool crossyRowIsSafe(int row) { return row == 6; }

static bool crossyPlayerOverlapsMoverInRow(int row);

static bool ensureCrossyGoalZoneSprite()
{
  return loadCrossyRowSprite(s_crossyGoalZoneSpr, s_crossyGoalZoneReady, s_crossyGoalZonePath,
                             sizeof(s_crossyGoalZonePath), crossyGoalZonePathForPet());
}

static bool ensureCrossyStartZoneSprite()
{
  return loadCrossyRowSprite(s_crossyStartZoneSpr, s_crossyStartZoneReady, s_crossyStartZonePath,
                             sizeof(s_crossyStartZonePath), crossyStartZonePathForPet());
}

static bool ensureCrossyLavaZoneSprite(uint8_t frame)
{
  const uint8_t i = frame & 1;
  const char *path = crossyLavaZonePathForPet(i);

  const bool ok = loadCrossyRowSprite(s_crossyLavaZoneSpr[i], s_crossyLavaZoneReady[i], s_crossyLavaZonePath[i],
                                      sizeof(s_crossyLavaZonePath[i]), path);
  return ok;
}

static bool ensureCrossyIntroSprite()
{
  const char *path = "/raising_hell/graphics/mini_games/crossy/dev/intro_goal.png";

  return loadCrossyRowSprite(s_crossyIntroSpr, s_crossyIntroReady, s_crossyIntroPath, sizeof(s_crossyIntroPath), path);
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

  static void freeCrossyZoneSprites()
  {
    s_crossyGoalZoneSpr.deleteSprite();
    s_crossyGoalZoneReady = false;
    s_crossyGoalZonePath[0] = 0;

    s_crossyStartZoneSpr.deleteSprite();
    s_crossyStartZoneReady = false;
    s_crossyStartZonePath[0] = 0;

    for (int i = 0; i < 2; ++i)
    {
      s_crossyLavaZoneSpr[i].deleteSprite();
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

  static bool loadCrossyRowSprite(M5Canvas & dst, bool &ready, char *cachedPath, size_t cachedPathSize,
                                  const char *path)
  {
    if (!path || !path[0] || !g_sdReady)
      return false;

    if (ready && strcmp(cachedPath, path) == 0)
      return true;

    if (ready)
    {
      dst.deleteSprite();
      ready = false;
      cachedPath[0] = 0;
    }

    int w = 0;
    int h = 0;
    const char *usePath = nullptr;
    if (!flappyReadPngDimsTrySlash(path, &w, &h, &usePath) || w <= 0 || h <= 0)
      return false;

    dst.deleteSprite();
    dst.setColorDepth(16);

    if (!dst.createSprite(w, h))
      return false;

    dst.fillSprite(TFT_BLACK);

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

  static void freeCrossyActorSprites()
  {
    if (s_crossyStoneReady)
    {
      s_crossyStoneSpr.deleteSprite();
      s_crossyStoneReady = false;
      s_crossyStonePath[0] = 0;
    }

    if (s_crossyImpReady)
    {
      s_crossyImpSpr.deleteSprite();
      s_crossyImpReady = false;
      s_crossyImpPath[0] = 0;
    }
    if (s_crossyStoneSmallReady)
    {
      s_crossyStoneSmallSpr.deleteSprite();
      s_crossyStoneSmallReady = false;
      s_crossyStoneSmallPath[0] = 0;
    }
    if (s_crossyStoneXSReady)
    {
      s_crossyStoneXSSpr.deleteSprite();
      s_crossyStoneXSReady = false;
      s_crossyStoneXSPath[0] = 0;
    }
  }

  static bool ensureCrossyStoneSprite()
  {
    const char *path = crossyStonePathForPet();
    if (!path || !path[0] || !g_sdReady)
      return false;

    if (s_crossyStoneReady && strcmp(s_crossyStonePath, path) == 0)
      return true;

    if (s_crossyStoneReady)
    {
      s_crossyStoneSpr.deleteSprite();
      s_crossyStoneReady = false;
      s_crossyStonePath[0] = 0;
    }

    int w = 0, h = 0;
    const char *usePath = nullptr;
    if (!flappyReadPngDimsTrySlash(path, &w, &h, &usePath) || w <= 0 || h <= 0)
      return false;

    s_crossyStoneSpr.setColorDepth(8);
    if (!s_crossyStoneSpr.createSprite(w, h))
      return false;

    s_crossyStoneSpr.fillSprite(kSpriteKey);

    if (!s_crossyStoneSpr.drawPngFile(SD, usePath, 0, 0))
    {
      s_crossyStoneSpr.deleteSprite();
      return false;
    }

    strlcpy(s_crossyStonePath, path, sizeof(s_crossyStonePath));
    s_crossyStoneReady = true;
    return true;
  }

  static bool ensureCrossyStoneSmallSprite()
  {
    const char *path = crossyStoneSmallPathForPet();
    if (!path || !path[0] || !g_sdReady)
      return false;

    if (s_crossyStoneSmallReady && strcmp(s_crossyStoneSmallPath, path) == 0)
      return true;

    if (s_crossyStoneSmallReady)
    {
      s_crossyStoneSmallSpr.deleteSprite();
      s_crossyStoneSmallReady = false;
      s_crossyStoneSmallPath[0] = 0;
    }

    int w = 0, h = 0;
    const char *usePath = nullptr;
    if (!flappyReadPngDimsTrySlash(path, &w, &h, &usePath) || w <= 0 || h <= 0)
      return false;

    s_crossyStoneSmallSpr.setColorDepth(8);
    if (!s_crossyStoneSmallSpr.createSprite(w, h))
      return false;

    s_crossyStoneSmallSpr.fillSprite(kSpriteKey);

    if (!s_crossyStoneSmallSpr.drawPngFile(SD, usePath, 0, 0))
    {
      s_crossyStoneSmallSpr.deleteSprite();
      return false;
    }

    strlcpy(s_crossyStoneSmallPath, path, sizeof(s_crossyStoneSmallPath));
    s_crossyStoneSmallReady = true;
    return true;
  }

  static bool ensureCrossyImpSprite()
  {
    const char *path = crossyImpPathForPet(s_crossyFacing);
    if (!path || !path[0] || !g_sdReady)
      return false;

    if (s_crossyImpReady && strcmp(s_crossyImpPath, path) == 0)
      return true;

    if (s_crossyImpReady)
    {
      s_crossyImpSpr.deleteSprite();
      s_crossyImpReady = false;
      s_crossyImpPath[0] = 0;
    }

    int w = 0, h = 0;
    const char *usePath = nullptr;
    if (!flappyReadPngDimsTrySlash(path, &w, &h, &usePath) || w <= 0 || h <= 0)
      return false;

    s_crossyImpSpr.setColorDepth(8);
    if (!s_crossyImpSpr.createSprite(w, h))
      return false;

    s_crossyImpSpr.fillSprite(kSpriteKey);

    if (!s_crossyImpSpr.drawPngFile(SD, usePath, 0, 0))
    {
      s_crossyImpSpr.deleteSprite();
      return false;
    }

    strlcpy(s_crossyImpPath, path, sizeof(s_crossyImpPath));
    s_crossyImpReady = true;
    return true;
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

  static bool ensureCrossyStoneXSSprite()
  {
    const char *path = crossyStoneXSPathForPet();
    if (!path || !path[0] || !g_sdReady)
      return false;

    if (s_crossyStoneXSReady && strcmp(s_crossyStoneXSPath, path) == 0)
      return true;

    if (s_crossyStoneXSReady)
    {
      s_crossyStoneXSSpr.deleteSprite();
      s_crossyStoneXSReady = false;
      s_crossyStoneXSPath[0] = 0;
    }

    int w = 0, h = 0;
    const char *usePath = nullptr;
    if (!flappyReadPngDimsTrySlash(path, &w, &h, &usePath) || w <= 0 || h <= 0)
      return false;

    s_crossyStoneXSSpr.setColorDepth(8);
    if (!s_crossyStoneXSSpr.createSprite(w, h))
      return false;

    s_crossyStoneXSSpr.fillSprite(kSpriteKey);

    if (!s_crossyStoneXSSpr.drawPngFile(SD, usePath, 0, 0))
    {
      s_crossyStoneXSSpr.deleteSprite();
      return false;
    }

    strlcpy(s_crossyStoneXSPath, path, sizeof(s_crossyStoneXSPath));
    s_crossyStoneXSReady = true;
    return true;
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

    s_showReward = false;
    s_rewardMsg[0] = 0;

    currentMiniGame = MiniGame::CROSSY_ROAD;

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
    ensureCrossyLavaZoneSprite(0);
    ensureCrossyLavaZoneSprite(1);

    ensureCrossyStoneSprite();
    ensureCrossyStoneSmallSprite();
    ensureCrossyStoneXSSprite();
    ensureCrossyImpSprite();

    invalidateBackgroundCache();
    requestUIRedraw();
    clearInputLatch();
    mgBeginInputLockout(220);
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

    // Clear accept-arming state while actively playing
    if (!s_showReward && !g_app.gameOver)
    {
      s_acceptArmed = false;
      s_gameOverMs = 0;
    }

    // ---------------------------------------------------------------------------
    // Reward modal: require an "armed" ENTER (prevents instant skip)
    // ---------------------------------------------------------------------------
    if (s_showReward)
    {
      if (s_gameOverMs == 0)
      {
        s_acceptArmed = false;
        s_gameOverMs = now + 180;
        mgBeginInputLockout(180);
        clearInputLatch();
        inputForceClear();
        return;
      }

      if (!s_acceptArmed && (int32_t)(now - s_gameOverMs) >= 0)
        s_acceptArmed = true;

      if (enterOnce && s_acceptArmed && !mgInputLockedOut())
      {
        s_acceptArmed = false;
        s_gameOverMs = 0;
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

      s_acceptArmed = false;
      s_gameOverMs = 0;
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
      playerWon = true;
      g_app.gameOver = true;
      requestUIRedraw();
      s_resultShown = true;
      soundConfirm();
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

    spr.fillSprite(TFT_BLACK);

    if (s_showReward)
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

      ensureCrossyIntroSprite();

      if (s_crossyIntroReady)
      {
        const int ix = (gW - s_crossyIntroSpr.width()) / 2;
        const int iy = 48;

        s_crossyIntroSpr.pushSprite(&spr, ix, iy, kSpriteKey);
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
    const uint8_t animBaseFrame = s_crossyLavaFrame & 1;

    const bool haveGoal = s_crossyGoalZoneReady;
    const bool haveStart = s_crossyStartZoneReady;

    for (int row = 0; row < kCrossyRows; ++row)
    {
      const int y = kCrossyOriginY + row * kCrossyTileH;

      switch (s_crossyLanes[row].type)
      {
      case CROSSY_LANE_GOAL:
        if (haveGoal && s_crossyGoalZoneReady)
          s_crossyGoalZoneSpr.pushSprite(&spr, 0, y);
        else
          spr.fillRect(0, y, 240, kCrossyTileH, TFT_RED);
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
        const uint8_t lavaFrame = (animBaseFrame + row) & 1;

        if (s_crossyLavaZoneReady[lavaFrame])
          s_crossyLavaZoneSpr[lavaFrame].pushSprite(&spr, 0, y);
        else
          spr.fillRect(0, y, 240, kCrossyTileH, TFT_RED);
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
  static uint32_t s_dodgerLastStepMs = 0;
  static uint32_t s_dodgerStartMs = 0;
  static uint32_t s_dodgerSpawnAccMs = 0;

  static int16_t s_dodgerPx = 0;
  static int16_t s_dodgerPy = 0;
  static int16_t s_dodgerSpeed = 3;
  static float s_dodgerPxF = 0.0f;
  static uint32_t s_dodgerMoveLastMs = 0;

  static int8_t s_dodgerMoveDir = 0;
  static uint32_t s_dodgerDirHoldMs = 0;

  static DodgerBall s_dodgerBalls[8];

  static const uint16_t kDodgerKey = kSpriteKey;

  static M5Canvas s_dodgerBgSpr(&M5.Display);
  static bool s_dodgerBgSprReady = false;
  static char s_dodgerBgCachePath[128] = {0};
  static int s_dodgerBgW = 0;
  static int s_dodgerBgH = 0;
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

  static void freeDodgerGoalFrames()
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

  static void freeDodgerGoreSprite()
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

  static bool loadDodgerSprite(LGFX_Sprite & dst, const char *path, int &outW, int &outH)
  {
    if (!path || !path[0] || !g_sdReady)
      return false;

    int w = 0, h = 0;
    const char *usePath = nullptr;

    if (!flappyReadPngDimsTrySlash(path, &w, &h, &usePath) || w <= 0 || h <= 0)
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

  static void freeDodgerGoalFrames();
  static void freeDodgerGoreSprite();
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

  static bool ensureDodgerGoreSprite(const char *path)
  {
    if (!path || !path[0] || !g_sdReady)
      return false;

    if (s_dodgerGoreReady && s_dodgerGorePath[0] && strcmp(s_dodgerGorePath, path) == 0)
      return true;

    freeDodgerGoreSprite();

    int w = 0, h = 0;
    if (!loadDodgerSprite(s_dodgerGoreSpr, path, w, h))
    {
      freeDodgerGoreSprite();
      return false;
    }

    s_dodgerGoreReady = true;
    strlcpy(s_dodgerGorePath, path, sizeof(s_dodgerGorePath));

    return true;
  }

  static void freeDodgerBgCache()
  {
    if (s_dodgerBgSprReady)
    {
      s_dodgerBgSpr.deleteSprite();
      s_dodgerBgSprReady = false;
    }

    s_dodgerBgCachePath[0] = 0;
    s_dodgerBgW = 0;
    s_dodgerBgH = 0;
    s_dodgerBgScrollY = 0;
  }

  static void freeDodgerFireballSprites()
  {
    for (int i = 0; i < 3; ++i)
      s_dodgerFireballSpr[i].deleteSprite();

    s_dodgerFireballReady = false;
    s_dodgerFireballDir[0] = 0;
    s_dodgerFireballW = 0;
    s_dodgerFireballH = 0;
  }

  static void freeDodgerCarSprite()
  {
    if (s_dodgerCarReady)
    {
      s_dodgerCarSpr.deleteSprite();
      s_dodgerCarReady = false;
    }

    s_dodgerCarPath[0] = 0;
    s_dodgerCarW = 0;
    s_dodgerCarH = 0;
  }

  static bool ensureDodgerBgCache(const char *path)
  {
    if (!path || !path[0])
      return false;

    if (s_dodgerBgSprReady && s_dodgerBgCachePath[0] && strcmp(s_dodgerBgCachePath, path) == 0)
      return true;

    const int w = (int)spr.width();
    const int h = (int)spr.height();
    if (w <= 0 || h <= 0)
      return false;

    if (!g_sdReady)
      return false;

    bool isPng = false;
    if (const char *ext = strrchr(path, '.'))
      isPng = (strcasecmp(ext, ".png") == 0);

    const char *usePath = path;
    bool exists = SD.exists(usePath);
    if (!exists && usePath[0] == '/')
    {
      usePath = usePath + 1;
      exists = SD.exists(usePath);
    }
    if (!exists)
      return false;

    if (!s_dodgerBgSprReady || s_dodgerBgW != w || s_dodgerBgH != h)
    {
      s_dodgerBgSpr.deleteSprite();
      s_dodgerBgSprReady = false;

      s_dodgerBgSpr.setColorDepth(8);
      if (!s_dodgerBgSpr.createSprite(w, h))
        return false;

      s_dodgerBgW = w;
      s_dodgerBgH = h;
      s_dodgerBgSprReady = true;
    }

    s_dodgerBgSpr.fillSprite(TFT_BLACK);

    bool ok = false;
    if (isPng)
      ok = s_dodgerBgSpr.drawPngFile(SD, usePath, 0, 0);
    else
      ok = s_dodgerBgSpr.drawJpgFile(SD, usePath, 0, 0);

    if (!ok)
      return false;

    strlcpy(s_dodgerBgCachePath, path, sizeof(s_dodgerBgCachePath));
    return true;
  }

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

      const char *usePath = path;
      if (!sdExistsTrySlash(path, &usePath))
      {
        freeDodgerFireballSprites();
        return false;
      }

      int w = 0, h = 0;
      const char *pngUse = nullptr;

      if (!flappyReadPngDimsTrySlash(usePath, &w, &h, &pngUse) || w <= 0 || h <= 0)
      {
        freeDodgerFireballSprites();
        return false;
      }

      s_dodgerFireballSpr[i].setColorDepth(8);

      if (!s_dodgerFireballSpr[i].createSprite(w, h))
      {
        freeDodgerFireballSprites();
        return false;
      }

      s_dodgerFireballSpr[i].fillSprite(kDodgerKey);

      if (!s_dodgerFireballSpr[i].drawPngFile(SD, pngUse, 0, 0))
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

    const char *usePath = path;
    if (!sdExistsTrySlash(path, &usePath))
      return false;

    int w = 0, h = 0;
    const char *pngUse = nullptr;
    if (!flappyReadPngDimsTrySlash(usePath, &w, &h, &pngUse) || w <= 0 || h <= 0)
      return false;

    s_dodgerCarSpr.setColorDepth(8);

    if (!s_dodgerCarSpr.createSprite(w, h))
      return false;

    s_dodgerCarSpr.fillSprite(kDodgerKey);

    if (!s_dodgerCarSpr.drawPngFile(SD, pngUse, 0, 0))
    {
      freeDodgerCarSprite();
      return false;
    }

    s_dodgerCarW = w;
    s_dodgerCarH = h;
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
    s_dodgerPy = gH - 14;
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

    s_showReward = false;
    s_rewardMsg[0] = 0;

    s_prevSelectHeld = false;

    currentMiniGame = MiniGame::INFERNAL_DODGER;

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
    ensureDodgerCarSprite(fireballRunCarPathForPet());

    s_dodgerInited = true;
    dodgerReset();

    invalidateBackgroundCache();
    s_dodgerShowIntro = true;
    s_dodgerDontShowAgain = false;
    s_dodgerIntroImpFrame = 0;
    s_dodgerIntroImpAnimMs = millis();
    requestUIRedraw();
    clearInputLatch();
    // Prevent the ENTER used to launch the mini-game from being interpreted as
    // an immediate "enterOnce" inside the mini-game on the first update tick.
    {
      auto st = M5Cardputer.Keyboard.keysState();
      s_prevSelectHeld = st.enter;
    }
    mgBeginInputLockout(220);
  }

  void updateInfernalDodger(const InputState &input)
  {
    const bool enterOnce = miniGameEnterOnce(input);

    if (s_showReward)
    {
      if (enterOnce)
        exitMiniGameToReturnUi(true);
      return;
    }

    if (g_app.gameOver)
    {
      mgApplyResultAndShowReward(playerWon);

      s_acceptArmed = false;
      s_gameOverMs = 0;
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

      (void)ensureDodgerGoalFrames(dodgerGoalFrame1PathForPet(), dodgerGoalFrame2PathForPet());
      (void)ensureDodgerGoreSprite(dodgerGoalGorePathForPet());

      Serial.printf("GOAL preload: f0=%d f1=%d gore=%d w=%d h=%d\n", s_dodgerGoalFrameReady[0] ? 1 : 0,
                    s_dodgerGoalFrameReady[1] ? 1 : 0, s_dodgerGoreReady ? 1 : 0, s_dodgerGoalW, s_dodgerGoalH);
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
    const char *carPath = fireballRunCarPathForPet();

    const bool haveBg = ensureDodgerBgCache(bgPath);
    const bool haveFireballs = ensureDodgerFireballSprites(bgPath);
    const bool haveCar = ensureDodgerCarSprite(carPath);

    const bool haveGoalFrames = s_dodgerGoalFrameReady[0] && s_dodgerGoalFrameReady[1];
    const bool haveGore = s_dodgerGoreReady;

    bool drewBg = false;
    if (haveBg && s_dodgerBgSprReady)
    {
      const int bh = (int)s_dodgerBgSpr.height();
      if (bh > 0)
      {
        int y = -(s_dodgerBgScrollY % bh);
        if (y > 0)
          y -= bh;

        s_dodgerBgSpr.pushSprite(&spr, 0, y);
        s_dodgerBgSpr.pushSprite(&spr, 0, y + bh);
        drewBg = true;
      }
    }

    if (!drewBg)
      spr.fillSprite(TFT_BLACK);

    if (s_showReward)
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

      if (ensureDodgerGoalFrames(dodgerGoalFrame1PathForPet(), dodgerGoalFrame2PathForPet()))
        s_dodgerGoalSpr[s_dodgerIntroImpFrame].pushSprite(&spr, impX, impY, kDodgerKey);

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

        if (haveFireballs && s_dodgerFireballReady)
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

    Serial.printf("GOAL draw: active=%d phase=%d ready0=%d ready1=%d x=%d y=%d w=%d h=%d\n", s_dodgerGoalActive ? 1 : 0,
                  (int)s_dodgerPhase, s_dodgerGoalFrameReady[0] ? 1 : 0, s_dodgerGoalFrameReady[1] ? 1 : 0,
                  s_dodgerGoalX, s_dodgerGoalY, s_dodgerGoalW, s_dodgerGoalH);

    if (s_dodgerGoalActive)
    {
      const int drawX = s_dodgerGoalX - (s_dodgerGoalW / 2);
      const int drawY = s_dodgerGoalY - (s_dodgerGoalH / 2);

      if (s_dodgerPhase == DODGER_PHASE_IMPACT || s_dodgerPhase == DODGER_PHASE_CAR_EXIT ||
          s_dodgerPhase == DODGER_PHASE_HOLD)
      {
        if (haveGore)
          s_dodgerGoreSpr.pushSprite(&spr, drawX, drawY, kDodgerKey);
        else
          spr.fillRect(drawX, drawY, 48, 16, TFT_RED);
      }
      else
      {
        if (haveGoalFrames)
          s_dodgerGoalSpr[s_dodgerGoalAnimFrame].pushSprite(&spr, drawX, drawY, kDodgerKey);
        else
          spr.fillRect(drawX, drawY, 48, 16, TFT_RED);
      }
    }

    if (s_dodgerPhase != DODGER_PHASE_HOLD && s_dodgerPhase != DODGER_PHASE_OFFROAD_HOLD)
    {
      if (haveCar && s_dodgerCarReady)
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

  static void mgSyncGameTimebases(uint32_t now)
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

#endif // RH_MINIGAMES_IMPL_IN_PAUSE_MENU