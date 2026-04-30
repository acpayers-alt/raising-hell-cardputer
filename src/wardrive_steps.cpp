#include "wardrive_steps.h"

#include <math.h>

#include "app_state.h"
#include "currency.h"
#include "display.h"
#include "graphics.h"
#include "inventory.h"
#include "motion.h"
#include "save_manager.h"

static constexpr uint32_t kSampleScreenOnMs = 50;
static constexpr uint32_t kSamplePocketModeMs = 120;
static constexpr uint32_t kStepCooldownMs = 340;

static constexpr int kStepDeltaMg = 155;
static constexpr int kStepsPerWardriveRoll = 18;
static constexpr int kHitChancePct = 24;
static constexpr int kRareItemChancePct = 5;

static uint32_t s_lastSampleMs = 0;
static uint32_t s_lastStepMs = 0;

static uint32_t s_totalSteps = 0;
static uint32_t s_sessionSteps = 0;
static uint32_t s_stepsTowardRoll = 0;
static uint32_t s_sessionHits = 0;

static int s_baselineMg = 1000;
static bool s_haveBaseline = false;

static int s_pendingInf = 0;
static ItemType s_pendingItem = ITEM_NONE;
static uint8_t s_pendingHits = 0;

static bool wardriveStepTrackingAllowed()
{
  switch (g_app.uiState)
  {
  case UIState::PET_SCREEN:
  case UIState::PET_SLEEPING:
  case UIState::CLOCK_MODE:
  case UIState::TITLE_MENU:
  case UIState::INVENTORY:
  case UIState::SHOP:
  case UIState::SLEEP_MENU:
    return true;

  default:
    return false;
  }
}

static ItemType randomWardriveItem()
{
  switch (random(0, 4))
  {
  case 0:
    return ITEM_CURSED_RELIC;
  case 1:
    return ITEM_DEMON_BONE;
  case 2:
    return ITEM_RITUAL_CHALK;
  default:
    return ITEM_ELDRITCH_EYE;
  }
}

static void queueWardriveNotice(int infReward, ItemType itemReward)
{
  if (s_pendingHits < 255)
    s_pendingHits++;

  s_pendingInf += infReward;

  if (itemReward != ITEM_NONE)
    s_pendingItem = itemReward;
}

static void flushWardriveNoticeIfVisible()
{
  if (!isScreenOn())
    return;

  if (s_pendingHits == 0 && s_pendingInf == 0 && s_pendingItem == ITEM_NONE)
    return;

  char msg[96];

  if (s_pendingItem != ITEM_NONE)
  {
    const char *itemName = g_app.inventory.getItemLabelForType(s_pendingItem);
    snprintf(msg, sizeof(msg), "Wardrive hit!\nINF +%d\n%s +1", s_pendingInf,
             (itemName && itemName[0]) ? itemName : "ITEM");
  }
  else
  {
    snprintf(msg, sizeof(msg), "Wardrive hit!\nINF +%d", s_pendingInf);
  }

  ui_showMessage(msg);

  s_pendingHits = 0;
  s_pendingInf = 0;
  s_pendingItem = ITEM_NONE;
}

static void awardWardriveHit()
{
  const int infReward = random(4, 13);

  addInf(infReward);

  ItemType itemReward = ITEM_NONE;
  if (random(0, 100) < kRareItemChancePct)
  {
    itemReward = randomWardriveItem();
    g_app.inventory.addItem(itemReward, 1);
  }

  if (s_sessionHits < UINT32_MAX)
    s_sessionHits++;

  queueWardriveNotice(infReward, itemReward);

  Serial.printf("[WARDRIVE] fictional hit steps=%lu sessionHits=%lu INF=+%d item=%d\n", (unsigned long)s_sessionSteps,
                (unsigned long)s_sessionHits, infReward, (int)itemReward);
}

static void rollWardrive()
{
  if (random(0, 100) < kHitChancePct)
    awardWardriveHit();
}

void wardriveStepsTick(uint32_t nowMs)
{
  flushWardriveNoticeIfVisible();

  if (!motionAvailable)
    return;

  if (!wardriveStepTrackingAllowed())
  {
    s_haveBaseline = false;
    s_stepsTowardRoll = 0;
    return;
  }

  const uint32_t sampleMs = isScreenOn() ? kSampleScreenOnMs : kSamplePocketModeMs;
  if ((uint32_t)(nowMs - s_lastSampleMs) < sampleMs)
    return;

  s_lastSampleMs = nowMs;

  const MotionData m = readMotion();

  const int64_t ax = (int64_t)m.ax;
  const int64_t ay = (int64_t)m.ay;
  const int64_t az = (int64_t)m.az;
  const int magMg = (int)sqrtf((float)(ax * ax + ay * ay + az * az));

  if (!s_haveBaseline)
  {
    s_baselineMg = magMg;
    s_haveBaseline = true;
    return;
  }

  s_baselineMg = ((s_baselineMg * 15) + magMg) / 16;

  const int deltaMg = abs(magMg - s_baselineMg);
  if (deltaMg < kStepDeltaMg)
    return;

  if ((uint32_t)(nowMs - s_lastStepMs) < kStepCooldownMs)
    return;

  s_lastStepMs = nowMs;

  if (s_totalSteps < UINT32_MAX)
    s_totalSteps++;
  if (s_sessionSteps < UINT32_MAX)
    s_sessionSteps++;

  s_stepsTowardRoll++;
  if (s_stepsTowardRoll >= kStepsPerWardriveRoll)
  {
    s_stepsTowardRoll = 0;
    rollWardrive();
  }

  // Do not force immediate SD writes per step. Rewards mark the save dirty;
  // saveManagerTick() will handle normal persistence.
}

void wardriveStepsResetRuntime()
{
  s_lastSampleMs = 0;
  s_lastStepMs = 0;
  s_sessionSteps = 0;
  s_stepsTowardRoll = 0;
  s_sessionHits = 0;
  s_haveBaseline = false;
  s_pendingInf = 0;
  s_pendingItem = ITEM_NONE;
  s_pendingHits = 0;
}

uint32_t wardriveStepsTotal() { return s_totalSteps; }
uint32_t wardriveStepsSession() { return s_sessionSteps; }
uint32_t wardriveHitsSession() { return s_sessionHits; }