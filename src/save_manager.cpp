#include "save_manager.h"

// -----------------------------------------------------------------------------
// Standard / C
// -----------------------------------------------------------------------------
#include <stdint.h>

// -----------------------------------------------------------------------------
// Arduino / ESP / platform
// -----------------------------------------------------------------------------
#include <Arduino.h>
#include <esp_system.h>

// -----------------------------------------------------------------------------
// External libraries
// -----------------------------------------------------------------------------
#include <ArduinoJson.h>
#include <FS.h>
#include <Preferences.h>
#include <SD.h>

// -----------------------------------------------------------------------------
// Project headers
// -----------------------------------------------------------------------------

// Core systems
#include "debug.h"
#include "savegame.h"

// Game systems
#include "inventory.h"
#include "pet.h"

// App / lifecycle
#include "app_lifecycle.h"
#include "app_state.h"
#include "boot_pipeline.h"

// Storage / SD
#include "sdcard.h"

// Time
#include "time_persist.h"
#include "time_state.h"
#include "timezone.h"

// WiFi
#include "wifi_store.h"

// UI / rendering
#include "display.h"
#include "display_state.h"
#include "graphics.h"
#include "ui_actions.h"
#include "ui_invalidate.h"
#include "ui_runtime.h"

// UI state / flows
#include "controls_help_state.h"
#include "inventory_state.h"
#include "new_pet_flow_state.h"
#include "runtime_flags_state.h"
#include "settings_state.h"
#include "settings_toggles_state.h"
#include "sleep_state.h"
#include "user_toggles_state.h"

// Misc
#include "asset_ota.h"
#include "brightness_state.h"
#include "input.h"
#include "motion.h"
#include "version.h"

// Include End Here

// Forward declarations for internal helpers used before definition
static void clearNamePendingFlag();
void resetRuntimeToCleanNoSaveState(bool resetName);

static const char *getFirmwareVersionString()
{
#ifdef FW_VERSION
  return FW_VERSION;
#elif defined(VERSION_STRING)
  return VERSION_STRING;
#else
  return "unknown";
#endif
}

static bool dirty = false;
static uint32_t lastSaveMs = 0;
static const uint32_t DEBOUNCE_MS = 2000;
static void newPetInternalNoSave(bool resetName = false);

// One (and ONLY one) instance
static SettingsData g_settings;

bool settingsWifiEnabled() { return (g_settings.wifiEnabled != 0); }

void settingsSetWifiEnabled(bool en)
{
  Serial.printf("[WIFI PREF WRITE] en=%d @ %s:%d\n", en ? 1 : 0, __FILE__, __LINE__);
  g_settings.wifiEnabled = en ? 1 : 0;
}

// Settings persistence
bool loadSettingsFromSD();
void saveSettingsToSD(); // exported wrapper

// Pet Stuff
static uint32_t g_birthEpoch = 0;

static uint8_t g_lastLoadErr = SLE_OK;
static uint32_t g_lastLoadSize = 0;

static bool g_lastLoadUsedBackup = false;
static const char *g_lastLoadPath = nullptr;

uint8_t saveManagerLastLoadErr() { return g_lastLoadErr; }
uint32_t saveManagerLastLoadSize() { return g_lastLoadSize; }

static void forceChoosePetFlowFromBoot();
static uint64_t generatePetId();

void saveManagerSetSleepPendingFlag();
void saveManagerClearSleepPendingFlag();
bool saveManagerSleepPendingFlagExists();
void saveManagerEnterSleepState();

// -----------------------------
// Save root + paths
// -----------------------------
static const char *SAVE_DIR = "/raising_hell/save";

static const char *SAVE_PATH = "/raising_hell/save/save.bin";
static const char *SAVE_TMP_PATH = "/raising_hell/save/save.tmp";

static const char *SET_PATH = "/raising_hell/save/settings.bin";
static const char *SET_TMP_PATH = "/raising_hell/save/settings.tmp";
static const char *GAMEOPT_PATH = "/raising_hell/save/gameopt.bin";
static const char *GAMEOPT_TMP_PATH = "/raising_hell/save/gameopt.tmp";
static const char *LEGACY_SAVE_PATH = "/raising_hell/save/raising_hell.sav";
static const char *SAVE_BAK1_PATH = "/raising_hell/save/save.bak1";
static const char *SAVE_BAK2_PATH = "/raising_hell/save/save.bak2";
static const char *SAVE_BAK3_PATH = "/raising_hell/save/save.bak3";
static const char *AUTO_HEAL_FALLBACK_PET_NAME = "Bub";

bool saveManagerAutoHeal();
static const char *NAME_PENDING_FLAG_PATH = "/raising_hell/save/name_pending.flag";
static const char *SLEEP_PENDING_FLAG_PATH = "/raising_hell/save/sleep_pending.flag";
static const char *BACKUPS_DIR = "/raising_hell/backup";
static const char *EXPORTS_DIR = "/raising_hell/exports";
static const char *EXPORT_MAGIC = "raising_hell_bub_export";
static const uint16_t EXPORT_VERSION = 1;

static void wipeSdRecursive(const char *path)
{
  File dir = SD.open(path);
  if (!dir || !dir.isDirectory())
    return;

  File file = dir.openNextFile();
  while (file)
  {
    String fullPath = String(path) + "/" + file.name();

    if (file.isDirectory())
    {
      file.close();
      wipeSdRecursive(fullPath.c_str());
      SD.rmdir(fullPath.c_str());
    }
    else
    {
      file.close();
      SD.remove(fullPath.c_str());
    }

    file = dir.openNextFile();
  }
}

static void clearNvsNamespace(const char *ns)
{
  Preferences p;
  if (p.begin(ns, false))
  {
    p.clear();
    p.end();
  }
}

static void tryRemove(const char *path)
{
  if (!g_sdReady || !path)
    return;

  if (SD.exists(path))
    SD.remove(path);
}

static bool ensurePetDir(const char *dirPath)
{
  if (!g_sdReady || !dirPath || !dirPath[0])
    return false;

  if (!SD.exists("/raising_hell"))
  {
    if (!SD.mkdir("/raising_hell"))
      return false;
  }

  if (!SD.exists(dirPath))
  {
    if (!SD.mkdir(dirPath))
      return false;
  }

  return true;
}

static bool ensureExportsDir() { return ensurePetDir(EXPORTS_DIR); }

static bool ensureBackupsDir() { return ensurePetDir(BACKUPS_DIR); }

static const char *petTypeToStringForExport(PetType t)
{
  switch (t)
  {
  case PET_ELDRITCH:
    return "eldritch";
  case PET_DEVIL:
  default:
    return "devil";
  }
}

static bool petTypeFromStringForImport(const char *s, PetType &out)
{
  if (!s || !*s)
    return false;

  if (!strcmp(s, "devil"))
  {
    out = PET_DEVIL;
    return true;
  }

  if (!strcmp(s, "eldritch"))
  {
    out = PET_ELDRITCH;
    return true;
  }

  // Legacy imports from removed pet lines collapse to Devil.
  if (!strcmp(s, "alien") || !strcmp(s, "kaiju") || !strcmp(s, "anubis") || !strcmp(s, "axolotl"))
  {
    out = PET_DEVIL;
    return true;
  }

  return false;
}

static const char *itemTypeToStringForExport(ItemType t)
{
  switch (t)
  {
  case ITEM_SOUL_FOOD:
    return "SOUL_FOOD";
  case ITEM_CURSED_RELIC:
    return "CURSED_RELIC";
  case ITEM_DEMON_BONE:
    return "DEMON_BONE";
  case ITEM_RITUAL_CHALK:
    return "RITUAL_CHALK";
  case ITEM_ELDRITCH_EYE:
    return "ELDRITCH_EYE";
  default:
    return "ITEM_NONE";
  }
}

static bool itemTypeFromStringForImport(const char *s, ItemType &out)
{
  if (!s || !*s)
    return false;
  if (!strcmp(s, "SOUL_FOOD"))
  {
    out = ITEM_SOUL_FOOD;
    return true;
  }
  if (!strcmp(s, "CURSED_RELIC"))
  {
    out = ITEM_CURSED_RELIC;
    return true;
  }
  if (!strcmp(s, "DEMON_BONE"))
  {
    out = ITEM_DEMON_BONE;
    return true;
  }
  if (!strcmp(s, "RITUAL_CHALK"))
  {
    out = ITEM_RITUAL_CHALK;
    return true;
  }
  if (!strcmp(s, "ELDRITCH_EYE"))
  {
    out = ITEM_ELDRITCH_EYE;
    return true;
  }
  if (!strcmp(s, "ITEM_NONE"))
  {
    out = ITEM_NONE;
    return true;
  }
  return false;
}

static bool readExportMetadata(const char *path, PetExportEntry &out)
{
  out.valid = false;
  out.path[0] = '\0';
  out.name[0] = '\0';
  out.petType[0] = '\0';
  out.petId[0] = '\0';
  out.createdAtEpoch = 0;

  File f = SD.open(path, FILE_READ);
  if (!f)
  {
    Serial.printf("[EXPORT LIST] open failed path=%s\n", path ? path : "(null)");
    return false;
  }

  DynamicJsonDocument filter(256);
  filter["format"] = true;
  filter["exportVersion"] = true;
  filter["createdAtEpoch"] = true;
  filter["profile"]["name"] = true;
  filter["profile"]["petType"] = true;
  filter["profile"]["petId"] = true;

  DynamicJsonDocument doc(1024);
  DeserializationOption::Filter filtered(filter);
  const DeserializationError err = deserializeJson(doc, f, filtered);
  f.close();

  if (err)
  {
    Serial.printf("[EXPORT LIST] json failed path=%s err=%s\n", path, err.c_str());
    return false;
  }

  const char *format = doc["format"] | "";
  const uint16_t exportVersion = doc["exportVersion"] | 0;
  if (strcmp(format, EXPORT_MAGIC) != 0 || exportVersion != EXPORT_VERSION)
  {
    Serial.printf("[EXPORT LIST] format/version failed path=%s format=%s version=%u\n", path, format,
                  (unsigned)exportVersion);
    return false;
  }

  const char *name = doc["profile"]["name"] | "";
  const char *petType = doc["profile"]["petType"] | "";
  const uint32_t createdAtEpoch = (uint32_t)(doc["createdAtEpoch"] | 0);
  const char *petId = doc["profile"]["petId"] | "";

  strncpy(out.path, path, sizeof(out.path) - 1);
  out.path[sizeof(out.path) - 1] = '\0';

  strncpy(out.name, (name && name[0]) ? name : "Bub", sizeof(out.name) - 1);
  out.name[sizeof(out.name) - 1] = '\0';

  strncpy(out.petType, (petType && petType[0]) ? petType : "DEVIL", sizeof(out.petType) - 1);
  out.petType[sizeof(out.petType) - 1] = '\0';

  strncpy(out.petId, petId, sizeof(out.petId) - 1);
  out.petId[sizeof(out.petId) - 1] = '\0';

  out.createdAtEpoch = createdAtEpoch;
  out.valid = true;

  Serial.printf("[EXPORT LIST] ok path=%s petId=%s name=%s created=%lu\n", out.path,
                out.petId[0] ? out.petId : "(none)", out.name, (unsigned long)out.createdAtEpoch);

  return true;
}

static void sortPetExportsNewestFirst(PetExportEntry *entries, int count)
{
  for (int i = 0; i < count - 1; ++i)
  {
    for (int j = i + 1; j < count; ++j)
    {
      if (entries[j].createdAtEpoch > entries[i].createdAtEpoch)
      {
        PetExportEntry tmp = entries[i];
        entries[i] = entries[j];
        entries[j] = tmp;
      }
    }
  }
}

static int listPetEntriesFromDir(const char *dirPath, PetExportEntry *outEntries, int maxEntries)
{
  if (!outEntries || maxEntries <= 0)
    return 0;

  if (!g_sdReady || !dirPath || !SD.exists(dirPath))
    return 0;

  File dir = SD.open(dirPath);
  if (!dir || !dir.isDirectory())
    return 0;

  int count = 0;

  File file = dir.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      const char *nm = file.name();
      if (nm && nm[0])
      {
        size_t len = strlen(nm);
        if (len >= 4 && strcmp(nm + len - 4, ".bub") == 0)
        {
          char full[128];
          snprintf(full, sizeof(full), "%s/%s", dirPath, nm);

          PetExportEntry entry{};
          if (readExportMetadata(full, entry))
          {
            bool merged = false;

            if (entry.petId[0])
            {
              for (int i = 0; i < count; ++i)
              {
                if (strcmp(outEntries[i].petId, entry.petId) == 0)
                {
                  if (entry.createdAtEpoch >= outEntries[i].createdAtEpoch)
                    outEntries[i] = entry;

                  merged = true;
                  break;
                }
              }
            }

            if (!merged)
            {
              if (count < maxEntries)
              {
                outEntries[count++] = entry;
              }
            }
          }
        }
      }
    }

    file.close();
    file = dir.openNextFile();
  }

  sortPetExportsNewestFirst(outEntries, count);
  return count;
}

int saveManagerListPetBackups(PetExportEntry *outEntries, int maxEntries)
{
  return listPetEntriesFromDir(BACKUPS_DIR, outEntries, maxEntries);
}

int saveManagerListPetExports(PetExportEntry *outEntries, int maxEntries)
{
  return listPetEntriesFromDir(EXPORTS_DIR, outEntries, maxEntries);
}

static void sanitizeExportFilename(const char *src, char *dst, size_t dstSize)
{
  if (!dst || dstSize == 0)
    return;

  size_t j = 0;
  if (!src || !src[0])
    src = "Bub";

  for (size_t i = 0; src[i] && j + 1 < dstSize; ++i)
  {
    const char c = src[i];
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
    {
      dst[j++] = c;
    }
    else if (c == ' ' || c == '-' || c == '_')
    {
      dst[j++] = '_';
    }
  }

  if (j == 0)
    dst[j++] = 'B';

  dst[j] = '\0';
}

static bool findLatestExportPath(char *outPath, size_t outPathSize)
{
  if (!outPath || outPathSize == 0)
    return false;

  outPath[0] = '\0';

  if (!g_sdReady || !SD.exists(EXPORTS_DIR))
    return false;

  File dir = SD.open(EXPORTS_DIR);
  if (!dir || !dir.isDirectory())
    return false;

  uint32_t bestTime = 0;
  char bestPath[128] = {0};

  File file = dir.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      const char *nm = file.name();
      const size_t len = strlen(nm);
      if (len >= 4 && !strcmp(nm + len - 4, ".bub"))
      {
        char full[128];
        snprintf(full, sizeof(full), "%s/%s", EXPORTS_DIR, nm);

        const uint32_t t = (uint32_t)file.getLastWrite();
        if (bestPath[0] == '\0' || t >= bestTime)
        {
          bestTime = t;
          strncpy(bestPath, full, sizeof(bestPath) - 1);
          bestPath[sizeof(bestPath) - 1] = '\0';
        }
      }
    }

    file.close();
    file = dir.openNextFile();
  }

  if (!bestPath[0])
    return false;

  strncpy(outPath, bestPath, outPathSize - 1);
  outPath[outPathSize - 1] = '\0';
  return true;
}

bool saveManagerHasImportableBubJson()
{
  char path[128];
  return findLatestExportPath(path, sizeof(path));
}

void saveManagerFullWipe()
{
  Serial.println("[DEV] FULL WIPE BEGIN");

  // --- SD wipe ---
  if (g_sdReady)
  {
    Serial.println("[DEV] wiping SD...");
    wipeSdRecursive("/");
    Serial.println("[DEV] SD wipe complete");
  }

  // --- NVS wipe ---
  Serial.println("[DEV] clearing NVS...");
  clearNvsNamespace("rh_settings");
  clearNvsNamespace("rh_wifi");
  clearNvsNamespace("rh_tz");

  // --- Time ---
  clearTimeAnchor();
  invalidateTimeNow();

  // --- EEPROM mirrors ---
  g_app.inventory.wipePersistedEeprom();

  // --- Flags ---
  tryRemove(NAME_PENDING_FLAG_PATH);
  tryRemove(SLEEP_PENDING_FLAG_PATH);

  Serial.println("[DEV] FULL WIPE DONE → rebooting");

  delay(200);
  ESP.restart();
}

// ------------------------------------------------------------
// NEW PET FLOW BOOT RESUME FLAG
//   - Present => user was mid new-pet creation
//   - Current policy does NOT resume NAME_PET on boot
//   - If an incomplete/new-pet save is encountered, fall back to CHOOSE_PET
// ------------------------------------------------------------

static bool namePendingFlagExists()
{
  if (!g_sdReady)
    return false;
  return SD.exists(NAME_PENDING_FLAG_PATH);
}

static void writeNamePendingFlag()
{
  if (!g_sdReady)
    return;

  // Ensure directory exists without relying on ensureSaveDir() ordering.
  if (!SD.exists("/raising_hell"))
  {
    if (!SD.mkdir("/raising_hell"))
      return;
  }
  if (!SD.exists("/raising_hell/save"))
  {
    if (!SD.mkdir("/raising_hell/save"))
      return;
  }

  File f = SD.open(NAME_PENDING_FLAG_PATH, FILE_WRITE);
  if (f)
  {
    f.print("1");
    f.close();
  }
}

static bool sleepPendingFlagExists()
{
  if (!g_sdReady)
    return false;
  return SD.exists(SLEEP_PENDING_FLAG_PATH);
}

static void writeSleepPendingFlag()
{
  if (!g_sdReady)
    return;

  if (!SD.exists("/raising_hell"))
  {
    if (!SD.mkdir("/raising_hell"))
      return;
  }
  if (!SD.exists("/raising_hell/save"))
  {
    if (!SD.mkdir("/raising_hell/save"))
      return;
  }

  File f = SD.open(SLEEP_PENDING_FLAG_PATH, FILE_WRITE);
  if (f)
  {
    f.print("1");
    f.close();
  }
}

static void clearSleepPendingFlag()
{
  if (!g_sdReady)
    return;
  if (SD.exists(SLEEP_PENDING_FLAG_PATH))
    SD.remove(SLEEP_PENDING_FLAG_PATH);
}

static void applySleepStateFromBootFlag()
{
  if (!sleepPendingFlagExists())
    return;

  pet.isSleeping = true;
  g_app.isSleeping = true;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  // Do NOT change UI state here.
  // Boot flow will decide final landing, and then we can override it once.
  Serial.println("[SAVE] restored sleeping pet from sleep_pending.flag");
}

static void rotateSaveBackups()
{
  if (!g_sdReady)
    return;

  if (SD.exists(SAVE_BAK3_PATH))
    SD.remove(SAVE_BAK3_PATH);
  if (SD.exists(SAVE_BAK2_PATH))
    SD.rename(SAVE_BAK2_PATH, SAVE_BAK3_PATH);
  if (SD.exists(SAVE_BAK1_PATH))
    SD.rename(SAVE_BAK1_PATH, SAVE_BAK2_PATH);
  if (SD.exists(SAVE_PATH))
    SD.rename(SAVE_PATH, SAVE_BAK1_PATH);
}

static void forceChoosePetFlowFromBoot()
{
  // Clean slate: absolutely no live pet should exist before egg selection.
  saveManagerDeletePetOnly();

  g_app.newPetFlowActive = false;
  clearNamePendingFlag();
  clearSleepPendingFlag();

  inputSetTextCapture(false);
  g_textCaptureMode = false;

  g_choosePetInputUnlockMs = millis() + 350;
  g_choosePetBlockHatchUntilRelease = true;

  g_pendingPetType = PET_DEVIL;
  pet.type = g_pendingPetType;

  uiActionEnterState(UIState::CHOOSE_PET, Tab::TAB_PET, true);

  g_app.uiNeedsRedraw = true;

  clearInputLatch();
}

void saveManagerAbortFreshPetFlow()
{
  // Abort any half-started new-pet lifecycle cleanly.
  clearNamePendingFlag();

  // Ensure there is no live pet save to "continue".
  saveManagerDeletePetOnly();

  // Return runtime to a clean no-save state.
  resetRuntimeToCleanNoSaveState(/*resetName=*/true);

  inputSetTextCapture(false);
  g_textCaptureMode = false;
  g_app.newPetFlowActive = false;

  dirty = false;
  clearInputLatch();

  Serial.println("[SAVE] aborted fresh pet flow");
}

void saveManagerAssignFreshPetId()
{
  pet.petId = generatePetId();
  Serial.printf("[PETID] assigned fresh petId=%016llX\n", (unsigned long long)pet.petId);
  saveManagerMarkDirty();
}

// ------------------------------------------------------------
// Forward decls
// ------------------------------------------------------------
static void printState(const char *tag);
static void pack(SavePayload &p);
static void unpack(const SavePayload &p);
static void newPetInternal();
static bool ensureSaveDir();
static void tryRemove(const char *path);
static void clearNamePendingFlag();
static bool sleepPendingFlagExists();
static void writeSleepPendingFlag();
static void clearSleepPendingFlag();
static void applySleepStateFromBootFlag();

static bool loadSettingsFromSD_internal(bool *outLoadedOld = nullptr);
static bool saveSettingsToSD_internal();
static bool saveGameOptionsToSD_internal();

static bool loadSaveFileInternal(const char *path);
static bool loadSaveFromSD_internal();
static bool saveSaveToSD_internal();

// ------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------
static void printState(const char *tag)
{
  DBG_ON("[%s] sdReady=%d dirty=%d lastSaveMs=%lu now=%lu\n", tag, (int)g_sdReady, (int)dirty,
         (unsigned long)lastSaveMs, (unsigned long)millis());

  DBG_ON("[%s] pet(h=%d m=%d e=%d hp=%d type=%d sleep=%d inf=%d) invCount=%d sel=%d\n", tag, pet.hunger, pet.happiness,
         pet.energy, pet.health, (int)pet.type, (int)pet.isSleeping, (int)pet.inf, g_app.inventory.countItems(),
         g_app.inventory.selectedIndex);
}

void saveManagerClearNamePendingFlag() { clearNamePendingFlag(); }
bool saveManagerNamePendingFlagExists() { return namePendingFlagExists(); }
void saveManagerSetSleepPendingFlag() { writeSleepPendingFlag(); }
void saveManagerClearSleepPendingFlag() { clearSleepPendingFlag(); }
bool saveManagerSleepPendingFlagExists() { return sleepPendingFlagExists(); }

void saveManagerEnterSleepState()
{
  pet.isSleeping = true;
  g_app.isSleeping = true;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  // Critical: free large UI caches before entering the sleeping screen.
  graphicsReleaseUiCachesForMiniGame();
  Serial.printf("[HEAPCHK] sleep-enter after-release free=%u largest=%u\n",
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT));

  writeSleepPendingFlag();
  saveManagerMarkDirty();

  uiActionEnterState(UIState::PET_SLEEPING, Tab::TAB_PET, true);
  g_app.uiNeedsRedraw = true;

  Serial.println("[SAVE] sleep entered + sleep_pending.flag set");
}

// Back-compat: older code called this name.
static void removeNamePendingFlag() { clearNamePendingFlag(); }

// ------------------------------------------------------------
// GAME OPTIONS (separate file so we don't break SettingsData)
// ------------------------------------------------------------
static const uint32_t GAMEOPT_MAGIC = 0x47504F54; // 'GPOT'
static const uint16_t GAMEOPT_VERSION = 2;

struct GameOptionsData
{
  uint32_t magic;
  uint16_t version;
  uint8_t decayMode; // 0=Super Slow, 1=Slow, 2=Normal, 3=Fast, 4=Super Fast, 5=Insane
  uint8_t _pad;      // alignment
};

static GameOptionsData g_gameopt = {GAMEOPT_MAGIC, GAMEOPT_VERSION, 2, 0};

static void gameoptDefaults()
{
  g_gameopt.magic = GAMEOPT_MAGIC;
  g_gameopt.version = GAMEOPT_VERSION;
  g_gameopt.decayMode = 2; // Normal
}

static bool loadGameOptionsFromSD_internal()
{
  if (!g_sdReady)
    return false;
  if (!ensureSaveDir())
    return false;

  if (!SD.exists(GAMEOPT_PATH))
  {
    DBG_ON("[SAVE] gameopt missing, using defaults\n");
    return false;
  }

  File f = SD.open(GAMEOPT_PATH, FILE_READ);
  if (!f)
  {
    DBG_ON("[SAVE] gameopt open failed\n");
    return false;
  }

  if ((size_t)f.size() != sizeof(GameOptionsData))
  {
    f.close();
    return false;
  }

  GameOptionsData tmp{};
  const int r = f.read((uint8_t *)&tmp, sizeof(tmp));
  f.close();

  if (r != (int)sizeof(tmp))
    return false;
  if (tmp.magic != GAMEOPT_MAGIC)
    return false;

  // Accept old version 1 (0=Normal,1=Slow,2=Off) and new version 2 (0..5)
  if (tmp.version != 1 && tmp.version != GAMEOPT_VERSION)
    return false;

  // Migration v1 -> v2
  if (tmp.version == 1)
  {
    if (tmp.decayMode == 0)
      tmp.decayMode = 2;
    else if (tmp.decayMode == 1)
      tmp.decayMode = 1;
    else
      tmp.decayMode = 0;
    tmp.version = GAMEOPT_VERSION;
  }

  if (tmp.decayMode > 5)
    tmp.decayMode = 2;

  g_gameopt = tmp;
  return true;
}

static bool saveGameOptionsToSD_internal()
{
  if (!g_sdReady)
    return false;
  if (!ensureSaveDir())
    return false;

  tryRemove(GAMEOPT_TMP_PATH);

  File f = SD.open(GAMEOPT_TMP_PATH, FILE_WRITE);
  if (!f)
    return false;

  const size_t w = f.write((const uint8_t *)&g_gameopt, sizeof(g_gameopt));
  f.flush();
  f.close();

  if (w != sizeof(g_gameopt))
  {
    tryRemove(GAMEOPT_TMP_PATH);
    return false;
  }

  tryRemove(GAMEOPT_PATH);
  if (!SD.rename(GAMEOPT_TMP_PATH, GAMEOPT_PATH))
  {
    DBG_ON("[SAVE] rename failed: %s -> %s\n", GAMEOPT_TMP_PATH, GAMEOPT_PATH);
    tryRemove(GAMEOPT_TMP_PATH);
    return false;
  }

  return true;
}

// ============================================================
// PET INIT PAYLOAD
// ============================================================
static uint32_t getNowEpochOrZero()
{
  time_t now = time(nullptr);
  return (now > 1700000000) ? (uint32_t)now : 0;
}

static uint64_t generatePetId()
{
  uint64_t hi = (uint64_t)esp_random();
  uint64_t lo = (uint64_t)esp_random();
  uint64_t id = (hi << 32) | lo;

  if (id == 0)
    id = ((uint64_t)getNowEpochOrZero() << 32) | 0xA5A5A5A5ULL;

  return id;
}

SavePayload makeDefaultSavePayload()
{
  SavePayload p{};
  p.magic = SAVE_MAGIC;
  p.version = SAVE_VERSION;

  p.pet.hunger = 50;
  p.pet.happiness = 100;
  p.pet.energy = 100;
  p.pet.health = 100;

  p.pet.petType = 0;
  p.pet.isSleeping = 0;
  p.pet.lastFedTimeMs = 0;
  p.pet.inf = 0;

  p.pet.level = 1;
  p.pet.xp = 0;
  p.pet.evoStage = 0;

  strcpy(p.pet.name, "Bub");
  p.pet.petId = generatePetId();

  memset(&p.inv, 0, sizeof(p.inv));
  p.inv.selectedIndex = 0;

  p.inv.slots[0].type = (uint8_t)ITEM_SOUL_FOOD;
  p.inv.slots[0].qty = 3;

  p.inv.slots[1].type = (uint8_t)ITEM_CURSED_RELIC;
  p.inv.slots[1].qty = 1;

  p.inv.slots[2].type = (uint8_t)ITEM_DEMON_BONE;
  p.inv.slots[2].qty = 1;

  p.inv.slots[3].type = (uint8_t)ITEM_ELDRITCH_EYE;
  p.inv.slots[3].qty = 0;

  p.birth_epoch = getNowEpochOrZero();
  return p;
}

static void migrateV2ToRuntime(const SavePayloadV2 &p2)
{
  PetPersist p3{};
  p3.hunger = p2.pet.hunger;
  p3.happiness = p2.pet.happiness;
  p3.energy = p2.pet.energy;
  p3.health = p2.pet.health;
  p3.petType = p2.pet.petType;
  p3.isSleeping = 0;
  p3.lastFedTimeMs = p2.pet.lastFedTimeMs;
  p3.inf = p2.pet.inf;
  p3.birth_epoch = p2.pet.birth_epoch;

  memcpy(p3.name, p2.pet.name, sizeof(p3.name));
  p3.name[PET_NAME_MAX] = '\0';

  p3.level = 1;
  p3.xp = 0;
  p3.evoStage = 0;

  pet.fromPersist(p3);

  pet.isSleeping = false;
  g_app.isSleeping = false;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  if (g_app.uiState == UIState::PET_SLEEPING)
  {
    uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
  }

  applySleepStateFromBootFlag();

  g_app.inventory.fromPersist(p2.inv);

  g_birthEpoch = (p2.birth_epoch != 0) ? p2.birth_epoch : p2.pet.birth_epoch;

  saveManagerMarkDirty();
  g_app.uiNeedsRedraw = true;
}

static void migrateV3ToRuntime(const SavePayloadV3 &p3)
{
  PetPersist p4{};
  p4.hunger = p3.pet.hunger;
  p4.happiness = p3.pet.happiness;
  p4.energy = p3.pet.energy;
  p4.health = p3.pet.health;
  p4.petType = p3.pet.petType;
  p4.isSleeping = p3.pet.isSleeping;
  p4.lastFedTimeMs = p3.pet.lastFedTimeMs;
  p4.inf = p3.pet.inf;
  p4.birth_epoch = p3.pet.birth_epoch;

  memcpy(p4.name, p3.pet.name, sizeof(p4.name));
  p4.name[PET_NAME_MAX] = '\0';

  p4.petId = generatePetId();

  // True 1.0.3 layout decode
  p4.level = (p3.pet.level == 0) ? 1 : p3.pet.level;
  p4.xp = p3.pet.xp;
  p4.evoStage = (p3.pet.evoStage > 3) ? 0 : p3.pet.evoStage;

  pet.fromPersist(p4);

  pet.isSleeping = p3.pet.isSleeping;
  g_app.isSleeping = pet.isSleeping;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  if (g_app.uiState == UIState::PET_SLEEPING)
  {
    uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
  }

  applySleepStateFromBootFlag();

  g_app.inventory.clear();

  for (int i = 0; i < SAVE_INV_MAX_ITEMS_V3; ++i)
  {
    const uint8_t t = p3.inv.slots[i].type;
    const uint8_t q = p3.inv.slots[i].qty;

    if (t == (uint8_t)ITEM_NONE || q == 0)
      continue;

    if (t > (uint8_t)ITEM_ELDRITCH_EYE)
    {
      Serial.printf("[SAVE][V3] skip invalid inv slot=%d type=%u qty=%u\n", i, (unsigned)t, (unsigned)q);
      continue;
    }

    g_app.inventory.addItem((ItemType)t, (int)q);
  }

  const int invCount = g_app.inventory.countItems();
  if (invCount <= 0)
    g_app.inventory.selectedIndex = 0;
  else
    g_app.inventory.selectedIndex = constrain((int)p3.inv.selectedIndex, 0, invCount - 1);

  g_app.inventory.syncEepromNoDirty();

  g_birthEpoch = (p3.birth_epoch != 0) ? p3.birth_epoch : p3.pet.birth_epoch;

  saveManagerMarkDirty();
  g_app.uiNeedsRedraw = true;
}

static void pack(SavePayload &p)
{
  memset(&p, 0, sizeof(p));
  p.magic = SAVE_MAGIC;
  p.version = SAVE_VERSION;

  uint32_t be = g_birthEpoch;
  if (be == 0)
    be = getNowEpochOrZero();
  g_birthEpoch = be;

  p.birth_epoch = be;

  pet.birth_epoch = be;

  pet.toPersist(p.pet);
  p.pet.birth_epoch = be;

  g_app.inventory.toPersist(p.inv);
}

static void unpack(const SavePayload &p)
{
  pet.fromPersist(p.pet);
  if (pet.petId == 0)
    pet.petId = generatePetId();
  g_app.inventory.fromPersist(p.inv);
  // Keep the EEPROM mirror in sync with the authoritative payload.
  // This prevents stale inventory from leaking across factory reset/death.
  g_app.inventory.syncEepromNoDirty();
  // Default to awake, then let boot flags override.
  pet.isSleeping = false;
  g_app.isSleeping = false;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  applySleepStateFromBootFlag();

  uint32_t be = p.birth_epoch;
  if (be == 0)
    be = getNowEpochOrZero();
  g_birthEpoch = be;

  pet.birth_epoch = be;
}

static void newPetInternal()
{
  SavePayload p = makeDefaultSavePayload();
  unpack(p);

  dirty = true;
  (void)saveManagerSave();
}

static bool ensureSaveDir()
{
  if (!g_sdReady)
    return false;
  if (SD.exists(SAVE_DIR))
    return true;

  if (!SD.exists("/raising_hell"))
  {
    if (!SD.mkdir("/raising_hell"))
      return false;
  }
  if (!SD.exists(SAVE_DIR))
  {
    if (!SD.mkdir(SAVE_DIR))
      return false;
  }
  return true;
}

// ------------------------------------------------------------
// SETTINGS IO (supports old settings.bin upgrade)
// ------------------------------------------------------------
static bool loadSettingsFromSD_internal(bool *outLoadedOld)
{
  if (outLoadedOld)
    *outLoadedOld = false;

  if (!g_sdReady)
    return false;
  if (!ensureSaveDir())
    return false;

  File f = SD.open(SET_PATH, FILE_READ);
  if (!f)
    return false;

  const size_t sz = (size_t)f.size();
  const size_t NEW_SZ = sizeof(SettingsData);

  const size_t OLD_SZ_4 = 4;
  const size_t OLD_SZ_5 = 5;
  const size_t OLD_SZ_7 = 7;
  const size_t OLD_SZ_8 = 8;
  const size_t OLD_SZ_11 = 11;

  SettingsData tmp{};
  bool ok = false;
  bool loadedOld = false;

  if (sz == NEW_SZ)
  {
    const int r = f.read((uint8_t *)&tmp, NEW_SZ);
    if (r == (int)NEW_SZ)
    {
      ok = true;

      if (tmp.brightnessLevel > 2)
        tmp.brightnessLevel = 1;
      if (tmp.tzIndex > 64)
        tmp.tzIndex = 2;
      if (tmp.autoScreenTimeoutSel > 3)
        tmp.autoScreenTimeoutSel = 0;
      if (tmp.shakeSensitivitySel > 3)
        tmp.shakeSensitivitySel = 1;
      tmp.autoScreenOffEnabled = (tmp.autoScreenTimeoutSel != 0);

      tmp.soundEnabled = (tmp.soundEnabled != 0);
      tmp.wifiEnabled = (tmp.wifiEnabled != 0);
      tmp.petDeathEnabled = (tmp.petDeathEnabled != 0);
      tmp.ledAlertsEnabled = (tmp.ledAlertsEnabled != 0);
      tmp.controlsHelpSeen = (tmp.controlsHelpSeen != 0);
      tmp.petScreenIntroFadeBootFlag = (tmp.petScreenIntroFadeBootFlag != 0);
    }
  }
  else if (sz == OLD_SZ_11)
  {
    uint8_t old[OLD_SZ_11];
    const int r = f.read(old, OLD_SZ_11);
    if (r == (int)OLD_SZ_11)
    {
      tmp.brightnessLevel = old[0];
      tmp.autoScreenOffEnabled = (old[1] != 0);
      tmp.soundEnabled = (old[2] != 0);
      tmp.wifiEnabled = (old[3] != 0);
      tmp.tzIndex = old[4];
      tmp.autoScreenTimeoutSel = old[5];
      tmp.petDeathEnabled = (old[6] != 0);
      tmp.ledAlertsEnabled = (old[7] != 0);
      tmp.controlsHelpSeen = (old[8] != 0);
      tmp.petPerfHudEnabled = (old[9] != 0);
      tmp.petScreenIntroFadeBootFlag = (old[10] != 0);
      tmp.shakeSensitivitySel = 1; // default old behavior = Low

      if (tmp.brightnessLevel > 2)
        tmp.brightnessLevel = 1;
      if (tmp.tzIndex > 64)
        tmp.tzIndex = 2;
      if (tmp.autoScreenTimeoutSel > 3)
        tmp.autoScreenTimeoutSel = 0;

      tmp.autoScreenOffEnabled = (tmp.autoScreenTimeoutSel != 0);

      ok = true;
      loadedOld = true;
    }
  }
  else if (sz == OLD_SZ_7)
  {
    uint8_t old[OLD_SZ_7];
    const int r = f.read(old, OLD_SZ_7);
    if (r == (int)OLD_SZ_7)
    {
      tmp.brightnessLevel = old[0];
      tmp.autoScreenOffEnabled = (old[1] != 0);
      tmp.soundEnabled = (old[2] != 0);
      tmp.wifiEnabled = (old[3] != 0);
      tmp.tzIndex = old[4];
      tmp.autoScreenTimeoutSel = old[5];
      tmp.petDeathEnabled = (old[6] != 0);

      tmp.shakeSensitivitySel = 1;
      tmp.ledAlertsEnabled = 1;
      tmp.controlsHelpSeen = 0;
      tmp.petScreenIntroFadeBootFlag = 0;

      if (tmp.brightnessLevel > 2)
        tmp.brightnessLevel = 1;
      if (tmp.tzIndex > 64)
        tmp.tzIndex = 2;
      if (tmp.autoScreenTimeoutSel > 3)
        tmp.autoScreenTimeoutSel = 0;
      tmp.autoScreenOffEnabled = (tmp.autoScreenTimeoutSel != 0);

      ok = true;
      loadedOld = true;
    }
  }
  else if (sz == OLD_SZ_8)
  {
    uint8_t old[OLD_SZ_8];
    const int r = f.read(old, OLD_SZ_8);
    if (r == (int)OLD_SZ_8)
    {
      tmp.brightnessLevel = old[0];
      tmp.autoScreenOffEnabled = (old[1] != 0);
      tmp.soundEnabled = (old[2] != 0);
      tmp.wifiEnabled = (old[3] != 0);
      tmp.tzIndex = old[4];
      tmp.autoScreenTimeoutSel = old[5];
      tmp.petDeathEnabled = (old[6] != 0);
      tmp.ledAlertsEnabled = (old[7] != 0);

      tmp.controlsHelpSeen = 0;
      tmp.shakeSensitivitySel = 1;
      tmp.petScreenIntroFadeBootFlag = 0;

      if (tmp.brightnessLevel > 2)
        tmp.brightnessLevel = 1;
      if (tmp.tzIndex > 64)
        tmp.tzIndex = 2;
      if (tmp.autoScreenTimeoutSel > 3)
        tmp.autoScreenTimeoutSel = 0;
      tmp.autoScreenOffEnabled = (tmp.autoScreenTimeoutSel != 0);

      ok = true;
      loadedOld = true;
    }
  }
  else if (sz == OLD_SZ_5)
  {
    uint8_t old[OLD_SZ_5];
    const int r = f.read(old, OLD_SZ_5);
    if (r == (int)OLD_SZ_5)
    {
      tmp.brightnessLevel = old[0];
      tmp.autoScreenOffEnabled = (old[1] != 0);
      tmp.soundEnabled = (old[2] != 0);
      tmp.wifiEnabled = (old[3] != 0);
      tmp.tzIndex = old[4];

      tmp.autoScreenTimeoutSel = tmp.autoScreenOffEnabled ? 2 : 0;

      tmp.petDeathEnabled = 1;
      tmp.ledAlertsEnabled = 1;
      tmp.controlsHelpSeen = 0;
      tmp.shakeSensitivitySel = 1;
      tmp.petScreenIntroFadeBootFlag = 0;

      if (tmp.brightnessLevel > 2)
        tmp.brightnessLevel = 1;
      if (tmp.tzIndex > 64)
        tmp.tzIndex = 2;

      ok = true;
      loadedOld = true;
    }
  }
  else if (sz == OLD_SZ_4)
  {
    uint8_t old[OLD_SZ_4];
    const int r = f.read(old, OLD_SZ_4);
    if (r == (int)OLD_SZ_4)
    {
      tmp.brightnessLevel = old[0];
      tmp.autoScreenOffEnabled = (old[1] != 0);
      tmp.soundEnabled = (old[2] != 0);
      tmp.wifiEnabled = (old[3] != 0);

      tmp.tzIndex = 2;
      tmp.autoScreenTimeoutSel = tmp.autoScreenOffEnabled ? 2 : 0;
      tmp.petDeathEnabled = 1;
      tmp.ledAlertsEnabled = 1;
      tmp.controlsHelpSeen = 0;
      tmp.shakeSensitivitySel = 1;
      tmp.petScreenIntroFadeBootFlag = 0;

      if (tmp.brightnessLevel > 2)
        tmp.brightnessLevel = 1;

      ok = true;
      loadedOld = true;
    }
  }
  else
  {
    DBG_ON("[SAVE] settings size unsupported: %u\n", (unsigned)sz);
  }

  f.close();

  if (!ok)
  {
    DBG_ON("[SAVE] settings read failed size=%u\n", (unsigned)sz);
    return false;
  }

  if (outLoadedOld)
    *outLoadedOld = loadedOld;

  g_settings = tmp;

  brightnessLevel = g_settings.brightnessLevel;
  if (brightnessLevel > 2)
    brightnessLevel = 1;

  displayRememberUserBrightness(brightnessLevel);

  soundEnabled = (g_settings.soundEnabled != 0);

  autoScreenTimeoutSel = g_settings.autoScreenTimeoutSel;
  g_app.autoScreenOffEnabled = (autoScreenTimeoutSel != 0);
  motionSetShakeSensitivity(g_settings.shakeSensitivitySel);

  petDeathEnabled = (g_settings.petDeathEnabled != 0);
  ledAlertsEnabled = (g_settings.ledAlertsEnabled != 0);

  g_controlsHelpSeen = (g_settings.controlsHelpSeen != 0);

  // ------------------------------------------------------------
  // One-shot boot flag: pet intro fade
  // ------------------------------------------------------------
  if (g_settings.petScreenIntroFadeBootFlag)
  {
    g_app.petScreenIntroFadePending = true;

    // clear it so it's one-shot
    g_settings.petScreenIntroFadeBootFlag = 0;

    // persist immediately so it doesn't retrigger next boot
    saveSettingsToSD_internal();

    Serial.println("[BOOT] pet intro fade armed (one-shot)");
  }

  uint8_t nvsTz;
  if (loadTzIndexFromNVS(&nvsTz))
  {
    tzIndex = nvsTz;
  }
  else
  {
    tzIndex = g_settings.tzIndex;
    saveTzIndexToNVS(tzIndex);
  }

  applyTimezoneIndex(tzIndex);

  const bool wifiEn = (g_settings.wifiEnabled != 0);

  // Do not apply Wi-Fi hardware/runtime state here.
  // Boot pipeline is the single owner of actual Wi-Fi bring-up.
  // Applying it here makes later boot logic think Wi-Fi is already enabled
  // even when the radio/connect path has not actually been restarted.
  Serial.printf("[WIFI LOAD] setting=%d runtime=%d (deferred apply)\n", wifiEn ? 1 : 0, wifiIsEnabled() ? 1 : 0);

  return true;
}

static bool saveSettingsToSD_internal()
{
  if (!g_sdReady)
    return false;
  if (!ensureSaveDir())
    return false;

  g_settings.brightnessLevel = displayGetUserBrightnessLevel();
  g_settings.autoScreenOffEnabled = g_app.autoScreenOffEnabled;
  g_settings.shakeSensitivitySel = motionGetShakeSensitivity();
  g_settings.soundEnabled = soundEnabled;
  g_settings.wifiEnabled = settingsWifiEnabled() ? 1 : 0;

  Serial.printf("[WIFI SAVE] setting=%d runtime=%d persisted=%d\n", settingsWifiEnabled() ? 1 : 0,
                wifiIsEnabled() ? 1 : 0, g_settings.wifiEnabled ? 1 : 0);
  g_settings.tzIndex = tzIndex;

  g_settings.autoScreenTimeoutSel = autoScreenTimeoutSel;
  g_settings.autoScreenOffEnabled = (autoScreenTimeoutSel != 0);
  g_settings.petDeathEnabled = petDeathEnabled ? 1 : 0;
  g_settings.ledAlertsEnabled = ledAlertsEnabled ? 1 : 0;
  g_settings.controlsHelpSeen = (g_controlsHelpSeen != 0) ? 1 : 0;
  g_settings.petScreenIntroFadeBootFlag = (g_settings.petScreenIntroFadeBootFlag != 0) ? 1 : 0;

  tryRemove(SET_TMP_PATH);

  File f = SD.open(SET_TMP_PATH, FILE_WRITE);
  if (!f)
    return false;

  const size_t w = f.write((const uint8_t *)&g_settings, sizeof(SettingsData));
  f.flush();
  f.close();

  if (w != sizeof(SettingsData))
  {
    tryRemove(SET_TMP_PATH);
    return false;
  }

  tryRemove(SET_PATH);
  if (!SD.rename(SET_TMP_PATH, SET_PATH))
  {
    DBG_ON("[SAVE] rename failed: %s -> %s\n", SET_TMP_PATH, SET_PATH);
    tryRemove(SET_TMP_PATH);
    return false;
  }

  return true;
}

static void newPetInternalNoSave(bool resetName)
{
  SavePayload p = makeDefaultSavePayload();
  unpack(p);

  pet.petId = generatePetId();
  Serial.printf("[PETID] new pet assigned %016llX\n", (unsigned long long)pet.petId);

  // New pets always start on Normal decay mode.
  g_gameopt.decayMode = 2;
  saveGameOptionsToSD_internal();

  if (resetName)
  {
    // Ensure the runtime pet name is blank for a fresh flow.
    pet.name[0] = '\0';
  }

  writeNamePendingFlag();
}

void resetRuntimeToCleanNoSaveState(bool resetName)
{
  (void)resetName;

  // ------------------------------------------------------------------
  // TRUE no-save state:
  // No pet payload, no stats, nothing that could be persisted.
  // ------------------------------------------------------------------

  // Completely clear pet runtime
  pet = Pet();

  // Clear all sleep/runtime flags
  g_app.isSleeping = false;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  // No birth time
  g_birthEpoch = 0;

  // Clean inventory
  g_app.inventory.init();
  g_app.inventory.selectedIndex = 0;
  g_app.inventory.syncEepromNoDirty();

  // Not in new pet flow
  g_app.newPetFlowActive = false;

  // Ensure no pending flags exist
  clearNamePendingFlag();
  clearSleepPendingFlag();

  // Keep decay sane
  g_gameopt.decayMode = 2;
  saveGameOptionsToSD_internal();

  // CRITICAL: do NOT mark dirty
  dirty = false;

  Serial.println("[SAVE] runtime reset to TRUE no-save state");
}

static void clearNamePendingFlag()
{
  if (!g_sdReady)
    return;
  if (SD.exists(NAME_PENDING_FLAG_PATH))
    SD.remove(NAME_PENDING_FLAG_PATH);
}

// ------------------------------------------------------------
// SAVEGAME IO (SavePayload)
// ------------------------------------------------------------
static bool loadSaveFileInternal(const char *path)
{
  g_lastLoadErr = SLE_OK;
  g_lastLoadSize = 0;
  g_lastLoadPath = nullptr;
  g_lastLoadUsedBackup = false;

  if (!g_sdReady)
  {
    g_lastLoadErr = SLE_SD_NOT_READY;
    Serial.printf("[SAVE] FAIL path=%s reason=sd_not_ready\n", path);
    return false;
  }

  if (!ensureSaveDir())
  {
    g_lastLoadErr = SLE_DIR_FAIL;
    Serial.printf("[SAVE] FAIL path=%s reason=dir_fail\n", path);
    return false;
  }

  File f = SD.open(path, FILE_READ);
  if (!f)
  {
    g_lastLoadErr = SLE_OPEN_FAIL;
    Serial.printf("[SAVE] FAIL path=%s reason=open_fail\n", path);
    return false;
  }

  const size_t sz = (size_t)f.size();
  g_lastLoadSize = (uint32_t)sz;

  if (sz == sizeof(SavePayload))
  {
    SavePayload p{};
    memset(&p, 0, sizeof(p));

    const size_t want = sizeof(p);
    const int r = f.read((uint8_t *)&p, want);
    f.close();

    if (r != (int)want)
    {
      g_lastLoadErr = SLE_READ_FAIL;
      Serial.printf("[SAVE] FAIL path=%s reason=read_fail got=%d want=%u size=%lu\n", path, r, (unsigned)want,
                    (unsigned long)sz);
      return false;
    }

    if (p.magic != SAVE_MAGIC)
    {
      g_lastLoadErr = SLE_MAGIC_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=magic_bad got=0x%08lx want=0x%08lx size=%lu\n", path,
                    (unsigned long)p.magic, (unsigned long)SAVE_MAGIC, (unsigned long)sz);
      return false;
    }

    if (p.version != SAVE_VERSION && p.version != 0)
    {
      g_lastLoadErr = SLE_VERSION_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=version_bad got=%u want=%u size=%lu\n", path, (unsigned)p.version,
                    (unsigned)SAVE_VERSION, (unsigned long)sz);
      return false;
    }
    // Reject obviously malformed payloads before they touch runtime.
    if (p.pet.petType >= PET_TYPE_COUNT)
    {
      g_lastLoadErr = SLE_VERSION_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=pet_type_bad got=%u size=%lu\n", path, (unsigned)p.pet.petType,
                    (unsigned long)sz);
      return false;
    }

    if (p.pet.evoStage > 3)
    {
      g_lastLoadErr = SLE_VERSION_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=evo_bad got=%u size=%lu\n", path, (unsigned)p.pet.evoStage,
                    (unsigned long)sz);
      return false;
    }

    unpack(p);
  }
  else if (sz == sizeof(SavePayloadV3))
  {
    SavePayloadV3 p3{};
    memset(&p3, 0, sizeof(p3));

    const size_t want = sizeof(p3);
    const int r = f.read((uint8_t *)&p3, want);
    f.close();

    if (r != (int)want)
    {
      g_lastLoadErr = SLE_READ_FAIL;
      Serial.printf("[SAVE] FAIL path=%s reason=read_fail_v3 got=%d want=%u size=%lu\n", path, r, (unsigned)want,
                    (unsigned long)sz);
      return false;
    }

    if (p3.magic != SAVE_MAGIC)
    {
      g_lastLoadErr = SLE_MAGIC_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=magic_bad_v3 got=0x%08lx want=0x%08lx size=%lu\n", path,
                    (unsigned long)p3.magic, (unsigned long)SAVE_MAGIC, (unsigned long)sz);
      return false;
    }

    // STRICT: only true V3 layout allowed here
    if (p3.version != 3)
    {
      g_lastLoadErr = SLE_VERSION_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=version_bad_v3 got=%u want=3 size=%lu\n", path, (unsigned)p3.version,
                    (unsigned long)sz);
      return false;
    }

    migrateV3ToRuntime(p3);
  }
  else if (sz == sizeof(SavePayloadV2))
  {
    SavePayloadV2 p2{};
    memset(&p2, 0, sizeof(p2));

    const size_t want = sizeof(p2);
    const int r = f.read((uint8_t *)&p2, want);
    f.close();

    if (r != (int)want)
    {
      g_lastLoadErr = SLE_READ_FAIL;
      Serial.printf("[SAVE] FAIL path=%s reason=read_fail_v2 got=%d want=%u size=%lu\n", path, r, (unsigned)want,
                    (unsigned long)sz);
      return false;
    }

    if (p2.magic != SAVE_MAGIC)
    {
      g_lastLoadErr = SLE_MAGIC_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=magic_bad_v2 got=0x%08lx want=0x%08lx size=%lu\n", path,
                    (unsigned long)p2.magic, (unsigned long)SAVE_MAGIC, (unsigned long)sz);
      return false;
    }

    if (p2.version != 2)
    {
      g_lastLoadErr = SLE_VERSION_BAD;
      Serial.printf("[SAVE] FAIL path=%s reason=version_bad_v2 got=%u want=2 size=%lu\n", path, (unsigned)p2.version,
                    (unsigned long)sz);
      return false;
    }

    migrateV2ToRuntime(p2);
  }
  else
  {
    g_lastLoadErr = SLE_READ_FAIL;
    Serial.printf("[SAVE] FAIL path=%s reason=payload_size_bad size=%lu need=%u/%u/%u\n", path, (unsigned long)sz,
                  (unsigned)sizeof(SavePayload), (unsigned)sizeof(SavePayloadV3), (unsigned)sizeof(SavePayloadV2));
    f.close();
    return false;
  }

  g_lastLoadPath = path;
  g_lastLoadUsedBackup = (strcmp(path, SAVE_PATH) != 0);

  Serial.printf("[SAVE] load OK path=%s size=%lu backup=%d\n", path, (unsigned long)g_lastLoadSize,
                g_lastLoadUsedBackup ? 1 : 0);

  return true;
}

static bool loadSaveFromSD_internal()
{
  Serial.printf("[SAVE] trying primary path=%s\n", SAVE_PATH);
  if (loadSaveFileInternal(SAVE_PATH))
  {
    Serial.println("[SAVE] primary load OK");
    return true;
  }

  Serial.printf("[SAVE] primary load failed err=%u size=%lu\n", (unsigned)g_lastLoadErr, (unsigned long)g_lastLoadSize);

  Serial.printf("[SAVE] trying backup 1 path=%s\n", SAVE_BAK1_PATH);
  if (loadSaveFileInternal(SAVE_BAK1_PATH))
  {
    Serial.println("[SAVE] backup 1 load OK");
    return true;
  }

  Serial.printf("[SAVE] backup 1 failed err=%u size=%lu\n", (unsigned)g_lastLoadErr, (unsigned long)g_lastLoadSize);

  Serial.printf("[SAVE] trying backup 2 path=%s\n", SAVE_BAK2_PATH);
  if (loadSaveFileInternal(SAVE_BAK2_PATH))
  {
    Serial.println("[SAVE] backup 2 load OK");
    return true;
  }

  Serial.printf("[SAVE] backup 2 failed err=%u size=%lu\n", (unsigned)g_lastLoadErr, (unsigned long)g_lastLoadSize);

  Serial.printf("[SAVE] trying backup 3 path=%s\n", SAVE_BAK3_PATH);
  if (loadSaveFileInternal(SAVE_BAK3_PATH))
  {
    Serial.println("[SAVE] backup 3 load OK");
    return true;
  }

  Serial.printf("[SAVE] backup 3 failed err=%u size=%lu\n", (unsigned)g_lastLoadErr, (unsigned long)g_lastLoadSize);

  Serial.printf("[SAVE] all save candidates failed final err=%u size=%lu\n", (unsigned)g_lastLoadErr,
                (unsigned long)g_lastLoadSize);
  return false;
}

static bool saveSaveToSD_internal()
{
  if (!g_sdReady)
    return false;
  if (!ensureSaveDir())
    return false;

  // 1) Export current pet to the locker/export system.
  //
  // Contract:
  // - This function removes the live on-disk save after a successful export.
  // - It does NOT reset runtime/UI state; callers must do that separately.  if (g_birthEpoch == 0)
  {
    Serial.println("[SAVE] SKIP (no active pet)");
    return true;
  }

  if (namePendingFlagExists())
  {
    static uint32_t lastSkipLogMs = 0;
    const uint32_t now = millis();

    if (now - lastSkipLogMs > 2000)
    {
      Serial.println("[SAVE] SKIP (pending new-pet flow)");
      lastSkipLogMs = now;
    }

    return true;
  }

  SavePayload p{};
  pack(p);

  tryRemove(SAVE_TMP_PATH);

  File f = SD.open(SAVE_TMP_PATH, FILE_WRITE);
  if (!f)
    return false;

  const size_t w = f.write((const uint8_t *)&p, sizeof(p));
  f.flush();
  f.close();

  if (w != sizeof(p))
  {
    tryRemove(SAVE_TMP_PATH);
    return false;
  }

  rotateSaveBackups();

  if (!SD.rename(SAVE_TMP_PATH, SAVE_PATH))
  {
    DBG_ON("[SAVE] rename failed: %s -> %s\n", SAVE_TMP_PATH, SAVE_PATH);
    tryRemove(SAVE_TMP_PATH);
    return false;
  }

  Serial.printf("[SAVE] WRITE OK path=%s level=%u xp=%lu name='%s' pending=%d\n", SAVE_PATH, (unsigned)pet.level,
                (unsigned long)pet.xp, pet.name, namePendingFlagExists() ? 1 : 0);

  return true;
}

// ------------------------------------------------------------
// Optional init hook
// ------------------------------------------------------------
void saveManagerBegin()
{
  dirty = false;
  lastSaveMs = 0;

  g_settings.brightnessLevel = 1;
  g_settings.autoScreenOffEnabled = true;
  g_settings.soundEnabled = true;
  g_settings.wifiEnabled = 1;
  g_settings.tzIndex = 2;
  g_settings.shakeSensitivitySel = 1; // Low = current behavior
  motionSetShakeSensitivity(1);
  gameoptDefaults();

  DBG_ON("[SAVE] begin sizeof(SavePayload)=%u sizeof(PetPersist)=%u sizeof(InvPersist)=%u sizeof(SettingsData)=%u\n",
         (unsigned)sizeof(SavePayload), (unsigned)sizeof(PetPersist), (unsigned)sizeof(InvPersist),
         (unsigned)sizeof(SettingsData));
}

static bool isBlankName(const char *s)
{
  if (!s)
    return true;
  for (const char *p = s; *p; ++p)
  {
    if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n')
      return false;
  }
  return true;
}

static bool autoHealLoadedSaveIfNeeded()
{
  const bool hadNamePending = namePendingFlagExists();
  const bool hadBlankPetName = isBlankName(pet.name);
  bool changed = false;

  Serial.printf("[SAVE][AUTOHEAL] begin namePending=%d blankPetName=%d name='%s'\n", hadNamePending ? 1 : 0,
                hadBlankPetName ? 1 : 0, pet.name);

  // --------------------------------------------------------------------------
  // Name / pending-flag recovery
  // --------------------------------------------------------------------------
  if (hadBlankPetName)
  {
    pet.setName(AUTO_HEAL_FALLBACK_PET_NAME);

    if (!isBlankName(pet.name))
    {
      Serial.printf("[SAVE][AUTOHEAL] repaired blank pet name -> '%s'\n", pet.name);
      changed = true;
    }
    else
    {
      Serial.println("[SAVE][AUTOHEAL] failed to apply fallback pet name");
    }
  }

  if (namePendingFlagExists() && !isBlankName(pet.name))
  {
    Serial.println("[SAVE][AUTOHEAL] clearing stale name_pending.flag");
    clearNamePendingFlag();
    changed = true;
  }

  // --------------------------------------------------------------------------
  // Core stat clamps
  // --------------------------------------------------------------------------
  const int hungerBefore = pet.hunger;
  const int happyBefore = pet.happiness;
  const int energyBefore = pet.energy;
  const int healthBefore = pet.health;

  pet.clampStats();

  if (pet.hunger != hungerBefore || pet.happiness != happyBefore || pet.energy != energyBefore ||
      pet.health != healthBefore)
  {
    Serial.printf("[SAVE][AUTOHEAL] clamped stats h=%d->%d m=%d->%d e=%d->%d hp=%d->%d\n", hungerBefore, pet.hunger,
                  happyBefore, pet.happiness, energyBefore, pet.energy, healthBefore, pet.health);
    changed = true;
  }

  // --------------------------------------------------------------------------
  // Pet type clamp
  // --------------------------------------------------------------------------
  const int petTypeBefore = (int)pet.type;
  switch (pet.type)
  {
  case PET_DEVIL:
  case PET_ELDRITCH:
    break;

  default:
    pet.type = PET_DEVIL;
    Serial.printf("[SAVE][AUTOHEAL] invalid pet type %d -> %d\n", petTypeBefore, (int)pet.type);
    changed = true;
    break;
  }

  // --------------------------------------------------------------------------
  // Level / XP / evo sanity
  // --------------------------------------------------------------------------
  const uint16_t levelBefore = pet.level;
  const uint32_t xpBefore = pet.xp;
  const uint8_t evoBefore = pet.evoStage;

  if (pet.level < 1)
  {
    pet.level = 1;
    changed = true;
  }
  if (pet.level > 999)
  {
    pet.level = 999;
    changed = true;
  }

  if (pet.evoStage > 3)
  {
    pet.evoStage = 3;
    changed = true;
  }

  const uint32_t nextXp = pet.xpForNextLevel();
  if (nextXp > 0 && pet.xp >= nextXp)
  {
    pet.xp = nextXp - 1;
    changed = true;
  }

  if (pet.level != levelBefore || pet.xp != xpBefore || pet.evoStage != evoBefore)
  {
    Serial.printf("[SAVE][AUTOHEAL] level/xp/evo repaired lvl=%u->%u xp=%lu->%lu evo=%u->%u\n", (unsigned)levelBefore,
                  (unsigned)pet.level, (unsigned long)xpBefore, (unsigned long)pet.xp, (unsigned)evoBefore,
                  (unsigned)pet.evoStage);
  }

  // --------------------------------------------------------------------------
  // Birth epoch sanity
  // --------------------------------------------------------------------------
  const uint32_t birthBefore = g_birthEpoch;
  const uint32_t nowEpoch = getNowEpochOrZero();

  if (g_birthEpoch != 0)
  {
    const bool tooOld = (g_birthEpoch < 946684800UL); // before 2000-01-01
    const bool tooFuture = (nowEpoch != 0 && g_birthEpoch > (nowEpoch + 86400UL));

    if (tooOld || tooFuture)
    {
      g_birthEpoch = (nowEpoch != 0) ? nowEpoch : 0;
      pet.birth_epoch = g_birthEpoch;
      Serial.printf("[SAVE][AUTOHEAL] repaired birth epoch %lu -> %lu\n", (unsigned long)birthBefore,
                    (unsigned long)g_birthEpoch);
      changed = true;
    }
  }

  // --------------------------------------------------------------------------
  // Inventory selection sanity
  // --------------------------------------------------------------------------
  const int invSelBefore = g_app.inventory.selectedIndex;
  const int invCount = g_app.inventory.countItems();

  if (invCount <= 0)
  {
    if (g_app.inventory.selectedIndex != 0)
    {
      g_app.inventory.selectedIndex = 0;
      Serial.printf("[SAVE][AUTOHEAL] inventory selectedIndex %d -> 0 (empty inventory)\n", invSelBefore);
      changed = true;
    }
  }
  else
  {
    if (g_app.inventory.selectedIndex < 0)
    {
      g_app.inventory.selectedIndex = 0;
      changed = true;
    }
    else if (g_app.inventory.selectedIndex >= invCount)
    {
      g_app.inventory.selectedIndex = invCount - 1;
      changed = true;
    }

    if (g_app.inventory.selectedIndex != invSelBefore)
    {
      Serial.printf("[SAVE][AUTOHEAL] inventory selectedIndex %d -> %d (count=%d)\n", invSelBefore,
                    g_app.inventory.selectedIndex, invCount);
    }
  }

  // --------------------------------------------------------------------------
  // Runtime sleep sanity
  // --------------------------------------------------------------------------
  const bool shouldRemainAsleep = saveManagerSleepPendingFlagExists();

  if (!shouldRemainAsleep && (pet.isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested ||
                              g_app.sleepUntilAwakened || g_app.sleepTargetEnergy != 0 || g_app.sleepStartTime != 0 ||
                              g_app.sleepDurationMs != 0))
  {
    pet.isSleeping = false;
    g_app.isSleeping = false;
    g_app.sleepingByTimer = false;
    g_app.sleepUntilRested = false;
    g_app.sleepUntilAwakened = false;
    g_app.sleepTargetEnergy = 0;
    g_app.sleepStartTime = 0;
    g_app.sleepDurationMs = 0;

    Serial.println("[SAVE][AUTOHEAL] normalized runtime sleep state");
    changed = true;
  }

  return changed;
}

// ------------------------------------------------------------
// Public API
// ------------------------------------------------------------
bool saveManagerLoad()
{

  if (!g_sdReady)
  {
    g_lastLoadErr = SLE_SD_NOT_READY;
    return false;
  }

  printState("SAVE LOAD(pre)");

  (void)ensureSaveDir();
  (void)loadGameOptionsFromSD_internal();

  const bool settingsOk = loadSettingsFromSD_internal(nullptr);
  if (!settingsOk)
  {
    g_settings.wifiEnabled = 1;
    settingsSetWifiEnabled(true);
  }
  const bool saveOk = loadSaveFromSD_internal();

  // Migration / recovery case:
  // if a real pet save exists but settings.bin was missing, synthesize sane
  // defaults now and persist them immediately so boot/finalize sees the right
  // Wi-Fi preference on this same boot and future boots.
  if (saveOk && !settingsOk)
  {
    settingsSetWifiEnabled(true);
    g_settings.wifiEnabled = 1;

    Serial.println("[SAVE] loaded pet with missing settings -> writing default settings (wifi on)");
    saveSettingsToSD_internal();
  }

  Serial.printf("[SAVE] load result=%d err=%u size=%lu\n", saveOk ? 1 : 0, (unsigned)g_lastLoadErr,
                (unsigned long)g_lastLoadSize);

  if (saveOk)
  {
    // If we loaded from backup, re-promote to primary
    if (g_lastLoadUsedBackup)
    {
      DBG_ON("[SAVE] recovered from backup path=%s -> re-promoting primary save\n",
             g_lastLoadPath ? g_lastLoadPath : "(unknown)");

      saveManagerMarkDirty();
      saveManagerForce();
    }

    // ---- Auto-heal + lifecycle handling ----
    const bool healed = autoHealLoadedSaveIfNeeded();

    const bool namePending = namePendingFlagExists();
    const bool blankPetName = isBlankName(pet.name);

    if (healed)
    {
      Serial.println("[SAVE] auto-heal changed loaded payload");

      // TEMP DEBUG GUARD:
      // Do not allow immediate persistence of a legacy V3 migration result while
      // we are still validating raw XP decode offsets.
      dirty = false;
    }

    Serial.printf("[SAVE] loaded OK after heal namePending=%d blankPetName=%d name='%s'\n", namePending ? 1 : 0,
                  blankPetName ? 1 : 0, pet.name);

    // --- PET NAME (once per boot) ---
    static bool s_loggedPetNameThisBoot = false;

    if (!s_loggedPetNameThisBoot && pet.name[0] != '\0')
    {
      Serial.printf("[PET] current '%s' lvl=%u xp=%lu type=%d\n", pet.name, (unsigned)pet.level, (unsigned long)pet.xp,
                    (int)pet.type);
    }
    // Boot policy:
    // If a loaded save is still in an unfinished fresh-pet state
    // (name_pending.flag or blank pet name after heal), do not resume NAME_PET.
    // Abort the incomplete lifecycle and restart from CHOOSE_PET.
    if (appLifecycleLoadedSaveRequiresChoosePet(namePending, blankPetName))
    {
      if (namePending)
        Serial.println("[SAVE] lifecycle forcing CHOOSE_PET (name_pending.flag remains)");
      else
        Serial.println("[SAVE] lifecycle forcing CHOOSE_PET (blank name remains after auto-heal)");

      forceChoosePetFlowFromBoot();
    }
    return true;
  }

  // -----------------------------------------------------------------------
  // No valid save found → normalize runtime to a clean no-save state.
  // -----------------------------------------------------------------------

  DBGLN_ON("[SAVE] No valid save found -> initializing clean no-save state");

  // Ownership split:
  // - saveManagerLoad() owns save/runtime normalization for the no-save case
  // - boot pipeline owns onboarding/provisioning flow and final landing state
  //
  // Do not enter CHOOSE_PET here and do not recreate name_pending.flag.
  resetRuntimeToCleanNoSaveState(/*resetName=*/true);

  inputSetTextCapture(false);
  g_textCaptureMode = false;
  g_app.newPetFlowActive = false;

  clearNamePendingFlag();

  const bool hadBootSetupPending = bootSetupPendingFlagExists();
  if (hadBootSetupPending)
    bootSetupClearPendingFlag();

  dirty = false;
  clearInputLatch();

  Serial.printf("[SAVE] no-save boot state newPetFlowActive=%d namePending=%d bootSetupPendingWasSet=%d\n",
                g_app.newPetFlowActive ? 1 : 0, saveManagerNamePendingFlagExists() ? 1 : 0,
                hadBootSetupPending ? 1 : 0);

  return false;
}

void saveManagerSetPetIntroFadeBootFlag()
{
  g_settings.petScreenIntroFadeBootFlag = 1;

  // Persist immediately so reboot sees it
  saveSettingsToSD_internal();

  Serial.println("[SAVE] pet intro fade boot flag set");
}

void saveManagerStampBirthNow()
{
  uint32_t now = getNowEpochOrZero();
  g_birthEpoch = now;
  pet.birth_epoch = now;
  saveManagerMarkDirty();
}

bool saveManagerAutoHeal()
{
  if (!g_sdReady)
  {
    Serial.println("[SAVE][AUTOHEAL] skipped: SD not ready");
    return false;
  }

  return autoHealLoadedSaveIfNeeded();
}

bool saveManagerSave()
{
  DBG_ON("[SAVE] SAVE begin dirty=%d sd=%d\n", dirty ? 1 : 0, g_sdReady ? 1 : 0);
  if (!g_sdReady)
    return false;

  const bool settingsOk = saveSettingsToSD_internal();
  const bool saveOk = saveSaveToSD_internal();
  const bool ok = settingsOk && saveOk;

  if (ok)
  {
    dirty = false;
    lastSaveMs = millis();
    DBGLN_ON("[SAVE] SAVE OK");

    if (namePendingFlagExists() && !isBlankName(pet.name))
    {
      clearNamePendingFlag();
    }
  }
  else
  {
    DBG_ON("[SAVE] SAVE FAIL settings=%d save=%d\n", settingsOk ? 1 : 0, saveOk ? 1 : 0);
  }

  return ok;
}

void saveManagerMarkDirty()
{
  dirty = true;
  DBG_ON("[SAVE] DIRTY now=%lu ui=%d tab=%d\n", (unsigned long)millis(), (int)g_app.uiState, (int)g_app.currentTab);
}

void saveManagerTick()
{
  if (!dirty)
    return;

  if (!g_sdReady)
  {
    DBG_ON("[SAVE] TICK dirty but SD not ready\n");
    return;
  }

  const uint32_t now = millis();
  if (now - lastSaveMs < DEBOUNCE_MS)
  {
    DBG_ON("[SAVE] TICK debounce now=%lu last=%lu delta=%lu\n", (unsigned long)now, (unsigned long)lastSaveMs,
           (unsigned long)(now - lastSaveMs));
    return;
  }

  DBG_ON("[SAVE] TICK firing save now=%lu\n", (unsigned long)now);
  (void)saveManagerSave();
}

void saveManagerForce()
{
  if (!g_sdReady)
    return;

  const bool settingsOk = saveSettingsToSD_internal();

  if (!dirty)
  {
    if (!settingsOk)
      DBG_ON("[SAVE] FORCE settings write failed\n");
    return;
  }

  const bool saveOk = saveSaveToSD_internal();
  if (settingsOk && saveOk)
  {
    dirty = false;
    lastSaveMs = millis();
    DBGLN_ON("[SAVE] FORCE OK");

    if (namePendingFlagExists() && !isBlankName(pet.name))
    {
      clearNamePendingFlag();
    }
  }
  else
  {
    DBG_ON("[SAVE] FORCE FAIL settings=%d save=%d\n", settingsOk ? 1 : 0, saveOk ? 1 : 0);
  }
}

bool loadSettingsFromSD()
{
  bool loadedOld = false;
  const bool ok = loadSettingsFromSD_internal(&loadedOld);
  if (ok && loadedOld)
  {
    saveSettingsToSD_internal();
  }
  return ok;
}

void saveSettingsToSD() { saveSettingsToSD_internal(); }

bool saveManagerSaveFileExists()
{
  if (!g_sdReady)
    return false;

  return SD.exists("/raising_hell/save/save.bin") || SD.exists("raising_hell/save/save.bin");
}

void saveManagerNewPet()
{
  if (!g_sdReady)
    return;
  (void)ensureSaveDir();
  newPetInternal();
}

void saveManagerNewPetNoSave()
{
  if (!g_sdReady)
  {
    newPetInternalNoSave();
    return;
  }

  ensureSaveDir();
  newPetInternalNoSave();
}

uint32_t saveManagerGetBirthEpoch() { return g_birthEpoch; }

uint8_t saveManagerGetDecayMode()
{
  uint8_t m = g_gameopt.decayMode;
  if (m > 5)
    m = 2;
  return m;
}

void saveManagerSetDecayMode(uint8_t m)
{
  if (m > 5)
    m = 2;
  g_gameopt.decayMode = m;

  if (!saveGameOptionsToSD_internal())
    DBG_ON("[SAVE] game options write failed\n");
}

void saveManagerFactoryReset()
{
  tryRemove("/raising_hell/save/pet.bin");
  tryRemove("/raising_hell/save/inventory.bin");
  tryRemove("/raising_hell/save/settings.bin");
  tryRemove(SAVE_PATH);
  tryRemove(SAVE_TMP_PATH);
  tryRemove(SAVE_BAK1_PATH);
  tryRemove(SAVE_BAK2_PATH);
  tryRemove(SAVE_BAK3_PATH);
  tryRemove("/raising_hell/save/gameopt.bin");
  tryRemove("/raising_hell/save/gameopt.tmp");
  tryRemove("/raising_hell/save/settings.tmp");
  tryRemove(NAME_PENDING_FLAG_PATH);
  tryRemove(SLEEP_PENDING_FLAG_PATH);
  tryRemove("/raising_hell/save/birth.txt");

  clearNvsNamespace("rh_settings");
  clearNvsNamespace("rh_wifi");
  clearNvsNamespace("rh_tz");
  clearTimeAnchor();
  invalidateTimeNow();

  // Clear ALL transient boot/new-pet flow flags so factory reset always
  // reboots into a clean first-boot/title-menu state.
  clearNamePendingFlag();
  bootSetupClearPendingFlag();
  bootPostProvisionControlsHelpClear();

  // Also wipe the EEPROM-backed inventory mirror so a brand-new pet cannot
  // inherit inventory across a factory reset.
  g_app.inventory.wipePersistedEeprom();

  delay(50);
  ESP.restart();
}

static void removeExportsWithPetId(const char *petIdStr)
{
  if (!g_sdReady || !petIdStr || !petIdStr[0] || !SD.exists(EXPORTS_DIR))
    return;

  File dir = SD.open(EXPORTS_DIR);
  if (!dir || !dir.isDirectory())
    return;

  File file = dir.openNextFile();
  while (file)
  {
    if (!file.isDirectory())
    {
      const char *nm = file.name();
      const size_t len = strlen(nm);
      if (len >= 4 && !strcmp(nm + len - 4, ".bub"))
      {
        char full[128];
        snprintf(full, sizeof(full), "%s/%s", EXPORTS_DIR, nm);

        PetExportEntry entry{};
        if (readExportMetadata(full, entry))
        {
          if (entry.petId[0] && strcmp(entry.petId, petIdStr) == 0)
          {
            SD.remove(full);
            Serial.printf("[EXPORT] removed prior stored copy '%s' petId=%s\n", full, petIdStr);
          }
        }
      }
    }

    file.close();
    file = dir.openNextFile();
  }
}

static bool writeCurrentBubJsonToDir(const char *dirPath, char *outPath, size_t outPathSize)
{
  if (outPath && outPathSize > 0)
    outPath[0] = '\0';

  if (!g_sdReady || !dirPath || !dirPath[0])
    return false;

  if (!ensurePetDir(dirPath))
    return false;

  char safeName[32];
  sanitizeExportFilename(pet.getName(), safeName, sizeof(safeName));

  const uint32_t nowEpoch = getNowEpochOrZero();

  char finalPath[128];
  snprintf(finalPath, sizeof(finalPath), "%s/bub_%s_%lu.bub", dirPath, safeName, (unsigned long)nowEpoch);

  char tmpPath[140];
  snprintf(tmpPath, sizeof(tmpPath), "%s.part", finalPath);

  tryRemove(tmpPath);

  DynamicJsonDocument doc(4096);
  doc["_warning"] = "Changing these values will bring on the curse. You have been warned.";
  doc["format"] = EXPORT_MAGIC;
  doc["exportVersion"] = EXPORT_VERSION;
  doc["createdAtEpoch"] = nowEpoch;
  doc["gameVersion"] = getFirmwareVersionString();
  JsonObject profile = doc.createNestedObject("profile");

  profile["name"] = pet.getName();
  profile["petType"] = petTypeToStringForExport(pet.type);
  profile["level"] = pet.level;
  profile["xp"] = pet.xp;
  profile["evoStage"] = pet.evoStage;
  profile["birthEpoch"] = g_birthEpoch;

  char petIdBuf[24];
  snprintf(petIdBuf, sizeof(petIdBuf), "%016llX", (unsigned long long)pet.petId);
  profile["petId"] = petIdBuf;

  profile["isSleeping"] = (pet.isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested ||
                           g_app.sleepUntilAwakened);

  JsonObject stats = profile.createNestedObject("stats");
  stats["hunger"] = pet.hunger;
  stats["happiness"] = pet.happiness;
  stats["energy"] = pet.energy;
  stats["health"] = pet.health;
  stats["inf"] = pet.inf;

  JsonObject gameOptions = doc.createNestedObject("gameOptions");
  gameOptions["decayMode"] = saveManagerGetDecayMode();

  JsonObject inventory = doc.createNestedObject("inventory");
  inventory["selectedIndex"] = g_app.inventory.selectedIndex;
  JsonArray slots = inventory.createNestedArray("slots");

  for (int i = 0; i < Inventory::MAX_ITEMS; ++i)
  {
    const Item &it = g_app.inventory.items[i];
    if (it.type == ITEM_NONE || it.quantity <= 0)
      continue;

    JsonObject row = slots.createNestedObject();
    row["item"] = itemTypeToStringForExport(it.type);
    row["qty"] = it.quantity;
  }

  File f = SD.open(tmpPath, FILE_WRITE);
  if (!f)
    return false;

  if (serializeJsonPretty(doc, f) == 0)
  {
    f.close();
    tryRemove(tmpPath);
    return false;
  }

  f.flush();
  const size_t writtenSize = (size_t)f.size();
  f.close();

  if (writtenSize == 0)
  {
    tryRemove(tmpPath);
    return false;
  }

  PetExportEntry verify{};
  if (!readExportMetadata(tmpPath, verify))
  {
    tryRemove(tmpPath);
    return false;
  }

  tryRemove(finalPath);
  if (!SD.rename(tmpPath, finalPath))
  {
    tryRemove(tmpPath);
    return false;
  }

  if (outPath && outPathSize > 0)
  {
    strncpy(outPath, finalPath, outPathSize - 1);
    outPath[outPathSize - 1] = '\0';
  }

  Serial.printf("[EXPORT] wrote '%s' pet='%s'\n", finalPath, pet.getName());
  return true;
}

bool saveManagerBackupCurrentPet(char *outPath, size_t outPathSize)
{
  return writeCurrentBubJsonToDir(BACKUPS_DIR, outPath, outPathSize);
}

bool saveManagerExportCurrentBubJson(char *outPath, size_t outPathSize)
{
  return writeCurrentBubJsonToDir(EXPORTS_DIR, outPath, outPathSize);
}

bool saveManagerBoxCurrentPet(char *outPath, size_t outPathSize)
{
  if (!g_sdReady)
    return false;

  // 1) Export current pet to the locker/export system.
  //
  // Contract:
  // - This function removes the live on-disk save after a successful export.
  // - It does NOT reset runtime/UI state; callers must do that separately.  if
  // (!saveManagerExportCurrentBubJson(outPath, outPathSize))
  return false;

  // 2) Remove the active unified save and temp/bak files.
  SD.remove(SAVE_PATH);
  SD.remove(SAVE_TMP_PATH);
  SD.remove(SAVE_BAK1_PATH);
  SD.remove(SAVE_BAK2_PATH);
  SD.remove(SAVE_BAK3_PATH);

  // 3) Remove legacy split save pieces too, just in case they still exist.
  SD.remove("/raising_hell/save/pet.bin");
  SD.remove("/raising_hell/save/inventory.bin");

  // 4) Keep the autosave system from immediately writing the old live pet back out.
  dirty = false;
  lastSaveMs = millis();

  return true;
}

bool saveManagerImportLatestBubJson(char *outPath, size_t outPathSize)
{
  char path[128];
  if (!findLatestExportPath(path, sizeof(path)))
    return false;

  return saveManagerImportBubAtPath(path, outPath, outPathSize, true);
}

bool saveManagerImportBubAtPath(const char *path, char *outPath, size_t outPathSize, bool backupCurrentFirst)
{
  if (outPath && outPathSize > 0)
    outPath[0] = '\0';

  if (!path || !path[0])
    return false;

  if (backupCurrentFirst && saveManagerSaveFileExists())
  {
    char backupPath[128];
    if (!saveManagerExportCurrentBubJson(backupPath, sizeof(backupPath)))
    {
      Serial.println("[IMPORT] backup-current failed");
      return false;
    }
  }

  File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  DynamicJsonDocument doc(4096);
  const DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err)
  {
    Serial.printf("[IMPORT] failed '%s' reason=json_parse\n", path);
    return false;
  }

  const char *format = doc["format"] | "";
  const uint16_t exportVersion = doc["exportVersion"] | 0;
  if (strcmp(format, EXPORT_MAGIC) != 0 || exportVersion != EXPORT_VERSION)
  {
    Serial.printf("[IMPORT] failed '%s' reason=format/version\n", path);
    return false;
  }

  JsonObject profile = doc["profile"];
  JsonObject stats = profile["stats"];
  JsonObject inventory = doc["inventory"];
  JsonArray slots = inventory["slots"];

  const bool importedSleeping = (bool)(profile["isSleeping"] | false);

  const char *name = profile["name"] | "";
  const char *petTypeStr = profile["petType"] | "";
  PetType importedType = PET_DEVIL;
  if (!petTypeFromStringForImport(petTypeStr, importedType))
    importedType = PET_DEVIL;

  pet.setName((name && name[0]) ? name : "Bub");
  pet.type = importedType;
  pet.level = (uint16_t)constrain((int)(profile["level"] | 1), 1, 999);
  pet.xp = (uint32_t)(profile["xp"] | 0);
  pet.evoStage = (uint8_t)constrain((int)(profile["evoStage"] | 0), 0, 3);

  pet.hunger = constrain((int)(stats["hunger"] | 50), 0, 100);
  pet.happiness = constrain((int)(stats["happiness"] | 50), 0, 100);
  pet.energy = constrain((int)(stats["energy"] | 50), 0, 100);
  pet.health = constrain((int)(stats["health"] | 100), 0, 100);
  pet.inf = (int)(stats["inf"] | 0);
  pet.isSleeping = importedSleeping;

  g_birthEpoch = (uint32_t)(profile["birthEpoch"] | 0);
  pet.birth_epoch = g_birthEpoch;

  const char *petIdStr = profile["petId"] | "";
  unsigned long long parsedPetId = 0ULL;
  if (petIdStr && petIdStr[0])
    sscanf(petIdStr, "%llx", &parsedPetId);

  if (parsedPetId != 0ULL)
  {
    pet.petId = (uint64_t)parsedPetId;
  }
  else
  {
    pet.petId = generatePetId();
    Serial.println("[PETID] import missing ID → generated new one");
  }

  g_app.inventory.clear();
  for (JsonObject row : slots)
  {
    const char *itemStr = row["item"] | "";
    const int qty = (int)(row["qty"] | 0);
    if (qty <= 0)
      continue;

    ItemType t = ITEM_NONE;
    if (!itemTypeFromStringForImport(itemStr, t))
      continue;

    if (t != ITEM_NONE)
      g_app.inventory.addItem(t, qty);
  }

  g_app.inventory.selectedIndex =
      constrain((int)(inventory["selectedIndex"] | 0), 0, max(0, g_app.inventory.countItems() - 1));

  saveManagerSetDecayMode((uint8_t)constrain((int)(doc["gameOptions"]["decayMode"] | 2), 0, 5));

  pet.clampStats();
  clearNamePendingFlag();
  g_app.inventory.syncEepromNoDirty();

  // Restore runtime sleep state from imported backup
  pet.isSleeping = importedSleeping;
  g_app.isSleeping = importedSleeping;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  if (importedSleeping)
    saveManagerSetSleepPendingFlag();
  else
    saveManagerClearSleepPendingFlag();

  saveManagerMarkDirty();
  saveManagerForce();
  return true;
}

bool saveManagerValidateBubAtPath(const char *path)
{
  if (!path || !path[0])
    return false;

  PetExportEntry entry{};
  return readExportMetadata(path, entry);
}

bool saveManagerDeletePetBackupAtPath(const char *path)
{
  if (!g_sdReady || !path || !path[0])
    return false;

  if (strncmp(path, BACKUPS_DIR, strlen(BACKUPS_DIR)) != 0)
    return false;

  if (!SD.exists(path))
    return false;

  return SD.remove(path);
}

// ------------------------------------------------------------
// DELETE ALL SAVES (true death)
// ------------------------------------------------------------
void saveManagerDeleteAll()
{
  // Used when the pet is buried (hard reset of pet-related data)
  if (!g_sdReady)
    return;

  // Remove all pet-related blobs (settings / game options may remain elsewhere)
  SD.remove(SAVE_PATH); // /raising_hell/save/save.bin
  SD.remove("/raising_hell/save/pet.bin");
  SD.remove("/raising_hell/save/inventory.bin");
  SD.remove(SAVE_BAK1_PATH);
  SD.remove(SAVE_BAK2_PATH);
  SD.remove(SAVE_BAK3_PATH);

  // Also wipe the EEPROM-backed inventory mirror so the next pet starts clean.
  g_app.inventory.wipePersistedEeprom();
}

void saveManagerDeletePetOnly()
{
  // Delete ONLY the pet + inventory. Keep settings/game options.
  if (!g_sdReady)
    return;

  SD.remove("/raising_hell/save/pet.bin");
  SD.remove("/raising_hell/save/inventory.bin");
  SD.remove(SAVE_PATH);     // active unified save.bin
  SD.remove(SAVE_TMP_PATH); // temp write
  SD.remove(SAVE_BAK1_PATH);
  SD.remove(SAVE_BAK2_PATH);
  SD.remove(SAVE_BAK3_PATH);

  // Also wipe the EEPROM-backed inventory mirror so the next pet starts clean.
  g_app.inventory.wipePersistedEeprom();

  // Prevent the old live pet from being re-saved by the debounce save path.
  dirty = false;
  lastSaveMs = millis();
}