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

static bool s_voidShowIntro = true;
static bool s_voidGameOver = false;
static bool s_voidWon = false;

uint32_t s_voidRitualLastMs = 0;

static int s_voidFocusLane = 1;
static int s_voidRitual = 0;
static int s_voidStability = 4;
static uint8_t s_voidPhaseIndex = 0;
static uint8_t s_voidPhaseMatches = 0;
static bool s_voidPhaseIntroActive = false;
static uint32_t s_voidPhaseIntroUntilMs = 0;

static uint32_t s_voidStartedMs = 0;
static uint32_t s_voidNextRiftMs = 0;
static uint32_t s_voidRiftExpiresMs = 0;
static uint32_t s_voidAnimMs = 0;
static uint8_t s_voidPhase = 0;

static uint8_t s_voidSelectedLane = 1;
static uint8_t s_voidTargetGlyph = 0;
static uint8_t s_voidLaneGlyphs[3] = {0, 1, 2};
static uint32_t s_voidRoundExpiresMs = 0;

static constexpr int kVoidGoal = 100;
static constexpr int kVoidMaxStability = 4;
static constexpr uint8_t kVoidPhaseCount = 3;
static constexpr uint8_t kVoidMatchesPerPhase = 6;
static constexpr uint32_t kVoidPhaseIntroMs = 1100;

static constexpr uint32_t kVoidRoundWindowMsByPhase[kVoidPhaseCount] = {
    5200, // Phase 1: slower
    4200, // Phase 2: current timing
    3200, // Phase 3: faster
};

static uint32_t voidCurrentRoundWindowMs()
{
  const uint8_t idx = (s_voidPhaseIndex < kVoidPhaseCount) ? s_voidPhaseIndex : (kVoidPhaseCount - 1);
  return kVoidRoundWindowMsByPhase[idx];
}

static void voidBeginPhaseIntro(uint32_t now)
{
  s_voidPhaseIntroActive = true;
  s_voidPhaseIntroUntilMs = now + kVoidPhaseIntroMs;
  s_voidRoundExpiresMs = 0;
}

bool voidRitualIsShowingIntro() { return s_voidShowIntro; }

void freeVoidRitualSprites()
{
  // Primitive first pass. No cached sprites yet.
}

static int voidLaneX(int lane, int gW)
{
  switch (lane)
  {
  case 0:
    return gW / 4;
  case 1:
    return gW / 2;
  case 2:
  default:
    return (gW * 3) / 4;
  }
}

static int voidRiftY(int gH) { return (gH / 2) - 4; }

static int voidFocusY(int gH) { return gH - 28; }

static void voidFinish(bool won)
{
  s_voidGameOver = true;
  s_voidWon = won;
  playerWon = won;
  g_app.gameOver = true;
  requestUIRedraw();
}

static void voidStartRound(uint32_t now)
{
  s_voidLaneGlyphs[0] = 0;
  s_voidLaneGlyphs[1] = 1;
  s_voidLaneGlyphs[2] = 2;

  // Shuffle glyph placement so the match moves around.
  for (int i = 0; i < 3; ++i)
  {
    const int j = random(0, 3);
    const uint8_t tmp = s_voidLaneGlyphs[i];
    s_voidLaneGlyphs[i] = s_voidLaneGlyphs[j];
    s_voidLaneGlyphs[j] = tmp;
  }

  s_voidTargetGlyph = random(0, 3);
  s_voidSelectedLane = 1;
  s_voidFocusLane = 1;
  s_voidRoundExpiresMs = now + voidCurrentRoundWindowMs();
}

static void voidMiss()
{
  if (s_voidStability > 0)
    s_voidStability--;

  soundError();

  if (s_voidStability <= 0)
    voidFinish(false);
}

static void voidHit()
{
  soundConfirm();

  if (s_voidPhaseMatches < 255)
    s_voidPhaseMatches++;

  const int totalMatchesNeeded = (int)kVoidPhaseCount * (int)kVoidMatchesPerPhase;
  const int completedMatches = ((int)s_voidPhaseIndex * (int)kVoidMatchesPerPhase) + (int)s_voidPhaseMatches;
  s_voidRitual = constrain((completedMatches * kVoidGoal) / totalMatchesNeeded, 0, kVoidGoal);

  const uint32_t now = millis();

  if (s_voidPhaseMatches >= kVoidMatchesPerPhase)
  {
    s_voidPhaseMatches = 0;

    if (s_voidPhaseIndex + 1 >= kVoidPhaseCount)
    {
      s_voidRitual = kVoidGoal;
      voidFinish(true);
      return;
    }

    s_voidPhaseIndex++;
    voidBeginPhaseIntro(now);
    return;
  }

  voidStartRound(now);
}

static void voidReset()
{
  s_voidShowIntro = true;
  s_voidGameOver = false;
  s_voidWon = false;

  s_voidFocusLane = 1;
  s_voidSelectedLane = 1;
  s_voidRitual = 0;
  s_voidStability = kVoidMaxStability;
  s_voidPhaseIndex = 0;
  s_voidPhaseMatches = 0;
  s_voidPhaseIntroActive = false;
  s_voidPhaseIntroUntilMs = 0;
  s_voidTargetGlyph = 0;
  s_voidLaneGlyphs[0] = 0;
  s_voidLaneGlyphs[1] = 1;
  s_voidLaneGlyphs[2] = 2;
  s_voidRoundExpiresMs = 0;

  const uint32_t now = millis();
  s_voidRitualLastMs = now;
  s_voidStartedMs = now;
  s_voidNextRiftMs = now + 900;
  s_voidRoundExpiresMs = 0;
  s_voidAnimMs = now;
  s_voidPhase = 0;
  voidBeginPhaseIntro(now);
}

void startVoidRitual()
{
  mgPauseReset();
  inputSetTextCapture(false);
  soundSetVolumeLevel(soundGetVolumeLevel());

  currentMiniGame = MiniGame::VOID_RITUAL;
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

  voidReset();

  invalidateBackgroundCache();
  requestUIRedraw();
}

void updateVoidRitual(const InputState &input)
{
  const uint32_t now = millis();
  const bool enterOnce = miniGameEnterOnce(input);

  if (mgRewardShowing())
  {
    if ((enterOnce && !mgInputLockedOut()) || mgRewardAutoDismissNow(now))
    {
      mgClearRewardState();
      mgResetAcceptState();

      onResurrectionMiniGameResult(s_voidWon);
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

  if (s_voidShowIntro)
  {
    const bool startPressed = enterOnce || input.mgSelectOnce || input.mgUpOnce;

    if (startPressed && !mgInputLockedOut())
    {
      s_voidShowIntro = false;
      s_voidRitualLastMs = now;

      clearInputLatch();
      inputForceClear();
      mgBeginInputLockout(120);
      requestUIRedraw();
    }

    return;
  }

  if (s_voidGameOver)
    return;

  if (s_voidPhaseIntroActive)
  {
    if ((int32_t)(now - s_voidPhaseIntroUntilMs) >= 0)
    {
      s_voidPhaseIntroActive = false;
      voidStartRound(now);
      requestUIRedraw();
    }

    return;
  }

  if ((uint32_t)(now - s_voidAnimMs) >= 120)
  {
    s_voidAnimMs = now;
    s_voidPhase++;
  }

  s_voidRitualLastMs = now;

  if (input.mgLeftOnce && s_voidSelectedLane > 0)
  {
    s_voidSelectedLane--;
    s_voidFocusLane = s_voidSelectedLane;
    soundClick();
  }

  if (input.mgRightOnce && s_voidSelectedLane < 2)
  {
    s_voidSelectedLane++;
    s_voidFocusLane = s_voidSelectedLane;
    soundClick();
  }

  if ((int32_t)(now - s_voidRoundExpiresMs) >= 0)
  {
    voidMiss();

    if (s_voidGameOver)
      return;

    voidStartRound(now);
  }

  if ((enterOnce || input.mgSelectOnce) && !mgInputLockedOut())
  {
    const bool match = (s_voidLaneGlyphs[s_voidSelectedLane] == s_voidTargetGlyph);

    if (match)
    {
      voidHit();

      if (s_voidGameOver)
        return;
    }
    else
    {
      voidMiss();

      if (s_voidGameOver)
        return;

      voidStartRound(now);
    }
  }
}

static void voidDrawBackground(int gW, int gH)
{
  spr.fillSprite(TFT_BLACK);

  for (int y = 12; y < gH; y += 17)
  {
    const int offset = (s_voidPhase * 3 + y) % 23;

    for (int x = offset; x < gW; x += 23)
    {
      const bool bright = (((x + y + s_voidPhase) & 7) == 0);
      spr.drawPixel(x, y, bright ? spr.color565(150, 80, 210) : spr.color565(45, 18, 70));
    }
  }

  const int cx = gW / 2;
  const int cy = gH + 18;

  spr.drawCircle(cx, cy, 54, spr.color565(60, 20, 90));
  spr.drawCircle(cx, cy, 72, spr.color565(40, 10, 70));
  spr.drawCircle(cx, cy, 92, spr.color565(30, 8, 55));
}

static void voidDrawHud(int gW)
{
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("STAB", 5, 4);

  for (int i = 0; i < kVoidMaxStability; ++i)
  {
    const uint16_t col = (i < s_voidStability) ? spr.color565(170, 70, 220) : TFT_DARKGREY;
    spr.fillRect(32 + i * 8, 5, 6, 5, col);
  }

  spr.drawString("RITUAL", 78, 4);

  const int barX = 120;
  const int barY = 5;
  const int barW = gW - barX - 8;
  const int fillW = (s_voidRitual * barW) / kVoidGoal;

  spr.drawRect(barX, barY, barW, 6, TFT_DARKGREY);
  spr.fillRect(barX + 1, barY + 1, constrain(fillW, 0, barW - 2), 4, spr.color565(180, 70, 255));
}

static void voidDrawRift(int x, int y, bool active)
{
  const uint16_t outer = active ? spr.color565(210, 100, 255) : spr.color565(70, 25, 100);
  const uint16_t inner = active ? spr.color565(90, 220, 255) : spr.color565(45, 15, 65);

  const int pulse = active ? (int)(s_voidPhase & 3) : 0;

  spr.drawCircle(x, y, 13 + pulse, outer);
  spr.drawCircle(x, y, 8 + pulse, outer);
  spr.drawLine(x - 8, y, x + 8, y, inner);
  spr.drawLine(x, y - 8, x, y + 8, inner);

  if (active)
  {
    spr.drawPixel(x - 3, y - 3, TFT_WHITE);
    spr.drawPixel(x + 4, y + 2, TFT_WHITE);
  }
}

static void voidDrawGlyph(int x, int y, uint8_t glyph, uint16_t col, bool selected)
{
  if (selected)
  {
    spr.drawRoundRect(x - 17, y - 17, 34, 34, 6, TFT_WHITE);
    spr.drawRoundRect(x - 15, y - 15, 30, 30, 5, col);
  }

  switch (glyph % 3)
  {
  case 0:
    // Eye glyph
    spr.drawEllipse(x, y, 13, 7, col);
    spr.fillCircle(x, y, 3, col);
    spr.drawPixel(x - 5, y - 1, TFT_WHITE);
    break;

  case 1:
    // Spiral / ring glyph
    spr.drawCircle(x, y, 12, col);
    spr.drawCircle(x, y, 7, col);
    spr.drawLine(x, y, x + 10, y - 5, col);
    spr.drawPixel(x + 3, y - 2, TFT_WHITE);
    break;

  case 2:
  default:
    // Rune fork glyph
    spr.drawFastVLine(x, y - 12, 24, col);
    spr.drawLine(x, y - 4, x - 10, y - 12, col);
    spr.drawLine(x, y - 4, x + 10, y - 12, col);
    spr.drawLine(x, y + 4, x - 8, y + 12, col);
    spr.drawLine(x, y + 4, x + 8, y + 12, col);
    break;
  }
}

static void voidDrawFocus(int x, int y)
{
  const uint16_t col = spr.color565(190, 120, 255);

  spr.drawCircle(x, y, 13, col);
  spr.drawCircle(x, y, 9, col);
  spr.drawLine(x - 16, y, x + 16, y, col);
  spr.drawLine(x, y - 16, x, y + 16, col);

  spr.fillCircle(x, y, 3, TFT_WHITE);
}

void drawVoidRitual()
{
  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  if (mgRewardShowing())
  {
    miniGameDrawRewardModal(gW, gH);
    return;
  }

  voidDrawBackground(gW, gH);

  if (s_voidShowIntro)
  {
    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(spr.color565(190, 100, 255), TFT_BLACK);
    spr.drawCentreString("VOID RITUAL", gW / 2, 14, 2);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("LEFT/RIGHT to choose", gW / 2, 38, 2);
    spr.drawCentreString("ENTER/G to match", gW / 2, 56, 2);
    spr.drawCentreString("Bind the correct rune", gW / 2, 80, 2);

    voidDrawRift(gW / 2, 104, true);

    spr.setTextColor(spr.color565(190, 100, 255), TFT_BLACK);
    spr.drawCentreString("ENTER to begin", gW / 2, 120, 2);

    spr.setTextDatum(TL_DATUM);
    return;
  }

  voidDrawHud(gW);

  if (s_voidPhaseIntroActive)
  {
    char phaseBuf[24];
    snprintf(phaseBuf, sizeof(phaseBuf), "PHASE %u", (unsigned)(s_voidPhaseIndex + 1));

    spr.setTextDatum(CC_DATUM);
    spr.setTextColor(spr.color565(190, 100, 255), TFT_BLACK);
    spr.drawCentreString(phaseBuf, gW / 2, 48, 4);

    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawCentreString("MATCH 6 RUNES", gW / 2, 82, 2);

    spr.setTextDatum(TL_DATUM);
    return;
  }

  const int glyphY = 52;
  const int targetY = 96;

  spr.setTextDatum(CC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawCentreString("MATCH THE RUNE", gW / 2, 22, 2);

  for (int lane = 0; lane < 3; ++lane)
  {
    const int x = voidLaneX(lane, gW);
    const bool selected = ((uint8_t)lane == s_voidSelectedLane);

    voidDrawGlyph(x, glyphY, s_voidLaneGlyphs[lane], spr.color565(190, 100, 255), selected);
  }

  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawCentreString("YOUR RUNE", gW / 2, 75, 1);

  voidDrawGlyph(gW / 2, targetY, s_voidTargetGlyph, TFT_WHITE, false);

  if (s_voidRoundExpiresMs != 0)
  {
    const uint32_t now = millis();
    const uint32_t remainingMs = (now >= s_voidRoundExpiresMs) ? 0 : (s_voidRoundExpiresMs - now);

    const int barW = 92;
    const int barX = (gW - barW) / 2;
    const int barY = gH - 20;
    const int fillW = (int)((remainingMs * (uint32_t)barW) / voidCurrentRoundWindowMs());

    spr.drawRect(barX, barY, barW, 5, TFT_DARKGREY);
    spr.fillRect(barX + 1, barY + 1, constrain(fillW, 0, barW - 2), 3, TFT_WHITE);
  }

  char buf[24];
  snprintf(buf, sizeof(buf), "PHASE %u/%u  %u/%u", (unsigned)(s_voidPhaseIndex + 1), (unsigned)kVoidPhaseCount,
           (unsigned)s_voidPhaseMatches, (unsigned)kVoidMatchesPerPhase);

  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.drawString(buf, gW - 4, gH - 11);

  spr.setTextDatum(TL_DATUM);
}