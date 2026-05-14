#include "activity_fishing.h"

#include <Arduino.h>

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
#include "save_manager.h"
#include "sound.h"
#include "ui_actions.h"

extern Pet pet;
extern M5Canvas spr;

namespace
{
enum class FishingState : uint8_t
{
  IDLE = 0,
  WAITING,
  REWARD
};

static FishingState s_state = FishingState::IDLE;
static uint32_t s_biteAtMs = 0;
static int s_lastReward = 0;

static constexpr int kFishingEnergyCost = 4;
static constexpr uint32_t kMinBiteDelayMs = 1800;
static constexpr uint32_t kMaxBiteDelayMs = 4200;

static const char *fishingBgForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/activities/fishing/eld_fishing_bg.jpg";
  case PET_ALIEN:
    return "/raising_hell/graphics/activities/fishing/al_fishing_bg.jpg";
  case PET_DEVIL:
  default:
    return "/raising_hell/graphics/activities/fishing/dev_fishing_bg.jpg";
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
  s_biteAtMs = now + random((long)kMinBiteDelayMs, (long)kMaxBiteDelayMs + 1L);
  s_state = FishingState::WAITING;

  soundClick();
  requestUIRedraw();
}

static void claimReward()
{
  s_lastReward = random(3, 13);
  addInf(s_lastReward);

  char msg[48];
  snprintf(msg, sizeof(msg), "Caught %d INF!", s_lastReward);
  ui_showMessage(msg);

  s_state = FishingState::REWARD;
  requestUIRedraw();
}

static const char *statusText()
{
  switch (s_state)
  {
  case FishingState::WAITING:
    return "Waiting for a bite...";
  case FishingState::REWARD:
    return "Nice catch! Cast again?";
  case FishingState::IDLE:
  default:
    return "Press Enter to cast";
  }
}
} // namespace

void activityFishingOnEnter()
{
  s_state = FishingState::IDLE;
  s_biteAtMs = 0;
  s_lastReward = 0;
}

void activityFishingHandle(InputState &in)
{
  if (in.escOnce || in.menuOnce || in.homeOnce)
  {
    uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_ACTIVITIES, true, in, 160);
    return;
  }

  const bool confirmOnce = in.selectOnce || in.encoderPressOnce || in.mgSelectOnce;

  if (confirmOnce)
  {
    if (s_state == FishingState::IDLE || s_state == FishingState::REWARD)
      beginCast();

    in.clearEdges();
    return;
  }

  if (s_state == FishingState::WAITING)
  {
    const uint32_t now = millis();
    if ((int32_t)(now - s_biteAtMs) >= 0)
      claimReward();
  }
}

void activityFishingDraw(bool redrawBg)
{
  (void)redrawBg;

  drawNonPetTabBackground();

  const char *bg = fishingBgForPet();
  if (bg && bg[0])
    sprDrawJpgFromSD(bg, 0, TOP_BAR_H);

  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;

  const int panelW = SCREEN_W - 18;
  const int panelH = 46;
  const int panelX = 9;
  const int panelY = contentY + contentH - panelH - 8;

  spr.fillRoundRect(panelX, panelY, panelW, panelH, 8, TFT_BLACK);
  spr.drawRoundRect(panelX, panelY, panelW, panelH, 8, uiModalOutline(pet.type));

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("Fishing", SCREEN_W / 2, panelY + 12);

  spr.setTextFont(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(statusText(), SCREEN_W / 2, panelY + 30);

  spr.setTextDatum(TL_DATUM);
}