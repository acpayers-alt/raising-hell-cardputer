#pragma once
#include <Arduino.h>

#include "pet_visuals.h"
#include "inventory.h"
#include "savegame.h"
#include "pet_defs.h"

// Global helpers (keep only if these are truly free functions)
void addInf(int amount);

// Prevent decay/regen "catch-up" after long pauses (sleep / console / screen off)
void petResetUpdateTimers();

// ------------------------------------------------------------
// PET TYPES
// ------------------------------------------------------------
enum PetType : uint8_t {
  PET_DEVIL = 0,
  PET_ELDRITCH,
  PET_TYPE_COUNT
};


// Array indexed by (int)PetType
extern const PetVisualProfile PET_PROFILES[PET_TYPE_COUNT];

// ------------------------------------------------------------
// PET MOOD / CONDITION (priority: sick > tired > hungry > mad > happy)
// ------------------------------------------------------------
enum PetMood : uint8_t {
  MOOD_SICK = 0,
  MOOD_TIRED,
  MOOD_HUNGRY,
  MOOD_MAD,
  MOOD_BORED,
  MOOD_HAPPY,
};

// Resolve current mood from stats.
// NOTE: thresholds live in pet.cpp for easy tuning.
PetMood petResolveMood(const class Pet& p);

// ------------------------------------------------------------
// PET CLASS — COMPLETE API MATCHING pet.cpp
// ------------------------------------------------------------
class Pet {
public:
  Pet();

  char name[PET_NAME_MAX + 1];

  void setName(const char* n);
  const char* getName() const { return name; }
  PetMood getMood() const { return petResolveMood(*this); }

  // Sleep quality is derived from current stats and affects sleep energy recovery.
  // Score: 0..100. MultQ8: fixed-point multiplier where 256 = 1.0x.
  uint8_t  getSleepQualityScore() const;
  uint16_t getSleepQualityMultQ8() const;
  const char* getSleepQualityLabel() const;

  // ---- Core Attributes ----
  PetType type;

  int hunger;
  int happiness;
  int energy;
  int health;
  int inf;
  uint32_t birth_epoch;
  uint64_t petId;
  bool isSleeping;
  unsigned long lastFedTime;

  // --------------------------------------------------------
  // Rendering State (required for SD streaming)
  // --------------------------------------------------------
  char spritePath[64];

  // On-screen position
  int x;  
  int y;

  // Dimensions for the RAW sprite
  int width;
  int height;

  // --------------------------------------------------------
  // Item each pet uses as food (replaces old FoodType)
  // --------------------------------------------------------
  ItemType getFoodItem() const {
    switch (type) {
      case PET_ELDRITCH: return ITEM_CURSED_RELIC;
      case PET_DEVIL:
      default:           return ITEM_SOUL_FOOD;
    }
  }


  const char* getEvolutionDescriptor() const;          // e.g. "Hellspawn"
  void buildDisplayName(char* out, size_t outSize) const; // "Jan - Hellspawn"

  // ---- Lifecycle ----
  void init();
  void update();

  // ---- Stat updates ----
  void updateStats();
  void recoverDuringSleep();
  void petSleepTick();

  // ---- Actions ----
  void feed(int amount);
  void heal(int amount);
  void tire(int amount);

  // ---- Persistence ----
  void load();
  void save();

  // SD Save adapters (NEW)
  void toPersist(PetPersist &out) const;
  void fromPersist(const PetPersist &in);

  // ---- Helpers ----
  void clampStats();
  const PetVisualProfile& getVisualProfile() const;

  // ---- Progression ----
  uint16_t level;
  uint32_t xp;

  // 0=baby, 1=teen, 2=adult, 3=elder
  uint8_t evoStage;

  // XP / Level
  void addXP(uint32_t amount);
  uint32_t xpForNextLevel() const;

  // Evolution
  bool canEvolveNext() const;
  uint16_t nextEvoMinLevel() const;
  bool tryEvolveUsingItem(ItemType it);   // returns true only if evolved
  void setEvoStage(uint8_t stage);        // 0=baby,1=teen,2=adult,3=elder
};

extern Pet pet;

// Death / resurrection helpers
void petEnterDeathState();
void petResurrectFull();
