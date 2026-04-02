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
  uint8_t petDeathEnabled;
  uint8_t ledAlertsEnabled;

  // v2+ (added later): 0 = not seen, 1 = seen
  uint8_t controlsHelpSeen;

  // v3+ perf HUD toggle
  uint8_t petPerfHudEnabled;
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
  char     name[PET_NAME_MAX + 1];

  uint16_t level;
  uint32_t xp;
  uint8_t  evoStage;
};

struct SavePayloadV3 {
  uint32_t magic;
  uint16_t version;

  PetPersistV3 pet;
  InvPersist   inv;
  uint32_t     birth_epoch;
};

struct SavePayloadV2 {
  uint32_t magic;
  uint16_t version;

  PetPersistV2 pet;
  InvPersist   inv;
  uint32_t     birth_epoch;
};

