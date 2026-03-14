#include "ui_state_wifi_setup.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>
#include "save_manager.h"

#include "app_state.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_input_common.h"   // uiDrainKb
#include "ui_runtime.h"        // requestUIRedraw
#include "wifi_setup_state.h"  // g_wifi, g_wifiSetupFromBootWizard
#include "wifi_time.h"
#include "wifi_store.h"

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static inline uint8_t clampU8(uint8_t v, uint8_t lo, uint8_t hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline uint8_t currentMaxLen()
{
  // SSID: 32 chars, PASS: 64 chars
  return (g_wifi.setupStage == 0) ? 32 : 64;
}

static void wifiSetupResetForStage(uint8_t stage)
{
  g_wifi.setupStage = clampU8(stage, 0, 1);
  g_wifi.buf[0] = '\0';
  requestUIRedraw();
}

static void wifiSetupCommitBufferToCurrentField()
{
  if (g_wifi.setupStage == 0)
  {
    strncpy(g_wifi.ssid, g_wifi.buf, sizeof(g_wifi.ssid) - 1);
    g_wifi.ssid[sizeof(g_wifi.ssid) - 1] = '\0';
  }
  else
  {
    strncpy(g_wifi.pass, g_wifi.buf, sizeof(g_wifi.pass) - 1);
    g_wifi.pass[sizeof(g_wifi.pass) - 1] = '\0';
  }
}

static void wifiSetupBackspace()
{
  const size_t n = strnlen(g_wifi.buf, sizeof(g_wifi.buf));
  if (n == 0) return;
  g_wifi.buf[n - 1] = '\0';
  requestUIRedraw();
}

static void wifiSetupAppendChar(char c)
{
  const uint8_t maxLen = currentMaxLen();
  const size_t n = strnlen(g_wifi.buf, sizeof(g_wifi.buf));
  if (n >= maxLen) return;
  if (n + 1 >= sizeof(g_wifi.buf)) return;

  g_wifi.buf[n] = c;
  g_wifi.buf[n + 1] = '\0';
  requestUIRedraw();
}

static void wifiSetupCancel()
{
  // Clear any pending keyboard input to prevent "fallthrough" presses.
  inputForceClear();

  // If this was entered from the boot wizard, return to its prompt.
  if (g_wifiSetupFromBootWizard)
  {
    uiActionEnterState(UIState::BOOT_WIFI_PROMPT, g_app.currentTab, true);
    return;
  }

  // Otherwise, return to settings.
  uiActionEnterState(UIState::SETTINGS, g_app.currentTab, true);
}

static void wifiSetupSelect()
{
  // Stage 0: SSID -> move to PASS
  if (g_wifi.setupStage == 0)
  {
    wifiSetupCommitBufferToCurrentField();
    wifiSetupResetForStage(1);
    return;
  }

  // Stage 1: PASS -> commit and proceed
  wifiSetupCommitBufferToCurrentField();

  // Persist credentials (so the rest of the system can use them)
  wifiStoreSave(String(g_wifi.ssid), String(g_wifi.pass));

  if (g_wifiSetupFromBootWizard)
  {
    settingsSetWifiEnabled(true);
    saveSettingsToSD();
  }
  
  // Kick off connection attempt NOW (this is what makes BOOT_WIFI_WAIT dismiss)
  wifiConsoleBeginConnect(g_wifi.ssid, g_wifi.pass);
  
  if (g_wifiSetupFromBootWizard)
  {
    uiActionEnterState(UIState::BOOT_WIFI_WAIT, g_app.currentTab, true);
    return;
  }

  // If entered from Settings, return there (connection continues in background)
  uiActionEnterState(UIState::SETTINGS, g_app.currentTab, true);
}

static void wifiSetupNavLeft()
{
  // Left goes back a stage (PASS -> SSID)
  if (g_wifi.setupStage == 1)
  {
    wifiSetupCommitBufferToCurrentField();

    // Load SSID into buffer for editing.
    strncpy(g_wifi.buf, g_wifi.ssid, sizeof(g_wifi.buf) - 1);
    g_wifi.buf[sizeof(g_wifi.buf) - 1] = '\0';

    g_wifi.setupStage = 0;
    requestUIRedraw();
  }
}

static void wifiSetupNavRight()
{
  // Right advances stage (SSID -> PASS)
  if (g_wifi.setupStage == 0)
  {
    wifiSetupCommitBufferToCurrentField();

    // Load PASS into buffer for editing.
    strncpy(g_wifi.buf, g_wifi.pass, sizeof(g_wifi.buf) - 1);
    g_wifi.buf[sizeof(g_wifi.buf) - 1] = '\0';

    g_wifi.setupStage = 1;
    requestUIRedraw();
  }
}

static void wifiSetupNavUp()   {}
static void wifiSetupNavDown() {}

// -----------------------------------------------------------------------------
// Public state handler
// -----------------------------------------------------------------------------
void uiWifiSetupHandle(InputState &in)
{
  // Ensure DEL/BKSP are treated as text editing, not menu/back.
  if (!g_textCaptureMode)
    inputSetTextCapture(true);

  // Keyboard text events: limit per tick so we don't starve rendering.
  bool didTextChange = false;
  for (int i = 0; i < 16; ++i)
  {
    if (in.kbQHead == in.kbQTail) break;

    // Your InputState::kbPop() returns a KeyEvent (no args).
    KeyEvent ev = in.kbPop();
    const uint8_t code = ev.code;

    // Ignore synthetic/meta keys.
    if (code == 0 || code == RH_KEY_FN || code == RH_KEY_SHIFT)
      continue;

    // Backspace / delete
    if (code == (uint8_t)'\b' || code == (uint8_t)0x7F || code == RH_KEY_BACKSPACE)
    {
      wifiSetupBackspace();
      didTextChange = true;
      continue;
    }

    // Enter / select
    if (code == (uint8_t)'\n' || code == (uint8_t)'\r')
    {
      wifiSetupSelect();
      continue;
    }

    // Printable character
    const char c = (char)code;
    if (isprint((unsigned char)c))
    {
      wifiSetupAppendChar(c);
      didTextChange = true;
    }
  }

  // Nav (kept harmless; screen mostly uses left/right stage switching).
  if (in.leftOnce)  wifiSetupNavLeft();
  if (in.rightOnce) wifiSetupNavRight();
  if (in.upOnce)    wifiSetupNavUp();
  if (in.downOnce)  wifiSetupNavDown();

  // Select and cancel.
  if (in.selectOnce) wifiSetupSelect();

  // Only ESC cancels. Do NOT use menuOnce here.
  if (in.escOnce)
  {
    wifiSetupCancel();
    return;
  }

  // If text changed, swallow remaining queued key events this tick.
  if (didTextChange)
    uiDrainKb(in);
}