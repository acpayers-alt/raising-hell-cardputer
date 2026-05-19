#include "mini_games.h"

#include <Arduino.h>

#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "mg_pause_core.h"
#include "mini_game_assets.h"
#include "mini_game_return_ui.h"
#include "mini_game_runtime.h"
#include "mini_games_internal.h"
#include "pet.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

extern bool g_sdReady;

namespace
{
enum TargetKind : uint8_t
{
  TARGET_COW = 0,
  TARGET_FARMER,
  TARGET_POLICE,
};

struct AbductionTarget
{
  int16_t x = 0;
  int16_t y = 0;
  int8_t dir = 1;
  uint8_t w = 18;
  uint8_t h = 12;
  TargetKind kind = TARGET_COW;
  bool active = false;
};

static constexpr uint32_t kGameMs = 30000;
static constexpr uint32_t kSpawnMinMs = 750;
static constexpr uint32_t kBeamMs = 220;
static constexpr uint8_t kMaxTargets = 5;
static constexpr uint8_t kMaxStrikes = 3;

static AbductionTarget s_targets[kMaxTargets];

static int16_t s_ufoX = 120;
static int16_t s_ufoY = 18;
static float s_ufoVelX = 0.0f;
static int16_t s_beamX = 120;

static uint32_t s_startedMs = 0;
static uint32_t s_lastStepMs = 0;
static uint32_t s_nextSpawnMs = 0;
static uint32_t s_beamUntilMs = 0;

static uint8_t s_score = 0;
static uint8_t s_strikes = 0;
static bool s_inited = false;
static bool s_abductionShowIntro = true;
static bool s_abductionIntroDrawnOnce = false;
static bool s_abductionAssetsPreloaded = false;

static uint32_t s_lastAbductionDrawMs = 0;
static constexpr uint32_t kAbductionFrameMs = 42; // ~24 FPS
static constexpr uint16_t kAbductionSpriteKey = TFT_MAGENTA;

static const int kGroundY = 112;

static bool beamActive() { return (int32_t)(millis() - s_beamUntilMs) < 0; }

static bool targetGood(TargetKind k) { return k == TARGET_COW; }

static int targetPoints(TargetKind k) { return 1; }

static const char *UFO_FRAMES[] = {
    "/raising_hell/graphics/mini_games/abduct/ufo1.png",
    "/raising_hell/graphics/mini_games/abduct/ufo2.png",
};

static const char *COW_FRAMES[] = {
    "/raising_hell/graphics/mini_games/abduct/cow1.png",
    "/raising_hell/graphics/mini_games/abduct/cow2.png",
};

static const char *TANK_FRAMES[] = {
    "/raising_hell/graphics/mini_games/abduct/tank1.png",
    "/raising_hell/graphics/mini_games/abduct/tank2.png",
};

static const char *MIB_FRAMES[] = {
    "/raising_hell/graphics/mini_games/abduct/mib1.png",
    "/raising_hell/graphics/mini_games/abduct/mib2.png",
};

struct AbductionSpriteDims
{
  int w = 0;
  int h = 0;
  bool ready = false;
};

struct AbductionStar
{
  int16_t x;
  int16_t y;
  uint8_t phase;
  uint8_t kind;
};

static constexpr int kAbductionStarCount = 18;
static AbductionStar s_abductionStars[kAbductionStarCount];

static AbductionSpriteDims s_cowDims[2];
static AbductionSpriteDims s_mibDims[2];
static AbductionSpriteDims s_tankDims[2];

static void cacheAbductionSpriteDims()
{
  const char *usePath = nullptr;

  for (uint8_t i = 0; i < 2; ++i)
  {
    if (!s_cowDims[i].ready)
      s_cowDims[i].ready = mgAssetsReadPngDims(COW_FRAMES[i], &s_cowDims[i].w, &s_cowDims[i].h, &usePath);

    if (!s_mibDims[i].ready)
      s_mibDims[i].ready = mgAssetsReadPngDims(MIB_FRAMES[i], &s_mibDims[i].w, &s_mibDims[i].h, &usePath);

    if (!s_tankDims[i].ready)
      s_tankDims[i].ready = mgAssetsReadPngDims(TANK_FRAMES[i], &s_tankDims[i].w, &s_tankDims[i].h, &usePath);
  }
}

static void resetTargets()
{
  for (uint8_t i = 0; i < kMaxTargets; ++i)
    s_targets[i] = AbductionTarget{};
}

static void spawnTarget(uint32_t now)
{
  for (uint8_t i = 0; i < kMaxTargets; ++i)
  {
    if (s_targets[i].active)
      continue;

    AbductionTarget &t = s_targets[i];

    const int roll = random(100);
    if (roll < 70)
      t.kind = TARGET_COW;
    else if (roll < 85)
      t.kind = TARGET_FARMER;
    else
      t.kind = TARGET_POLICE;

    switch (t.kind)
    {
    case TARGET_COW:
      t.w = 18;
      t.h = 11;
      break;
    case TARGET_FARMER:
      t.w = 14;
      t.h = 22;
      break;
    case TARGET_POLICE:
      t.w = 32;
      t.h = 14;
      break;
    }

    t.dir = random(2) == 0 ? -1 : 1;
    t.x = (t.dir > 0) ? -t.w : SCREEN_W + t.w;
    t.y = kGroundY - t.h;
    t.active = true;

    const uint32_t faster = min<uint32_t>(450, (uint32_t)s_score * 18);
    s_nextSpawnMs = now + max<uint32_t>(kSpawnMinMs - faster, 360);
    return;
  }

  s_nextSpawnMs = now + 300;
}

static void initAbductionStars()
{
  const int skyBottom = SCREEN_H - 80;

  for (int i = 0; i < kAbductionStarCount; ++i)
  {
    s_abductionStars[i].x = (int16_t)random(0, SCREEN_W);
    s_abductionStars[i].y = (int16_t)random(4, max(8, skyBottom - 4));
    s_abductionStars[i].phase = (uint8_t)random(0, 64);
    s_abductionStars[i].kind = (uint8_t)random(0, 4);
  }
}

static void drawAbductionStars(uint32_t now)
{
  const int skyBottom = SCREEN_H - 80;
  const uint32_t tick = now / 90U;

  for (int i = 0; i < kAbductionStarCount; ++i)
  {
    const AbductionStar &s = s_abductionStars[i];

    if (s.y < 0 || s.y >= skyBottom)
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

    spr.drawPixel(s.x, s.y, c);

    if (glow >= 8 && (s.kind == 1 || s.kind == 3))
    {
      if (s.x > 0)
        spr.drawPixel(s.x - 1, s.y, c);
      if (s.x + 1 < SCREEN_W)
        spr.drawPixel(s.x + 1, s.y, c);
    }

    if (glow >= 8 && (s.kind == 2 || s.kind == 3))
    {
      if (s.y > 0)
        spr.drawPixel(s.x, s.y - 1, c);
      if (s.y + 1 < skyBottom)
        spr.drawPixel(s.x, s.y + 1, c);
    }
  }
}

static void finishGame(bool won)
{
  playerWon = won;
  g_app.gameOver = true;
  s_resultShown = true;

  if (won)
    soundWin();
  else
    soundError();

  requestUIRedraw();
}

static void resetGame()
{
  s_ufoX = SCREEN_W / 2;
  s_ufoY = 18;
  s_ufoVelX = 0.0f;
  s_beamX = s_ufoX;
  s_startedMs = millis();
  s_lastStepMs = s_startedMs;
  s_nextSpawnMs = s_startedMs + 500;
  s_beamUntilMs = 0;
  s_lastAbductionDrawMs = 0; // ADD THIS LINE
  s_score = 0;
  s_strikes = 0;
  initAbductionStars();
  resetTargets();
}

static bool getAbductionSprite(const char *assetId, const char *path, M5Canvas *&out)
{
  return mgmem::ensureSprite(MiniGame::ABDUCTION_BEAM, assetId, path, 8, kAbductionSpriteKey, out);
}

static void drawCachedSpriteMirroredX(M5Canvas *src, int x, int y)
{
  if (!src || src->width() <= 0 || src->height() <= 0)
    return;

  const int w = src->width();
  const int h = src->height();

  for (int yy = 0; yy < h; ++yy)
  {
    for (int xx = 0; xx < w; ++xx)
    {
      const uint16_t c = src->readPixel(xx, yy);
      if (c == kAbductionSpriteKey)
        continue;

      spr.drawPixel(x + (w - 1 - xx), y + yy, c);
    }
  }
}

static void preloadAbductionSprites()
{
  M5Canvas *unused = nullptr;

  getAbductionSprite("ufo_0", UFO_FRAMES[0], unused);
  getAbductionSprite("ufo_1", UFO_FRAMES[1], unused);

  getAbductionSprite("cow_0", COW_FRAMES[0], unused);
  getAbductionSprite("cow_1", COW_FRAMES[1], unused);

  getAbductionSprite("mib_0", MIB_FRAMES[0], unused);
  getAbductionSprite("mib_1", MIB_FRAMES[1], unused);

  getAbductionSprite("tank_0", TANK_FRAMES[0], unused);
  getAbductionSprite("tank_1", TANK_FRAMES[1], unused);
}

static void abductionPreloadAssetsForIntro()
{
  if (s_abductionAssetsPreloaded)
    return;

  mgAssetsLogHeap("abduction-deferred-preload-begin");

  preloadAbductionSprites();

  s_abductionAssetsPreloaded = true;
  requestUIRedraw();

  mgAssetsLogHeap("abduction-deferred-preload-complete");
}

static void drawUfo()
{
  const char **frames = UFO_FRAMES;

  const uint8_t frame = (millis() / 160) & 1;
  const char *path = frames[frame];

  // Center a 48x32-ish UFO sprite on the existing UFO anchor.
  // Tune these offsets if your PNG dimensions differ.
  const int drawX = s_ufoX - 24;
  const int drawY = s_ufoY - 16;

  M5Canvas *ufo = nullptr;
  const char *assetId = frame ? "ufo_1" : "ufo_0";

  if (getAbductionSprite(assetId, path, ufo) && ufo)
  {
    ufo->pushSprite(&spr, drawX, drawY, kAbductionSpriteKey);
    return;
  }

  // Fallback code-drawn UFO if assets are missing.
  spr.fillEllipse(s_ufoX, s_ufoY, 18, 6, TFT_DARKGREY);
  spr.drawEllipse(s_ufoX, s_ufoY, 18, 6, TFT_LIGHTGREY);
  spr.fillRoundRect(s_ufoX - 9, s_ufoY - 9, 18, 8, 5, TFT_DARKCYAN);
  spr.drawRoundRect(s_ufoX - 9, s_ufoY - 9, 18, 8, 5, TFT_CYAN);

  spr.fillCircle(s_ufoX - 10, s_ufoY + 3, 2, TFT_GREEN);
  spr.fillCircle(s_ufoX, s_ufoY + 4, 2, TFT_GREEN);
  spr.fillCircle(s_ufoX + 10, s_ufoY + 3, 2, TFT_GREEN);
}

static void drawBeam()
{
  if (!beamActive())
    return;

  const int topY = s_ufoY + 7;
  const int botY = kGroundY;
  const int halfTop = 4;
  const int halfBot = 18;

  spr.fillTriangle(s_beamX - halfTop, topY, s_beamX + halfTop, topY, s_beamX - halfBot, botY, TFT_DARKCYAN);
  spr.fillTriangle(s_beamX + halfTop, topY, s_beamX - halfBot, botY, s_beamX + halfBot, botY, TFT_DARKCYAN);
  spr.drawLine(s_beamX - halfTop, topY, s_beamX - halfBot, botY, TFT_CYAN);
  spr.drawLine(s_beamX + halfTop, topY, s_beamX + halfBot, botY, TFT_CYAN);
}

static void drawTarget(const AbductionTarget &t)
{
  switch (t.kind)
  {
  case TARGET_COW:
  {
    const uint8_t frame = (millis() / 180) & 1;
    const char *path = COW_FRAMES[frame];

    M5Canvas *cow = nullptr;
    const char *assetId = frame ? "cow_1" : "cow_0";

    if (getAbductionSprite(assetId, path, cow) && cow)
    {
      const int drawX = t.x;
      const int drawY = t.y + t.h - cow->height();

      if (t.dir < 0)
        cow->pushSprite(&spr, drawX, drawY, kAbductionSpriteKey);
      else
        drawCachedSpriteMirroredX(cow, drawX, drawY);

      break;
    }

    spr.fillRoundRect(t.x, t.y + 4, t.w, 8, 3, TFT_WHITE);
    spr.fillRect(t.x + 3, t.y + 7, 3, 3, TFT_BLACK);
    spr.fillRect(t.x + 11, t.y + 6, 3, 3, TFT_BLACK);
    spr.fillRect(t.x + 2, t.y + 12, 2, 4, TFT_WHITE);
    spr.fillRect(t.x + 13, t.y + 12, 2, 4, TFT_WHITE);
    break;
  }

  case TARGET_FARMER:
  {
    const uint8_t frame = (millis() / 180) & 1;
    const char *path = MIB_FRAMES[frame];

    M5Canvas *mib = nullptr;
    const char *assetId = frame ? "mib_1" : "mib_0";

    if (getAbductionSprite(assetId, path, mib) && mib)
    {
      const int drawX = t.x;
      const int drawY = t.y + t.h - mib->height();

      if (t.dir < 0)
        mib->pushSprite(&spr, drawX, drawY, kAbductionSpriteKey);
      else
        drawCachedSpriteMirroredX(mib, drawX, drawY);

      break;
    }

    spr.fillCircle(t.x + 7, t.y + 4, 3, TFT_ORANGE);
    spr.fillRect(t.x + 4, t.y + 7, 6, 10, TFT_BLACK);
    spr.drawLine(t.x + 5, t.y + 17, t.x + 3, t.y + 21, TFT_DARKGREY);
    spr.drawLine(t.x + 9, t.y + 17, t.x + 11, t.y + 21, TFT_DARKGREY);
    break;
  }
  }
}

static void drawHud()
{
  const uint32_t elapsed = millis() - s_startedMs;
  const int remain = (elapsed >= kGameMs) ? 0 : (int)((kGameMs - elapsed + 999) / 1000);

  char hud[48];
  snprintf(hud, sizeof(hud), "S:%u  X:%u/%u  T:%d", s_score, s_strikes, kMaxStrikes, remain);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(hud, 4, 4);
}

static void handleBeamHit()
{
  s_beamX = s_ufoX;
  s_beamUntilMs = millis() + kBeamMs;

  const int beamHalfW = 18;
  int best = -1;
  int bestDy = 999;

  for (uint8_t i = 0; i < kMaxTargets; ++i)
  {
    const AbductionTarget &t = s_targets[i];
    if (!t.active)
      continue;

    const int cx = t.x + (t.w / 2);
    const int dx = abs(cx - s_beamX);

    if (dx <= beamHalfW)
    {
      const int dy = abs((int)t.y - kGroundY);
      if (dy < bestDy)
      {
        bestDy = dy;
        best = i;
      }
    }
  }

  if (best < 0)
  {
    soundClick();
    return;
  }

  AbductionTarget &t = s_targets[best];

  if (targetGood(t.kind))
  {
    s_score = min<int>(255, s_score + targetPoints(t.kind));
    soundConfirm();
  }
  else
  {
    s_strikes++;
    soundError();

    if (s_strikes >= kMaxStrikes)
    {
      finishGame(false);
    }
  }

  t.active = false;
}

} // namespace

bool abductionBeamIsShowingIntro() { return s_abductionShowIntro; }

void startAbductionBeam()
{
  inputSetTextCapture(false);
  mgPauseReset();

  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;

  mgClearRewardState();
  mgResetAcceptState();

  currentMiniGame = MiniGame::ABDUCTION_BEAM;

  graphicsReleaseUiCachesForMiniGame();
  mgAssetsBeginSession(currentMiniGame, "startAbductionBeam");
  mgmem::beginSession(currentMiniGame, pet.type);

  UIState retUi = g_app.uiState;
  if (retUi == UIState::MINI_GAME || retUi == UIState::MG_PAUSE)
    retUi = UIState::PET_SCREEN;

  miniGameSetReturnUi(retUi, g_app.currentTab);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  s_inited = true;
  s_abductionShowIntro = true;
  s_abductionIntroDrawnOnce = false;
  s_abductionAssetsPreloaded = false;
  resetGame();

  invalidateBackgroundCache();
  requestUIRedraw();
  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);
}

void updateAbductionBeam(const InputState &input)
{
  const bool enterOnce = miniGameEnterOnce(input);
  const uint32_t now = millis();

  if (mgRewardShowing())
  {
    if ((enterOnce && !mgInputLockedOut()) || mgRewardAutoDismissNow(now))
    {
      mgClearRewardState();
      mgResetAcceptState();
      miniGameExitToReturnUi(true);
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

  if (!s_inited)
  {
    s_inited = true;
    resetGame();
  }

  if (s_abductionShowIntro)
  {
    if (s_abductionIntroDrawnOnce && !s_abductionAssetsPreloaded)
    {
      abductionPreloadAssetsForIntro();
      return;
    }

    if (input.mgQuitOnce && !mgInputLockedOut())
    {
      miniGameCancelFromIntro();
      return;
    }

    const bool startPressed = s_abductionAssetsPreloaded && (enterOnce || input.mgSelectOnce || input.mgUpOnce);

    if (startPressed && !mgInputLockedOut())
    {
      s_abductionShowIntro = false;
      resetGame();

      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  if (input.mgQuitOnce && !mgInputLockedOut())
  {
    miniGameExitToReturnUi(true);
    return;
  }

  const bool moveLeft = input.mgLeftHeld || input.leftHeld;
  const bool moveRight = input.mgRightHeld || input.rightHeld;

  int moveDir = 0;
  if (moveLeft && !moveRight)
    moveDir = -1;
  else if (moveRight && !moveLeft)
    moveDir = 1;

  // Faster movement with light inertia.
  // Velocity is pixels/frame-ish at the current update cadence.
  static constexpr float kUfoAccel = 0.75f;
  static constexpr float kUfoFriction = 0.82f;
  static constexpr float kUfoMaxVel = 5.25f;

  if (moveDir != 0)
  {
    s_ufoVelX += (float)moveDir * kUfoAccel;

    if (s_ufoVelX > kUfoMaxVel)
      s_ufoVelX = kUfoMaxVel;
    else if (s_ufoVelX < -kUfoMaxVel)
      s_ufoVelX = -kUfoMaxVel;
  }
  else
  {
    s_ufoVelX *= kUfoFriction;

    if (s_ufoVelX > -0.05f && s_ufoVelX < 0.05f)
      s_ufoVelX = 0.0f;
  }

  int nextUfoX = s_ufoX + (int)(s_ufoVelX + ((s_ufoVelX >= 0.0f) ? 0.5f : -0.5f));
  nextUfoX = constrain(nextUfoX, 18, SCREEN_W - 18);

  if (nextUfoX == 18 || nextUfoX == SCREEN_W - 18)
    s_ufoVelX = 0.0f;

  s_ufoX = nextUfoX;

  const bool beamPressed =
      enterOnce || input.mgSelectOnce || input.mgUpOnce || input.mgDownOnce || input.upOnce || input.downOnce;

  if (beamPressed && !mgInputLockedOut())
    handleBeamHit();

  const uint32_t dt = now - s_lastStepMs;
  if (dt >= 16)
  {
    s_lastStepMs = now;

    const int speed = 1 + min<int>(4, s_score / 6);

    for (uint8_t i = 0; i < kMaxTargets; ++i)
    {
      AbductionTarget &t = s_targets[i];
      if (!t.active)
        continue;

      t.x += t.dir * speed;

      if ((t.dir > 0 && t.x > SCREEN_W + 24) || (t.dir < 0 && t.x < -32))
        t.active = false;
    }
  }

  if ((int32_t)(now - s_nextSpawnMs) >= 0)
    spawnTarget(now);

  if ((now - s_startedMs) >= kGameMs)
    finishGame(s_score >= 6 && s_strikes < kMaxStrikes);
}

void drawAbductionBeam()
{
  const int gW = screenW;
  const int gH = screenH;

  if (mgRewardShowing())
  {
    miniGameDrawRewardModal(gW, gH);
    return;
  }

  const uint32_t now = millis();
  if ((uint32_t)(now - s_lastAbductionDrawMs) < kAbductionFrameMs)
    return;

  s_lastAbductionDrawMs = now;

  spr.fillSprite(TFT_BLACK);

  if (s_abductionShowIntro)
  {
    spr.setTextDatum(CC_DATUM);
    spr.setTextFont(2);
    spr.setTextSize(1);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Soul Beam", SCREEN_W / 2, 14, 2);

    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawCentreString("Abduct cows", SCREEN_W / 2, 38, 2);
    spr.drawCentreString("Avoid humans", SCREEN_W / 2, 56, 2);

    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.drawCentreString("UP/DOWN/ENTER = Beam", SCREEN_W / 2, 84, 2);

    spr.setTextColor(s_abductionAssetsPreloaded ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    spr.drawCentreString(s_abductionAssetsPreloaded ? "ENTER to begin" : "Loading...", SCREEN_W / 2, 116, 2);

    s_abductionIntroDrawnOnce = true;
    spr.setTextDatum(TL_DATUM);
    return;
  }

  spr.setTextDatum(CC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);

  // Draw black sky first.
  spr.fillSprite(TFT_BLACK);

  // Draw terrain layer with real PNG transparency.
  static constexpr int kAbductionBgH = 80;

  if (g_sdReady)
  {
    if (!sprDrawJpgFromSD("/raising_hell/graphics/mini_games/abduct/al_abd_bg.jpg", 0, SCREEN_H - kAbductionBgH))
    {
      spr.fillRect(0, kGroundY, SCREEN_W, SCREEN_H - kGroundY, TFT_DARKGREEN);
      spr.drawLine(0, kGroundY, SCREEN_W, kGroundY, TFT_GREEN);
    }
  }

  // Draw stars after the background so they are visible.
  drawAbductionStars(now);

  drawHud();
  drawBeam();
  drawUfo();

  for (uint8_t i = 0; i < kMaxTargets; ++i)
  {
    if (s_targets[i].active)
      drawTarget(s_targets[i]);
  }
}