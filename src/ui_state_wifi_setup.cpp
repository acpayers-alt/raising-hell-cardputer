#include "ui_state_wifi_setup.h"

#include "save_manager.h"
#include <Arduino.h>
#include <ctype.h>
#include <string.h>

#include "app_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_input_common.h" // uiDrainKb
#include "ui_runtime.h"      // requestUIRedraw
#include "wifi_power.h"
#include "wifi_setup_state.h" // g_wifi, g_wifiSetupFromBootWizard
#include "wifi_store.h"
#include "wifi_time.h"
#include <WiFi.h>

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline uint8_t clampU8(uint8_t v, uint8_t lo, uint8_t hi)
{
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static inline uint8_t currentMaxLen() { return (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID) ? 32 : 64; }

static void wifiSetupResetForStage(uint8_t stage)
{
  g_wifi.setupStage = clampU8(stage, WIFI_SETUP_STAGE_SCAN, WIFI_SETUP_STAGE_PASS);
  g_wifi.buf[0] = '\0';
  requestUIRedraw();
}

static void wifiSetupResetScanState()
{
  g_wifi.scanStarted = false;
  g_wifi.scanInProgress = false;
  g_wifi.scanCount = 0;
  g_wifi.scanIndex = 0;

  for (int i = 0; i < WifiSetupState::kMaxScanResults; ++i)
  {
    g_wifi.scanSsids[i][0] = '\0';
    g_wifi.scanRssi[i] = -127;
  }
}

static void wifiSetupStartScan()
{
  wifiConsoleDisconnect(false);

  g_wifi.scanStarted = true;
  g_wifi.scanInProgress = true;
  g_wifi.scanCount = 0;
  g_wifi.scanIndex = 0;

  requestUIRedraw();
}

static void wifiSetupRunBlockingScan()
{
  wifiConsoleDisconnect(false);

  applyWifiPower(true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_MODE_NULL);
  delay(100);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, true);
  WiFi.scanDelete();
  delay(300);

  Serial.printf("[WIFI SETUP] blocking scan begin mode=%d status=%d\n", (int)WiFi.getMode(), (int)WiFi.status());

  const int n = WiFi.scanNetworks(false, true);

  Serial.printf("[WIFI SETUP] blocking scan result n=%d mode=%d status=%d\n", n, (int)WiFi.getMode(),
                (int)WiFi.status());

  g_wifi.scanInProgress = false;
  g_wifi.scanCount = 0;

  if (n <= 0)
  {
    WiFi.scanDelete();
    g_wifi.scanStarted = false;
    g_wifi.scanIndex = 0;
    requestUIRedraw();
    return;
  }

  for (int i = 0; i < n && g_wifi.scanCount < WifiSetupState::kMaxScanResults; ++i)
  {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0)
      continue;

    bool dup = false;
    for (int j = 0; j < g_wifi.scanCount; ++j)
    {
      if (strcmp(g_wifi.scanSsids[j], ssid.c_str()) == 0)
      {
        dup = true;
        break;
      }
    }
    if (dup)
      continue;

    strlcpy(g_wifi.scanSsids[g_wifi.scanCount], ssid.c_str(), sizeof(g_wifi.scanSsids[g_wifi.scanCount]));
    g_wifi.scanRssi[g_wifi.scanCount] = (int16_t)WiFi.RSSI(i);
    g_wifi.scanCount++;
  }

  WiFi.scanDelete();

  const int totalItems = (g_wifi.scanCount > 0) ? (g_wifi.scanCount + 1) : 2;
  if (g_wifi.scanIndex >= totalItems)
    g_wifi.scanIndex = totalItems - 1;
  if (g_wifi.scanIndex < 0)
    g_wifi.scanIndex = 0;

  requestUIRedraw();
}

static void wifiSetupBeginPasswordEntry()
{
  g_wifi.pass[0] = '\0';
  g_wifi.buf[0] = '\0';
  g_wifi.setupStage = WIFI_SETUP_STAGE_PASS;
  requestUIRedraw();
}

static void wifiSetupSelectScanItem()
{
  if (g_wifi.scanInProgress)
    return;

  const bool hasResults = (g_wifi.scanCount > 0);

  if (!hasResults)
  {
    // Initial menu:
    //   0 = Scan for networks
    //   1 = Manual entry
    if (g_wifi.scanIndex == 0)
    {
      wifiSetupResetScanState();
      wifiSetupStartScan();
      return;
    }

    if (g_wifi.scanIndex == 1)
    {
      g_wifi.ssid[0] = '\0';
      g_wifi.buf[0] = '\0';
      g_wifi.connectFailCount = 0;
      g_wifi.setupStage = WIFI_SETUP_STAGE_SSID;
      requestUIRedraw();
      return;
    }

    return;
  }

  // After results exist:
  //   0..scanCount-1 = networks
  //   scanCount      = Manual entry
  if (g_wifi.scanIndex < g_wifi.scanCount)
  {
    strlcpy(g_wifi.ssid, g_wifi.scanSsids[g_wifi.scanIndex], sizeof(g_wifi.ssid));
    g_wifi.connectFailCount = 0;
    wifiSetupBeginPasswordEntry();
    return;
  }

  if (g_wifi.scanIndex == g_wifi.scanCount)
  {
    g_wifi.ssid[0] = '\0';
    g_wifi.buf[0] = '\0';
    g_wifi.connectFailCount = 0;
    g_wifi.setupStage = WIFI_SETUP_STAGE_SSID;
    requestUIRedraw();
    return;
  }
}

static void wifiSetupCommitBufferToCurrentField()
{
  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID)
  {
    strncpy(g_wifi.ssid, g_wifi.buf, sizeof(g_wifi.ssid) - 1);
    g_wifi.ssid[sizeof(g_wifi.ssid) - 1] = '\0';
  }
  else if (g_wifi.setupStage == WIFI_SETUP_STAGE_PASS)
  {
    strncpy(g_wifi.pass, g_wifi.buf, sizeof(g_wifi.pass) - 1);
    g_wifi.pass[sizeof(g_wifi.pass) - 1] = '\0';
  }
}

static void wifiSetupBackspace()
{
  const size_t n = strnlen(g_wifi.buf, sizeof(g_wifi.buf));
  if (n == 0)
    return;
  g_wifi.buf[n - 1] = '\0';
  requestUIRedraw();
}

static void wifiSetupAppendChar(char c)
{
  const uint8_t maxLen = currentMaxLen();
  const size_t n = strnlen(g_wifi.buf, sizeof(g_wifi.buf));
  if (n >= maxLen)
    return;
  if (n + 1 >= sizeof(g_wifi.buf))
    return;

  g_wifi.buf[n] = c;
  g_wifi.buf[n + 1] = '\0';
  requestUIRedraw();
}

static void wifiSetupCancel()
{
  inputForceClear();

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_PASS)
  {
    g_wifi.buf[0] = '\0';
    g_wifi.pass[0] = '\0';
    g_wifi.setupStage = WIFI_SETUP_STAGE_SCAN;
    g_wifi.aborted = true;
    uiActionEnterState(g_wifi.returnState, g_wifi.returnTab, true);
    requestUIRedraw();
    return;
  }

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID)
  {
    g_wifi.buf[0] = '\0';
    g_wifi.setupStage = WIFI_SETUP_STAGE_SCAN;
    requestUIRedraw();
    return;
  }

  g_wifi.aborted = true;
  uiActionEnterState(g_wifi.returnState, g_wifi.returnTab, true);
  requestUIRedraw();
}

static void wifiSetupSelect()
{
  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SCAN)
  {
    wifiSetupSelectScanItem();
    return;
  }

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID)
  {
    wifiSetupCommitBufferToCurrentField();
    wifiSetupBeginPasswordEntry();
    return;
  }

  wifiSetupCommitBufferToCurrentField();

  wifiStoreSave(String(g_wifi.ssid), String(g_wifi.pass));

  if (g_wifiSetupFromBootWizard)
  {
    settingsSetWifiEnabled(true);
    saveSettingsToSD();
  }

  wifiConsoleBeginConnect(g_wifi.ssid, g_wifi.pass);
  uiActionEnterState(UIState::WIFI_CONNECT_WAIT, g_app.currentTab, true);
  requestUIRedraw();
  return;
}

static void wifiSetupNavLeft()
{
  if (g_wifi.setupStage == WIFI_SETUP_STAGE_PASS)
  {
    strncpy(g_wifi.buf, g_wifi.ssid, sizeof(g_wifi.buf) - 1);
    g_wifi.buf[sizeof(g_wifi.buf) - 1] = '\0';
    g_wifi.setupStage = WIFI_SETUP_STAGE_SSID;
    requestUIRedraw();
  }
  else if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID)
  {
    g_wifi.buf[0] = '\0';
    g_wifi.setupStage = WIFI_SETUP_STAGE_SCAN;
    requestUIRedraw();
  }
}

static void wifiSetupNavRight()
{
  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID)
  {
    wifiSetupCommitBufferToCurrentField();
    wifiSetupBeginPasswordEntry();
  }
}

static void wifiSetupNavUp()
{
  if (g_wifi.setupStage != WIFI_SETUP_STAGE_SCAN || g_wifi.scanInProgress)
    return;

  const int totalItems = (g_wifi.scanCount > 0) ? (g_wifi.scanCount + 1) : 2;
  if (totalItems <= 0)
    return;

  g_wifi.scanIndex--;
  if (g_wifi.scanIndex < 0)
    g_wifi.scanIndex = totalItems - 1;
  requestUIRedraw();
}

static void wifiSetupNavDown()
{
  if (g_wifi.setupStage != WIFI_SETUP_STAGE_SCAN || g_wifi.scanInProgress)
    return;

  const int totalItems = (g_wifi.scanCount > 0) ? (g_wifi.scanCount + 1) : 2;
  if (totalItems <= 0)
    return;

  g_wifi.scanIndex++;
  if (g_wifi.scanIndex >= totalItems)
    g_wifi.scanIndex = 0;
  requestUIRedraw();
}

// -----------------------------------------------------------------------------
// Public state handler
// -----------------------------------------------------------------------------
void uiWifiSetupHandle(InputState &in)
{

  static uint32_t s_lastWifiInputSig = 0;

  uint32_t sig = 0;
  sig |= (uint32_t)(in.upOnce ? 1 : 0) << 0;
  sig |= (uint32_t)(in.downOnce ? 1 : 0) << 1;
  sig |= (uint32_t)(in.leftOnce ? 1 : 0) << 2;
  sig |= (uint32_t)(in.rightOnce ? 1 : 0) << 3;
  sig |= (uint32_t)(in.selectOnce ? 1 : 0) << 4;
  sig |= (uint32_t)(in.menuOnce ? 1 : 0) << 5;
  sig |= (uint32_t)(in.encoderPressOnce ? 1 : 0) << 6;
  sig |= (uint32_t)(in.escOnce ? 1 : 0) << 7;
  sig |= (uint32_t)(g_wifi.setupStage & 0xFF) << 16;

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SCAN && g_wifi.scanInProgress)
  {
    wifiSetupRunBlockingScan();
    return;
  }

  bool didTextChange = false;

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID || g_wifi.setupStage == WIFI_SETUP_STAGE_PASS)
  {
    for (int i = 0; i < 16; ++i)
    {
      if (in.kbQHead == in.kbQTail)
        break;

      KeyEvent ev = in.kbPop();
      const uint8_t code = ev.code;

      if (code == 0 || code == RH_KEY_FN || code == RH_KEY_SHIFT)
        continue;

      if (code == (uint8_t)'\b' || code == (uint8_t)0x7F || code == RH_KEY_BACKSPACE)
      {
        wifiSetupBackspace();
        didTextChange = true;
        continue;
      }

      if (code == (uint8_t)'\n' || code == (uint8_t)'\r')
      {
        wifiSetupSelect();
        continue;
      }

      const char c = (char)code;
      if (isprint((unsigned char)c))
      {
        wifiSetupAppendChar(c);
        didTextChange = true;
      }
    }
  }

  // Nav (kept harmless; screen mostly uses left/right stage switching).
  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SCAN)
  {
    // Cardputer nav cluster can arrive as queued punctuation chars.
    for (int i = 0; i < 8; ++i)
    {
      if (in.kbQHead == in.kbQTail)
        break;

      KeyEvent ev = in.kbPop();
      const uint8_t code = ev.code;

      if (code == ';' || code == ',')
      {
        wifiSetupNavUp();
        continue;
      }

      if (code == '.' || code == '/')
      {
        wifiSetupNavDown();
        continue;
      }
    }

    // Confirm/back come from input_cardputer.cpp as edge flags in non-text mode.
    if (in.selectOnce)
      wifiSetupSelect();

    if (in.escOnce || in.menuOnce)
    {
      wifiSetupCancel();
      return;
    }
  }

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID ||
    g_wifi.setupStage == WIFI_SETUP_STAGE_PASS)
{
  if (in.escOnce || in.menuOnce)
  {
    wifiSetupCancel();
    return;
  }
}

  // If text changed, swallow remaining queued key events this tick.
  if (didTextChange)
    uiDrainKb(in);
}