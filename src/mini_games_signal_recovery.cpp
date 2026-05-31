#include "mini_games.h"

#include <Arduino.h>

#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include "menu_actions.h"
#include "mg_pause_core.h"
#include "mini_game_return_ui.h"
#include "mini_game_runtime.h"
#include "mini_games_internal.h"
#include "pet.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

static bool s_sigShowIntro = true;
static bool s_sigGameOver = false;
static bool s_sigWon = false;

uint32_t s_signalRecoveryLastMs = 0;

static int s_sigPlayerY = 0;
static int s_sigHp = 3;
static int s_sigSignal = 0;

static uint32_t s_sigStartedMs = 0;
static uint32_t s_sigNextEnemyMs = 0;
static uint32_t s_sigNextFragmentMs = 0;
static uint32_t s_sigFireCooldownMs = 0;
static uint32_t s_sigAnimMs = 0;
static uint8_t s_sigStarPhase = 0;

static constexpr int kSigGoal = 100;
static constexpr int kSigMaxHp = 3;
static constexpr uint32_t kSigMaxRunMs = 45000;

static constexpr int kSigPlayerX = 24;
static constexpr int kSigPlayerW = 22;
static constexpr int kSigPlayerH = 14;

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

bool signalRecoveryIsShowingIntro() { return s_sigShowIntro; }

void freeSignalRecoverySprites()
{
  // Primitive first pass. No cached sprites yet.
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
    e.speed = random(60, 105);
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
    b.x = kSigPlayerX + kSigPlayerW - 2;
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
  s_sigGameOver = false;
  s_sigWon = false;

  s_sigPlayerY = (gH / 2) - (kSigPlayerH / 2);
  s_sigHp = kSigMaxHp;
  s_sigSignal = 0;

  const uint32_t now = millis();
  s_signalRecoveryLastMs = now;
  s_sigStartedMs = now;
  s_sigNextEnemyMs = now + 700;
  s_sigNextFragmentMs = now + 1600;
  s_sigFireCooldownMs = 0;
  s_sigAnimMs = now;
  s_sigStarPhase = 0;

  sigClearObjects();
}

void startSignalRecovery()
{
  mgPauseReset();
  inputSetTextCapture(false);
  soundSetVolumeLevel(soundGetVolumeLevel());

  currentMiniGame = MiniGame::SIGNAL_RECOVERY;
  graphicsReleaseUiCachesForMiniGame();

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
    const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;

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
    s_sigNextEnemyMs = now + random(520, 850);
  }

  if ((int32_t)(now - s_sigNextFragmentMs) >= 0)
  {
    sigSpawnDriftingFragment();
    s_sigNextFragmentMs = now + random(1300, 2100);
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

        s_sigSignal = constrain(s_sigSignal + 5, 0, kSigGoal);
        if (random(3) != 0)
          sigSpawnFragment(e.x, e.y);

        soundConfirm();

        if (s_sigSignal >= kSigGoal)
          sigFinish(true);

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

    if (f.x < -8)
    {
      f.active = false;
      continue;
    }

    if (sigAabb(kSigPlayerX, s_sigPlayerY, kSigPlayerW, kSigPlayerH, f.x - 3, f.y - 3, 7, 7))
    {
      f.active = false;
      s_sigSignal = constrain(s_sigSignal + 12, 0, kSigGoal);
      soundConfirm();

      if (s_sigSignal >= kSigGoal)
      {
        sigFinish(true);
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
  const int x = kSigPlayerX;
  const int y = s_sigPlayerY;

  spr.fillEllipse(x + 11, y + 7, 11, 5, spr.color565(80, 210, 120));
  spr.drawEllipse(x + 11, y + 7, 11, 5, TFT_BLACK);
  spr.fillCircle(x + 12, y + 4, 5, spr.color565(150, 255, 180));
  spr.drawCircle(x + 12, y + 4, 5, TFT_BLACK);

  spr.drawFastHLine(x - 4, y + 7, 5, TFT_GREEN);
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
    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("SIGNAL RECOVERY", gW / 2, 14, 2);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("UP/DOWN to move", gW / 2, 38, 2);
    spr.drawCentreString("ENTER/G to fire", gW / 2, 56, 2);
    spr.drawCentreString("Recover the life signal", gW / 2, 80, 2);

    sigDrawPlayer();

    spr.setTextColor(TFT_GREEN, TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 118, 2);
    spr.setTextDatum(TL_DATUM);
    return;
  }

  sigDrawHud(gW);

  for (const auto &f : s_fragments)
  {
    if (!f.active)
      continue;

    spr.fillCircle(f.x, f.y, 3, TFT_GREEN);
    spr.drawCircle(f.x, f.y, 4, TFT_WHITE);
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

    spr.fillCircle(e.x, e.y, e.r, spr.color565(90, 0, 130));
    spr.drawCircle(e.x, e.y, e.r, TFT_MAGENTA);
    spr.drawPixel(e.x - 2, e.y - 1, TFT_WHITE);
    spr.drawPixel(e.x + 2, e.y + 1, TFT_WHITE);
  }

  sigDrawPlayer();

  const uint32_t elapsed = millis() - s_sigStartedMs;
  const int remaining = (elapsed >= kSigMaxRunMs) ? 0 : (int)((kSigMaxRunMs - elapsed) / 1000UL);

  char buf[20];
  snprintf(buf, sizeof(buf), "%ds", remaining);

  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString(buf, gW - 4, gH - 11);

  spr.setTextDatum(TL_DATUM);
}