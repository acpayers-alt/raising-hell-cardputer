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
  LINE_OUT,
  BITE,
  REELING,
};

static FishingState s_state = FishingState::IDLE;
static uint32_t s_nextBiteAtMs = 0;
static uint32_t s_biteExpiresAtMs = 0;
static uint32_t s_nextAnimRedrawMs = 0;
static int s_lastReward = 0;

static constexpr int kFishingEnergyCost = 4;
static constexpr uint32_t kMinBiteDelayMs = 1800;
static constexpr uint32_t kMaxBiteDelayMs = 4200;
static constexpr uint32_t kBiteWindowMs = 1500;
static constexpr uint32_t kFishingAnimFrameMs = 120;

static constexpr int kRodTipX = 88;
static constexpr int kRodTipY = TOP_BAR_H + 47;
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

static constexpr uint8_t kReelTapsNeeded = 6;
static constexpr uint32_t kReelWindowMs = 2200;

static const char *fishingPetPlaceholderFrame()
{
  const AnimClip *clip = animGetClip(ANIM_ALIEN_BABY_BORED_IDLE);
  if (!clip || !clip->frames || clip->frameCount == 0)
    return nullptr;

  return clip->frames[0];
}

static void drawFishingPetPlaceholder()
{
  const char *path = fishingPetPlaceholderFrame();
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

static void drawFishingLine() { spr.drawLine(kRodTipX, kRodTipY, kBobberX, kBobberY, TFT_WHITE); }

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
  if (s_state == FishingState::IDLE)
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
  scheduleNextBite(now);

  s_biteExpiresAtMs = 0;
  s_state = FishingState::LINE_OUT;

  soundClick();
  requestUIRedraw();
}

static void fishEscaped()
{
  const uint32_t now = millis();

  scheduleNextBite(now);
  s_biteExpiresAtMs = 0;
  s_reelTaps = 0;
  s_reelExpiresAtMs = 0;
  s_state = FishingState::LINE_OUT;

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
  scheduleNextBite(now);

  s_biteExpiresAtMs = 0;
  s_reelTaps = 0;
  s_reelExpiresAtMs = 0;
  s_state = FishingState::LINE_OUT;

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
  s_lastReward = 0;
  s_reelTaps = 0;
  s_reelExpiresAtMs = 0;
  s_sessionCatches = 0;
  s_sessionInf = 0;
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

  if (confirmOnce)
  {
    if (s_state == FishingState::IDLE)
    {
      beginCast();
    }
    else if (s_state == FishingState::BITE)
    {
      s_state = FishingState::REELING;
      s_reelTaps = 1;
      s_reelExpiresAtMs = now + kReelWindowMs;
      soundClick();
      requestUIRedraw();
    }
    else if (s_state == FishingState::REELING)
    {
      if (s_reelTaps < 255)
        s_reelTaps++;

      soundClick();

      if (s_reelTaps >= kReelTapsNeeded)
        claimReward();
      else
        requestUIRedraw();
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
    snprintf(buf, sizeof(buf), "Mash Enter! %u/%u", (unsigned)s_reelTaps, (unsigned)kReelTapsNeeded);
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

  drawFishingPetPlaceholder();
  drawFishingRig();

  drawTopBar();
  drawFishingBottomBar();

  // HUD/status overlay temporarily disabled so the full background art is visible.
  spr.setTextDatum(TL_DATUM);
}