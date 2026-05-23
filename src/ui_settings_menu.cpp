#include "ui_settings_menu.h"

// --- Standard / Platform ---
#include <esp_system.h>

// --- Core App ---
#include "app_loop.h"
#include "app_state.h"
#include "build_flags.h"

// --- Core Systems ---
#include "auto_screen.h"
#include "brightness_state.h"
#include "display.h"
#include "motion.h"
#include "save_manager.h"
#include "sdcard.h"
#include "sound.h"

// --- UI Core ---
#include "ui_actions.h"
#include "ui_input_common.h"
#include "ui_input_utils.h"
#include "ui_runtime.h"
#include "ui_settings_actions.h"
#include "ui_settings_pages.h"
#include "ui_state_clock_mode.h"

// --- UI / Flow ---
#include "menu_actions.h"
#include "name_entry_state.h"
#include "settings_flow_state.h"
#include "settings_nav_state.h"
#include "system_status_state.h"
#include "ui_state_backup_pet_list.h"
#include "ui_state_import_pet_list.h"

// --- Networking / Time ---
#include "wifi_power.h"
#include "wifi_setup_state.h"
#include "wifi_store.h"
#include "wifi_time.h"

// --- Asset / Provisioning ---
#include "asset_ota.h"
#include "asset_provision_request.h"

// --- Feature Flows ---
#include "flow_console.h"
#include "flow_controls_help.h"
#include "flow_factory_reset.h"
#include "flow_time_editor.h"

// --- Game / User Systems ---
#include "game_options_state.h"
#include "user_toggles_state.h"

// --- Graphics ---
#include "graphics.h" // ui_showMessage
#include "ui_state_choose_pet.h"

// End of Evangelincludes

// ------------------------------------------------------------
// Minimal embedded menu model
// ------------------------------------------------------------
struct MenuItem
{
  const char *label;

  void (*onSelect)(InputState &);
  void (*onLeft)(InputState &);
  void (*onRight)(InputState &);

  bool (*isEnabled)();
};

struct MenuPageDef
{
  SettingsPage page;
  MenuItem *items;
  uint8_t itemCount;

  // Cursor accessor per page (keeps your existing indices in g_app / g_wifi)
  int &(*cursor)();

  // Optional per-page hook that runs every tick (used for special flows like Factory Reset)
  void (*pageHook)(InputState &, int cursor);
};

static int &cursorTop() { return g_app.settingsIndex; }
static int &cursorScreen() { return g_app.screenSettingsIndex; }
static int &cursorPet() { return g_app.petSettingsIndex; }
static int &cursorWifi() { return g_wifi.wifiSettingsIndex; }
static int &cursorGame() { return g_app.gameOptionsIndex; }
static int &cursorSystem() { return g_app.systemSettingsIndex; }
static int &cursorAutoScreen() { return g_app.autoScreenIndex; }
static int &cursorDecayMode() { return g_app.decayModeIndex; }

// ------------------------------------------------------------
// Common helpers
// ------------------------------------------------------------
static inline void wrapMove(int &idx, int count, int move)
{
  if (count <= 0)
  {
    idx = 0;
    return;
  }
  idx += move;
  while (idx < 0)
    idx += count;
  while (idx >= count)
    idx -= count;
}

// ------------------------------------------------------------
// TOP page actions
// ------------------------------------------------------------
static void actTop_VolumeSelect(InputState &)
{
  soundAdjustVolume(+1);
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_VolumeLeft(InputState &)
{
  soundAdjustVolume(-1);
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_VolumeRight(InputState &)
{
  soundAdjustVolume(+1);
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_Controls(InputState &)
{
  openControlsHelpFromSettings();
  playBeep();
  clearInputLatch();
}

static void actTop_OpenPet(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::PET;
  g_app.petSettingsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_OpenScreen(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::SCREEN;
  g_app.screenSettingsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_OpenSystem(InputState &input)
{
  g_settingsFlow.settingsPage = SettingsPage::SYSTEM;
  g_app.systemSettingsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_OpenWifi(InputState &input)
{
  g_settingsFlow.settingsPage = SettingsPage::WIFI;
  g_wifi.wifiSettingsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_OpenGame(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::GAME;
  g_app.gameOptionsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_TogglePetPerfHud(InputState &)
{
  g_petPerfHudEnabled = !g_petPerfHudEnabled;
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static bool enConsole() { return true; }

static bool enHasLivePet() { return saveManagerSaveFileExists(); }

static void actTop_Console(InputState &input)
{
  openConsoleWithReturn(UIState::SETTINGS, g_app.currentTab, true, g_settingsFlow.settingsPage);
  uiDrainKb(input);
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_OpenStatus(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::STATUS;
  g_systemStatus.scrollOffset = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_Credits(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::CREDITS;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_MainMenu(InputState &in)
{
  resetSettingsNav(true);
  g_settingsFlow.settingsPage = SettingsPage::TOP;
  uiPopReturnTarget();

  playBeep();
  uiActionEnterStateClean(UIState::TITLE_MENU, Tab::TAB_PET, true, in, 120);
}

// ------------------------------------------------------------
// AUTO_SCREEN picker actions
// ------------------------------------------------------------

static void actAutoScreen_Apply(InputState &)
{
  // Cursor index maps to the timeout options
  autoScreenTimeoutSel = (uint8_t)g_app.autoScreenIndex;
  saveSettingsToSD();
  saveManagerMarkDirty();

  // Return to whoever opened it (Screen page)
  g_settingsFlow.settingsPage = g_settingsFlow.settingsReturnPage;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

// ------------------------------------------------------------
// DECAY_MODE picker actions
// ------------------------------------------------------------

static void actDecayMode_Apply(InputState &)
{
  saveManagerSetDecayMode((uint8_t)g_app.decayModeIndex);
  saveSettingsToSD();
  saveManagerMarkDirty();

  g_settingsFlow.settingsPage = g_settingsFlow.settingsReturnPage;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

// ------------------------------------------------------------
// SCREEN page actions
// ------------------------------------------------------------
static void actScreen_BrightnessLeft(InputState &)
{
  brightnessLevel -= 1;
  if (brightnessLevel < 0)
    brightnessLevel = 2;

  setBacklight((uint16_t)brightnessValues[brightnessLevel]);

  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_BrightnessRight(InputState &)
{
  brightnessLevel += 1;
  if (brightnessLevel > 2)
    brightnessLevel = 0;

  setBacklight((uint16_t)brightnessValues[brightnessLevel]);

  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_BrightnessSelect(InputState &input) { actScreen_BrightnessRight(input); }

static void actScreen_AutoScreenLeft(InputState &)
{
  int sel = (int)autoScreenTimeoutSel - 1;
  if (sel < 0)
    sel = 3;

  autoScreenTimeoutSel = (uint8_t)sel;
  autoScreenSetEnabled(autoScreenTimeoutSel != 3);

  if (autoScreenTimeoutSel != 3)
  {
    autoClockTimeoutSel = 3;
    autoClockSetEnabled(false);
  }

  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_AutoScreenRight(InputState &)
{
  int sel = (int)autoScreenTimeoutSel + 1;
  if (sel > 3)
    sel = 0;

  autoScreenTimeoutSel = (uint8_t)sel;
  autoScreenSetEnabled(autoScreenTimeoutSel != 3);

  if (autoScreenTimeoutSel != 3)
  {
    autoClockTimeoutSel = 3;
    autoClockSetEnabled(false);
  }

  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_AutoScreenSelect(InputState &input) { actScreen_AutoScreenRight(input); }

static void actScreen_AutoClockLeft(InputState &)
{
  int sel = (int)autoClockTimeoutSel - 1;
  if (sel < 0)
    sel = 3;

  autoClockTimeoutSel = (uint8_t)sel;
  autoClockSetEnabled(autoClockTimeoutSel != 3);

  if (autoClockTimeoutSel != 3)
  {
    autoScreenTimeoutSel = 3;
    autoScreenSetEnabled(false);
  }

  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_AutoClockRight(InputState &)
{
  int sel = (int)autoClockTimeoutSel + 1;
  if (sel > 3)
    sel = 0;

  autoClockTimeoutSel = (uint8_t)sel;
  autoClockSetEnabled(autoClockTimeoutSel != 3);

  if (autoClockTimeoutSel != 3)
  {
    autoScreenTimeoutSel = 3;
    autoScreenSetEnabled(false);
  }

  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_AutoClockSelect(InputState &input) { actScreen_AutoClockRight(input); }

static void actScreen_ShakeSensitivityLeft(InputState &)
{
  int sel = (int)motionGetShakeSensitivity() - 1;
  if (sel < 0)
    sel = 3;

  motionSetShakeSensitivity((uint8_t)sel);
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_ShakeSensitivityRight(InputState &)
{
  int sel = (int)motionGetShakeSensitivity() + 1;
  if (sel > 3)
    sel = 0;

  motionSetShakeSensitivity((uint8_t)sel);
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actScreen_ShakeSensitivitySelect(InputState &input) { actScreen_ShakeSensitivityRight(input); }

// ------------------------------------------------------------
// WIFI page actions
// ------------------------------------------------------------
static void actWifi_Toggle(InputState &)
{
  const bool en = !wifiIsEnabled();

  wifiSetEnabled(en);
  Serial.printf("[WIFI PREF WRITE] source=ui_settings_menu en=%d state=%d tab=%d\n", en ? 1 : 0, (int)g_app.uiState,
                (int)g_app.currentTab);
  settingsSetWifiEnabled(en);
  applyWifiPower(en);

  saveSettingsToSD();
  saveManagerMarkDirty();

  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actWifi_SetNetwork(InputState &input)
{
  g_wifiSetupFromBootWizard = false;

  g_wifi.setupStage = 0;
  g_wifi.buf[0] = '\0';
  g_wifi.ssid[0] = '\0';
  g_wifi.pass[0] = '\0';

  g_wifi.returnState = UIState::SETTINGS;
  g_wifi.returnTab = g_app.currentTab;
  g_wifi.aborted = false;

  uiActionEnterState(UIState::WIFI_SETUP, g_app.currentTab, true);
  requestUIRedraw();

  inputSetTextCapture(true);
  g_textCaptureMode = true;

  uiDrainKb(input);
  playBeep();
  clearInputLatch();
}

static void actWifi_StoredNetworks(InputState &input)
{
  g_settingsFlow.settingsPage = SettingsPage::STORED_NETWORKS;
  g_wifi.storedNetworkIndex = 0;
  g_wifi.storedNetworkActionIndex = 0;
  g_wifi.storedNetworkActionActive = false;

  requestUIRedraw();
  uiDrainKb(input);
  playBeep();
  clearInputLatch();
}

static void actWifi_Reset(InputState &)
{
  wifiResetSettings();
  wifiStoreClear();

  ui_showMessage("WiFi reset");
  requestUIRedraw();

  playBeep();
  clearInputLatch();
}

static void actWifi_TzSelect(InputState &)
{
  settingsCycleTimeZone(+1);
  // (settingsCycleTimeZone already redraws + beeps + clears latch)
}

static void actWifi_TzLeft(InputState &) { settingsCycleTimeZone(-1); }

static void actWifi_TzRight(InputState &) { settingsCycleTimeZone(+1); }

static void actWifi_CheckAssetOta(InputState &)
{
  assetOtaSetConfirmActive(true);
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

// ------------------------------------------------------------
// GAME page actions
// ------------------------------------------------------------
static void actGame_RenamePet(InputState &input)
{
  strncpy(g_pendingPetName, pet.getName(), PET_NAME_MAX);
  g_pendingPetName[PET_NAME_MAX] = '\0';

  g_namePetRenameMode = true;
  g_namePetJustOpened = true;
  g_settingsFlow.settingsPage = SettingsPage::GAME;

  inputSetTextCapture(true);
  g_textCaptureMode = true;

  uiActionEnterStateClean(UIState::NAME_PET, g_app.currentTab, true, input, 120);

  requestUIRedraw();
  invalidateBackgroundCache();
  playBeep();
}

static void actGame_ExportBub(InputState &)
{
  char path[128];
  if (saveManagerExportCurrentBubJson(path, sizeof(path)))
  {
    ui_showMessage("Pet exported");
    Serial.printf("[UI] Export Pet OK path=%s\n", path);
  }
  else
  {
    ui_showMessage("Export failed");
    Serial.println("[UI] Export Pet FAILED");
  }

  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_ImportBub(InputState &input)
{
  uiActionEnterState(UIState::IMPORT_PET_LIST, g_app.currentTab, true);
  requestFullUIRedraw();
  playBeep();
  uiDrainKb(input);
  clearInputLatch();
}

static void actGame_NewPet(InputState &)
{
  UiSettingsPages::ShowGameNewPetConfirm();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_DecayModeLeft(InputState &)
{
  int mode = (int)saveManagerGetDecayMode() - 1;
  if (mode < 0)
    mode = 5;

  saveManagerSetDecayMode((uint8_t)mode);
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_DecayModeRight(InputState &)
{
  int mode = (int)saveManagerGetDecayMode() + 1;
  if (mode > 5)
    mode = 0;

  saveManagerSetDecayMode((uint8_t)mode);
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_DecayModeSelect(InputState &input) { actGame_DecayModeRight(input); }

static void actGame_TogglePassiveXp(InputState &)
{
  passiveXpEnabled = !passiveXpEnabled;
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_ToggleStepCounter(InputState &)
{
  setStepCounterEnabled(!isStepCounterEnabled());
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_ToggleDeath(InputState &)
{
  petDeathEnabled = !petDeathEnabled;
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actGame_ToggleLedAlerts(InputState &)
{
  ledAlertsEnabled = !ledAlertsEnabled;
#if LED_STATUS_ENABLED
  ledUpdatePetStatus(LED_PET_OFF);
#endif
  saveSettingsToSD();
  saveManagerMarkDirty();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actPet_RenamePet(InputState &input)
{
  if (!saveManagerSaveFileExists())
  {
    ui_showMessage("No pet loaded");
    soundError();
    requestUIRedraw();
    clearInputLatch();
    return;
  }
  strncpy(g_pendingPetName, pet.getName(), PET_NAME_MAX);
  g_pendingPetName[PET_NAME_MAX] = '\0';

  g_namePetRenameMode = true;
  g_namePetJustOpened = true;

  // Make NAME_PET return to Pet Options, not Game.
  g_settingsFlow.settingsPage = SettingsPage::PET;

  inputSetTextCapture(true);
  g_textCaptureMode = true;

  uiActionEnterStateClean(UIState::NAME_PET, g_app.currentTab, true, input, 120);

  requestUIRedraw();
  invalidateBackgroundCache();
  playBeep();
}

static void actPet_StorePet(InputState &input)
{
  char boxedPath[128] = {0};

  if (!saveManagerBoxCurrentPet(boxedPath, sizeof(boxedPath)))
  {
    ui_showMessage("Store failed");
    Serial.println("[UI] Store Pet FAILED");
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return;
  }

  Serial.printf("[UI] Store Pet OK path=%s\n", boxedPath);

  // Box/store already removed the live save on disk.
  // Only reset runtime/UI state here so Title correctly shows "New Pet".
  resetRuntimeToCleanNoSaveState(/*resetName=*/true);
  g_app.newPetFlowActive = false;
  saveManagerClearNamePendingFlag();
  saveManagerClearSleepPendingFlag();

  resetSettingsNav(true);
  g_settingsFlow.settingsPage = SettingsPage::TOP;
  uiPopReturnTarget();

  playBeep();
  uiActionEnterStateClean(UIState::TITLE_MENU, Tab::TAB_PET, true, input, 120);
}

static void actPet_StoredPets(InputState &input)
{
  g_settingsFlow.settingsPage = SettingsPage::PET;

  playBeep();
  openImportPetListFromSettings(SettingsPage::PET, input);
  requestUIRedraw();
}

static void actPet_BackupCurrentPet(InputState &input)
{
  char path[128];
  if (!saveManagerBackupCurrentPet(path, sizeof(path)))
    ui_showMessage("Backup Failed");
  else
    ui_showSuccessMessage("Pet Saved!");

  g_settingsFlow.settingsPage = SettingsPage::PET;
  requestUIRedraw();
  uiDrainKb(input);
  clearInputLatch();
  playBeep();
}

static void actPet_RestoreFromBackup(InputState &input)
{
  playBeep();
  openBackupPetListFromSettings(SettingsPage::PET, input);
  requestUIRedraw();
}

static void actPet_NewPet(InputState &input)
{
  (void)input;

  g_settingsFlow.settingsPage = SettingsPage::PET;
  UiSettingsPages::ShowGameNewPetConfirm();

  requestFullUIRedraw();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

// ------------------------------------------------------------
// SYSTEM page actions + hook
// ------------------------------------------------------------

static void hookSystem(InputState &input, int cursor)
{
  // Preserve existing behavior exactly: this hook manages the Factory Reset row
  factoryResetSystemSettingsHook(input, cursor);
}

static void actSystem_SetTime(InputState &)
{
  beginSetTimeEditorFromSettings(SettingsPage::SYSTEM, UIState::SETTINGS, g_app.currentTab);
  clearInputLatch();
}

static void actSystem_TimeZoneSelect(InputState &)
{
  settingsCycleTimeZone(+1);
  // settingsCycleTimeZone already redraws + beeps + clears latch
}

static void actSystem_Toggle24Hour(InputState &)
{
  settingsSetUse24HourTime(!settingsUse24HourTime());
  saveSettingsToSD();
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actSystem_TimeZoneLeft(InputState &) { settingsCycleTimeZone(-1); }

static void actSystem_TimeZoneRight(InputState &) { settingsCycleTimeZone(+1); }

static void actSystem_OpenWifi(InputState &input)
{
  g_settingsFlow.settingsPage = SettingsPage::WIFI;
  g_wifi.wifiSettingsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static MenuItem kSystemItems[] = {
    {"Set Time", actSystem_SetTime, nullptr, nullptr, nullptr},
    {"Time Zone", actSystem_TimeZoneSelect, actSystem_TimeZoneLeft, actSystem_TimeZoneRight, nullptr},
    {"24 Hour Time", actSystem_Toggle24Hour, nullptr, nullptr, nullptr},
    {"System Status", actTop_OpenStatus, nullptr, nullptr, nullptr},
    {"Factory Reset", nullptr, nullptr, nullptr, nullptr}, // handled by hookSystem()
};

// ------------------------------------------------------------
// Menu definitions
// ------------------------------------------------------------
static MenuItem kTopItems[] = {
    {"Care Guide", actTop_Controls, nullptr, nullptr, nullptr},
    {"Volume", actTop_VolumeSelect, actTop_VolumeLeft, actTop_VolumeRight, nullptr},
    {"WiFi Settings", actTop_OpenWifi, nullptr, nullptr, nullptr},
    {"Pet Options", actTop_OpenPet, nullptr, nullptr, enHasLivePet},
    {"Game Options", actTop_OpenGame, nullptr, nullptr, nullptr},
    {"Screen Settings", actTop_OpenScreen, nullptr, nullptr, nullptr},
    {"System Settings", actTop_OpenSystem, nullptr, nullptr, nullptr},
    {"Console", actTop_Console, nullptr, nullptr, enConsole},
    {"Credits", actTop_Credits, nullptr, nullptr, nullptr},
    {"Store Pet", actPet_StorePet, nullptr, nullptr, enHasLivePet},
    {"Main Menu", actTop_MainMenu, nullptr, nullptr, nullptr},
};

static MenuItem kScreenItems[] = {
    {"Brightness", actScreen_BrightnessSelect, actScreen_BrightnessLeft, actScreen_BrightnessRight, nullptr},
    {"Auto Screen", actScreen_AutoScreenSelect, actScreen_AutoScreenLeft, actScreen_AutoScreenRight, nullptr},
    {"Auto Clock", actScreen_AutoClockSelect, actScreen_AutoClockLeft, actScreen_AutoClockRight, nullptr},
    {"Shake Sensitivity", actScreen_ShakeSensitivitySelect, actScreen_ShakeSensitivityLeft,
     actScreen_ShakeSensitivityRight, nullptr},
};

#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
#pragma message("\033[42m\033[30m [ PUBLIC BUILD ] \033[0m")
#else
#pragma message("\033[41m\033[37m [ DEV BUILD - DO NOT SHIP ] \033[0m")
#endif

static MenuItem kWifiItems[] = {
    {"WiFi", actWifi_Toggle, nullptr, nullptr, nullptr},
    {"Set Network", actWifi_SetNetwork, nullptr, nullptr, nullptr},
    {"Stored Networks", actWifi_StoredNetworks, nullptr, nullptr, nullptr},
    {"Reset WiFi", actWifi_Reset, nullptr, nullptr, nullptr},
};

static MenuItem kPetItems[] = {
    {"Rename Pet", actPet_RenamePet, nullptr, nullptr, enHasLivePet},
    {"Backup Current Pet", actPet_BackupCurrentPet, nullptr, nullptr, enHasLivePet},
    {"Restore From Backup", actPet_RestoreFromBackup, nullptr, nullptr, enHasLivePet},
    {"New Pet", actPet_NewPet, nullptr, nullptr, nullptr},
};

static MenuItem kGameItems[] = {
    {"Pace", actGame_DecayModeSelect, actGame_DecayModeLeft, actGame_DecayModeRight, nullptr},
    {"Passive XP", actGame_TogglePassiveXp, nullptr, nullptr, nullptr},
    {"Step Counter", actGame_ToggleStepCounter, nullptr, nullptr, nullptr},
    {"Pet Death", actGame_ToggleDeath, nullptr, nullptr, nullptr},
    {"LED Alerts", actGame_ToggleLedAlerts, nullptr, nullptr, nullptr},
};

static MenuItem kAutoScreenItems[] = {
    {"", actAutoScreen_Apply, nullptr, nullptr, nullptr},
    {"", actAutoScreen_Apply, nullptr, nullptr, nullptr},
    {"", actAutoScreen_Apply, nullptr, nullptr, nullptr},
    {"", actAutoScreen_Apply, nullptr, nullptr, nullptr},
};

static MenuItem kDecayModeItems[] = {
    {"", actDecayMode_Apply, nullptr, nullptr, nullptr}, {"", actDecayMode_Apply, nullptr, nullptr, nullptr},
    {"", actDecayMode_Apply, nullptr, nullptr, nullptr}, {"", actDecayMode_Apply, nullptr, nullptr, nullptr},
    {"", actDecayMode_Apply, nullptr, nullptr, nullptr}, {"", actDecayMode_Apply, nullptr, nullptr, nullptr},
};

static MenuPageDef kPages[] = {
    {SettingsPage::TOP, kTopItems, (uint8_t)(sizeof(kTopItems) / sizeof(kTopItems[0])), cursorTop, nullptr},
    {SettingsPage::PET, kPetItems, (uint8_t)(sizeof(kPetItems) / sizeof(kPetItems[0])), cursorPet, nullptr},
    {SettingsPage::SCREEN, kScreenItems, (uint8_t)(sizeof(kScreenItems) / sizeof(kScreenItems[0])), cursorScreen,
     nullptr},
    {SettingsPage::SYSTEM, kSystemItems, (uint8_t)(sizeof(kSystemItems) / sizeof(kSystemItems[0])), cursorSystem,
     hookSystem},
    {SettingsPage::WIFI, kWifiItems, (uint8_t)(sizeof(kWifiItems) / sizeof(kWifiItems[0])), cursorWifi, nullptr},
    {SettingsPage::GAME, kGameItems, (uint8_t)(sizeof(kGameItems) / sizeof(kGameItems[0])), cursorGame, nullptr},
    {SettingsPage::AUTO_SCREEN, kAutoScreenItems, (uint8_t)(sizeof(kAutoScreenItems) / sizeof(kAutoScreenItems[0])),
     cursorAutoScreen, nullptr},
    {SettingsPage::DECAY_MODE, kDecayModeItems, (uint8_t)(sizeof(kDecayModeItems) / sizeof(kDecayModeItems[0])),
     cursorDecayMode, nullptr},
};

static const MenuPageDef *findPage(SettingsPage page)
{
  for (uint8_t i = 0; i < (uint8_t)(sizeof(kPages) / sizeof(kPages[0])); ++i)
  {
    if (kPages[i].page == page)
      return &kPages[i];
  }
  return nullptr;
}

static bool handleStoredNetworksPage(InputState &input, int move)
{
  int count = wifiStoreCount();

  if (count <= 0)
  {
    g_wifi.storedNetworkIndex = 0;
    g_wifi.storedNetworkActionActive = false;

    if (input.menuOnce || input.escOnce)
    {
      g_settingsFlow.settingsPage = SettingsPage::WIFI;
      g_wifi.wifiSettingsIndex = 2;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    if (input.hotSettings)
    {
      g_settingsFlow.settingsPage = SettingsPage::TOP;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    clearInputLatch();
    return true;
  }

  if (g_wifi.storedNetworkIndex < 0)
    g_wifi.storedNetworkIndex = 0;
  if (g_wifi.storedNetworkIndex >= count)
    g_wifi.storedNetworkIndex = count - 1;

  if (g_wifi.storedNetworkActionActive)
  {
    if (input.leftOnce || input.upOnce)
    {
      g_wifi.storedNetworkActionIndex = 0;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    if (input.rightOnce || input.downOnce)
    {
      g_wifi.storedNetworkActionIndex = 1;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    if (input.menuOnce || input.escOnce)
    {
      g_settingsFlow.settingsPage = SettingsPage::WIFI;
      g_wifi.wifiSettingsIndex = 2;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    if (input.hotSettings)
    {
      g_settingsFlow.settingsPage = SettingsPage::TOP;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    if (uiIsSelect(input))
    {
      const int selectedProfile = g_wifi.storedNetworkIndex;

      if (g_wifi.storedNetworkActionIndex == 0)
      {
        String ssid;
        String pass;

        if (wifiStoreLoadProfile(selectedProfile, ssid, pass) && ssid.length() > 0)
        {
          settingsSetWifiEnabled(true);
          wifiSetEnabled(true);
          saveSettingsToSD();
          saveManagerMarkDirty();

          strlcpy(g_wifi.ssid, ssid.c_str(), sizeof(g_wifi.ssid));
          strlcpy(g_wifi.pass, pass.c_str(), sizeof(g_wifi.pass));

          wifiConsoleBeginConnect(ssid.c_str(), pass.c_str());
          ui_showMessage("Connecting...");
        }
        else
        {
          ui_showMessage("Network missing");
          soundError();
        }
      }
      else
      {
        if (wifiStoreDeleteProfile(selectedProfile))
        {
          ui_showMessage("Network deleted");

          count = wifiStoreCount();
          if (g_wifi.storedNetworkIndex >= count)
            g_wifi.storedNetworkIndex = count - 1;
          if (g_wifi.storedNetworkIndex < 0)
            g_wifi.storedNetworkIndex = 0;
        }
        else
        {
          ui_showMessage("Delete failed");
          soundError();
        }
      }

      g_wifi.storedNetworkActionActive = false;
      requestUIRedraw();
      playBeep();
      clearInputLatch();
      return true;
    }

    clearInputLatch();
    return true;
  }

  if (input.menuOnce || input.escOnce || input.hotSettings)
  {
    g_settingsFlow.settingsPage = SettingsPage::WIFI;
    g_wifi.wifiSettingsIndex = 2;
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return true;
  }

  if (move != 0)
  {
    wrapMove(g_wifi.storedNetworkIndex, count, move);
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return true;
  }

  if (uiIsSelect(input))
  {
    g_wifi.storedNetworkActionActive = true;
    g_wifi.storedNetworkActionIndex = 0;
    requestUIRedraw();
    playBeep();
    clearInputLatch();
    return true;
  }

  return true;
}

// ------------------------------------------------------------
// Public entry point
// ------------------------------------------------------------
namespace UiSettingsMenu
{

static int wifiVisibleItemCount()
{
  int count = 0;
  for (int i = 0; i < (int)(sizeof(kWifiItems) / sizeof(kWifiItems[0])); ++i)
  {
    if (!kWifiItems[i].isEnabled || kWifiItems[i].isEnabled())
      ++count;
  }
  return count;
}

int WifiItemCount() { return (int)(sizeof(kWifiItems) / sizeof(kWifiItems[0])); }

const char *WifiItemLabel(int index)
{
  const int count = WifiItemCount();
  if (index < 0 || index >= count)
    return "";
  return kWifiItems[index].label ? kWifiItems[index].label : "";
}

bool Handle(InputState &input, int move)
{
  if (g_settingsFlow.settingsPage == SettingsPage::STORED_NETWORKS)
    return handleStoredNetworksPage(input, move);

  const MenuPageDef *def = findPage(g_settingsFlow.settingsPage);
  if (!def)
    return false;

  int &cursor = def->cursor();
  const int count = (int)def->itemCount;

  if (assetOtaConfirmActive())
  {
    if (input.menuOnce || input.escOnce)
    {
      assetOtaSetConfirmActive(false);
      requestUIRedraw();
      clearInputLatch();
      playBeep();
      return true;
    }

    if (uiIsSelect(input))
    {
      assetOtaSetConfirmActive(false);
      requestAssetProvisionOnNextBoot();
      clearInputLatch();
      delay(80);
      ESP.restart();
      return true;
    }

    return true;
  }

  // ------------------------------------------------------------
  // New Pet confirm modal (Pet Options)
  // ------------------------------------------------------------
  if (UiSettingsPages::GameNewPetConfirmActive())
  {
    if (input.leftOnce || input.upOnce)
    {
      UiSettingsPages::SetGameNewPetConfirmIndex(0);
      playBeep();
      requestUIRedraw();
      clearInputLatch();
      return true;
    }

    if (input.rightOnce || input.downOnce)
    {
      UiSettingsPages::SetGameNewPetConfirmIndex(1);
      playBeep();
      requestUIRedraw();
      clearInputLatch();
      return true;
    }

    if (input.menuOnce || input.escOnce || input.hotSettings)
    {
      UiSettingsPages::HideGameNewPetConfirm();
      requestUIRedraw();
      uiDrainKb(input);
      inputForceClear();
      clearInputLatch();
      return true;
    }

    if (uiIsSelect(input))
    {
      const bool storeFirst = (UiSettingsPages::GameNewPetConfirmIndex() == 0);

      if (storeFirst && saveManagerSaveFileExists())
      {
        char boxedPath[128] = {0};

        if (!saveManagerBoxCurrentPet(boxedPath, sizeof(boxedPath)))
        {
          soundError();
          ui_showMessage("Store failed");
          Serial.println("[UI] New Pet Store Current FAILED");
          UiSettingsPages::HideGameNewPetConfirm();
          requestUIRedraw();
          clearInputLatch();
          return true;
        }

        Serial.printf("[UI] New Pet Store Current OK path=%s\n", boxedPath);
      }
      else
      {
        // User chose not to store current pet first.
        saveManagerDeletePetOnly();
      }

      UiSettingsPages::HideGameNewPetConfirm();
      playBeep();

      // Box/store already removes the live save when storeFirst is true.
      // Otherwise saveManagerDeletePetOnly() handled removal above.

      UiSettingsPages::HideGameNewPetConfirm();
      playBeep();

      // Wipe live save and also clear runtime pet state so the title menu
      // correctly shows "New Pet" after storing/removing the active pet.
      saveManagerDeletePetOnly();
      resetRuntimeToCleanNoSaveState(/*resetName=*/true);
      g_app.newPetFlowActive = false;
      saveManagerClearNamePendingFlag();
      saveManagerClearSleepPendingFlag();

      // Now enter egg selection clean
      uiActionEnterState(UIState::CHOOSE_PET, Tab::TAB_PET, true);
      uiChoosePetOnEnter(input);

      requestFullUIRedraw();
      requestUIRedraw();
      clearInputLatch();
      return true;
    }

    clearInputLatch();
    return true;
  }

  // Move
  if (move != 0)
  {
    wrapMove(cursor, count, move);
    requestUIRedraw();
    playBeep();
    return true;
  }

  if (count <= 0)
    return true;

  // Clamp cursor just in case
  if (cursor < 0)
    cursor = 0;
  if (cursor >= count)
    cursor = count - 1;

  MenuItem &item = def->items[cursor];

  // Optional per-page hook (e.g., Factory Reset flow on SYSTEM page)
  if (def->pageHook)
  {
    def->pageHook(input, cursor);
  }

  // Disabled items: block select/left/right (still allow moving)
  if (item.isEnabled && !item.isEnabled())
  {
    if (uiIsSelect(input) || input.leftOnce || input.rightOnce)
    {
      soundError();
      clearInputLatch();
      return true;
    }
    return true;
  }

  // Left/Right
  if (input.leftOnce && item.onLeft)
  {
    item.onLeft(input);
    return true;
  }
  if (input.rightOnce && item.onRight)
  {
    item.onRight(input);
    return true;
  }

  // Select
  if ((uiIsSelect(input)) && item.onSelect)
  {
    item.onSelect(input);
    return true;
  }

  return true;
}

} // namespace UiSettingsMenu