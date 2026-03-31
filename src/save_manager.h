// save_manager.h
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Game Options persistence (stored in /raising_hell/save/gameopt.bin)
uint8_t saveManagerGetDecayMode();            // 0=Normal, 1=Slow, 2=Off (example)
void    saveManagerSetDecayMode(uint8_t mode);

// SD-backed save system (game + settings)
bool saveManagerLoad();      // returns true only if save.bin loaded OK
bool saveManagerSave();      // returns true if write succeeded
void saveManagerBegin();     // optional init hook (safe to call)
void saveManagerMarkDirty(); // call when anything changes
void saveManagerTick();      // call in loop()
void saveManagerForce();     // force a save (and settings), used before reboot/sleep
bool saveManagerAutoHeal();
void saveManagerNewPet();
void saveManagerNewPetNoSave();
bool saveManagerSaveFileExists();
bool saveManagerHasImportableBubJson();
void saveManagerStartFreshPetFlow();
bool saveManagerAutoHeal();
void saveManagerFullWipe();

uint32_t saveManagerGetBirthEpoch();   // handy for stats screen later

// Settings persistence (settings.bin under /raising_hell/save/)
bool loadSettingsFromSD();
void saveSettingsToSD();
void saveManagerNewPet();
uint8_t saveManagerGetDecayMode();
void    saveManagerSetDecayMode(uint8_t mode);
extern bool ledAlertsEnabled;

// Persisted WiFi preference (settings.bin)
bool settingsWifiEnabled();
void settingsSetWifiEnabled(bool en);

void saveManagerFactoryReset();

enum SaveLoadErr : uint8_t {
  SLE_OK = 0,
  SLE_SD_NOT_READY,
  SLE_DIR_FAIL,
  SLE_OPEN_FAIL,
  SLE_SIZE_UNKNOWN,
  SLE_READ_FAIL,
  SLE_MAGIC_BAD,
  SLE_VERSION_BAD
};

uint8_t saveManagerLastLoadErr();
uint32_t saveManagerLastLoadSize();


// Deletes all save files (used when the pet is buried)
void saveManagerDeleteAll();
// Delete ONLY the pet save (used for burial). Keeps settings/game options.
void saveManagerDeletePetOnly();

void saveManagerStampBirthNow();

void saveManagerClearNamePendingFlag();
bool saveManagerNamePendingFlagExists();

bool saveManagerExportCurrentBubJson(char *outPath, size_t outPathSize);
bool saveManagerImportLatestBubJson(char *outPath, size_t outPathSize);