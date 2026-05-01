// save_manager.h
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ─────────────────────────────────────
// Core Save System (game + settings)
// ─────────────────────────────────────
bool saveManagerLoad();      // true if save.bin loaded OK
bool saveManagerSave();      // true if write succeeded
void saveManagerBegin();     // optional init hook
void saveManagerMarkDirty(); // mark dirty for deferred save
void saveManagerTick();      // call in loop()
void saveManagerForce();     // force immediate save

bool saveManagerSaveFileExists();

int saveManagerLastExportScanInvalidCount();
int saveManagerLastExportScanValidCount();

// ─────────────────────────────────────
// Pet Lifecycle
// ─────────────────────────────────────
void saveManagerNewPet();
void saveManagerNewPetNoSave();
void saveManagerFullWipe();

uint32_t saveManagerGetBirthEpoch();
void saveManagerStampBirthNow();

// ─────────────────────────────────────
// Auto-heal / integrity
// ─────────────────────────────────────
bool saveManagerAutoHeal();

// ─────────────────────────────────────
// Delete / reset
// ─────────────────────────────────────
void saveManagerDeleteAll();      // delete everything
void saveManagerDeletePetOnly();  // delete only pet save
void saveManagerFactoryReset();

// ─────────────────────────────────────
// Game Options (gameopt.bin)
// ─────────────────────────────────────
uint8_t saveManagerGetDecayMode(); // 0=Normal, 1=Slow, 2=Off
void saveManagerSetDecayMode(uint8_t mode);
bool isStepCounterEnabled();
void setStepCounterEnabled(bool en);

// ─────────────────────────────────────
// Settings persistence (settings.bin)
// ─────────────────────────────────────
bool loadSettingsFromSD();
void saveSettingsToSD();

extern bool ledAlertsEnabled;

// WiFi preference
bool settingsWifiEnabled();
void settingsSetWifiEnabled(bool en);

// ─────────────────────────────────────
// Sleep state persistence
// ─────────────────────────────────────
void saveManagerSetSleepPendingFlag();
void saveManagerClearSleepPendingFlag();
bool saveManagerSleepPendingFlagExists();
void saveManagerEnterSleepState();

// ─────────────────────────────────────
// Name / intro flags
// ─────────────────────────────────────
void saveManagerClearNamePendingFlag();
bool saveManagerNamePendingFlagExists();
void saveManagerSetPetIntroFadeBootFlag();

// ─────────────────────────────────────
// Fresh pet flow control
// ─────────────────────────────────────
void saveManagerAbortFreshPetFlow();
void resetRuntimeToCleanNoSaveState(bool resetName);

// ─────────────────────────────────────
// Backup / Export / Import
// ─────────────────────────────────────
struct PetExportEntry
{
  char path[128];
  char name[24];
  char petType[16];
  char petId[24];
  uint32_t createdAtEpoch;
  bool valid;
};

int  saveManagerListPetBackups(PetExportEntry *outEntries, int maxEntries);
bool saveManagerBackupCurrentPet(char *outPath, size_t outPathSize);
bool saveManagerValidateBubAtPath(const char *path);
bool saveManagerDeletePetBackupAtPath(const char *path);

int  saveManagerListPetExports(PetExportEntry *outEntries, int maxEntries);
bool saveManagerImportBubAtPath(const char *path, char *outPath, size_t outPathSize, bool backupCurrentFirst = true);
bool saveManagerBoxCurrentPet(char *outPath, size_t outPathSize);

bool saveManagerExportCurrentBubJson(char *outPath, size_t outPathSize);
bool saveManagerImportLatestBubJson(char *outPath, size_t outPathSize);

void saveManagerAssignFreshPetId();

// ─────────────────────────────────────
// Load diagnostics
// ─────────────────────────────────────
enum SaveLoadErr : uint8_t
{
  SLE_OK = 0,
  SLE_SD_NOT_READY,
  SLE_DIR_FAIL,
  SLE_OPEN_FAIL,
  SLE_SIZE_UNKNOWN,
  SLE_READ_FAIL,
  SLE_MAGIC_BAD,
  SLE_VERSION_BAD
};

uint8_t  saveManagerLastLoadErr();
uint32_t saveManagerLastLoadSize();

// ─────────────────────────────────────
// Import helpers
// ─────────────────────────────────────
bool saveManagerHasImportableBubJson();