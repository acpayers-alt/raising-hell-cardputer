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
#include "inventory.h"
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

static constexpr uint32_t kFishingCatchXp = 50;
static constexpr uint32_t kMinBiteDelayMs = 2200;
static constexpr uint32_t kMaxBiteDelayMs = 7200;
static constexpr uint32_t kBiteWindowMs = 1500;
static constexpr uint32_t kFishingAnimFrameMs = 120;
static constexpr uint32_t kCastingPoseMs = 260;

static constexpr int kRodTipX = 115;
static constexpr int kRodTipY = TOP_BAR_H + 18;
static constexpr int kReelRodTipX = 123;
static constexpr int kReelRodTipY = TOP_BAR_H + 33;
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

static constexpr uint32_t kReelWindowMs = 4800;

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
  const bool alienBaby = (pet.type == PET_ALIEN && pet.evoStage == 0);
  const bool alienTeen = (pet.type == PET_ALIEN && pet.evoStage == 1);
  const bool alienAdult = (pet.type == PET_ALIEN && pet.evoStage == 2);
  const bool alienElder = (pet.type == PET_ALIEN && pet.evoStage == 3);
  const bool eldritchBaby = (pet.type == PET_ELDRITCH && pet.evoStage == 0);
  const bool eldritchTeen = (pet.type == PET_ELDRITCH && pet.evoStage == 1);
  const bool eldritchAdult = (pet.type == PET_ELDRITCH && pet.evoStage == 2);
  const bool eldritchElder = (pet.type == PET_ELDRITCH && pet.evoStage == 3);
  const bool devilBaby = (pet.type == PET_DEVIL && pet.evoStage == 0);
  const bool devilTeen = (pet.type == PET_DEVIL && pet.evoStage == 1);
  const bool devilAdult = (pet.type == PET_DEVIL && pet.evoStage == 2);
  const bool devilElder = (pet.type == PET_DEVIL && pet.evoStage == 3);

  if (alienBaby || alienTeen || alienAdult || alienElder || eldritchBaby || eldritchTeen || eldritchAdult ||
      eldritchElder || devilBaby || devilTeen || devilAdult || devilElder)
  {
    switch (s_state)
    {
    case FishingState::CASTING:
      if (alienElder)
        return "/raising_hell/graphics/activities/fishing/al/ed/al_ed_fsh_anim4.png";

      if (devilElder)
        return "/raising_hell/graphics/activities/fishing/dev/ed/dev_eld_fsh_anim4.png";

      if (devilAdult)
        return "/raising_hell/graphics/activities/fishing/dev/ad/dev_ad_fsh_anim4.png";

      if (devilTeen)
        return "/raising_hell/graphics/activities/fishing/dev/tn/dev_tn_fsh_anim4.png";

      if (devilBaby)
        return "/raising_hell/graphics/activities/fishing/dev/bb/dev_bb_fsh_anim4.png";

      if (eldritchElder)
        return "/raising_hell/graphics/activities/fishing/ed/ed/eld_ed_fsh_anim4.png";

      if (eldritchAdult)
        return "/raising_hell/graphics/activities/fishing/ed/ad/eld_ad_fsh_anim4.png";

      if (eldritchTeen)
        return "/raising_hell/graphics/activities/fishing/ed/tn/eld_tn_fsh_anim4.png";

      if (eldritchBaby)
        return "/raising_hell/graphics/activities/fishing/ed/bb/eld_bb_fsh_anim4.png";

      if (alienAdult)
        return "/raising_hell/graphics/activities/fishing/al/ad/al_ad_fsh_anim4.png";

      return alienTeen ? "/raising_hell/graphics/activities/fishing/al/tn/al_tn_fsh_anim4.png"
                       : "/raising_hell/graphics/activities/fishing/al/bb/al_bb_fsh_anim4.png";

    case FishingState::LINE_OUT:
    case FishingState::BITE:
      if (devilElder)
        return "/raising_hell/graphics/activities/fishing/dev/ed/dev_eld_fsh_anim2.png";

      if (devilAdult)
        return "/raising_hell/graphics/activities/fishing/dev/ad/dev_ad_fsh_anim2.png";

      if (devilTeen)
        return "/raising_hell/graphics/activities/fishing/dev/tn/dev_tn_fsh_anim2.png";

      if (devilBaby)
        return "/raising_hell/graphics/activities/fishing/dev/bb/dev_bb_fsh_anim2.png";

      if (eldritchElder)
        return "/raising_hell/graphics/activities/fishing/ed/ed/eld_ed_fsh_anim2.png";

      if (eldritchAdult)
        return "/raising_hell/graphics/activities/fishing/ed/ad/eld_ad_fsh_anim2.png";

      if (eldritchTeen)
        return "/raising_hell/graphics/activities/fishing/ed/tn/eld_tn_fsh_anim2.png";

      if (eldritchBaby)
        return "/raising_hell/graphics/activities/fishing/ed/bb/eld_bb_fsh_anim2.png";

      if (alienElder)
        return "/raising_hell/graphics/activities/fishing/al/ed/al_ed_fsh_anim2.png";

      if (alienAdult)
        return "/raising_hell/graphics/activities/fishing/al/ad/al_ad_fsh_anim2.png";

      return alienTeen ? "/raising_hell/graphics/activities/fishing/al/tn/al_tn_fsh_anim2.png"
                       : "/raising_hell/graphics/activities/fishing/al/bb/al_bb_fsh_anim2.png";

    case FishingState::REELING:
      if (devilElder)
        return "/raising_hell/graphics/activities/fishing/dev/ed/dev_eld_fsh_anim3.png";

      if (devilAdult)
        return "/raising_hell/graphics/activities/fishing/dev/ad/dev_ad_fsh_anim3.png";

      if (devilTeen)
        return "/raising_hell/graphics/activities/fishing/dev/tn/dev_tn_fsh_anim3.png";

      if (devilBaby)
        return "/raising_hell/graphics/activities/fishing/dev/bb/dev_bb_fsh_anim3.png";

      if (eldritchElder)
        return "/raising_hell/graphics/activities/fishing/ed/ed/eld_ed_fsh_anim3.png";

      if (eldritchAdult)
        return "/raising_hell/graphics/activities/fishing/ed/ad/eld_ad_fsh_anim3.png";

      if (eldritchTeen)
        return "/raising_hell/graphics/activities/fishing/ed/tn/eld_tn_fsh_anim3.png";

      if (eldritchBaby)
        return "/raising_hell/graphics/activities/fishing/ed/bb/eld_bb_fsh_anim3.png";

      if (alienElder)
        return "/raising_hell/graphics/activities/fishing/al/ed/al_ed_fsh_anim3.png";

      if (alienAdult)
        return "/raising_hell/graphics/activities/fishing/al/ad/al_ad_fsh_anim3.png";

      return alienTeen ? "/raising_hell/graphics/activities/fishing/al/tn/al_tn_fsh_anim3.png"
                       : "/raising_hell/graphics/activities/fishing/al/bb/al_bb_fsh_anim3.png";

    case FishingState::POST_CATCH:
      if (s_showCatchPose)
      {
        if (devilElder)
          return "/raising_hell/graphics/activities/fishing/dev/ed/dev_eld_fsh_anim5.png";

        if (devilAdult)
          return "/raising_hell/graphics/activities/fishing/dev/ad/dev_ad_fsh_anim5.png";

        if (devilTeen)
          return "/raising_hell/graphics/activities/fishing/dev/tn/dev_tn_fsh_anim5.png";

        if (devilBaby)
          return "/raising_hell/graphics/activities/fishing/dev/bb/dev_bb_fsh_anim5.png";

        if (eldritchElder)
          return "/raising_hell/graphics/activities/fishing/ed/ed/eld_ed_fsh_anim5.png";

        if (eldritchAdult)
          return "/raising_hell/graphics/activities/fishing/ed/ad/eld_ad_fsh_anim5.png";

        if (eldritchTeen)
          return "/raising_hell/graphics/activities/fishing/ed/tn/eld_tn_fsh_anim5.png";

        if (eldritchBaby)
          return "/raising_hell/graphics/activities/fishing/ed/bb/eld_bb_fsh_anim5.png";

        if (alienElder)
          return "/raising_hell/graphics/activities/fishing/al/ed/al_ed_fsh_anim5.png";

        if (alienAdult)
          return "/raising_hell/graphics/activities/fishing/al/ad/al_ad_fsh_anim5.png";

        return alienTeen ? "/raising_hell/graphics/activities/fishing/al/tn/al_tn_fsh_anim5.png"
                         : "/raising_hell/graphics/activities/fishing/al/bb/al_bb_fsh_anim5.png";
      }

      if (devilElder)
        return "/raising_hell/graphics/activities/fishing/dev/ed/dev_eld_fsh_anim1.png";

      if (devilAdult)
        return "/raising_hell/graphics/activities/fishing/dev/ad/dev_ad_fsh_anim1.png";

      if (devilTeen)
        return "/raising_hell/graphics/activities/fishing/dev/tn/dev_tn_fsh_anim1.png";

      if (devilBaby)
        return "/raising_hell/graphics/activities/fishing/dev/bb/dev_bb_fsh_anim1.png";

      if (eldritchElder)
        return "/raising_hell/graphics/activities/fishing/ed/ed/eld_ed_fsh_anim1.png";

      if (eldritchAdult)
        return "/raising_hell/graphics/activities/fishing/ed/ad/eld_ad_fsh_anim1.png";

      if (eldritchTeen)
        return "/raising_hell/graphics/activities/fishing/ed/tn/eld_tn_fsh_anim1.png";

      if (eldritchBaby)
        return "/raising_hell/graphics/activities/fishing/ed/bb/eld_bb_fsh_anim1.png";

      if (alienElder)
        return "/raising_hell/graphics/activities/fishing/al/ed/al_ed_fsh_anim1.png";

      if (alienAdult)
        return "/raising_hell/graphics/activities/fishing/al/ad/al_ad_fsh_anim1.png";

      return alienTeen ? "/raising_hell/graphics/activities/fishing/al/tn/al_tn_fsh_anim1.png"
                       : "/raising_hell/graphics/activities/fishing/al/bb/al_bb_fsh_anim1.png";

    case FishingState::IDLE:
    default:
      if (devilElder)
        return "/raising_hell/graphics/activities/fishing/dev/ed/dev_eld_fsh_anim1.png";

      if (devilAdult)
        return "/raising_hell/graphics/activities/fishing/dev/ad/dev_ad_fsh_anim1.png";

      if (devilTeen)
        return "/raising_hell/graphics/activities/fishing/dev/tn/dev_tn_fsh_anim1.png";

      if (devilBaby)
        return "/raising_hell/graphics/activities/fishing/dev/bb/dev_bb_fsh_anim1.png";

      if (eldritchElder)
        return "/raising_hell/graphics/activities/fishing/ed/ed/eld_ed_fsh_anim1.png";

      if (eldritchAdult)
        return "/raising_hell/graphics/activities/fishing/ed/ad/eld_ad_fsh_anim1.png";

      if (eldritchTeen)
        return "/raising_hell/graphics/activities/fishing/ed/tn/eld_tn_fsh_anim1.png";

      if (eldritchBaby)
        return "/raising_hell/graphics/activities/fishing/ed/bb/eld_bb_fsh_anim1.png";

      if (alienElder)
        return "/raising_hell/graphics/activities/fishing/al/ed/al_ed_fsh_anim1.png";

      if (alienAdult)
        return "/raising_hell/graphics/activities/fishing/al/ad/al_ad_fsh_anim1.png";

      return alienTeen ? "/raising_hell/graphics/activities/fishing/al/tn/al_tn_fsh_anim1.png"
                       : "/raising_hell/graphics/activities/fishing/al/bb/al_bb_fsh_anim1.png";
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

  int drawX = kFishingPetAnchorX - (w / 2);
  int drawY = kFishingPetAnchorBottomY - h;

  if (pet.type == PET_DEVIL && (pet.evoStage == 2 || pet.evoStage == 3))
  {
    drawX -= 15;
    drawY += 5;
  }

  if (pet.type == PET_ALIEN && pet.evoStage == 3)
  {
    drawX -= 8;
    drawY += 4;
  }

  sprDrawPngFromSD(path, drawX, drawY);
}

static void scheduleNextBite(uint32_t now)
{
  s_nextBiteAtMs = now + random((long)kMinBiteDelayMs, (long)kMaxBiteDelayMs + 1L);
}

struct FishingLineOrigin
{
  int x;
  int y;
};

static FishingLineOrigin fishingLineOriginForPet()
{
  FishingLineOrigin origin = {
      (s_state == FishingState::REELING) ? kReelRodTipX : kRodTipX,
      (s_state == FishingState::REELING) ? kReelRodTipY : kRodTipY,
  };

  // -----------------------------------------------------------------------
  // Alien Teen
  // -----------------------------------------------------------------------
  if (pet.type == PET_ALIEN && pet.evoStage == 1)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 18;
      origin.y -= 3;
      break;

    case FishingState::CASTING:
      origin.x -= 28;
      origin.y += 2;
      break;

    default:
      origin.x -= 12;
      origin.y -= 1;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Alien Adult
  // -----------------------------------------------------------------------
  if (pet.type == PET_ALIEN && pet.evoStage == 2)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 8;
      origin.y -= 16;
      break;

    case FishingState::CASTING:
      origin.x -= 2;
      origin.y -= 10;
      break;

    default:
      origin.x -= 9;
      origin.y -= 2;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Alien Elder
  // -----------------------------------------------------------------------
  if (pet.type == PET_ALIEN && pet.evoStage == 3)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 19;
      origin.y -= 17;
      break;

    case FishingState::CASTING:
      origin.x -= 2;
      origin.y -= 10;
      break;

    default:
      origin.x -= 9;
      origin.y -= 2;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Devil Baby
  // -----------------------------------------------------------------------
  if (pet.type == PET_DEVIL && pet.evoStage == 0)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 14;
      origin.y -= 19;
      break;

    case FishingState::CASTING:
      origin.x -= 8;
      origin.y -= 8;
      break;

    default:
      origin.x -= 6;
      origin.y -= 6;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Devil Teen
  // -----------------------------------------------------------------------
  if (pet.type == PET_DEVIL && pet.evoStage == 1)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 19;
      origin.y -= 28;
      break;

    case FishingState::CASTING:
      origin.x -= 12;
      origin.y -= 5;
      break;

    default:
      origin.x += 5;
      origin.y += 24;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Devil Adult
  // -----------------------------------------------------------------------
  if (pet.type == PET_DEVIL && pet.evoStage == 2)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 20;
      origin.y -= 35;
      break;

    case FishingState::CASTING:
      origin.x += 8;
      origin.y -= 8;
      break;

    default:
      origin.x -= 3;
      origin.y -= 9;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Devil Elder
  // -----------------------------------------------------------------------
  if (pet.type == PET_DEVIL && pet.evoStage == 3)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 16;
      origin.y -= 40;
      break;

    case FishingState::CASTING:
      origin.x -= 10;
      origin.y -= 18;
      break;

    default:
      origin.x -= 10;
      origin.y -= 18;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Eldritch Baby
  // -----------------------------------------------------------------------
  if (pet.type == PET_ELDRITCH && pet.evoStage == 0)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 15;
      origin.y -= 34;
      break;

    case FishingState::CASTING:
      origin.x -= 8;
      origin.y -= 12;
      break;

    default:
      origin.x -= 8;
      origin.y -= 13;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Eldritch Teen
  // -----------------------------------------------------------------------
  if (pet.type == PET_ELDRITCH && pet.evoStage == 1)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 5;
      origin.y -= 15;
      break;

    case FishingState::CASTING:
      origin.x += 8;
      origin.y -= 16;
      break;

    default:
      origin.x += 5;
      origin.y -= 12;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Eldritch Adult
  // -----------------------------------------------------------------------
  if (pet.type == PET_ELDRITCH && pet.evoStage == 2)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x += 4;
      origin.y -= 20;
      break;

    case FishingState::CASTING:
      origin.x += 10;
      origin.y -= 18;
      break;

    default:
      origin.x += 7;
      origin.y -= 9;
      break;
    }
  }

  // -----------------------------------------------------------------------
  // Eldritch Elder
  // -----------------------------------------------------------------------
  if (pet.type == PET_ELDRITCH && pet.evoStage == 3)
  {
    switch (s_state)
    {
    case FishingState::REELING:
      origin.x -= 1;
      origin.y -= 19;
      break;

    case FishingState::CASTING:
      origin.x += 8;
      origin.y -= 20;
      break;

    default:
      origin.x += 3;
      origin.y -= 10;
      break;
    }
  }

  return origin;
}

static void drawFishingLine()
{
  const FishingLineOrigin origin = fishingLineOriginForPet();

  spr.drawLine(origin.x, origin.y, kBobberX, kBobberY, TFT_WHITE);
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
    return "/raising_hell/graphics/activities/fishing/ed/eld_fsh_bg.jpg";
  case PET_ALIEN:
    return "/raising_hell/graphics/activities/fishing/al/al_fsh_bg.jpg";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/activities/fishing/dev/dev_fsh_bg.jpg";
  }
}

static void beginCast()
{
  if (!g_app.inventory.hasItem(ITEM_FISHING_BAIT))
  {
    ui_showMessage("Need bait.");
    soundError();
    return;
  }

  g_app.inventory.removeItem(ITEM_FISHING_BAIT, 1);

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
  s_lastReward = (int)kFishingCatchXp;
  pet.addXP(kFishingCatchXp);

  s_sessionCatches++;

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
      // Reel in before a bite: return the unused bait.
      g_app.inventory.addItem(ITEM_FISHING_BAIT, 1);

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
    snprintf(buf, sizeof(buf), "Press Enter to cast, ESC to exit");
  }
  else if (s_state == FishingState::LINE_OUT)
  {
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
    snprintf(buf, sizeof(buf), "%u catches  +%u XP", (unsigned)s_sessionCatches,
             (unsigned)(s_sessionCatches * kFishingCatchXp));
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

static void drawBaitCounter()
{
  const int baitCount = g_app.inventory.countType(ITEM_FISHING_BAIT);

  char buf[24];
  std::snprintf(buf, sizeof(buf), "Bait: %d", baitCount);

  spr.setTextFont(2);
  spr.setTextSize(1);

  const int textW = spr.textWidth(buf);
  const int textH = spr.fontHeight();

  const int padX = 4;
  const int padY = 2;
  const int boxW = textW + padX * 2;
  const int boxH = textH + padY * 2;
  const int boxX = SCREEN_W - boxW - 4;
  const int boxY = TOP_BAR_H + 3;

  for (int y = boxY; y < boxY + boxH; ++y)
  {
    for (int x = boxX; x < boxX + boxW; ++x)
    {
      if (((x + y) & 1) == 0)
        spr.drawPixel(x, y, 0x4208);
    }
  }

  spr.drawRect(boxX, boxY, boxW, boxH, 0x630C);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE); // transparent text background
  spr.drawString(buf, boxX + padX, boxY + padY);

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
  drawBaitCounter();

  drawTopBar();
  drawFishingBottomBar();

  // HUD/status overlay temporarily disabled so the full background art is visible.
  spr.setTextDatum(TL_DATUM);
}