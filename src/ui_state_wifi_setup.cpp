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
#include "ui_state_wifi_connect_wait.h"
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
    g_wifi.scanOpen[i] = false;
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

static int wifiSetupScanTotalItems() { return (g_wifi.scanCount > 0) ? (g_wifi.scanCount + 2) : 2; }

static void wifiSetupRunBlockingScan()
{
  wifiConsoleDisconnect(false);

  applyWifiPower(true);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, false);
  WiFi.scanDelete();
  delay(150);

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
      g_wifi.scanOpen[g_wifi.scanCount] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      g_wifi.scanCount++;
  }

  WiFi.scanDelete();

  const int totalItems = wifiSetupScanTotalItems();

  // move selection off Rescan after scan completes
  if (g_wifi.scanCount > 0 && g_wifi.scanIndex == 0)
    g_wifi.scanIndex = 1;
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

  // Prevent the ENTER used to pick the SSID from immediately submitting
  // the password screen on the next frame.
  clearInputLatch();
  inputForceClear();

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
  //   0              = Rescan
  //   1..scanCount   = networks
  //   scanCount + 1  = Manual entry

  if (g_wifi.scanIndex == 0)
  {
    Serial.println("[WIFI SETUP] rescan selected");

    wifiSetupResetScanState();
    wifiSetupStartScan();
    return;
  }

  if (g_wifi.scanIndex >= 1 && g_wifi.scanIndex <= g_wifi.scanCount)
  {
    const int realIndex = g_wifi.scanIndex - 1;

    strlcpy(g_wifi.ssid, g_wifi.scanSsids[realIndex], sizeof(g_wifi.ssid));
    g_wifi.connectFailCount = 0;

    if (g_wifi.scanOpen[realIndex])
    {
      g_wifi.pass[0] = '\0';

      if (g_wifiSetupFromBootWizard)
      {
        settingsSetWifiEnabled(true);
        saveSettingsToSD();
      }

      wifiStoreSave(String(g_wifi.ssid), String(""));
      Serial.printf("[WIFI] setup saved open network SSID: %s\n", g_wifi.ssid);

      wifiResetConnectUiState();
      wifiConsoleBeginConnect(g_wifi.ssid, "");

      clearInputLatch();
      inputForceClear();

      uiActionEnterState(UIState::WIFI_CONNECT_WAIT, g_wifi.returnTab, true);
      requestUIRedraw();
      return;
    }

    wifiSetupBeginPasswordEntry();
    return;
  }

  if (g_wifi.scanIndex == g_wifi.scanCount + 1)
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
    g_wifi.connectFailCount = 0;
    g_wifi.setupStage = WIFI_SETUP_STAGE_SCAN;
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

    if (g_wifi.ssid[0] == '\0')
    {
      requestUIRedraw();
      return;
    }

    wifiSetupBeginPasswordEntry();
    return;
  }

  // PASS stage
  wifiSetupCommitBufferToCurrentField();

  if (g_wifi.ssid[0] == '\0')
  {
    strncpy(g_wifi.buf, g_wifi.ssid, sizeof(g_wifi.buf) - 1);
    g_wifi.buf[sizeof(g_wifi.buf) - 1] = '\0';
    g_wifi.setupStage = WIFI_SETUP_STAGE_SSID;
    requestUIRedraw();
    return;
  }

  if (g_wifiSetupFromBootWizard)
  {
    settingsSetWifiEnabled(true);
    saveSettingsToSD();
  }

  // Persist creds immediately on submit while this is still the source-of-truth moment.
  if (g_wifi.ssid[0])
  {
    wifiStoreSave(String(g_wifi.ssid), String(g_wifi.pass));
    Serial.printf("[WIFI] setup saved network for SSID: %s passLen=%u\n", g_wifi.ssid, (unsigned)strlen(g_wifi.pass));
  }
  else
  {
    Serial.printf("[WIFI] setup skip save: empty ssid\n");
  }

  wifiResetConnectUiState();
  wifiConsoleBeginConnect(g_wifi.ssid, g_wifi.pass);

  clearInputLatch();
  inputForceClear();

  uiActionEnterState(UIState::WIFI_CONNECT_WAIT, g_wifi.returnTab, true);
  requestUIRedraw();
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
    if (g_wifi.ssid[0] != '\0')
      wifiSetupBeginPasswordEntry();
  }
}

static void wifiSetupNavUp()
{
  if (g_wifi.setupStage != WIFI_SETUP_STAGE_SCAN || g_wifi.scanInProgress)
    return;

  const int totalItems = wifiSetupScanTotalItems();
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

  const int totalItems = wifiSetupScanTotalItems();
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
  (void)s_lastWifiInputSig;

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

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SCAN)
  {
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

    if (in.selectOnce)
      wifiSetupSelect();

    if (in.escOnce || in.menuOnce)
    {
      wifiSetupCancel();
      return;
    }
  }

  if (g_wifi.setupStage == WIFI_SETUP_STAGE_SSID || g_wifi.setupStage == WIFI_SETUP_STAGE_PASS)
  {
    if (in.escOnce || in.menuOnce)
    {
      wifiSetupCancel();
      return;
    }
  }

  if (didTextChange)
    uiDrainKb(in);
}