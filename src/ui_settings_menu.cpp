#include "ui_settings_menu.h"

#include "app_state.h"
#include "menu_actions.h"
#include "save_manager.h"
#include "sdcard.h"
#include "settings_flow_state.h"
#include "sound.h"
#include "ui_input_common.h"
#include "ui_runtime.h"

#include "auto_screen.h"
#include "brightness_state.h"
#include "display.h"

#include "wifi_power.h"
#include "wifi_setup_state.h"
#include "wifi_store.h"
#include "wifi_time.h"

#include "flow_controls_help.h"
#include "flow_factory_reset.h"
#include "flow_time_editor.h"
#include "game_options_state.h"
#include "graphics.h"       // ui_showMessage
#include "ui_input_utils.h" // uiDrainKb
#include "flow_console.h"
#include "ui_settings_actions.h"
#include "asset_ota.h"

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

static void actTop_OpenScreen(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::SCREEN;
  g_app.screenSettingsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static void actTop_OpenSystem(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::SYSTEM;
  g_app.systemSettingsIndex = 0;
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

static bool enConsole()
{
#if PUBLIC_BUILD
  return false;
#else
  return true;
#endif
}

static void actTop_Console(InputState &input)
{
#if PUBLIC_BUILD
  ui_showMessage("Console disabled");
  soundError();
  clearInputLatch();
  (void)input;
#else
  openConsoleWithReturn(UIState::SETTINGS, g_app.currentTab, true, g_settingsFlow.settingsPage);
  uiDrainKb(input);
  requestUIRedraw();
  playBeep();
  clearInputLatch();
#endif
}

static void actTop_Credits(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::CREDITS;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
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

static void actScreen_AutoScreenSelect(InputState &)
{
  g_app.autoScreenIndex = (int)autoScreenTimeoutSel;
  g_settingsFlow.settingsReturnPage = SettingsPage::SCREEN;
  g_settingsFlow.settingsPage = SettingsPage::AUTO_SCREEN;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

// ------------------------------------------------------------
// WIFI page actions
// ------------------------------------------------------------
static void actWifi_Toggle(InputState &)
{
  const bool en = !wifiIsEnabled();

  wifiSetEnabled(en);
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

  g_app.uiState = UIState::WIFI_SETUP;
  requestUIRedraw();

  inputSetTextCapture(true);
  g_textCaptureMode = true;

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
  String msg;
  const bool ok = assetOtaCheckNow(&msg);

  if (msg.isEmpty())
    msg = ok ? "Asset OTA done" : "Asset OTA failed";

  ui_showMessage(msg.c_str());
  requestUIRedraw();

  if (ok)
    playBeep();
  else
    soundError();

  clearInputLatch();
}

// ------------------------------------------------------------
// GAME page actions
// ------------------------------------------------------------
static void actGame_DecayMode(InputState &)
{
  g_app.decayModeIndex = (int)saveManagerGetDecayMode();
  g_settingsFlow.settingsReturnPage = SettingsPage::GAME;
  g_settingsFlow.settingsPage = SettingsPage::DECAY_MODE;
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

static void actSystem_OpenWifi(InputState &)
{
  g_settingsFlow.settingsPage = SettingsPage::WIFI;
  g_wifi.wifiSettingsIndex = 0;
  requestUIRedraw();
  playBeep();
  clearInputLatch();
}

static MenuItem kSystemItems[] = {
    {"Set Time", actSystem_SetTime, nullptr, nullptr, nullptr},
    {"Factory Reset", nullptr, nullptr, nullptr, nullptr}, // handled by hookSystem()
    {"WiFi", actSystem_OpenWifi, nullptr, nullptr, nullptr},
};

// ------------------------------------------------------------
// Menu definitions
// ------------------------------------------------------------
static MenuItem kTopItems[] = {
    {"Volume", actTop_VolumeSelect, actTop_VolumeLeft, actTop_VolumeRight, nullptr},
    {"Controls", actTop_Controls, nullptr, nullptr, nullptr},
    {"Screen", actTop_OpenScreen, nullptr, nullptr, nullptr},
    {"System", actTop_OpenSystem, nullptr, nullptr, nullptr},
    {"Game", actTop_OpenGame, nullptr, nullptr, nullptr},
    {"Console", actTop_Console, nullptr, nullptr, enConsole},
    {"Credits", actTop_Credits, nullptr, nullptr, nullptr},
};

static MenuItem kScreenItems[] = {
    {"Brightness", nullptr, actScreen_BrightnessLeft, actScreen_BrightnessRight, nullptr},
    {"Auto Screen", actScreen_AutoScreenSelect, nullptr, nullptr, nullptr},
};

static MenuItem kWifiItems[] = {
  {"WiFi", actWifi_Toggle, nullptr, nullptr, nullptr},
  {"Set Network", actWifi_SetNetwork, nullptr, nullptr, nullptr},
  {"Reset WiFi", actWifi_Reset, nullptr, nullptr, nullptr},
  {"Time Zone", actWifi_TzSelect, actWifi_TzLeft, actWifi_TzRight, nullptr},
  {"Check Asset OTA", actWifi_CheckAssetOta, nullptr, nullptr, nullptr},
};

static MenuItem kGameItems[] = {
    {"Decay Mode", actGame_DecayMode, nullptr, nullptr, nullptr},
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

// ------------------------------------------------------------
// Public entry point
// ------------------------------------------------------------
namespace UiSettingsMenu
{

bool Handle(InputState &input, int move)
{
  const MenuPageDef *def = findPage(g_settingsFlow.settingsPage);
  if (!def)
    return false;

  int &cursor = def->cursor();
  const int count = (int)def->itemCount;

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