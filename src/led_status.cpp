#include "led_status.h"
#include "app_state.h"
#include "pet.h"
#include "save_manager.h"
#include "sleep_state.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

#include "display.h"
#include "graphics.h"
#include "user_toggles_state.h"

static bool g_ledLocked = false;

#if defined(ESP32)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE g_ledMux = portMUX_INITIALIZER_UNLOCKED;
#endif

// IMPORTANT:
// - display.h defines a FUNCTION named screenOff().
// - Do NOT use `if (screenOff)` as a boolean (that’s always “true” as a function pointer).
// - We track screen-off state locally via ledSetScreenOff().
static bool s_screenIsOff = false;

// Backlight pulse state (screen-off only rail-power hack)
static bool s_pulseActive = false;

static constexpr uint8_t LED_PIN = 21;
static constexpr uint8_t LED_COUNT = 1;

// Cardputer: GRB order confirmed
static Adafruit_NeoPixel g_led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

static bool g_inited = false;

static bool ledAlertsSuppressedForUiState()
{
  switch (g_app.uiState)
  {
  case UIState::BOOT:
  case UIState::BOOT_WIFI_PROMPT:
  case UIState::BOOT_WIFI_IMPORTED:
  case UIState::BOOT_WIFI_WAIT:
  case UIState::BOOT_TZ_PICK:
  case UIState::BOOT_NTP_WAIT:
  case UIState::BOOT_ASSET_WIFI_REQUIRED:
  case UIState::WIFI_SETUP:
  case UIState::WIFI_CONNECT_WAIT:
  case UIState::SET_TIME:
  case UIState::CONTROLS_HELP:
  case UIState::WHATS_NEW:
    return true;

  default:
    return false;
  }
}

bool ledInputLockActive() { return s_screenIsOff && s_pulseActive; }

void ledSetScreenOff(bool isOff)
{
  if (!isOff && s_pulseActive)
  {
    backlightRailPulseAdoptScreenOn();
    s_pulseActive = false;
  }

  s_screenIsOff = isOff;
}

bool ledIsSupported() { return true; }

void ledInit()
{
  if (g_inited)
    return;

  g_led.begin();
  g_led.setBrightness(255);
  g_led.clear();
  g_led.show();

  g_inited = true;
}

void ledOff()
{
  if (!g_inited)
    return;

  g_led.clear();
  g_led.show();
}

// ------------------------------------------------------------
// Backlight pulse helpers (screen-off only)
// ------------------------------------------------------------
static void pulseBacklightBeginIfNeeded()
{
  if (!s_screenIsOff)
    return;
  if (s_pulseActive)
    return;

  s_pulseActive = true;
  backlightRailPulseBegin(255);
}

static void pulseBacklightEndIfNeeded()
{
  if (!s_screenIsOff)
    return;
  if (!s_pulseActive)
    return;

  backlightRailPulseEnd();
  s_pulseActive = false;
}

// ------------------------------------------------------------
// Low-level set (does NOT manage heartbeat scheduling)
// ------------------------------------------------------------
void ledSetRGB(uint8_t r, uint8_t g, uint8_t b)
{
  if (!ledAlertsEnabled)
    return;
  if (ledAlertsSuppressedForUiState())
    return;
  if (g_ledLocked)
    return;

  ledInit();

#if defined(ESP32)
  portENTER_CRITICAL(&g_ledMux);
#endif
  g_led.setPixelColor(0, g_led.Color(r, g, b));
#if defined(ESP32)
  portEXIT_CRITICAL(&g_ledMux);
#endif

  // If screen is off, make sure pulse is active and rail is stable BEFORE show().
  if (s_screenIsOff)
    pulseBacklightBeginIfNeeded();

  g_led.show();

  delay(1);
  yield();
}

// ------------------------------------------------------------
// Mode/color + heartbeat driver
// ------------------------------------------------------------
static void modeColor(LedPetMode mode, uint8_t &r, uint8_t &g, uint8_t &b)
{
  switch (mode)
  {
  default:

  case LED_PET_SIGNAL_HIT:
    r = 0;
    g = 90;
    b = 255;
    break;

  case LED_PET_OFF:
    r = 0;
    g = 0;
    b = 0;
    break;

  // Positive / okay
  case LED_PET_OK:
  case LED_PET_BORED:
    r = 0;
    g = 60;
    b = 0;
    break;

  // Needs attention
  case LED_PET_HUNGRY:
  case LED_PET_TIRED:
  case LED_PET_SLEEPING:
  case LED_PET_MAD:
    r = 110;
    g = 45;
    b = 0;
    break;

  // Critical
  case LED_PET_DANGER:
    r = 110;
    g = 0;
    b = 0;
    break;
  }
}

static uint8_t flashesForMode(LedPetMode mode)
{
  switch (mode)
  {
  case LED_PET_DANGER:
    return 5;
  case LED_PET_HUNGRY:
    return 2;
  case LED_PET_TIRED:
    return 3;
  case LED_PET_SLEEPING:
    return 1;
  case LED_PET_BORED:
    return 2;
  case LED_PET_MAD:
    return 4;
  case LED_PET_OK:
    return 1;
  case LED_PET_SIGNAL_HIT:
    return 3;
  case LED_PET_OFF:
  default:
    return 0;
  }
}

// Heartbeat interval
static uint32_t heartbeatIntervalMs(LedPetMode mode)
{
  const uint8_t flashes = flashesForMode(mode);

  // Urgent modes = 2 flashes
  if (flashes > 1)
    return 120000UL; // 2 minutes

  // Everything else
  return 600000UL; // 10 minutes
}

// LED on-time (you asked for “stay lit a little longer”)
static uint16_t onTimeMs(LedPetMode mode)
{
  switch (mode)
  {
  case LED_PET_SLEEPING:
    return 1100;
  case LED_PET_OK:
    return 800;
  case LED_PET_SIGNAL_HIT:
    return 450;
  default:
    return 700;
  }
}

static uint16_t gapTimeMs(LedPetMode /*mode*/) { return 300; }

// Driver state
static LedPetMode g_lastMode = LED_PET_OFF;
static uint32_t g_nextHeartbeatMs = 0;

static bool g_burstActive = false;
static uint8_t g_burstFlashesRemaining = 0;
static bool g_burstLedOn = false;
static uint32_t g_burstNextMs = 0;

static bool g_oneShotAlertActive = false;
static LedPetMode g_oneShotAlertMode = LED_PET_OFF;

void ledTriggerWarwalkSignalHit()
{
  if (!ledAlertsEnabled)
    return;

  if (ledAlertsSuppressedForUiState())
    return;

  g_oneShotAlertActive = true;
  g_oneShotAlertMode = LED_PET_SIGNAL_HIT;

  // Force the heartbeat driver to start this burst immediately on the next
  // ledUpdatePetStatus() tick.
  g_burstActive = false;
  g_burstFlashesRemaining = 0;
  g_burstLedOn = false;
  g_burstNextMs = 0;
  g_nextHeartbeatMs = millis();
}

void ledUpdatePetStatus(LedPetMode mode)
{
  ledInit();

  // ------------------------------------------------------------------
  // BOOT / ONBOARDING GATE:
  // Do not allow pet LED alerts while boot, provisioning, Wi-Fi setup,
  // time setup, controls help, or What's New screens own the display.
  // ------------------------------------------------------------------
  if (ledAlertsSuppressedForUiState())
  {
    g_lastMode = mode;
    g_nextHeartbeatMs = millis() + heartbeatIntervalMs(mode);

    g_burstActive = false;
    g_burstFlashesRemaining = 0;
    g_burstLedOn = false;
    g_burstNextMs = 0;

    ledOff();
    pulseBacklightEndIfNeeded();

    if (s_screenIsOff)
      SET_BACKLIGHT(0);

    return;
  }

  // ------------------------------------------------------------------
  // HARD GATE: if LED alerts are disabled, force everything OFF and
  // clear any scheduled heartbeats/bursts immediately.
  // ------------------------------------------------------------------
  if (!ledAlertsEnabled)
  {
    g_lastMode = LED_PET_OFF;
    g_nextHeartbeatMs = 0;

    g_burstActive = false;
    g_burstFlashesRemaining = 0;
    g_burstLedOn = false;
    g_burstNextMs = 0;

    ledOff();
    pulseBacklightEndIfNeeded();

    // Only force backlight low in true screen-off mode.
    if (s_screenIsOff)
      SET_BACKLIGHT(0);

    return;
  }

  const uint32_t now = millis();
  const LedPetMode effectiveMode = g_oneShotAlertActive ? g_oneShotAlertMode : mode;

  // Mode change: restart soon. One-shot alerts start immediately.
  if (effectiveMode != g_lastMode)
  {
    g_lastMode = effectiveMode;
    g_nextHeartbeatMs = g_oneShotAlertActive ? now : now + heartbeatIntervalMs(effectiveMode);

    g_burstActive = false;
    g_burstFlashesRemaining = 0;
    g_burstLedOn = false;
    g_burstNextMs = 0;

    ledOff();
    pulseBacklightEndIfNeeded();
  }

  // OFF means fully off.
  if (effectiveMode == LED_PET_OFF)
  {
    ledOff();

    if (s_screenIsOff)
      SET_BACKLIGHT(0);

    pulseBacklightEndIfNeeded();
    return;
  }

  // Not bursting: wait for heartbeat time.
  if (!g_burstActive)
  {
    if (g_nextHeartbeatMs == 0)
      g_nextHeartbeatMs = now + heartbeatIntervalMs(effectiveMode);

    if ((int32_t)(now - g_nextHeartbeatMs) < 0)
    {
      // Keep dark between heartbeats.
      ledOff();

      if (s_screenIsOff)
        SET_BACKLIGHT(0);

      pulseBacklightEndIfNeeded();
      return;
    }

    // Start burst.
    g_burstActive = true;
    g_burstFlashesRemaining = flashesForMode(effectiveMode);
    g_burstLedOn = false;

    // If screen-off: start rail-only pulse and paint the hardware color
    // directly. This does not wake app/UI state.
    pulseBacklightBeginIfNeeded();

    if (s_screenIsOff)
    {
      uint8_t r, g, b;
      modeColor(effectiveMode, r, g, b);
      backlightRailPulseShowColor(r, g, b);
    }

    g_burstNextMs = now + 120;
  }

  // Waiting for next toggle.
  if ((int32_t)(now - g_burstNextMs) < 0)
    return;

  // Done?
  if (g_burstFlashesRemaining == 0)
  {
    g_burstActive = false;

    if (g_oneShotAlertActive)
    {
      g_oneShotAlertActive = false;
      g_oneShotAlertMode = LED_PET_OFF;
      g_lastMode = mode;
    }

    g_nextHeartbeatMs = now + heartbeatIntervalMs(mode);

    ledOff();

    // End rail-only pulse after final LED off.
    pulseBacklightEndIfNeeded();

    if (s_screenIsOff)
      SET_BACKLIGHT(0);

    return;
  }

  // Toggle.
  g_burstLedOn = !g_burstLedOn;

  if (g_burstLedOn)
  {
    uint8_t r, g, b;
    modeColor(effectiveMode, r, g, b);
    ledSetRGB(r, g, b);
    g_burstNextMs = now + onTimeMs(effectiveMode);
  }
  else
  {
    ledOff();
    g_burstFlashesRemaining--;
    g_burstNextMs = now + gapTimeMs(effectiveMode);
  }
}

bool isPetSleepingNow()
{
  return pet.isSleeping || g_app.isSleeping || g_app.sleepingByTimer || g_app.sleepUntilRested ||
         g_app.sleepUntilAwakened || (g_app.uiState == UIState::PET_SLEEPING);
}

LedPetMode computeLedMode()
{
  if (!saveManagerSaveFileExists())
    return LED_PET_OFF;

  if (isPetSleepingNow())
    return LED_PET_SLEEPING;

  if (pet.health <= 25)
    return LED_PET_DANGER;
  if (pet.hunger <= 25)
    return LED_PET_HUNGRY;
  if (pet.energy <= 25)
    return LED_PET_TIRED;

  static LedPetMode s_lastMood = LED_PET_OK;

  // Small hysteresis so the LED doesn't flicker at boundary.
  static const int MAD_ON = 25;
  static const int MAD_OFF = 30;
  static const int BORED_ON = 60;
  static const int BORED_OFF = 65;

  if (s_lastMood == LED_PET_MAD)
  {
    if (pet.happiness >= MAD_OFF)
      s_lastMood = LED_PET_OK;
  }
  else if (s_lastMood == LED_PET_BORED)
  {
    if (pet.happiness < MAD_ON)
      s_lastMood = LED_PET_MAD;
    else if (pet.happiness >= BORED_OFF)
      s_lastMood = LED_PET_OK;
  }
  else
  {
    if (pet.happiness <= MAD_ON)
      s_lastMood = LED_PET_MAD;
    else if (pet.happiness <= BORED_ON)
      s_lastMood = LED_PET_BORED;
    else
      s_lastMood = LED_PET_OK;
  }

  return s_lastMood;
}