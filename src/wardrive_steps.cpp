#include "wardrive_steps.h"

#include <math.h>
#include <time.h>

#include "app_state.h"
#include "currency.h"
#include "display.h"
#include "graphics.h"
#include "inventory.h"
#include "motion.h"
#include "save_manager.h"

static constexpr uint32_t kSampleScreenOnMs = 50;
static constexpr uint32_t kSamplePocketModeMs = 120;
static constexpr int kStepDeltaMg = 320;
static constexpr uint32_t kStepCooldownMs = 680;
static constexpr uint16_t kStepsPerWardriveRoll = 18;
static constexpr uint8_t kHitChancePct = 22;
static constexpr uint8_t kRareItemChancePct = 5;
static bool s_stepArmed = true;
static uint32_t s_stepQuietSinceMs = 0;

static uint32_t s_lastSampleMs = 0;
static uint32_t s_lastStepMs = 0;

static uint32_t s_stepsToday = 0;
static uint32_t s_hitsToday = 0;
static uint16_t s_stepsTowardRoll = 0;
static constexpr int kStepHighMg = 520;
static constexpr int kStepLowMg = 100;
static constexpr uint32_t kStepRearmQuietMs = 120;

static int s_baselineMg = 1000;
static bool s_haveBaseline = false;

static int s_dayKey = -1;

static uint16_t s_pendingHits = 0;
static int s_pendingInf = 0;
static ItemType s_pendingItem = ITEM_NONE;

static bool wardriveStepTrackingAllowed()
{
  switch (g_app.uiState)
  {
  case UIState::PET_SCREEN:
  case UIState::PET_SLEEPING:
  case UIState::CLOCK_MODE:
  case UIState::TITLE_MENU:
  case UIState::SETTINGS:
  case UIState::INVENTORY:
  case UIState::SHOP:
  case UIState::SLEEP_MENU:
    return true;

  default:
    return false;
  }
}

static int currentLocalDayKey()
{
  const time_t now = time(nullptr);
  if (now < 1700000000)
    return -1;

  struct tm lt;
  if (!localtime_r(&now, &lt))
    return -1;

  return ((lt.tm_year + 1900) * 1000) + lt.tm_yday;
}

static void resetIfNewDay()
{
  const int dayKey = currentLocalDayKey();
  if (dayKey < 0)
    return;

  if (s_dayKey < 0)
  {
    s_dayKey = dayKey;
    return;
  }

  if (dayKey != s_dayKey)
  {
    s_dayKey = dayKey;
    s_stepsToday = 0;
    s_hitsToday = 0;
    s_stepsTowardRoll = 0;
    s_pendingHits = 0;
    s_pendingInf = 0;
    s_pendingItem = ITEM_NONE;
    s_haveBaseline = false;

    Serial.println("[WARDRIVE] daily counter reset");
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
  if (s_pendingHits < 65535)
    s_pendingHits++;

  s_pendingInf += infReward;

  if (itemReward != ITEM_NONE)
    s_pendingItem = itemReward;
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
    saveManagerMarkDirty();
  }

  if (s_hitsToday < UINT32_MAX)
    s_hitsToday++;

  queueWardriveNotice(infReward, itemReward);

  Serial.printf("[WARDRIVE] fictional hit stepsToday=%lu hitsToday=%lu INF=+%d item=%d\n", (unsigned long)s_stepsToday,
                (unsigned long)s_hitsToday, infReward, (int)itemReward);
}

static void rollWardrive()
{
  if (random(0, 100) < kHitChancePct)
    awardWardriveHit();
}

void wardriveStepsNotifyUserActivity()
{
  if (!isScreenOn())
    return;

  if (s_pendingHits == 0)
    return;

  char msg[96];

  if (s_pendingItem != ITEM_NONE)
  {
    const char *itemName = g_app.inventory.getItemLabelForType(s_pendingItem);
    snprintf(msg, sizeof(msg), "Wardriving hit!\n%u signals\nINF +%d\n%s +1", (unsigned)s_pendingHits, s_pendingInf,
             (itemName && itemName[0]) ? itemName : "ITEM");
  }
  else
  {
    snprintf(msg, sizeof(msg), "Wardriving hit!\n%u signals\nINF +%d", (unsigned)s_pendingHits, s_pendingInf);
  }

  ui_showMessage(msg);

  s_pendingHits = 0;
  s_pendingInf = 0;
  s_pendingItem = ITEM_NONE;
}

void wardriveStepsTick(uint32_t nowMs)
{
  resetIfNewDay();

  // War Walking is always active under the normal safety/state gates.
  // The Game Options "Step Counter" toggle only controls badge visibility.
  if (!motionAvailable)
    return;

  if (!wardriveStepTrackingAllowed())
  {
    s_haveBaseline = false;
    s_stepArmed = true;
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

  // Re-arm only after motion settles. This prevents one bump/wobble
  // from counting as several steps.
  if (!s_stepArmed)
  {
    if (deltaMg <= kStepLowMg)
    {
      if (s_stepQuietSinceMs == 0)
        s_stepQuietSinceMs = nowMs;

      if ((uint32_t)(nowMs - s_stepQuietSinceMs) >= kStepRearmQuietMs)
        s_stepArmed = true;
    }
    else
    {
      s_stepQuietSinceMs = 0;
    }

    return;
  }

  if (deltaMg < kStepHighMg)
    return;

  if ((uint32_t)(nowMs - s_lastStepMs) < kStepCooldownMs)
    return;

  s_stepArmed = false;
  s_stepQuietSinceMs = 0;
  s_lastStepMs = nowMs;

  if (s_stepsToday < UINT32_MAX)
    s_stepsToday++;

  if (s_stepsTowardRoll < UINT16_MAX)
    s_stepsTowardRoll++;

  if (s_stepsTowardRoll >= kStepsPerWardriveRoll)
  {
    s_stepsTowardRoll = 0;
    rollWardrive();
  }
}

void wardriveStepsResetRuntime()
{
  s_lastSampleMs = 0;
  s_lastStepMs = 0;
  s_stepsTowardRoll = 0;
  s_hitsToday = 0;
  s_haveBaseline = false;
  s_pendingInf = 0;
  s_pendingItem = ITEM_NONE;
  s_pendingHits = 0;
}

uint32_t wardriveStepsToday()
{
  resetIfNewDay();
  return s_stepsToday;
}

uint32_t wardriveHitsToday()
{
  resetIfNewDay();
  return s_hitsToday;
}