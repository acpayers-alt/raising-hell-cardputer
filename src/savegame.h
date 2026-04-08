#pragma once
#include <stdint.h>
#include <time.h>
#include <string.h>
#include "pet_defs.h"

// ============================================================
// SAVE FILE FORMAT
// ============================================================

// 'SHHR' (your constant; keep as-is if you already wrote files with it)
#define SAVE_MAGIC   0x52484853
#define SAVE_VERSION 4

// Back-compat names used by older code (sdcard.cpp)
#define RH_SAVE_MAGIC   SAVE_MAGIC
#define RH_SAVE_VERSION SAVE_VERSION

// MUST match Inventory::MAX_ITEMS
#define SAVE_INV_MAX_ITEMS 16

// ============================================================
// HEADER (written before payload)
// ============================================================

struct SaveHeader {
  uint32_t magic;       // RH_SAVE_MAGIC
  uint16_t version;     // RH_SAVE_VERSION
  uint16_t headerSize;  // sizeof(SaveHeader)
  uint32_t payloadSize; // sizeof(SavePayload)
  uint32_t crc32;       // CRC32 of payload bytes
};

// ============================================================
// PET PERSISTENCE
// ============================================================

struct PetPersist {
  uint8_t  hunger;
  uint8_t  happiness;
  uint8_t  energy;
  uint8_t  health;
  uint8_t  petType;
  uint8_t  isSleeping;
  uint32_t lastFedTimeMs;
  int32_t  inf;
  uint32_t birth_epoch;
  uint64_t petId;
  char     name[PET_NAME_MAX + 1];

  // v3 progression
  uint16_t level;
  uint32_t xp;
  uint8_t  evoStage;
};

// ============================================================
// INVENTORY PERSISTENCE
// ============================================================

struct InvSlotPersist {
  uint8_t type;
  uint8_t qty;
};

struct InvPersist {
  InvSlotPersist slots[SAVE_INV_MAX_ITEMS];
  int16_t selectedIndex;
};

// ============================================================
// FULL SAVE PAYLOAD (CRC covers exactly these bytes)
// ============================================================

struct SavePayload {
  // Keeping these is OK; just be consistent on read/write.
  uint32_t magic;    // SAVE_MAGIC
  uint16_t version;  // SAVE_VERSION

  PetPersist pet;
  InvPersist inv;
  uint32_t birth_epoch;
};

// ============================================================
// SETTINGS DATA
// ============================================================
// IMPORTANT: use uint8_t for persisted fields (stable layout)
struct SettingsData {
  uint8_t brightnessLevel;
  uint8_t autoScreenOffEnabled;
  uint8_t soundEnabled;
  uint8_t wifiEnabled;
  uint8_t tzIndex;
  uint8_t autoScreenTimeoutSel;
  uint8_t shakeSensitivitySel;
  uint8_t petDeathEnabled;
  uint8_t ledAlertsEnabled;

  // v2+ (added later): 0 = not seen, 1 = seen
  uint8_t controlsHelpSeen;

  // v3+ perf HUD toggle
  uint8_t petPerfHudEnabled;

  // one-shot dev/test flag:
  // when set, next boot arms the first pet-screen intro fade
  uint8_t petScreenIntroFadeBootFlag;
};

// ============================================================
// V2 BACK-COMPAT PAYLOAD (no XP/level/evoStage)
// IMPORTANT: Must match EXACT old layout used in v2 saves.
// ============================================================

struct PetPersistV2 {
  uint8_t  hunger;
  uint8_t  happiness;
  uint8_t  energy;
  uint8_t  health;
  uint8_t  petType;
  uint8_t  isSleeping;
  uint32_t lastFedTimeMs;
  int32_t  inf;
  uint32_t birth_epoch;
  char     name[PET_NAME_MAX + 1];
};

// ============================================================
// V3 BACK-COMPAT PAYLOAD (true 1.0.3 layout)
// IMPORTANT: this must match the exact 1.0.3 compiler-written struct.
// ============================================================

static constexpr int SAVE_INV_MAX_ITEMS_V3 = 16;
static constexpr int LEGACY_PET_NAME_MAX_V3 = 24;
static constexpr int LEGACY_PET_NAME_LEN_V3 = LEGACY_PET_NAME_MAX_V3 + 1;

struct PetPersistV3 {
  uint8_t  hunger;
  uint8_t  happiness;
  uint8_t  energy;
  uint8_t  health;
  uint8_t  petType;
  uint8_t  isSleeping;
  uint32_t lastFedTimeMs;
  int32_t  inf;
  uint32_t birth_epoch;
  char     name[LEGACY_PET_NAME_LEN_V3];

  uint16_t level;
  uint32_t xp;
  uint8_t  evoStage;
};

struct InvPersistV3 {
  InvSlotPersist slots[SAVE_INV_MAX_ITEMS_V3];
  int16_t        selectedIndex;
};

struct SavePayloadV3 {
  uint32_t magic;
  uint16_t version;

  PetPersistV3 pet;
  InvPersistV3 inv;
  uint32_t     birth_epoch;
};

static_assert(sizeof(PetPersistV3) == 56, "PetPersistV3 must match 1.0.3 56-byte layout");
static_assert(sizeof(InvPersistV3) == 34, "InvPersistV3 must match 1.0.3 34-byte layout");
static_assert(sizeof(SavePayloadV3) == 104, "SavePayloadV3 must match 1.0.3 104-byte layout");

struct SavePayloadV2 {
  uint32_t magic;
  uint16_t version;

  PetPersistV2 pet;
  InvPersist   inv;
  uint32_t     birth_epoch;
};