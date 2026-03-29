#include "save_manager.h"
#include <Arduino.h>
#include <SD.h>
#include <stdint.h>

#include "debug.h"
#include "inventory.h"
#include "pet.h"
#include "savegame.h"
#include "sdcard.h"
#include "timezone.h"
#include <Preferences.h>
#include <esp_system.h>

#include "wifi_store.h"

#include "ui_invalidate.h"
#include "ui_runtime.h"

#include "app_state.h"
#include "brightness_state.h"
#include "controls_help_state.h"
#include "display_state.h"
#include "graphics.h"
#include "input.h"
#include "inventory_state.h"
#include "runtime_flags_state.h"
#include "settings_state.h"
#include "settings_toggles_state.h" // wifiEnabled, g_app.autoScreenOffEnabled
#include "sleep_state.h"
#include "time_persist.h"
#include "time_state.h"
#include "user_toggles_state.h"
#include "app_lifecycle.h"
#include "ui_actions.h"

static bool dirty = false;
static uint32_t lastSaveMs = 0;
static const uint32_t DEBOUNCE_MS = 2000;
static void newPetInternalNoSave(bool resetName = false);

// One (and ONLY one) instance
static SettingsData g_settings;

bool settingsWifiEnabled() { return (g_settings.wifiEnabled != 0); }

void settingsSetWifiEnabled(bool en) { g_settings.wifiEnabled = en ? 1 : 0; }

// Settings persistence
bool loadSettingsFromSD();
void saveSettingsToSD(); // exported wrapper

// Pet Stuff
static uint32_t g_birthEpoch = 0;

static uint8_t g_lastLoadErr = SLE_OK;
static uint32_t g_lastLoadSize = 0;

uint8_t saveManagerLastLoadErr() { return g_lastLoadErr; }
uint32_t saveManagerLastLoadSize() { return g_lastLoadSize; }

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

// ------------------------------------------------------------
// NEW PET FLOW BOOT RESUME FLAG
//   - Present => user was mid "Name Pet" flow (resume NAME_PET on boot)
//   - Missing => do NOT force NAME_PET; if name blank, go to CHOOSE_PET instead
// ------------------------------------------------------------
static const char *NAME_PENDING_FLAG_PATH = "/raising_hell/save/name_pending.flag";

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

static void forceChoosePetFlowFromBoot()
{
  // Starting a brand-new pet lifecycle.
  // Reset *all* runtime state (including inventory) to defaults so nothing can
  // carry over from a previous pet (death path, deleted saves, etc.).
  newPetInternalNoSave(/*resetName=*/true);

  inputSetTextCapture(false);
  g_textCaptureMode = false;

  // IMPORTANT:
  // Do NOT clear the pending-name flag here.
  // Choose-pet is still part of the unfinished first-boot/new-pet flow.
  // The pending-name flag must survive until the pet is actually finalized
  // from NAME_PET.
  // clearNamePendingFlag();
  
  g_choosePetInputUnlockMs = millis() + 350;
  g_choosePetBlockHatchUntilRelease = true;
  // Go to Choose Pet flow state
  uiActionEnterState(UIState::CHOOSE_PET, Tab::TAB_PET, true);
  g_app.uiNeedsRedraw = true;

  // Clear latches
  clearInputLatch();
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

static bool loadSettingsFromSD_internal(bool *outLoadedOld = nullptr);
static bool saveSettingsToSD_internal();
static bool saveGameOptionsToSD_internal();

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

SavePayload makeDefaultSavePayload()
{
  SavePayload p{};
  p.magic = SAVE_MAGIC;
  p.version = SAVE_VERSION;

  p.pet.hunger = 50;
  p.pet.happiness = 50;
  p.pet.energy = 50;
  p.pet.health = 100;

  p.pet.petType = 0;
  p.pet.isSleeping = 0;
  p.pet.lastFedTimeMs = 0;
  p.pet.inf = 0;

  p.pet.level = 1;
  p.pet.xp = 0;
  p.pet.evoStage = 0;

  strcpy(p.pet.name, "Baby Demon");

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

  // Always reboot awake
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

  g_app.inventory.fromPersist(p2.inv);

  g_birthEpoch = (p2.birth_epoch != 0) ? p2.birth_epoch : p2.pet.birth_epoch;

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
  g_app.inventory.fromPersist(p.inv);
  // Keep the EEPROM mirror in sync with the authoritative payload.
  // This prevents stale inventory from leaking across factory reset/death.
  g_app.inventory.syncEepromNoDirty();
  // Always come up awake
  pet.isSleeping = false;
  g_app.isSleeping = false;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

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

      tmp.autoScreenOffEnabled = (tmp.autoScreenTimeoutSel != 0);

      tmp.soundEnabled = (tmp.soundEnabled != 0);
      tmp.wifiEnabled = (tmp.wifiEnabled != 0);
      tmp.petDeathEnabled = (tmp.petDeathEnabled != 0);
      tmp.ledAlertsEnabled = (tmp.ledAlertsEnabled != 0);
      tmp.controlsHelpSeen = (tmp.controlsHelpSeen != 0);
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

      tmp.ledAlertsEnabled = 1;
      tmp.controlsHelpSeen = 0;

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
  soundEnabled = (g_settings.soundEnabled != 0);

  autoScreenTimeoutSel = g_settings.autoScreenTimeoutSel;
  g_app.autoScreenOffEnabled = (autoScreenTimeoutSel != 0);

  petDeathEnabled = (g_settings.petDeathEnabled != 0);
  ledAlertsEnabled = (g_settings.ledAlertsEnabled != 0);

  g_controlsHelpSeen = (g_settings.controlsHelpSeen != 0);

  // ---- Perf HUD toggle ----
  g_petPerfHudEnabled = (g_settings.petPerfHudEnabled != 0);

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
  return true;
}

static bool saveSettingsToSD_internal()
{
  if (!g_sdReady)
    return false;
  if (!ensureSaveDir())
    return false;

  g_settings.brightnessLevel = brightnessLevel;
  g_settings.autoScreenOffEnabled = g_app.autoScreenOffEnabled;
  g_settings.soundEnabled = soundEnabled;
  g_settings.wifiEnabled = settingsWifiEnabled() ? 1 : 0;
  g_settings.tzIndex = tzIndex;

  g_settings.autoScreenTimeoutSel = autoScreenTimeoutSel;
  g_settings.autoScreenOffEnabled = (autoScreenTimeoutSel != 0);
  g_settings.petDeathEnabled = petDeathEnabled ? 1 : 0;
  g_settings.ledAlertsEnabled = ledAlertsEnabled ? 1 : 0;
  g_settings.controlsHelpSeen = (g_controlsHelpSeen != 0) ? 1 : 0;

  // ---- Perf HUD toggle ----
  g_settings.petPerfHudEnabled = g_petPerfHudEnabled ? 1 : 0;

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
static bool loadSaveFromSD_internal()
{
  g_lastLoadErr = SLE_OK;
  g_lastLoadSize = 0;

  if (!g_sdReady)
  {
    g_lastLoadErr = SLE_SD_NOT_READY;
    return false;
  }
  if (!ensureSaveDir())
  {
    g_lastLoadErr = SLE_DIR_FAIL;
    return false;
  }

  File f = SD.open(SAVE_PATH, FILE_READ);
  if (!f)
  {
    g_lastLoadErr = SLE_OPEN_FAIL;
    return false;
  }

  const size_t sz = (size_t)f.size();
  g_lastLoadSize = (uint32_t)sz;

  if (sz < sizeof(uint32_t) + sizeof(uint16_t))
  {
    g_lastLoadErr = SLE_READ_FAIL;
    f.close();
    return false;
  }

  SavePayload p{};
  memset(&p, 0, sizeof(p));

  const size_t want = (sz < sizeof(p)) ? sz : sizeof(p);
  const int r = f.read((uint8_t *)&p, want);
  f.close();

  if (r != (int)want)
  {
    g_lastLoadErr = SLE_READ_FAIL;
    return false;
  }
  if (p.magic != SAVE_MAGIC)
  {
    g_lastLoadErr = SLE_MAGIC_BAD;
    return false;
  }

  if (p.version != SAVE_VERSION && p.version != 0)
  {
    g_lastLoadErr = SLE_VERSION_BAD;
    return false;
  }

  p.magic = SAVE_MAGIC;
  p.version = SAVE_VERSION;

  if (p.version == SAVE_VERSION && sz < sizeof(SavePayload))
  {
    g_lastLoadErr = SLE_READ_FAIL;
    return false;
  }

  unpack(p);

  //  (void)saveSaveToSD_internal();
  return true;
}

static bool saveSaveToSD_internal()
{
  if (!g_sdReady)
    return false;
  if (!ensureSaveDir())
    return false;

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

  tryRemove(SAVE_PATH);
  if (!SD.rename(SAVE_TMP_PATH, SAVE_PATH))
  {
    DBG_ON("[SAVE] rename failed: %s -> %s\n", SAVE_TMP_PATH, SAVE_PATH);
    tryRemove(SAVE_TMP_PATH);
    return false;
  }

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

static void forceNamePetFlowFromBoot()
{
  inputSetTextCapture(true);
  uiActionEnterState(UIState::NAME_PET, Tab::TAB_PET, true);

  g_app.uiNeedsRedraw = true;
  requestFullUIRedraw();
  clearInputLatch();
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

  if (saveOk)
  {

    const bool namePending = namePendingFlagExists();
    const bool blankPetName = isBlankName(pet.name);
    
    if (appLifecycleLoadedSaveRequiresChoosePet(namePending, blankPetName))
    {
      if (namePending)
        DBGLN_ON("[SAVE] lifecycle: name_pending.flag present -> forcing CHOOSE_PET flow");
      else
        DBGLN_ON("[SAVE] lifecycle: blank pet name -> forcing CHOOSE_PET");
    
      forceChoosePetFlowFromBoot();
    }
        
    return true;
  }

  // -----------------------------------------------------------------------
  // NEW: No valid save found (factory reset / first boot / corruption)
  //      Initialize a clean default payload so inventory and pet state
  //      are reset properly.
  // -----------------------------------------------------------------------

  DBGLN_ON("[SAVE] No valid save found -> applying default payload");

  SavePayload def = makeDefaultSavePayload();
  unpack(def);

  // Ensure a fresh save gets written soon so we don't re-default every boot.
  saveManagerMarkDirty();

  // Force new pet flow on fresh boot.
  forceChoosePetFlowFromBoot();

  return false;
}

void saveManagerStampBirthNow()
{
  uint32_t now = getNowEpochOrZero();
  g_birthEpoch = now;
  pet.birth_epoch = now;
  saveManagerMarkDirty();
}

bool saveManagerSave()
{
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

void saveManagerMarkDirty() { dirty = true; }

void saveManagerTick()
{
  if (!dirty)
    return;
  if (!g_sdReady)
    return;

  const uint32_t now = millis();
  if (now - lastSaveMs < DEBOUNCE_MS)
    return;

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

static void tryRemove(const char *path)
{
  if (!path || !g_sdReady)
    return;
  if (SD.exists(path))
    SD.remove(path);
}

static void writeFirstRunFlag()
{
  if (!g_sdReady)
    return;
  File f = SD.open("/raising_hell/first_run.flag", FILE_WRITE);
  if (f)
  {
    f.print("1");
    f.close();
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

void saveManagerFactoryReset()
{
  tryRemove("/raising_hell/save/pet.bin");
  tryRemove("/raising_hell/save/inventory.bin");
  tryRemove("/raising_hell/save/settings.bin");
  tryRemove("/raising_hell/save/save.bin");
  tryRemove("/raising_hell/save/save.tmp");
  tryRemove("/raising_hell/save/gameopt.bin");
  tryRemove("/raising_hell/save/gameopt.tmp");
  tryRemove("/raising_hell/save/settings.tmp");
  tryRemove(NAME_PENDING_FLAG_PATH);

  tryRemove("/raising_hell/save/birth.txt");

  clearNvsNamespace("rh_settings");
  clearNvsNamespace("rh_wifi");
  clearNvsNamespace("rh_tz");
  clearTimeAnchor();
  invalidateTimeNow();

  writeFirstRunFlag();
  // Also wipe the EEPROM-backed inventory mirror so a brand-new pet cannot
  // inherit inventory across a factory reset.
  g_app.inventory.wipePersistedEeprom();

  delay(50);
  ESP.restart();
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
  SD.remove(SAVE_PATH); // if you still store some pet-state here

  // Also wipe the EEPROM-backed inventory mirror so the next pet starts clean.
  g_app.inventory.wipePersistedEeprom();
}