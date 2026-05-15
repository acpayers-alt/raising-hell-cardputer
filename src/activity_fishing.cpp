#include "activity_fishing.h"

#include <Arduino.h>

#include "anim_clips.h"
#include "anim_engine.h"
#include "app_state.h"
#include "currency.h"
#include "display.h"
#include "graphics.h"
#include "graphics_chrome.h"
#include "graphics_nonpet_bg.h"
#include "graphics_sd_draw.h"
#include "graphics_shared_utils.h"
#include "graphics_ui_common.h"
#include "input.h"
#include "pet.h"
#include "pet_visuals.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"

extern Pet pet;
extern M5Canvas spr;

bool getPngWH(const char *path, int &w, int &h);

namespace
{
enum class FishingState : uint8_t
{
  IDLE = 0,
  CASTING,
  LINE_OUT,
  BITE,
  REELING,
  POST_CATCH
};

static FishingState s_state = FishingState::IDLE;
static uint32_t s_nextBiteAtMs = 0;
static uint32_t s_biteExpiresAtMs = 0;
static uint32_t s_nextAnimRedrawMs = 0;
static uint32_t s_castingUntilMs = 0;
static bool s_showCatchPose = false;
static int s_lastReward = 0;

static constexpr int kFishingEnergyCost = 4;
static constexpr uint32_t kMinBiteDelayMs = 1800;
static constexpr uint32_t kMaxBiteDelayMs = 4200;
static constexpr uint32_t kBiteWindowMs = 1500;
static constexpr uint32_t kFishingAnimFrameMs = 120;
static constexpr uint32_t kCastingPoseMs = 260;

static constexpr int kRodTipX = 110;
static constexpr int kRodTipY = TOP_BAR_H + 20;
static constexpr int kReelRodTipX = 123;
static constexpr int kReelRodTipY = TOP_BAR_H + 30;
static constexpr int kBobberX = 171;
static constexpr int kBobberY = TOP_BAR_H + 61;

static constexpr int kActivityBgX = 0;
static constexpr int kActivityBgY = TOP_BAR_H;
static constexpr int kActivityBgW = SCREEN_W;
static constexpr int kActivityBgH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;

static constexpr int kFishingPetAnchorX = 68;
static constexpr int kFishingPetAnchorBottomY = TOP_BAR_H + kActivityBgH - 4;

static uint8_t s_reelTaps = 0;
static uint32_t s_reelExpiresAtMs = 0;
static uint16_t s_sessionCatches = 0;
static uint16_t s_sessionInf = 0;

static uint8_t s_reelTapsNeeded = 6;

static constexpr uint8_t kReelTapsMin = 4;
static constexpr uint8_t kReelTapsMax = 10;

static constexpr uint32_t kReelWindowMs = 3600;

static uint32_t s_inputLockedUntilMs = 0;
static constexpr uint32_t kPostCatchInputLockMs = 1000;
static constexpr uint32_t kPostCatchExtendLockMs = 250;

static uint32_t s_postCatchUntilMs = 0;
static uint32_t s_postCatchQuietSinceMs = 0;
static constexpr uint32_t kPostCatchQuietMs = 900;
static bool s_castArmed = true;
static constexpr uint32_t kPostCatchReadyMs = 900;

static const char *fishingPetFrameForState()
{
  if (pet.type == PET_ALIEN && pet.evoStage == 0)
  {
    switch (s_state)
    {
    case FishingState::CASTING:
      return "/raising_hell/graphics/activities/fishing/al_bb_fsh_anim4.png";
    case FishingState::LINE_OUT:
    case FishingState::BITE:
      return "/raising_hell/graphics/activities/fishing/al_bb_fsh_anim2.png";
    case FishingState::REELING:
      return "/raising_hell/graphics/activities/fishing/al_bb_fsh_anim3.png";
    case FishingState::POST_CATCH:
      return s_showCatchPose ? "/raising_hell/graphics/activities/fishing/al_bb_fsh_anim5.png"
                             : "/raising_hell/graphics/activities/fishing/al_bb_fsh_anim1.png";
    case FishingState::IDLE:
    default:
      return "/raising_hell/graphics/activities/fishing/al_bb_fsh_anim1.png";
    }
  }

  const AnimClip *clip = animGetClip(ANIM_ALIEN_BABY_BORED_IDLE);
  if (!clip || !clip->frames || clip->frameCount == 0)
    return nullptr;

  return clip->frames[0];
}

static void drawFishingPet()
{
  const char *path = fishingPetFrameForState();
  if (!path || !path[0])
    return;

  int w = 64;
  int h = 64;
  (void)getPngWH(path, w, h);

  const int drawX = kFishingPetAnchorX - (w / 2);
  const int drawY = kFishingPetAnchorBottomY - h;

  sprDrawPngFromSD(path, drawX, drawY);
}

static void scheduleNextBite(uint32_t now)
{
  s_nextBiteAtMs = now + random((long)kMinBiteDelayMs, (long)kMaxBiteDelayMs + 1L);
}

static void drawFishingLine()
{
  const int rodX = (s_state == FishingState::REELING) ? kReelRodTipX : kRodTipX;
  const int rodY = (s_state == FishingState::REELING) ? kReelRodTipY : kRodTipY;

  spr.drawLine(rodX, rodY, kBobberX, kBobberY, TFT_WHITE);
}

static void drawBobber(bool visible)
{
  if (!visible)
    return;

  spr.fillCircle(kBobberX, kBobberY, 3, TFT_WHITE);
  spr.fillCircle(kBobberX, kBobberY - 1, 2, TFT_RED);
  spr.drawCircle(kBobberX, kBobberY, 3, TFT_BLACK);
}

static void drawIdleWaves(uint32_t now)
{
  const uint8_t phase = (now / kFishingAnimFrameMs) & 3U;
  const int offset = (int)phase;
  const uint16_t col = 0x7D7C;

  spr.drawFastHLine(kBobberX - 15 - offset, kBobberY + 5, 8, col);
  spr.drawFastHLine(kBobberX + 7 + offset, kBobberY + 5, 8, col);

  if (phase & 1U)
  {
    spr.drawFastHLine(kBobberX - 10, kBobberY + 8, 6, col);
    spr.drawFastHLine(kBobberX + 5, kBobberY + 8, 6, col);
  }
}

static void drawBiteRipples(uint32_t now)
{
  const uint8_t phase = (now / kFishingAnimFrameMs) & 3U;
  const uint16_t col = 0x7D7C;

  spr.drawCircle(kBobberX, kBobberY + 4, 6 + phase, col);
  spr.drawCircle(kBobberX, kBobberY + 4, 11 + phase, col);
}

static void drawFishingRig()
{
  if (s_state == FishingState::IDLE || s_state == FishingState::CASTING || s_state == FishingState::POST_CATCH)
    return;

  if (s_state == FishingState::LINE_OUT && s_nextBiteAtMs == 0)
    return;

  const uint32_t now = millis();

  drawFishingLine();

  if (s_state == FishingState::BITE || s_state == FishingState::REELING)
  {
    const bool bobberVisible = ((now / 160UL) & 1U) == 0;
    drawBobber(bobberVisible);
    drawBiteRipples(now);
  }
  else
  {
    drawBobber(true);
    drawIdleWaves(now);
  }
}

static const char *fishingBgForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/activities/fishing/eld_bb_fsh_bg.jpg";
  case PET_ALIEN:
    return "/raising_hell/graphics/activities/fishing/al_bb_fsh_bg.jpg";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/activities/fishing/dev_bb_fsh_bg.jpg";
  }
}

static void beginCast()
{
  if (pet.energy < kFishingEnergyCost)
  {
    ui_showMessage("Too tired to cast.");
    soundError();
    return;
  }

  pet.energy = constrain(pet.energy - kFishingEnergyCost, 0, 100);
  saveManagerMarkDirty();

  const uint32_t now = millis();

  s_nextBiteAtMs = 0;
  s_biteExpiresAtMs = 0;
  s_castingUntilMs = now + kCastingPoseMs;
  s_showCatchPose = false;
  s_state = FishingState::CASTING;

  soundClick();
  requestUIRedraw();
}

static void fishEscaped()
{
  const uint32_t now = millis();

  s_nextBiteAtMs = 0;
  s_biteExpiresAtMs = 0;
  s_reelTaps = 0;
  s_reelExpiresAtMs = 0;
  s_postCatchUntilMs = now + kPostCatchReadyMs;
  s_postCatchQuietSinceMs = 0;
  s_state = FishingState::POST_CATCH;

  soundError();
  requestUIRedraw();
}

static void claimReward()
{
  s_lastReward = random(3, 13);
  addInf(s_lastReward);

  s_sessionCatches++;
  s_sessionInf += s_lastReward;

  const uint32_t now = millis();
  s_nextBiteAtMs = 0;
  s_biteExpiresAtMs = 0;
  s_reelTaps = 0;
  s_reelExpiresAtMs = 0;
  s_inputLockedUntilMs = now + kPostCatchInputLockMs;
  s_postCatchUntilMs = now + kPostCatchReadyMs;
  s_postCatchQuietSinceMs = 0;
  s_showCatchPose = true;
  s_state = FishingState::POST_CATCH;
  s_castArmed = false;
  inputForceClear();
  inputForceClear();

  soundWin();
  requestUIRedraw();
}
} // namespace

void activityFishingOnEnter()
{
  s_state = FishingState::IDLE;
  s_nextBiteAtMs = 0;
  s_biteExpiresAtMs = 0;
  s_nextAnimRedrawMs = 0;
  s_castingUntilMs = 0;
  s_showCatchPose = false;
  s_lastReward = 0;
  s_reelTaps = 0;
  s_reelExpiresAtMs = 0;
  s_sessionCatches = 0;
  s_sessionInf = 0;
  s_inputLockedUntilMs = 0;
  s_reelTapsNeeded = 6;
  s_postCatchUntilMs = 0;
  s_castArmed = true;
  s_postCatchQuietSinceMs = 0;
}

void activityFishingHandle(InputState &in)
{
  if (in.escOnce || in.menuOnce || in.homeOnce)
  {
    uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_ACTIVITIES, true, in, 160);
    return;
  }

  const bool confirmOnce = in.selectOnce || in.encoderPressOnce || in.mgSelectOnce;
  const uint32_t now = millis();

  if (s_state == FishingState::POST_CATCH)
  {
    const bool confirmDown =
        in.selectOnce || in.encoderPressOnce || in.mgSelectOnce || in.selectHeld || in.encoderHeld || in.mgSelectHeld;

    if (confirmDown)
    {
      s_postCatchQuietSinceMs = 0;
      s_postCatchUntilMs = now + kPostCatchReadyMs;
      in.clearEdges();
      return;
    }

    if (s_postCatchQuietSinceMs == 0)
      s_postCatchQuietSinceMs = now;

    const bool cooldownDone = (int32_t)(now - s_postCatchUntilMs) >= 0;
    const bool quietDone = (uint32_t)(now - s_postCatchQuietSinceMs) >= kPostCatchQuietMs;

    if (cooldownDone && quietDone)
    {
      s_state = FishingState::IDLE;
      s_postCatchUntilMs = 0;
      s_postCatchQuietSinceMs = 0;
      s_castArmed = true;
      requestUIRedraw();
    }

    in.clearEdges();
    return;
  }

  if (s_state == FishingState::CASTING)
  {
    if ((int32_t)(now - s_castingUntilMs) >= 0)
    {
      s_castingUntilMs = 0;
      s_state = FishingState::LINE_OUT;
      s_nextAnimRedrawMs = now + 180UL;
      requestUIRedraw();
    }

    in.clearEdges();
    return;
  }

  if (s_state == FishingState::LINE_OUT && s_nextBiteAtMs == 0)
  {
    if ((int32_t)(now - s_nextAnimRedrawMs) >= 0)
    {
      scheduleNextBite(now);
      requestUIRedraw();
    }

    in.clearEdges();
    return;
  }

  const bool inputLocked = (int32_t)(now - s_inputLockedUntilMs) < 0;

  if (inputLocked)
  {
    if (confirmOnce)
      s_inputLockedUntilMs = now + kPostCatchExtendLockMs;

    in.clearEdges();
    return;
  }

  if (confirmOnce)
  {
    if (s_state == FishingState::IDLE && s_castArmed)
    {
      beginCast();
    }
    else if (s_state == FishingState::LINE_OUT)
    {
      s_nextBiteAtMs = 0;
      s_biteExpiresAtMs = 0;
      s_reelTaps = 0;
      s_reelExpiresAtMs = 0;
      s_showCatchPose = false;
      s_state = FishingState::IDLE;
      s_castArmed = true;
      requestUIRedraw();
    }
    else if (s_state == FishingState::BITE)
    {
      s_state = FishingState::REELING;
      s_reelTapsNeeded = (uint8_t)random((long)kReelTapsMin, (long)kReelTapsMax + 1L);
      s_reelTaps = 1;
      s_reelExpiresAtMs = now + kReelWindowMs;
      requestUIRedraw();
    }
    else if (s_state == FishingState::REELING)
    {
      if (s_reelTaps < 255)
        s_reelTaps++;

      if (s_reelTaps >= s_reelTapsNeeded)
      {
        claimReward();
      }
      else
      {
        s_nextAnimRedrawMs = 0;
        requestUIRedraw();
      }
    }

    in.clearEdges();
    return;
  }

  if (s_state == FishingState::LINE_OUT && (int32_t)(now - s_nextBiteAtMs) >= 0)
  {
    s_state = FishingState::BITE;
    s_biteExpiresAtMs = now + kBiteWindowMs;
    soundClick();
    requestUIRedraw();
  }
  else if (s_state == FishingState::BITE && (int32_t)(now - s_biteExpiresAtMs) >= 0)
  {
    fishEscaped();
  }
  else if (s_state == FishingState::REELING && (int32_t)(now - s_reelExpiresAtMs) >= 0)
  {
    fishEscaped();
  }

  if (s_state != FishingState::IDLE && (int32_t)(now - s_nextAnimRedrawMs) >= 0)
  {
    s_nextAnimRedrawMs = now + kFishingAnimFrameMs;
    requestUIRedraw();
  }
}

static void drawReelProgressBar(int y)
{
  if (s_state != FishingState::REELING)
    return;

  const int barW = 86;
  const int barH = 5;
  const int barX = (SCREEN_W - barW) / 2;
  const int barY = y - barH - 3;

  uint8_t needed = s_reelTapsNeeded;
  if (needed == 0)
    needed = 1;

  int fillW = ((int)s_reelTaps * barW) / (int)needed;
  fillW = clampi(fillW, 0, barW);

  spr.drawRect(barX, barY, barW, barH, TFT_DARKGREY);
  spr.fillRect(barX + 1, barY + 1, fillW > 2 ? fillW - 2 : 0, barH - 2, TFT_WHITE);
}

static void drawFishingBottomBar()
{
  const PetUIColorScheme ui = uiSchemeForPet(pet.type);
  const int y = SCREEN_H - TAB_BAR_H;

  spr.fillRect(0, y, SCREEN_W, TAB_BAR_H, ui.tabBg);
  spr.drawFastHLine(0, y, SCREEN_W, ui.tabOutline);

  char buf[64];

  if (s_state == FishingState::IDLE)
  {
    snprintf(buf, sizeof(buf), "Press Enter to cast");
  }
  else if (s_state == FishingState::LINE_OUT)
  {
    if (s_sessionCatches > 0)
      snprintf(buf, sizeof(buf), "%u catches  %u INF", (unsigned)s_sessionCatches, (unsigned)s_sessionInf);
    else
      snprintf(buf, sizeof(buf), "Waiting for a bite...");
  }
  else if (s_state == FishingState::BITE)
  {
    snprintf(buf, sizeof(buf), "Bite! Press Enter!");
  }
  else if (s_state == FishingState::REELING)
  {
    snprintf(buf, sizeof(buf), "Mash Enter to reel!");
  }
  else if (s_state == FishingState::POST_CATCH)
  {
    snprintf(buf, sizeof(buf), "%u catches  %u INF", (unsigned)s_sessionCatches, (unsigned)s_sessionInf);
  }
  else if (s_state == FishingState::CASTING)
  {
    snprintf(buf, sizeof(buf), "Casting...");
  }
  else
  {
    snprintf(buf, sizeof(buf), "Fishing");
  }

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(ui.tabTextOff, ui.tabBg);
  spr.drawString(buf, SCREEN_W / 2, y + (TAB_BAR_H / 2));
  drawReelProgressBar(y);
  spr.setTextDatum(TL_DATUM);
}

void activityFishingDraw(bool redrawBg)
{
  (void)redrawBg;

  spr.fillSprite(TFT_BLACK);

  const char *bg = fishingBgForPet();
  if (bg && bg[0])
  {
    const bool ok = sprDrawJpgFromSD(bg, kActivityBgX, kActivityBgY);
    if (!ok)
      spr.fillRect(kActivityBgX, kActivityBgY, kActivityBgW, kActivityBgH, TFT_BLACK);
  }
  else
  {
    spr.fillRect(kActivityBgX, kActivityBgY, kActivityBgW, kActivityBgH, TFT_BLACK);
  }

  drawFishingPet();
  drawFishingRig();

  drawTopBar();
  drawFishingBottomBar();

  // HUD/status overlay temporarily disabled so the full background art is visible.
  spr.setTextDatum(TL_DATUM);
}