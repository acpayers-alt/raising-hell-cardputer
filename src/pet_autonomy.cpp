#include "pet_autonomy.h"

#include "app_state.h"
#include "asset_ota.h"
#include "currency.h"
#include "display_state.h"
#include "graphics.h"
#include "pet.h"
#include "save_manager.h"
#include "sleep_state.h"
#include "ui_actions.h"

static constexpr uint32_t kAutonomyRollIntervalMs = 60UL * 60UL * 1000UL;

static constexpr int kPizzaCost = 25;
static constexpr int kPizzaHungerGain = 35;
static constexpr int kPizzaHappinessGain = 5;

static constexpr uint8_t kPizzaChancePct = 35;
static constexpr uint8_t kAutoSleepChancePct = 100;
static constexpr uint8_t kMischiefChancePct = 25;

static constexpr int kAutoSleepEnergyThreshold = 5;
static constexpr int kAutoSleepHealthThreshold = 20;
static constexpr int kAutoSleepTargetEnergy = 40;

static uint32_t s_lastPizzaRollMs = 0;
static uint32_t s_lastAutoSleepRollMs = 0;
static uint32_t s_lastMischiefRollMs = 0;

static uint8_t s_pizzaCount = 0;
static int16_t s_pizzaInfSpent = 0;

static uint8_t s_autoSleepCount = 0;

static uint8_t s_mischiefCount = 0;
static int16_t s_mischiefInfLost = 0;

static uint32_t s_lastNotifyMs = 0;

void petAutonomyReset()
{
  s_lastPizzaRollMs = 0;
  s_lastAutoSleepRollMs = 0;
  s_lastMischiefRollMs = 0;

  s_pizzaCount = 0;
  s_pizzaInfSpent = 0;
  s_autoSleepCount = 0;
  s_mischiefCount = 0;
  s_mischiefInfLost = 0;

  s_lastNotifyMs = 0;
}

static bool petAutonomyUiAllowsActions()
{
  switch (g_app.uiState)
  {
  case UIState::PET_SCREEN:
  case UIState::CLOCK_MODE:
  case UIState::INVENTORY:
  case UIState::SHOP:
  case UIState::SLEEP_MENU:
    return true;

  default:
    return false;
  }
}

static bool petAutonomyUiAllowsNotify()
{
  if (pet.isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested || g_app.sleepUntilAwakened)
  {
    return false;
  }

  switch (g_app.uiState)
  {
  case UIState::PET_SCREEN:
  case UIState::CLOCK_MODE:
    return true;

  default:
    return false;
  }
}

static bool petAutonomyOtaBusy()
{
  const AssetOtaStatus ota = assetOtaStatus();
  return ota == AssetOtaStatus::CHECKING || ota == AssetOtaStatus::DOWNLOADING || ota == AssetOtaStatus::INSTALLING;
}

static bool petAutonomyEligible()
{
  if (!petAutonomyUiAllowsActions())
    return false;

  if (petAutonomyOtaBusy())
    return false;

  if (pet.health <= 0)
    return false;

  if (g_app.inMiniGame || g_app.gameOver)
    return false;

  if (g_app.newPetFlowActive)
    return false;

  if (pet.isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested || g_app.sleepUntilAwakened)
  {
    return false;
  }

  return true;
}

static bool hourlyRollReady(bool conditionActive, uint32_t nowMs, uint32_t &lastRollMs)
{
  if (!conditionActive)
  {
    lastRollMs = 0;
    return false;
  }

  if (lastRollMs == 0)
  {
    lastRollMs = nowMs;
    return false;
  }

  if ((uint32_t)(nowMs - lastRollMs) < kAutonomyRollIntervalMs)
    return false;

  lastRollMs = nowMs;
  return true;
}

static bool rollPercent(uint8_t pct)
{
  if (pct == 0)
    return false;
  if (pct >= 100)
    return true;

  return random(100) < pct;
}

static void tallyPizza(int cost)
{
  if (s_pizzaCount < 255)
    s_pizzaCount++;

  const int total = (int)s_pizzaInfSpent + cost;
  s_pizzaInfSpent = (int16_t)constrain(total, 0, 32767);
}

static void tallyAutoSleep()
{
  if (s_autoSleepCount < 255)
    s_autoSleepCount++;
}

static void tallyMischief(int loss)
{
  if (s_mischiefCount < 255)
    s_mischiefCount++;

  const int total = (int)s_mischiefInfLost + loss;
  s_mischiefInfLost = (int16_t)constrain(total, 0, 32767);
}

static void doPizza()
{
  if (!spendInf(kPizzaCost))
  {
    Serial.printf("[PET][AUTO] pizza wanted but not enough INF have=%d cost=%d\n", getInf(), kPizzaCost);
    return;
  }

  const int oldHunger = pet.hunger;
  const int oldHappy = pet.happiness;

  pet.hunger = constrain(pet.hunger + kPizzaHungerGain, 0, 100);
  pet.happiness = constrain(pet.happiness + kPizzaHappinessGain, 0, 100);

  tallyPizza(kPizzaCost);
  saveManagerMarkDirty();
  requestUIRedraw();

  Serial.printf("[PET][AUTO] pizza name='%s' INF=-%d hunger %d->%d happiness %d->%d\n", pet.getName(), kPizzaCost,
                oldHunger, pet.hunger, oldHappy, pet.happiness);
}

static void doAutoSleep()
{
  tallyAutoSleep();

  saveManagerEnterSleepState();

  // Bad sleep: the pet only sleeps itself to a low recovery target instead of
  // using the player's nicer "until rested" sleep flow.
  g_app.sleepTargetEnergy = kAutoSleepTargetEnergy;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepingByTimer = false;
  g_app.sleepStartTime = millis();
  g_app.sleepDurationMs = 0;

  saveManagerMarkDirty();
  requestUIRedraw();

  Serial.printf("[PET][AUTO] passed out name='%s' targetEnergy=%d energy=%d\n", pet.getName(), kAutoSleepTargetEnergy,
                pet.energy);
}

static void doMischief()
{
  const int have = getInf();
  if (have <= 0)
  {
    Serial.printf("[PET][AUTO] mischief skipped no INF name='%s'\n", pet.getName());
    return;
  }

  const int wantedLoss = (int)random(5, 16); // 5..15
  const int loss = constrain(wantedLoss, 1, have);

  if (!spendInf(loss))
    return;

  tallyMischief(loss);
  saveManagerMarkDirty();
  requestUIRedraw();

  Serial.printf("[PET][AUTO] mischief name='%s' INF=-%d remaining=%d\n", pet.getName(), loss, getInf());
}

void petAutonomyTick(uint32_t nowMs)
{
  if (!petAutonomyEligible())
    return;

  const bool starving = (pet.hunger <= 10);
  const bool exhausted = (pet.energy <= kAutoSleepEnergyThreshold);
  const bool criticallyStarving = (pet.hunger <= 0 && pet.health <= kAutoSleepHealthThreshold);
  const bool shouldPassOut = exhausted || criticallyStarving;
  const bool angry = (pet.getMood() == MOOD_MAD);

  if (hourlyRollReady(starving, nowMs, s_lastPizzaRollMs) && rollPercent(kPizzaChancePct))
  {
    doPizza();
  }

  if (hourlyRollReady(shouldPassOut, nowMs, s_lastAutoSleepRollMs) && rollPercent(kAutoSleepChancePct))
  {
    doAutoSleep();
    return;
  }

  if (hourlyRollReady(angry, nowMs, s_lastMischiefRollMs) && rollPercent(kMischiefChancePct))
  {
    doMischief();
  }
}

static bool hasPendingSummary() { return s_pizzaCount || s_autoSleepCount || s_mischiefCount; }

static void clearPendingSummary()
{
  s_pizzaCount = 0;
  s_pizzaInfSpent = 0;
  s_autoSleepCount = 0;
  s_mischiefCount = 0;
  s_mischiefInfLost = 0;
}

void petAutonomyNotifyIfPending(uint32_t nowMs)
{
  if (!hasPendingSummary())
    return;

  if (!petAutonomyUiAllowsNotify())
    return;

  // Avoid fighting other short-lived UI messages right after state transitions.
  if (s_lastNotifyMs != 0 && (uint32_t)(nowMs - s_lastNotifyMs) < 5000UL)
    return;

  char msg[128];
  msg[0] = '\0';

  bool first = true;

  if (s_pizzaCount)
  {
    snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "%sPizza -%d INF", first ? "" : "\n", (int)s_pizzaInfSpent);
    first = false;
  }

  if (s_mischiefCount)
  {
    snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "%sMischief -%d INF", first ? "" : "\n",
             (int)s_mischiefInfLost);
    first = false;
  }

  if (s_autoSleepCount)
  {
    snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "%s%s passed out", first ? "" : "\n", pet.getName());
  }

  ui_showTimedMessage(msg, 3200);

  Serial.printf("[PET][AUTO] summary pizzaCount=%u pizzaInf=%d mischiefCount=%u mischiefInf=%d autoSleep=%u\n",
                (unsigned)s_pizzaCount, (int)s_pizzaInfSpent, (unsigned)s_mischiefCount, (int)s_mischiefInfLost,
                (unsigned)s_autoSleepCount);

  clearPendingSummary();
  s_lastNotifyMs = nowMs;
}