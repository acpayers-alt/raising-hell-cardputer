#include "mini_games.h"

#include <Arduino.h>

#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "menu_actions.h"
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

static bool s_sigShowIntro = true;
static bool s_sigIntroDrawnOnce = false;
static bool s_sigAssetsPreloaded = false;
static bool s_sigGameOver = false;
static bool s_sigWon = false;

uint32_t s_signalRecoveryLastMs = 0;

static int s_sigPlayerY = 0;
static int s_sigHp = 3;
static int s_sigSignal = 0;

static uint8_t s_sigPhaseIndex = 0;
static uint8_t s_sigPhaseSignals = 0;
static bool s_sigPhaseIntroActive = false;
static uint32_t s_sigPhaseIntroUntilMs = 0;

static uint32_t s_sigStartedMs = 0;
static uint32_t s_sigNextEnemyMs = 0;
static uint32_t s_sigNextFragmentMs = 0;
static uint32_t s_sigFireCooldownMs = 0;
static uint32_t s_sigAnimMs = 0;
static uint8_t s_sigStarPhase = 0;

static constexpr int kSigGoal = 100;
static constexpr int kSigMaxHp = 3;
static constexpr uint8_t kSigPhaseCount = 3;
static constexpr uint8_t kSigSignalsPerPhase = 6;
static constexpr uint32_t kSigPhaseIntroMs = 1100;
static constexpr uint32_t kSigMaxRunMs = 60000;

static constexpr int kSigPlayerX = 24;

// Visual sprite size. Keep collision a little smaller so it feels fair.
static constexpr int kSigShipSpriteW = 32;
static constexpr int kSigShipSpriteH = 16;

static constexpr int kSigSignalSpriteW = 16;
static constexpr int kSigSignalSpriteH = 16;

static constexpr int kSigAsteroidSpriteW = 16;
static constexpr int kSigAsteroidSpriteH = 16;

static constexpr int kSigPlayerW = 24;
static constexpr int kSigPlayerH = 12;

static const char *kSigShipPath = "/raising_hell/graphics/mini_games/signal/ship.png";
static const char *kSigSignalPath = "/raising_hell/graphics/mini_games/signal/signal.png";
static const char *kSigAsteroidPath = "/raising_hell/graphics/mini_games/signal/asteroid.png";

static constexpr uint16_t kSigSpriteKey = 0x0001; // near-black cache fill / transparency key

static M5Canvas *s_sigShipSpr = nullptr;
static M5Canvas *s_sigSignalSpr = nullptr;
static M5Canvas *s_sigAsteroidSpr = nullptr;

struct SigBullet
{
  int x;
  int y;
  bool active;
};

struct SigEnemy
{
  int x;
  int y;
  int r;
  int speed;
  bool active;
};

struct SigFragment
{
  int x;
  int y;
  bool active;
};

static SigBullet s_bullets[5];
static SigEnemy s_enemies[7];
static SigFragment s_fragments[5];

static uint16_t sigEnemySpeedMinForPhase()
{
  switch (s_sigPhaseIndex)
  {
  case 0:
    return 45;
  case 1:
    return 60;
  case 2:
  default:
    return 80;
  }
}

static uint16_t sigEnemySpeedMaxForPhase()
{
  switch (s_sigPhaseIndex)
  {
  case 0:
    return 85;
  case 1:
    return 105;
  case 2:
  default:
    return 135;
  }
}

static uint32_t sigEnemySpawnMinMsForPhase()
{
  switch (s_sigPhaseIndex)
  {
  case 0:
    return 760;
  case 1:
    return 520;
  case 2:
  default:
    return 380;
  }
}

static uint32_t sigEnemySpawnMaxMsForPhase()
{
  switch (s_sigPhaseIndex)
  {
  case 0:
    return 1050;
  case 1:
    return 850;
  case 2:
  default:
    return 650;
  }
}

static uint32_t sigFragmentSpawnMinMsForPhase()
{
  switch (s_sigPhaseIndex)
  {
  case 0:
    return 1000;
  case 1:
    return 900;
  case 2:
  default:
    return 760;
  }
}

static uint32_t sigFragmentSpawnMaxMsForPhase()
{
  switch (s_sigPhaseIndex)
  {
  case 0:
    return 1600;
  case 1:
    return 1500;
  case 2:
  default:
    return 1300;
  }
}

static void sigBeginPhaseIntro(uint32_t now)
{
  s_sigPhaseIntroActive = true;
  s_sigPhaseIntroUntilMs = now + kSigPhaseIntroMs;
}

bool signalRecoveryIsShowingIntro() { return s_sigShowIntro; }

static bool sigEnsureSpriteCache()
{
  const bool shipOk =
      mgmem::ensureSprite(MiniGame::SIGNAL_RECOVERY, "ship", kSigShipPath, 16, kSigSpriteKey, s_sigShipSpr);

  const bool signalOk =
      mgmem::ensureSprite(MiniGame::SIGNAL_RECOVERY, "signal", kSigSignalPath, 16, kSigSpriteKey, s_sigSignalSpr);

  const bool asteroidOk =
      mgmem::ensureSprite(MiniGame::SIGNAL_RECOVERY, "asteroid", kSigAsteroidPath, 16, kSigSpriteKey, s_sigAsteroidSpr);

  return shipOk && signalOk && asteroidOk;
}

static void sigPreloadAssetsForIntro()
{
  if (s_sigAssetsPreloaded)
    return;

  mgmem::logUsage("signal-deferred-preload-begin");

  freeSignalRecoverySprites();

  const bool ok = sigEnsureSpriteCache();

  Serial.printf("[SIGNAL] deferred preload sprites=%d free=%u largest=%u\n", ok ? 1 : 0, (unsigned)mgmem::freeBytes(),
                (unsigned)mgmem::largestBlock());

  s_sigAssetsPreloaded = true;
  requestUIRedraw();

  mgmem::logUsage("signal-deferred-preload-complete");
}

void freeSignalRecoverySprites()
{
  mgmem::releaseSprite(MiniGame::SIGNAL_RECOVERY, "ship");
  mgmem::releaseSprite(MiniGame::SIGNAL_RECOVERY, "signal");
  mgmem::releaseSprite(MiniGame::SIGNAL_RECOVERY, "asteroid");

  s_sigShipSpr = nullptr;
  s_sigSignalSpr = nullptr;
  s_sigAsteroidSpr = nullptr;
}

static bool sigAabb(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
  return (ax < bx + bw) && (ax + aw > bx) && (ay < by + bh) && (ay + ah > by);
}

static void sigClearObjects()
{
  for (auto &b : s_bullets)
    b.active = false;

  for (auto &e : s_enemies)
    e.active = false;

  for (auto &f : s_fragments)
    f.active = false;
}

static void sigSpawnEnemy()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  for (auto &e : s_enemies)
  {
    if (e.active)
      continue;

    e.active = true;
    e.x = gW + 8;
    e.y = random(22, gH - 24);
    e.r = random(5, 9);
    e.speed = random((long)sigEnemySpeedMinForPhase(), (long)sigEnemySpeedMaxForPhase() + 1L);
    return;
  }
}

static void sigSpawnFragment(int x, int y)
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  for (auto &f : s_fragments)
  {
    if (f.active)
      continue;

    f.active = true;
    f.x = constrain(x, 0, gW - 8);
    f.y = constrain(y, 18, gH - 16);
    return;
  }
}

static void sigSpawnDriftingFragment()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  for (auto &f : s_fragments)
  {
    if (f.active)
      continue;

    f.active = true;
    f.x = gW + 6;
    f.y = random(20, gH - 18);
    return;
  }
}

static void sigFire()
{
  for (auto &b : s_bullets)
  {
    if (b.active)
      continue;

    b.active = true;
    b.x = kSigPlayerX + kSigShipSpriteW - 4;
    b.y = s_sigPlayerY + (kSigPlayerH / 2);
    soundClick();
    return;
  }
}

static void sigFinish(bool won)
{
  s_sigGameOver = true;
  s_sigWon = won;
  playerWon = won;
  g_app.gameOver = true;
  requestUIRedraw();
}

static void sigReset()
{
  const int gH = (screenH > 0) ? screenH : 135;

  s_sigShowIntro = true;
  s_sigIntroDrawnOnce = false;
  s_sigAssetsPreloaded = false;
  s_sigGameOver = false;
  s_sigWon = false;

  s_sigPlayerY = (gH / 2) - (kSigPlayerH / 2);
  s_sigHp = kSigMaxHp;
  s_sigSignal = 0;
  s_sigPhaseIndex = 0;
  s_sigPhaseSignals = 0;
  s_sigPhaseIntroActive = false;
  s_sigPhaseIntroUntilMs = 0;

  const uint32_t now = millis();
  s_signalRecoveryLastMs = now;
  s_sigStartedMs = now;
  s_sigNextEnemyMs = now + 700;
  s_sigNextFragmentMs = now + 1100;
  s_sigFireCooldownMs = 0;
  s_sigAnimMs = now;
  s_sigStarPhase = 0;

  sigClearObjects();
  sigBeginPhaseIntro(now);
}

void startSignalRecovery()
{
  mgPauseReset();
  inputSetTextCapture(false);
  soundSetVolumeLevel(soundGetVolumeLevel());

  currentMiniGame = MiniGame::SIGNAL_RECOVERY;
  graphicsReleaseUiCachesForMiniGame();
  mgmem::beginSession(currentMiniGame, pet.type);

  miniGameSetReturnUi(UIState::DEATH, Tab::TAB_PET);
  uiActionEnterState(UIState::MINI_GAME, g_app.currentTab, false);

  g_app.inMiniGame = true;
  g_app.gameOver = false;
  playerWon = false;
  s_resultShown = false;

  mgClearRewardState();
  mgResetAcceptState();

  clearInputLatch();
  inputForceClear();
  mgBeginInputLockout(220);

  sigReset();

  invalidateBackgroundCache();
  requestUIRedraw();
}

void updateSignalRecovery(const InputState &input)
{
  const uint32_t now = millis();
  const bool enterOnce = miniGameEnterOnce(input);

  if (mgRewardShowing())
  {
    if ((enterOnce && !mgInputLockedOut()) || mgRewardAutoDismissNow(now))
    {
      mgClearRewardState();
      mgResetAcceptState();

      onResurrectionMiniGameResult(s_sigWon);
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
    requestUIRedraw();
    return;
  }

  if (s_sigShowIntro)
  {
    if (s_sigIntroDrawnOnce && !s_sigAssetsPreloaded)
    {
      sigPreloadAssetsForIntro();
      return;
    }

    const bool startPressed = s_sigAssetsPreloaded && (enterOnce || input.mgSelectOnce || input.mgUpOnce);

    if (startPressed && !mgInputLockedOut())
    {
      s_sigShowIntro = false;
      s_signalRecoveryLastMs = now;

      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  if (s_sigGameOver)
    return;

  if (s_sigPhaseIntroActive)
  {
    if ((int32_t)(now - s_sigPhaseIntroUntilMs) >= 0)
    {
      s_sigPhaseIntroActive = false;
      s_signalRecoveryLastMs = now;
      s_sigNextEnemyMs = now + 450;
      s_sigNextFragmentMs = now + 650;
      requestUIRedraw();
    }

    return;
  }

  if ((uint32_t)(now - s_sigAnimMs) >= 180)
  {
    s_sigAnimMs = now;
    s_sigStarPhase++;
  }

  uint32_t dtMs = now - s_signalRecoveryLastMs;
  s_signalRecoveryLastMs = now;
  if (dtMs > 40)
    dtMs = 40;

  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  const int movePx = (int)((120.0f * dtMs) / 1000.0f) + 1;

  if (input.mgUpHeld)
    s_sigPlayerY -= movePx;
  if (input.mgDownHeld)
    s_sigPlayerY += movePx;

  s_sigPlayerY = constrain(s_sigPlayerY, 16, gH - kSigPlayerH - 8);

  if ((input.mgSelectOnce || input.mgSelectHeld) && (int32_t)(now - s_sigFireCooldownMs) >= 0)
  {
    sigFire();
    s_sigFireCooldownMs = now + 190;
  }

  if ((int32_t)(now - s_sigNextEnemyMs) >= 0)
  {
    sigSpawnEnemy();
    s_sigNextEnemyMs = now + random((long)sigEnemySpawnMinMsForPhase(), (long)sigEnemySpawnMaxMsForPhase() + 1L);
  }

  if ((int32_t)(now - s_sigNextFragmentMs) >= 0)
  {
    sigSpawnDriftingFragment();
    s_sigNextFragmentMs =
        now + random((long)sigFragmentSpawnMinMsForPhase(), (long)sigFragmentSpawnMaxMsForPhase() + 1L);
  }

  for (auto &b : s_bullets)
  {
    if (!b.active)
      continue;

    b.x += (int)((185.0f * dtMs) / 1000.0f) + 2;
    if (b.x > gW + 8)
      b.active = false;
  }

  for (auto &e : s_enemies)
  {
    if (!e.active)
      continue;

    e.x -= (int)(((float)e.speed * dtMs) / 1000.0f) + 1;

    if (e.x < -16)
    {
      e.active = false;
      continue;
    }

    const int enemyX = e.x - e.r;
    const int enemyY = e.y - e.r;
    const int enemyW = e.r * 2;
    const int enemyH = e.r * 2;

    for (auto &b : s_bullets)
    {
      if (!b.active)
        continue;

      if (sigAabb(b.x, b.y - 1, 6, 3, enemyX, enemyY, enemyW, enemyH))
      {
        b.active = false;
        e.active = false;

        // Shooting asteroids is defensive only. Signal must be collected
        // directly by flying into signal fragments.
        soundConfirm();

        break;
      }
    }

    if (!e.active)
      continue;

    if (sigAabb(kSigPlayerX + 3, s_sigPlayerY + 2, kSigPlayerW - 6, kSigPlayerH - 4, enemyX, enemyY, enemyW, enemyH))
    {
      e.active = false;
      s_sigHp--;
      soundError();

      if (s_sigHp <= 0)
      {
        sigFinish(false);
        return;
      }
    }
  }

  for (auto &f : s_fragments)
  {
    if (!f.active)
      continue;

    f.x -= (int)((70.0f * dtMs) / 1000.0f) + 1;

    if (f.x < -(kSigSignalSpriteW / 2))
    {
      f.active = false;
      continue;
    }

    if (sigAabb(kSigPlayerX, s_sigPlayerY, kSigPlayerW, kSigPlayerH, f.x - 5, f.y - 5, 11, 11))
    {
      f.active = false;
      soundConfirm();

      if (s_sigPhaseSignals < 255)
        s_sigPhaseSignals++;

      const int totalSignalsNeeded = (int)kSigPhaseCount * (int)kSigSignalsPerPhase;
      const int collectedSignals = ((int)s_sigPhaseIndex * (int)kSigSignalsPerPhase) + (int)s_sigPhaseSignals;
      s_sigSignal = constrain((collectedSignals * kSigGoal) / totalSignalsNeeded, 0, kSigGoal);

      if (s_sigPhaseSignals >= kSigSignalsPerPhase)
      {
        s_sigPhaseSignals = 0;

        if (s_sigPhaseIndex + 1 >= kSigPhaseCount)
        {
          s_sigSignal = kSigGoal;
          sigFinish(true);
          return;
        }

        s_sigPhaseIndex++;
        sigClearObjects();
        sigBeginPhaseIntro(now);
        requestUIRedraw();
        return;
      }
    }
  }

  if ((uint32_t)(now - s_sigStartedMs) >= kSigMaxRunMs)
  {
    sigFinish(false);
    return;
  }
}

static void sigDrawStars(int gW, int gH)
{
  static const uint8_t xs[] = {12, 31, 55, 78, 104, 128, 151, 176, 203, 226, 18, 69, 139, 214};
  static const uint8_t ys[] = {17, 42, 25, 69, 14, 52, 31, 82, 48, 21, 94, 101, 92, 112};

  for (uint8_t i = 0; i < sizeof(xs); ++i)
  {
    const int x = xs[i] % gW;
    const int y = ys[i] % gH;
    const bool bright = (((i + s_sigStarPhase) & 3) == 0);

    spr.drawPixel(x, y, bright ? TFT_WHITE : TFT_DARKGREY);

    if (bright && y + 1 < gH)
      spr.drawPixel(x, y + 1, TFT_DARKGREY);
  }
}

static void sigDrawPlayer()
{
  // Draw the sprite slightly larger than the collision box, centered on it.
  const int drawX = kSigPlayerX - 4;
  const int drawY = s_sigPlayerY - 2;

  if (s_sigShipSpr)
  {
    s_sigShipSpr->pushSprite(&spr, drawX, drawY, kSigSpriteKey);
    return;
  }

  // Fallback primitive ship if the asset is missing.
  const int x = kSigPlayerX;
  const int y = s_sigPlayerY;

  spr.fillTriangle(x, y + 1, x, y + kSigPlayerH - 1, x + kSigPlayerW, y + (kSigPlayerH / 2),
                   spr.color565(120, 220, 160));
  spr.drawTriangle(x, y + 1, x, y + kSigPlayerH - 1, x + kSigPlayerW, y + (kSigPlayerH / 2), TFT_WHITE);
  spr.drawFastHLine(x - 4, y + (kSigPlayerH / 2), 4, TFT_GREEN);
}

static void sigDrawHud(int gW)
{
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("HP", 5, 4);

  for (int i = 0; i < kSigMaxHp; ++i)
  {
    const uint16_t col = (i < s_sigHp) ? TFT_RED : TFT_DARKGREY;
    spr.fillRect(22 + i * 8, 5, 6, 5, col);
  }

  spr.drawString("SIGNAL", 70, 4);

  const int barX = 115;
  const int barY = 5;
  const int barW = gW - barX - 8;
  const int fillW = (s_sigSignal * barW) / kSigGoal;

  spr.drawRect(barX, barY, barW, 6, TFT_DARKGREY);
  spr.fillRect(barX + 1, barY + 1, constrain(fillW, 0, barW - 2), 4, TFT_GREEN);
}

void drawSignalRecovery()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  spr.fillSprite(TFT_BLACK);

  if (mgRewardShowing())
  {
    miniGameDrawRewardModal(gW, gH);
    return;
  }

  sigDrawStars(gW, gH);

  if (s_sigShowIntro)
  {
    spr.fillSprite(TFT_BLACK);

    spr.setTextDatum(CC_DATUM);
    spr.setTextFont(2);
    spr.setTextSize(1);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("Collect lost signals", gW / 2, 8, 2);
    spr.drawCentreString("Shoot asteroids", gW / 2, 26, 2);

    // Center the ship + signal preview as a pair.
    const int previewY = 65;
    const int previewGap = 10;
    const int previewGroupW = kSigShipSpriteW + previewGap + kSigSignalSpriteW;
    const int groupLeftX = (gW - previewGroupW) / 2;

    const int shipDrawX = groupLeftX;
    const int shipDrawY = previewY - (kSigShipSpriteH / 2);

    const int signalDrawX = groupLeftX + kSigShipSpriteW + previewGap;
    const int signalDrawY = previewY - (kSigSignalSpriteH / 2);

    if (s_sigAssetsPreloaded && s_sigShipSpr)
    {
      s_sigShipSpr->pushSprite(&spr, shipDrawX, shipDrawY, kSigSpriteKey);
    }
    else
    {
      // Fallback primitive ship preview
      const int x = shipDrawX + 4;
      const int y = shipDrawY + 2;

      spr.fillTriangle(x, y + 1, x, y + kSigPlayerH - 1, x + kSigPlayerW, y + (kSigPlayerH / 2),
                       spr.color565(120, 220, 160));
      spr.drawTriangle(x, y + 1, x, y + kSigPlayerH - 1, x + kSigPlayerW, y + (kSigPlayerH / 2), TFT_WHITE);
      spr.drawFastHLine(x - 4, y + (kSigPlayerH / 2), 4, TFT_GREEN);
    }

    if (s_sigAssetsPreloaded && s_sigSignalSpr)
    {
      s_sigSignalSpr->pushSprite(&spr, signalDrawX, signalDrawY, kSigSpriteKey);
    }
    else
    {
      // Fallback primitive signal preview
      const int cx = signalDrawX + (kSigSignalSpriteW / 2);
      const int cy = signalDrawY + (kSigSignalSpriteH / 2);

      spr.drawCircle(cx, cy, 5, TFT_MAGENTA);
      spr.drawCircle(cx, cy, 3, TFT_WHITE);
      spr.drawPixel(cx, cy, TFT_MAGENTA);
      spr.drawFastHLine(cx - 6, cy, 3, TFT_PURPLE);
      spr.drawFastHLine(cx + 4, cy, 3, TFT_PURPLE);
    }

    spr.setTextColor(TFT_CYAN, TFT_BLACK);
    spr.drawCentreString("UP/DOWN = Move", gW / 2, 88, 2);
    spr.drawCentreString("ENTER/G = Fire", gW / 2, 104, 2);

    spr.setTextColor(s_sigAssetsPreloaded ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    spr.drawCentreString(s_sigAssetsPreloaded ? "ENTER to begin" : "Loading...", gW / 2, 120, 2);

    s_sigIntroDrawnOnce = true;
    spr.setTextDatum(TL_DATUM);
    return;
  }

  sigDrawHud(gW);

  if (s_sigPhaseIntroActive)
  {
    char phaseBuf[24];
    snprintf(phaseBuf, sizeof(phaseBuf), "PHASE %u", (unsigned)(s_sigPhaseIndex + 1));

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString(phaseBuf, gW / 2, 44, 4);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("COLLECT 6 SIGNALS", gW / 2, 78, 2);

    if (s_sigPhaseIndex == 0)
      spr.drawCentreString("ASTEROIDS APPROACH", gW / 2, 98, 1);
    else if (s_sigPhaseIndex == 1)
      spr.drawCentreString("SIGNAL DECAY RISING", gW / 2, 98, 1);
    else
      spr.drawCentreString("FINAL LOCK REQUIRED", gW / 2, 98, 1);

    spr.setTextDatum(TL_DATUM);
    return;
  }

  for (const auto &f : s_fragments)
  {
    if (!f.active)
      continue;

    // Recoverable life signal. f.x/f.y is the signal center.
    const int drawX = f.x - (kSigSignalSpriteW / 2);
    const int drawY = f.y - (kSigSignalSpriteH / 2);

    if (s_sigSignalSpr)
    {
      s_sigSignalSpr->pushSprite(&spr, drawX, drawY, kSigSpriteKey);
    }
    else
    {
      // Fallback primitive signal if the asset is missing.
      spr.drawCircle(f.x, f.y, 5, TFT_MAGENTA);
      spr.drawCircle(f.x, f.y, 3, TFT_WHITE);
      spr.drawPixel(f.x, f.y, TFT_MAGENTA);
      spr.drawFastHLine(f.x - 6, f.y, 3, TFT_PURPLE);
      spr.drawFastHLine(f.x + 4, f.y, 3, TFT_PURPLE);
    }
  }

  for (const auto &b : s_bullets)
  {
    if (!b.active)
      continue;

    spr.drawFastHLine(b.x, b.y, 7, TFT_GREEN);
    spr.drawPixel(b.x + 7, b.y, TFT_WHITE);
  }

  for (const auto &e : s_enemies)
  {
    if (!e.active)
      continue;

    const int drawX = e.x - (kSigAsteroidSpriteW / 2);
    const int drawY = e.y - (kSigAsteroidSpriteH / 2);

    if (s_sigAsteroidSpr)
    {
      s_sigAsteroidSpr->pushSprite(&spr, drawX, drawY, kSigSpriteKey);
    }
    else
    {
      // Fallback primitive asteroid if the asset is missing.
      const uint16_t rockDark = spr.color565(85, 76, 72);
      const uint16_t rockMid = spr.color565(135, 122, 110);
      const uint16_t rockLight = spr.color565(190, 176, 150);

      spr.fillCircle(e.x, e.y, e.r, rockMid);
      spr.drawCircle(e.x, e.y, e.r, rockLight);
      spr.drawPixel(e.x - 3, e.y - 2, rockDark);
      spr.drawPixel(e.x + 2, e.y + 1, rockDark);
      spr.drawFastHLine(e.x - 4, e.y + 3, 5, rockDark);

      if (e.r > 6)
        spr.drawCircle(e.x - 2, e.y - 1, 2, rockDark);
    }
  }

  sigDrawPlayer();

  char buf[28];
  snprintf(buf, sizeof(buf), "PHASE %u/%u  %u/%u", (unsigned)(s_sigPhaseIndex + 1), (unsigned)kSigPhaseCount,
           (unsigned)s_sigPhaseSignals, (unsigned)kSigSignalsPerPhase);

  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString(buf, gW - 4, gH - 11);

  spr.setTextDatum(TL_DATUM);
}