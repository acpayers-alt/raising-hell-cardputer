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
#include "sdcard.h"
#include <SD.h>

static const char *WARDRIVE_PATH = "/raising_hell/save/warwalk.bin";
static uint32_t s_lastPersistMs = 0;
static uint32_t s_lastPersistSteps = 0;

static constexpr uint32_t kSampleScreenOnMs = 50;
static constexpr uint32_t kSamplePocketModeMs = 120;
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

struct WarwalkPersist
{
  uint32_t magic;
  int32_t dayKey;
  uint32_t stepsToday;
  uint32_t hitsToday;
  uint16_t stepsTowardRoll;
};

static constexpr uint32_t WARWALK_MAGIC = 0x5757414C; // 'WWAL'

static bool loadWarwalkPersist()
{
  if (!g_sdReady)
    return false;

  if (!SD.exists(WARDRIVE_PATH))
  {
    Serial.println("[WARWALK] no persist file");
    return true;
  }

  File f = SD.open(WARDRIVE_PATH, FILE_READ);
  if (!f)
  {
    Serial.println("[WARWALK] load failed: open");
    return true;
  }

  if (f.size() != sizeof(WarwalkPersist))
  {
    Serial.printf("[WARWALK] load rejected: size=%u want=%u\n", (unsigned)f.size(), (unsigned)sizeof(WarwalkPersist));
    f.close();
    return true;
  }

  WarwalkPersist p{};
  const int r = f.read((uint8_t *)&p, sizeof(p));
  f.close();

  if (r != (int)sizeof(p) || p.magic != WARWALK_MAGIC)
  {
    Serial.println("[WARWALK] load rejected: bad read/magic");
    return true;
  }

  s_dayKey = p.dayKey;
  s_stepsToday = p.stepsToday;
  s_hitsToday = p.hitsToday;
  s_stepsTowardRoll = p.stepsTowardRoll;
  s_lastPersistSteps = s_stepsToday;

  Serial.printf("[WARWALK] loaded day=%d steps=%lu hits=%lu\n", s_dayKey, (unsigned long)s_stepsToday,
                (unsigned long)s_hitsToday);

  return true;
}

static void saveWarwalkPersist(bool force)
{
  if (!g_sdReady)
    return;

  const uint32_t now = millis();

  if (!force)
  {
    if ((uint32_t)(now - s_lastPersistMs) < 30000)
      return;

    if ((s_stepsToday - s_lastPersistSteps) < 10)
      return;
  }

  if (!SD.exists("/raising_hell"))
    SD.mkdir("/raising_hell");

  if (!SD.exists("/raising_hell/save"))
    SD.mkdir("/raising_hell/save");

  WarwalkPersist p{};
  p.magic = WARWALK_MAGIC;
  p.dayKey = s_dayKey;
  p.stepsToday = s_stepsToday;
  p.hitsToday = s_hitsToday;
  p.stepsTowardRoll = s_stepsTowardRoll;

  if (SD.exists(WARDRIVE_PATH))
    SD.remove(WARDRIVE_PATH);

  File f = SD.open(WARDRIVE_PATH, FILE_WRITE);
  if (!f)
  {
    Serial.println("[WARWALK] save failed: open");
    return;
  }

  const size_t w = f.write((const uint8_t *)&p, sizeof(p));
  f.flush();
  f.close();

  if (w != sizeof(p))
  {
    Serial.printf("[WARWALK] save short write got=%u want=%u\n", (unsigned)w, (unsigned)sizeof(p));
    return;
  }

  s_lastPersistMs = now;
  s_lastPersistSteps = s_stepsToday;

  Serial.printf("[WARWALK] saved day=%d steps=%lu hits=%lu\n", s_dayKey, (unsigned long)s_stepsToday,
                (unsigned long)s_hitsToday);
}

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
    saveWarwalkPersist(true);

    Serial.println("[WARWALK] daily counter reset");
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
    snprintf(msg, sizeof(msg), "War Walking hit!\n%u signals\nINF +%d\n%s +1", (unsigned)s_pendingHits, s_pendingInf,
             (itemName && itemName[0]) ? itemName : "ITEM");
  }
  else
  {
    snprintf(msg, sizeof(msg), "War Walking hit!\n%u signals\nINF +%d", (unsigned)s_pendingHits, s_pendingInf);
  }

  ui_showMessage(msg);

  s_pendingHits = 0;
  s_pendingInf = 0;
  s_pendingItem = ITEM_NONE;
  saveWarwalkPersist(true);
}

void wardriveStepsTick(uint32_t nowMs)
{
  resetIfNewDay();

  static bool s_loadedPersist = false;
  if (!s_loadedPersist)
  {
    if (!loadWarwalkPersist())
      return;

    s_loadedPersist = true;
    resetIfNewDay();
  }

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

  static float s_stepAccumulator = 0.0f;
  static constexpr float kStepScale = 1.7f;

  s_stepAccumulator += kStepScale;

  while (s_stepAccumulator >= 1.0f)
  {
    if (s_stepsToday < UINT32_MAX)
      s_stepsToday++;

    s_stepAccumulator -= 1.0f;
  }

  if (s_stepsTowardRoll < UINT16_MAX)
    s_stepsTowardRoll++;

  if (s_stepsTowardRoll >= kStepsPerWardriveRoll)
  {
    s_stepsTowardRoll = 0;
    rollWardrive();
  }
  saveWarwalkPersist(false);
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
  s_stepsToday = 0;
  s_stepQuietSinceMs = 0;
  saveWarwalkPersist(true);
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