#include "pet.h"

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>

#include "app_state.h"
#include "ui_runtime.h"
#include "ui_state_utils.h"

#include "console.h"
#include "death_state.h"
#include "feed_actions.h"
#include "game_options_state.h"
#include "graphics.h"
#include "input.h"
#include "mini_games.h"
#include "pet_state.h"
#include "save_manager.h"
#include "sleep_state.h"
#include "ui_actions.h"
#include "user_toggles_state.h"

// -----------------------------------------------------------------------------
// PRE-BIRTH GUARD
// If we're in the new-pet flow (egg select / hatching / naming / boot wizard),
// the pet is not "real" yet, so:
//  - NO decay
//  - NO death transitions
//  - NO sleep tick / sleep flags
// Also: reset accumulators so we don't "catch up" a huge dt on first real frame.
// -----------------------------------------------------------------------------
static inline bool isPreBirthUiState(UIState s)
{
  switch (s)
  {
  case UIState::CHOOSE_PET:
  case UIState::HATCHING:
  case UIState::NAME_PET:
  case UIState::CONTROLS_HELP:
  case UIState::WHATS_NEW:
  case UIState::BOOT_WIFI_PROMPT:
  case UIState::BOOT_WIFI_WAIT:
  case UIState::BOOT_TZ_PICK:
  case UIState::BOOT_NTP_WAIT:
    return true;
  default:
    return false;
  }
}

// -----------------------------------------------------------------------------
// Pet::update() timing state
//
// These used to be function-local statics inside Pet::update(). That caused a
// large dt to accumulate whenever Pet::update() wasn't called (e.g. during the
// death screen / resurrection mini-game). On the first frame back, the decay
// loops could advance many "steps" at once and immediately kill the pet again.
//
// Keep these at file-scope so we can reset them when resurrecting.
// -----------------------------------------------------------------------------
static uint32_t s_petLastUpdateMs = 0;
static uint32_t s_hungerAccMs = 0;
static uint32_t s_energyAccMs = 0;
static uint32_t s_moodAccMs = 0;
static uint32_t s_hpAccMs = 0;
static uint32_t s_regenAccMs = 0;

void petResetUpdateTimers()
{
  const uint32_t now = (uint32_t)millis();
  s_petLastUpdateMs = now;
  s_hungerAccMs = 0;
  s_energyAccMs = 0;
  s_moodAccMs = 0;
  s_hpAccMs = 0;
  s_regenAccMs = 0;
}

// ------------------------------------------------------------
// Pet-O-Matic
// ------------------------------------------------------------

static const char *getSpritePathForTypeAndStage(PetType type, uint8_t stage);

Pet::Pet()
    : type(PET_DEVIL), hunger(0), happiness(100), energy(100), health(100), inf(0), birth_epoch(0), petId(0),
      isSleeping(false), lastFedTime(0), x(0), y(0), width(0), height(0), level(1), xp(0), evoStage(0)
{
  name[0] = '\0';
  strncpy(spritePath, getSpritePathForTypeAndStage(PET_DEVIL, 0), sizeof(spritePath));
  spritePath[sizeof(spritePath) - 1] = '\0';
}

// ------------------------------------------------------------
// Mood resolver (priority: sick > tired > hungry > mad > bored > happy)
// ------------------------------------------------------------
// Thresholds (0..100). Tweak freely.
// Match STAT tab thresholds so animations/bio/status are consistent.
static constexpr int MOOD_SICK_HEALTH_MAX = 60;   // <60 = Sick (STAT tab uses 60)
static constexpr int MOOD_TIRED_ENERGY_MAX = 10;  // <=30 = Tired
static constexpr int MOOD_HUNGRY_HUNGER_MAX = 10; // <=30 = Hungry
static constexpr int MOOD_MAD_HAPPY_MAX = 10;     // <=30 = Angry

// If not sick/tired/hungry/angry:
static constexpr int MOOD_BORED_HAPPY_MAX = 59; // <60 = Bored

PetMood petResolveMood(const Pet &p)
{
  if (p.health <= MOOD_SICK_HEALTH_MAX)
    return MOOD_SICK;
  if (p.energy <= MOOD_TIRED_ENERGY_MAX)
    return MOOD_TIRED;
  if (p.hunger <= MOOD_HUNGRY_HUNGER_MAX)
    return MOOD_HUNGRY;
  if (p.happiness <= MOOD_MAD_HAPPY_MAX)
    return MOOD_MAD;
  if (p.happiness < MOOD_BORED_HAPPY_MAX + 1)
    return MOOD_BORED; // <60 bored
  return MOOD_HAPPY;
}

static inline uint32_t applyDecayModeToStep(uint32_t baseMs)
{
  const uint8_t m = saveManagerGetDecayMode(); // 0..5
  if (baseMs == 0)
    return 0;

  switch (m)
  {
  default:
  case 2: // Normal => same speed as old Fast => steps take half as long
    return (baseMs <= 1) ? 1 : (baseMs / 2);

  case 0: // Super Slow => steps take 4x longer
    return (baseMs > (UINT32_MAX / 4)) ? UINT32_MAX : (baseMs * 4);

  case 1: // Slow => steps take 2x longer
    return (baseMs > (UINT32_MAX / 2)) ? UINT32_MAX : (baseMs * 2);

  case 3: // Fast => 4x speed => steps take quarter as long
    return (baseMs <= 3) ? 1 : (baseMs / 4);

  case 4: // Super Fast => 8x speed => steps take eighth as long
    return (baseMs <= 7) ? 1 : (baseMs / 8);

  case 5: // Insane => 32x speed => steps take 1/32 as long (UNCHANGED)
    return (baseMs <= 31) ? 1 : (baseMs / 32);
  }
}

uint8_t Pet::getSleepQualityScore() const
{
  int score = 50;

  // Hunger influence
  if (hunger < 25)
    score -= 20;
  else if (hunger < 50)
    score -= 10;
  else if (hunger > 75)
    score += 10;

  // Mood influence (priority-based mood resolver)
  const PetMood mood = getMood();
  switch (mood)
  {
  case MOOD_HAPPY:
    score += 15;
    break;
  case MOOD_BORED:
    score += 5;
    break;

  case MOOD_MAD:
    score -= 15;
    break;
  case MOOD_HUNGRY:
    score -= 10;
    break;
  case MOOD_TIRED:
    score -= 10;
    break;

  case MOOD_SICK:
    score -= 30;
    break;
  default:
    break;
  }

  // Health influence (gentle)
  score += (health - 50) / 5; // roughly -10..+10

  if (score < 0)
    score = 0;
  if (score > 100)
    score = 100;
  return (uint8_t)score;
}

uint16_t Pet::getSleepQualityMultQ8() const
{
  // Map score 0..100 to multiplier 0.5x..1.5x in Q8:
  // 0.5x = 128, 1.0x = 256, 1.5x = 384
  const uint8_t score = getSleepQualityScore();
  return (uint16_t)(128U + (((uint32_t)score * 256UL) / 100UL));
}

const char *Pet::getSleepQualityLabel() const
{
  const uint8_t s = getSleepQualityScore();
  if (s >= 75)
    return "GREAT";
  if (s >= 50)
    return "OK";
  if (s >= 30)
    return "POOR";
  return "AWFUL";
}

static uint32_t xpNeededForLevel(uint16_t lvl)
{
  (void)lvl;
  // Unused currently (kept for future curve tuning)
  return 0;
}

static uint16_t evoMinLevelForStage(uint8_t stageNext)
{
  // stageNext is the stage you are trying to reach (1..3)
  switch (stageNext)
  {
  case 1:
    return 10; // teen
  case 2:
    return 20; // adult
  case 3:
    return 30; // elder
  default:
    return 0;
  }
}

// -----------------------------------------------------
// Sleep tick (member)
// -----------------------------------------------------
static constexpr int kSleepHealthRecoveryFloor = 61;
static bool s_sleepRecoveryMessageShown = false;

void Pet::petSleepTick()
{
  // Pre-birth: never allow sleep flags or sleep tick to run.
  if (isPreBirthUiState(g_app.uiState))
  {
    if (this->isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested ||
        g_app.sleepUntilAwakened)
    {
      this->isSleeping = false;
      g_app.isSleeping = false;
      g_app.sleepingByTimer = false;
      g_app.sleepUntilRested = false;
      g_app.sleepUntilAwakened = false;
      g_app.sleepTargetEnergy = 0;
      g_app.sleepStartTime = 0;
      g_app.sleepDurationMs = 0;

      saveManagerClearSleepPendingFlag();
      saveManagerMarkDirty();
      requestUIRedraw();
    }
    return;
  }

  // Sleep transitions are owned by explicit UI/state actions.
  // Do not auto-wake here just because we are temporarily not in PET_SLEEPING.
  const UIState ui = g_app.uiState;
  const bool allowSleepUi = (ui == UIState::PET_SLEEPING) || (ui == UIState::POWER_MENU) ||
                            (ui == UIState::PET_SCREEN) || (ui == UIState::TITLE_MENU) || isSettingsState(ui);

  if (!allowSleepUi)
  {
    return;
  }

  static bool s_sleepFlagLatched = false;
  static uint32_t lastSleepUpdate = 0;
  static uint32_t sleepAccMs = 0;
  static uint8_t sleepSubTick = 0;  // gate small sleep effects
  static uint8_t sleepHealTick = 0; // gate HP regen during sleep

  if (!g_app.isSleeping)
  {
    s_sleepFlagLatched = false;
    lastSleepUpdate = 0;
    sleepAccMs = 0;
    sleepSubTick = 0;
    sleepHealTick = 0;
    s_sleepRecoveryMessageShown = false;
    return;
  }

  // If the persisted flag already exists, this is either:
  //   - a sleep restored from boot, or
  //   - an already-established sleeping session.
  // In either case, do NOT re-set the flag or mark dirty again.

  if (saveManagerSleepPendingFlagExists())
  {
    s_sleepFlagLatched = true;
  }
  else if (!s_sleepFlagLatched)
  {
    saveManagerSetSleepPendingFlag();
    saveManagerMarkDirty();
#if !PUBLIC_BUILD
    Serial.println("[SLEEP] entered → flag set");
#endif

    s_sleepFlagLatched = true;
  }

  bool changed = false;

  uint32_t now = millis();
  if (lastSleepUpdate == 0)
    lastSleepUpdate = now;

  uint32_t dt = now - lastSleepUpdate;
  lastSleepUpdate = now;

  sleepAccMs += dt;

  // ---------------------------------------------------------------------------
  // Sleep rest recovery rate (energy) scales by sleep quality.
  // Baseline: 8 hours -> 100 energy.
  //
  // Keep sleep-quality tuning centralized in:
  //   getSleepQualityScore()
  //   getSleepQualityMultQ8()
  //   getSleepQualityLabel()
  // ---------------------------------------------------------------------------
  const uint32_t baseSleepRateMs = (8UL * 60UL * 60UL * 1000UL) / 100UL;
  const uint32_t baseRateMs = applyDecayModeToStep(baseSleepRateMs);
  const uint16_t multQ8 = getSleepQualityMultQ8();

  // Effective ms per +1 energy:
  uint32_t sleepRateMs = (uint32_t)(((uint64_t)baseRateMs * 256ULL) / (uint64_t)multQ8);

  // Floors: never stall; never go absurdly fast
  const uint32_t minBenefitMs = baseRateMs * 4UL; // no slower than 25% baseline speed
  if (sleepRateMs > minBenefitMs)
    sleepRateMs = minBenefitMs;
  if (sleepRateMs < 250UL)
    sleepRateMs = 250UL;

  while (sleepAccMs >= sleepRateMs)
  {
    sleepAccMs -= sleepRateMs;

    if (energy < 100)
    {
      energy++;
      changed = true;
    }

    // HP regen during sleep (since Pet::update() does not run while sleeping).
    //
    // Sleep always restores enough health to clear Sick, even if the pet is
    // starving. Above that recovery floor, normal sleep regen still requires
    // basic care so sleep does not become a free full-heal button.
    const bool needsRecoveryFloor = (health < kSleepHealthRecoveryFloor);
    const bool canBonusSleepRegen = (hunger >= 25 && energy >= 10);

    if (needsRecoveryFloor || (canBonusSleepRegen && health < 100))
    {
      if (sleepHealTick < 255)
        sleepHealTick++;

      if (sleepHealTick >= 2)
      {
        sleepHealTick = 0;

        const int oldHealth = health;

        if (needsRecoveryFloor)
        {
          health++;
          if (health > kSleepHealthRecoveryFloor)
            health = kSleepHealthRecoveryFloor;
        }
        else
        {
          health++;
          if (health > 100)
            health = 100;
        }

        changed = (health != oldHealth) || changed;

        Serial.printf("[PET] sleep health recovery %d->%d floor=%d hunger=%d energy=%d\n", oldHealth, health,
                      kSleepHealthRecoveryFloor, hunger, energy);

        if (!s_sleepRecoveryMessageShown && oldHealth < kSleepHealthRecoveryFloor &&
            health >= kSleepHealthRecoveryFloor)
        {
          s_sleepRecoveryMessageShown = true;
        }
      }
    }
    else
    {
      sleepHealTick = 0;
    }

    // Every 2 sleep energy ticks: +1 happiness, -1 hunger (with hunger floor 25)
    if (sleepSubTick < 255)
      sleepSubTick++;
    if (sleepSubTick >= 2)
    {
      sleepSubTick = 0;

      if (happiness < 100)
      {
        happiness++;
        changed = true;
      }

      if (hunger > 25)
      {
        hunger--;
        changed = true;
      }
    }

    // Auto-sleep / bad sleep ends at a low target energy.
    if (g_app.sleepTargetEnergy > 0 && energy >= g_app.sleepTargetEnergy)
    {
      if (energy > g_app.sleepTargetEnergy)
        energy = g_app.sleepTargetEnergy;

      this->isSleeping = false;
      g_app.isSleeping = false;
      g_app.sleepingByTimer = false;

      g_app.sleepUntilRested = false;
      g_app.sleepUntilAwakened = false;
      g_app.sleepTargetEnergy = 0;
      g_app.sleepStartTime = 0;
      g_app.sleepDurationMs = 0;

      saveManagerClearSleepPendingFlag();
      saveManagerMarkDirty();
      requestUIRedraw();

      if (g_app.uiState == UIState::PET_SLEEPING)
      {
        graphicsReleaseUiCachesForMiniGame();
        uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
        requestFullUIRedraw();
        forceRenderUIOnce();
      }

      return;
    }

    // "Until rested" ends at full energy
    if (g_app.sleepUntilRested && energy >= 100)
    {
      energy = 100;

      this->isSleeping = false;
      g_app.isSleeping = false;
      g_app.sleepingByTimer = false;

      g_app.sleepUntilRested = false;
      g_app.sleepUntilAwakened = false;
      g_app.sleepTargetEnergy = 0;
      g_app.sleepStartTime = 0;
      g_app.sleepDurationMs = 0;

      saveManagerClearSleepPendingFlag();
      saveManagerMarkDirty();
      requestUIRedraw();

      if (g_app.uiState == UIState::PET_SLEEPING)
      {
        graphicsReleaseUiCachesForMiniGame();
        uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
        requestFullUIRedraw();
        forceRenderUIOnce();
      }
      return;
    }

    // Timer-based sleep ends when time is up
    if (g_app.sleepingByTimer)
    {
      if (g_app.sleepDurationMs > 0)
      {
        uint32_t elapsed = now - g_app.sleepStartTime;
        if (elapsed >= g_app.sleepDurationMs)
        {
          this->isSleeping = false;
          g_app.isSleeping = false;
          g_app.sleepingByTimer = false;

          g_app.sleepUntilRested = false;
          g_app.sleepUntilAwakened = false;
          g_app.sleepTargetEnergy = 0;
          g_app.sleepStartTime = 0;
          g_app.sleepDurationMs = 0;

          saveManagerClearSleepPendingFlag();
          saveManagerMarkDirty();
          requestUIRedraw();

          if (g_app.uiState == UIState::PET_SLEEPING)
          {
            graphicsReleaseUiCachesForMiniGame();
            uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
            requestFullUIRedraw();
            forceRenderUIOnce();
          }

          return;
        }
      }
    }
  }

  if (changed)
  {
    saveManagerMarkDirty();
    requestUIRedraw();
  }
}

// -------------------------------------------
// Sprite resolver based on pet type
// -------------------------------------------
static const char *getSpritePathForTypeAndStage(PetType type, uint8_t stage)
{
  if (type == PET_DEVIL)
  {
    switch (stage)
    {
    case 0:
      return "/graphics/pet/devil_baby_normal.jpg";
    case 1:
      return "/graphics/pet/devil_teen_normal.jpg";
    case 2:
      return "/graphics/pet/devil_adult_normal.jpg";
    case 3:
      return "/graphics/pet/devil_elder_normal.jpg";
    default:
      return "/graphics/pet/devil_baby_normal.jpg";
    }
  }

  switch (type)
  {
  case PET_ELDRITCH:
    return "/graphics/pet/eldritch_normal.jpg";
  case PET_DEVIL:
  default:
    return "/graphics/pet/devil_baby_normal.jpg";
  }
}

// -----------------------------------------------------
// INIT
// -----------------------------------------------------
void Pet::init()
{
  load();

  width = 150;
  height = 150;

  x = (320 - width) / 2;
  y = (240 - height) / 2;

  strncpy(spritePath, getSpritePathForTypeAndStage(type, evoStage), sizeof(spritePath));
  spritePath[sizeof(spritePath) - 1] = '\0';

  clampStats();
}

// -----------------------------------------------------
// MAIN UPDATE LOOP — called every frame
// -----------------------------------------------------
void Pet::update()
{
  const uint32_t now = (uint32_t)millis();

  // Always-advancing dt
  if (s_petLastUpdateMs == 0)
    s_petLastUpdateMs = now;
  const uint32_t dt = (uint32_t)(now - s_petLastUpdateMs);
  s_petLastUpdateMs = now;

  const UIState ui = g_app.uiState;

  // -------------------------------------------------
  // PRE-BIRTH: NO DECAY, NO DEATH, NO "CATCH UP"
  // -------------------------------------------------
  if (isPreBirthUiState(ui))
  {
    s_hungerAccMs = 0;
    s_energyAccMs = 0;
    s_moodAccMs = 0;
    s_hpAccMs = 0;
    s_regenAccMs = 0;

    if (this->isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested ||
        g_app.sleepUntilAwakened)
    {
      this->isSleeping = false;
      g_app.isSleeping = false;
      g_app.sleepingByTimer = false;
      g_app.sleepUntilRested = false;
      g_app.sleepUntilAwakened = false;
      g_app.sleepTargetEnergy = 0;
    }

    return;
  }

  // -------------------------------------------------
  // SLEEP MODE — continue while sleeping screen OR settings/console
  // AND while the Power Menu is open (GO-hold should not wake the pet).
  // -------------------------------------------------
  const bool sleepingNow = this->isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested ||
                           g_app.sleepUntilAwakened;

  // Do NOT force-exit sleep here.
  // UI/state system owns sleep transitions.
  // Do NOT force-exit sleep from update loop.
  // Sleep transitions must be handled explicitly via UI/state logic.
  // This avoids breaking rendering/caches during state transitions.

  // -------------------------------------------------
  // REAL-TIME DECAY CONSTANTS
  // -------------------------------------------------
  static constexpr uint32_t HUNGER_STEP_MS = 864000UL;
  static constexpr uint32_t ENERGY_STEP_MS = 576000UL;
  static constexpr uint32_t HP_STEP_MS = 216000UL;

  const uint32_t hungerStep = applyDecayModeToStep(HUNGER_STEP_MS);
  const uint32_t energyStep = applyDecayModeToStep(ENERGY_STEP_MS);
  const uint32_t hpStep = applyDecayModeToStep(HP_STEP_MS);

  bool changed = false;

  // Hunger + Energy decay
  s_hungerAccMs += dt;
  s_energyAccMs += dt;

  // While asleep, do not allow hunger to decay below 25.
  const int hungerFloor = sleepingNow ? 25 : 0;
  while (s_hungerAccMs >= hungerStep)
  {
    s_hungerAccMs -= hungerStep;
    const int old = hunger;
    hunger = constrain(hunger - 1, hungerFloor, 100);
    if (hunger != old)
      changed = true;

    if (sleepingNow && hunger <= hungerFloor)
    {
      s_hungerAccMs = 0;
      break;
    }
  }

  // If we entered sleep already hungry, bump to the minimum safe floor.
  if (sleepingNow && hunger < 25)
  {
    hunger = 25;
    changed = true;
  }

  while (s_energyAccMs >= energyStep)
  {
    s_energyAccMs -= energyStep;
    const int old = energy;
    energy = constrain(energy - 1, 0, 100);
    if (energy != old)
      changed = true;
  }

  // Mood decay
  static constexpr uint32_t MOOD_DECAY_OK_STEP_MS = 300000UL; // 5m
  static constexpr uint32_t MOOD_DECAY_BAD_STEP_MS = 30000UL; // 30s

  const bool moodBad = (hunger < 25) || (energy < 25);
  s_moodAccMs += dt;

  const uint32_t moodBase = moodBad ? MOOD_DECAY_BAD_STEP_MS : MOOD_DECAY_OK_STEP_MS;
  const uint32_t moodStep = applyDecayModeToStep(moodBase);

  while (s_moodAccMs >= moodStep)
  {
    s_moodAccMs -= moodStep;
    const int old = happiness;
    happiness = constrain(happiness - 1, 0, 100);
    if (happiness != old)
      changed = true;
  }

  // HP decay: ONLY hunger can kill.
  const bool hungerEmpty = (hunger <= 0);

  if (hungerEmpty)
  {
    s_hpAccMs += dt;

    while (s_hpAccMs >= hpStep)
    {
      s_hpAccMs -= hpStep;

      const int old = health;
      int next = health - 1;
      if (next < 0)
        next = 0;

      health = constrain(next, 0, 100);
      if (health != old)
        changed = true;

      if (health <= 0)
      {
        s_hpAccMs = 0;
        break;
      }
    }
  }
  else
  {
    s_hpAccMs = 0;

    if (health <= 0)
    {
      health = 1;
      changed = true;
    }
  }

  // Gentle HP regen (awake only)
  static constexpr uint32_t REGEN_STEP_MS = 600000UL; // 10m
  const uint32_t regenStep = applyDecayModeToStep(REGEN_STEP_MS);

  const bool canRegen = (hunger >= 30 && energy > 15);
  if (canRegen && health < 100)
  {
    s_regenAccMs += dt;
    while (s_regenAccMs >= regenStep)
    {
      s_regenAccMs -= regenStep;
      const int old = health;
      health = constrain(health + 1, 0, 100);
      if (health != old)
        changed = true;
    }
  }
  else
  {
    s_regenAccMs = 0;
  }

  clampStats();

  if (changed)
  {
    saveManagerMarkDirty();
    requestUIRedraw();
  }

  // -------------------------------------------------
  // DEATH HANDLING
  // -------------------------------------------------
  const bool hungerEmptyNow = (hunger <= 0);
  const bool deadNow = hungerEmptyNow && (health <= 0);

  if (deadNow)
  {
    if (petResurrectGraceActive())
    {
      health = 1;
      return;
    }

    if (petDeathEnabled)
    {
      health = 0;

      if (petDeathShouldAutoEnterForUi(ui))
      {
        petEnterDeathState();
      }
      return;
    }

    health = 1;
    saveManagerMarkDirty();
  }
  else
  {
    if (!hungerEmptyNow && health <= 0)
    {
      health = 1;
      saveManagerMarkDirty();
    }
  }
}

// -----------------------------------------------------
// FEED
// -----------------------------------------------------
void Pet::feed(int amount)
{
  (void)amount;
  hunger = constrain(hunger + 1, 0, 100);
  happiness = constrain(happiness + 5, 0, 100);
  lastFedTime = millis();
  clampStats();
  save();
}

// -----------------------------------------------------
// SAVE/LOAD
// -----------------------------------------------------
void Pet::save()
{
  saveManagerMarkDirty();

  static uint32_t lastEepMs = 0;
  uint32_t now = millis();
  if (now - lastEepMs < 60000)
    return;
  lastEepMs = now;

  EEPROM.write(10, (uint8_t)type);
  EEPROM.write(11, health);
  EEPROM.write(12, hunger);
  EEPROM.write(13, energy);
  EEPROM.write(14, happiness);
  EEPROM.write(15, this->isSleeping ? 1 : 0);
  EEPROM.put(16, inf);
  EEPROM.commit();
}

void Pet::load()
{
  uint8_t rawType = EEPROM.read(10);
  if (rawType >= PET_TYPE_COUNT)
    rawType = PET_DEVIL;

  type = (PetType)rawType;
  health = EEPROM.read(11);
  hunger = EEPROM.read(12);
  energy = EEPROM.read(13);
  happiness = EEPROM.read(14);
  this->isSleeping = (EEPROM.read(15) == 1);

  EEPROM.get(16, inf);
  clampStats();
}

void Pet::toPersist(PetPersist &out) const
{
  out.hunger = (uint8_t)constrain(hunger, 0, 255);
  out.happiness = (uint8_t)constrain(happiness, 0, 255);
  out.energy = (uint8_t)constrain(energy, 0, 255);
  out.health = (uint8_t)constrain(health, 0, 255);
  out.petType = (uint8_t)type;
  out.isSleeping = this->isSleeping ? 1 : 0;
  out.lastFedTimeMs = (uint32_t)lastFedTime;
  out.inf = (int32_t)inf;
  out.birth_epoch = birth_epoch;
  out.petId = petId;

  strncpy(out.name, name, PET_NAME_MAX);
  out.name[PET_NAME_MAX] = '\0';

  out.level = level;
  out.xp = xp;
  out.evoStage = evoStage;
}

void Pet::fromPersist(const PetPersist &in)
{
  hunger = (int)in.hunger;
  happiness = (int)in.happiness;
  energy = (int)in.energy;
  health = (int)in.health;

  type = (PetType)in.petType;

  // Do NOT blindly restore sleep state from persist.
  // Sleep state is owned by sleep_pending.flag (boot restore logic).
  // This avoids overwriting a restored sleeping state.
  if (!saveManagerSleepPendingFlagExists())
  {
    this->isSleeping = (in.isSleeping != 0);
  }

  lastFedTime = (unsigned long)in.lastFedTimeMs;
  inf = (int)in.inf;
  birth_epoch = in.birth_epoch;
  petId = in.petId;

  strncpy(name, in.name, PET_NAME_MAX);
  name[PET_NAME_MAX] = '\0';

  level = (in.level == 0) ? 1 : in.level;
  xp = in.xp;

  evoStage = in.evoStage;
  if (evoStage > 3)
    evoStage = 3;

  strncpy(spritePath, getSpritePathForTypeAndStage(type, evoStage), sizeof(spritePath));
  spritePath[sizeof(spritePath) - 1] = '\0';

  clampStats();
}

void Pet::clampStats()
{
  health = constrain(health, 0, 100);
  hunger = constrain(hunger, 0, 100);
  energy = constrain(energy, 0, 100);
  happiness = constrain(happiness, 0, 100);
}

// -----------------------------------------------------
// Name Pet
// -----------------------------------------------------
void Pet::setName(const char *n)
{
  if (!n)
    return;

  while (*n == ' ')
    n++;
  if (!*n)
    return;

  strncpy(name, n, PET_NAME_MAX);
  name[PET_NAME_MAX] = '\0';
}

// -----------------------------------------------------
// DEATH / RESURRECTION
// -----------------------------------------------------
bool petDeathShouldAutoEnterForUi(UIState ui)
{
  switch (ui)
  {
  case UIState::PET_SCREEN:
  case UIState::PET_SLEEPING:
  case UIState::CLOCK_MODE:
    return true;

  default:
    return false;
  }
}

void petEnterDeathState()
{
  g_app.inMiniGame = false;
  currentMiniGame = MiniGame::NONE;

  inputSetTextCapture(false);
  g_textCaptureMode = false;

  inputForceClear();
  clearInputLatch();

  beginDeathTransition();

  if (consoleIsOpen())
  {
    consoleClose();
  }

  resetDeathMenu();
  requestUIRedraw();
  invalidateBackgroundCache();
}

void Pet::addXP(uint32_t amount)
{
  if (amount == 0)
    return;

  if (xp > 0xFFFFFFFFu - amount)
    xp = 0xFFFFFFFFu;
  else
    xp += amount;

  while (level < 999)
  {
    const uint32_t need = xpForNextLevel();
    if (xp < need)
      break;

    xp -= need;
    level++;

    Serial.printf("[XP] LEVEL UP → level=%u next=%lu\n", level, xpForNextLevel());
  }

  saveManagerMarkDirty();
  requestUIRedraw();
}

uint16_t Pet::nextEvoMinLevel() const
{
  if (evoStage >= 3)
    return 0;
  return evoMinLevelForStage((uint8_t)(evoStage + 1));
}

bool Pet::canEvolveNext() const
{
  if (evoStage >= 3)
    return false;
  const uint16_t need = nextEvoMinLevel();
  return (need > 0) && (level >= need);
}

bool Pet::tryEvolveUsingItem(ItemType it)
{
  if (it != ITEM_ELDRITCH_EYE)
    return false;

  if (!canEvolveNext())
    return false;
  const PetMood mood = getMood();
  if (mood != MOOD_HAPPY && mood != MOOD_BORED)
    return false;

  evoStage++;

  strncpy(spritePath, getSpritePathForTypeAndStage(type, evoStage), sizeof(spritePath));
  spritePath[sizeof(spritePath) - 1] = '\0';

  saveManagerMarkDirty();
  requestUIRedraw();
  return true;
}

void Pet::setEvoStage(uint8_t stage)
{
  if (stage > 3)
    stage = 3;

  evoStage = stage;

  strncpy(spritePath, getSpritePathForTypeAndStage(type, evoStage), sizeof(spritePath));
  spritePath[sizeof(spritePath) - 1] = '\0';

  invalidateBackgroundCache();
  requestUIRedraw();

  saveManagerMarkDirty();
}

static const char *evoStageToDevilDescriptor(uint8_t stage)
{
  switch (stage)
  {
  case 0:
    return "Hellspawn";
  case 1:
    return "Infernal Youth";
  case 2:
    return "Tormented Adult";
  case 3:
    return "Elder Demon";
  default:
    return "Hellspawn";
  }
}

static const char *evoStageToEldritchDescriptor(uint8_t stage)
{
  switch (stage)
  {
  case 0:
    return "Voidspawn";
  case 1:
    return "Warped Youth";
  case 2:
    return "Cult Horror";
  case 3:
    return "Outer Horror";
  default:
    return "Voidspawn";
  }
}

const char *Pet::getEvolutionDescriptor() const
{
  switch (type)
  {
  case PET_DEVIL:
    return evoStageToDevilDescriptor(evoStage);
  case PET_ELDRITCH:
    return evoStageToEldritchDescriptor(evoStage);
  default:
    return "";
  }
}

void Pet::buildDisplayName(char *out, size_t outSize) const
{
  if (!out || outSize == 0)
    return;
  out[0] = '\0';

  const char *nm = getName();
  if (!nm || !*nm)
    nm = "Pet";

  const char *evo = getEvolutionDescriptor();

  if (!evo || !*evo)
  {
    snprintf(out, outSize, "%s", nm);
    return;
  }

  snprintf(out, outSize, "%s - %s", nm, evo);
}

uint32_t Pet::xpForNextLevel() const
{
  const uint32_t L = (level < 1) ? 1 : level;
  const uint32_t n = L - 1;

  const uint32_t xp = 120 + 20 * n + 12 * n * n;

  return xp;
}