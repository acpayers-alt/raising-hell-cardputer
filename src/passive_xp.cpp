#include "passive_xp.h"

#include "app_state.h"
#include "asset_ota.h"
#include "game_options_state.h"
#include "pet.h"

static constexpr uint32_t kPassiveXpIntervalMs = 5UL * 60UL * 1000UL;

static uint32_t s_lastPassiveXpMs = 0;

void passiveXpResetTimer(uint32_t nowMs)
{
  s_lastPassiveXpMs = nowMs;
}

static bool passiveXpUiEligible(UIState s)
{
  switch (s)
  {
  case UIState::PET_SCREEN:
  case UIState::CLOCK_MODE:
    return true;

  default:
    return false;
  }
}

static uint16_t passiveXpMoodMultiplierPct(PetMood mood)
{
  switch (mood)
  {
  case MOOD_HAPPY:
    return 150;

  case MOOD_BORED:
    return 100;

  case MOOD_TIRED:
    return 60;

  case MOOD_HUNGRY:
    return 50;

  case MOOD_MAD:
    return 35;

  case MOOD_SICK:
    return 10;

  default:
    return 100;
  }
}

static bool passiveXpHasPerfectCareBonus()
{
  return pet.health >= 80 &&
         pet.hunger >= 80 &&
         pet.happiness >= 80 &&
         pet.energy >= 60 &&
         pet.getMood() == MOOD_HAPPY;
}

static bool passiveXpEligible()
{
  if (!passiveXpEnabled)
    return false;

  if (!passiveXpUiEligible(g_app.uiState))
    return false;
    
  if (pet.health <= 0)
    return false;

  const AssetOtaStatus ota = assetOtaStatus();
  if (ota == AssetOtaStatus::CHECKING ||
      ota == AssetOtaStatus::DOWNLOADING ||
      ota == AssetOtaStatus::INSTALLING)
  {
    return false;
  }

  return true;
}

static uint32_t passiveXpComputeAward()
{
  const uint32_t next = pet.xpForNextLevel();

  // Roughly 1/60th of the next level requirement per 5-minute tick.
  // At neutral care this is about one passive level every ~5 hours.
  uint32_t base = next / 60UL;
  if (base < 3)
    base = 3;
  if (base > 250)
    base = 250;

  const PetMood mood = pet.getMood();
  uint32_t multPct = passiveXpMoodMultiplierPct(mood);

  if (passiveXpHasPerfectCareBonus())
    multPct += 25;

  uint32_t award = (base * multPct) / 100UL;
  if (award < 1)
    award = 1;

  return award;
}

void passiveXpTick(uint32_t nowMs)
{
  if (s_lastPassiveXpMs == 0)
  {
    s_lastPassiveXpMs = nowMs;
    return;
  }

  if ((uint32_t)(nowMs - s_lastPassiveXpMs) < kPassiveXpIntervalMs)
    return;

  // Reset timer even if ineligible. This prevents banking passive XP while
  // sitting in console/provisioning/menus and receiving a delayed payout later.
  s_lastPassiveXpMs = nowMs;

  if (!passiveXpEligible())
    return;

  const PetMood mood = pet.getMood();
  const uint32_t award = passiveXpComputeAward();
  if (award == 0)
    return;

  pet.addXP(award);

  Serial.printf("[XP] passive +%lu lvl=%u mood=%u next=%lu hp=%d hunger=%d happy=%d energy=%d\n",
                (unsigned long)award,
                (unsigned)pet.level,
                (unsigned)mood,
                (unsigned long)pet.xpForNextLevel(),
                pet.health,
                pet.hunger,
                pet.happiness,
                pet.energy);
}