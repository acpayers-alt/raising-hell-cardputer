#include "led_status.h"
#include "app_state.h"
#include "pet.h"
#include "sleep_state.h"

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include "display.h"
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

static constexpr uint8_t LED_PIN   = 21;
static constexpr uint8_t LED_COUNT = 1;

// Cardputer: GRB order confirmed
static Adafruit_NeoPixel g_led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

static bool g_inited = false;

void ledSetScreenOff(bool isOff) {
  // Ignore the temporary ON state created by the rail-power pulse.
  // Otherwise the pulse teardown path gets disabled and the screen
  // never turns back off after an LED alert.
  if (s_pulseActive && !isOff)
    return;

  s_screenIsOff = isOff;
}

bool ledIsSupported() { return true; }

void ledInit() {
  if (g_inited) return;

  g_led.begin();
  g_led.setBrightness(255);
  g_led.clear();
  g_led.show();

  g_inited = true;
}

void ledOff() {
  if (!g_inited) return;
  g_led.clear();
  g_led.show();
}

// ------------------------------------------------------------
// Backlight pulse helpers (screen-off only)
// ------------------------------------------------------------
static void pulseBacklightBeginIfNeeded() {
  if (!s_screenIsOff) return;
  if (s_pulseActive) return;

  s_pulseActive = true;
  backlightPulseBegin(255);
}

static void pulseBacklightEndIfNeeded() {
  if (!s_screenIsOff) return;
  if (!s_pulseActive) return;

  s_pulseActive = false;
  backlightPulseEnd();
}

// ------------------------------------------------------------
// Low-level set (does NOT manage heartbeat scheduling)
// ------------------------------------------------------------
void ledSetRGB(uint8_t r, uint8_t g, uint8_t b) {
  if (!ledAlertsEnabled) return;
  if (g_ledLocked) return;
  ledInit();

#if defined(ESP32)
  portENTER_CRITICAL(&g_ledMux);
#endif
  g_led.setPixelColor(0, g_led.Color(r, g, b));
#if defined(ESP32)
  portEXIT_CRITICAL(&g_ledMux);
#endif

  // If screen is off, make sure pulse is active and rail is stable BEFORE show().
  if (s_screenIsOff) {
    pulseBacklightBeginIfNeeded();

    // Send twice to be extra robust if the first show races power-up
    g_led.show();
    delay(8);
    g_led.show();
  } else {
    g_led.show();
  }

  delay(1);
  yield();
}

// ------------------------------------------------------------
// Mode/color + heartbeat driver
// ------------------------------------------------------------
static void modeColor(LedPetMode mode, uint8_t& r, uint8_t& g, uint8_t& b) {
  switch (mode) {
    default:
    case LED_PET_OFF:      r = 0;   g = 0;   b = 0;   break;
    case LED_PET_OK:       r = 0;   g = 50;  b = 0;   break;
    case LED_PET_HUNGRY:   r = 70;  g = 45;  b = 0;   break;
    case LED_PET_TIRED:    r = 0;   g = 0;   b = 90;  break;
    case LED_PET_SLEEPING: r = 55;  g = 0;   b = 70;  break;
    case LED_PET_DANGER:   r = 110; g = 0;   b = 0;   break;
    case LED_PET_BORED:    r = 0;   g = 70;  b = 70;  break; // cyan-ish
    case LED_PET_MAD:      r = 120; g = 25;  b = 0;   break; // angry orange/red
  }
}

static uint8_t flashesForMode(LedPetMode mode) {
  switch (mode) {
    case LED_PET_DANGER:   return 5;
    case LED_PET_HUNGRY:   return 2;
    case LED_PET_TIRED:    return 3;
    case LED_PET_SLEEPING: return 1;
    case LED_PET_BORED:    return 2;
    case LED_PET_MAD:      return 4;
    case LED_PET_OK:       return 1;
    case LED_PET_OFF:
    default:               return 0;
  }
}

// Heartbeat interval
static uint32_t heartbeatIntervalMs(LedPetMode /*mode*/) {
  return 60000UL; // 1 minute
}

// LED on-time (you asked for “stay lit a little longer”)
static uint16_t onTimeMs(LedPetMode mode) {
  switch (mode) {
    case LED_PET_SLEEPING: return 650;
    case LED_PET_OK:       return 450;
    default:               return 320;
  }
}

static uint16_t gapTimeMs(LedPetMode /*mode*/) {
  return 220;
}

// Driver state
static LedPetMode g_lastMode = LED_PET_OFF;
static uint32_t   g_nextHeartbeatMs = 0;

static bool     g_burstActive = false;
static uint8_t  g_burstFlashesRemaining = 0;
static bool     g_burstLedOn = false;
static uint32_t g_burstNextMs = 0;

void ledUpdatePetStatus(LedPetMode mode) {
  ledInit();

  // ------------------------------------------------------------------
  // HARD GATE: if LED alerts are disabled, force everything OFF and
  // clear any scheduled heartbeats/bursts immediately.
  // ------------------------------------------------------------------
  if (!ledAlertsEnabled) {
    g_lastMode = LED_PET_OFF;
    g_nextHeartbeatMs = 0;

    g_burstActive = false;
    g_burstFlashesRemaining = 0;
    g_burstLedOn = false;
    g_burstNextMs = 0;

    ledOff();
    pulseBacklightEndIfNeeded();

    // Only force backlight low in true screen-off mode
    if (s_screenIsOff) SET_BACKLIGHT(0);
    return;
  }

  const uint32_t now = millis();

  // Mode change: restart soon
  if (mode != g_lastMode) {
    g_lastMode = mode;
    g_nextHeartbeatMs = now + 250;

    g_burstActive = false;
    g_burstFlashesRemaining = 0;
    g_burstLedOn = false;
    g_burstNextMs = 0;

    ledOff();
    pulseBacklightEndIfNeeded();
  }

  // OFF means fully off
  if (mode == LED_PET_OFF) {
    ledOff();
    if (s_screenIsOff) SET_BACKLIGHT(0);
    pulseBacklightEndIfNeeded();
    return;
  }

  // Not bursting: wait for heartbeat time
  if (!g_burstActive) {
    if (g_nextHeartbeatMs == 0) g_nextHeartbeatMs = now + heartbeatIntervalMs(mode);

    if ((int32_t)(now - g_nextHeartbeatMs) < 0) {
      // keep dark between heartbeats
      ledOff();
      if (s_screenIsOff) SET_BACKLIGHT(0);
      pulseBacklightEndIfNeeded();
      return;
    }

    // Start burst
    g_burstActive = true;
    g_burstFlashesRemaining = flashesForMode(mode);
    g_burstLedOn = false;

    // If screen-off: start pulse and give rail a moment BEFORE first LED show
    pulseBacklightBeginIfNeeded();
    g_burstNextMs = now + 10;
  }

  // Waiting for next toggle
  if ((int32_t)(now - g_burstNextMs) < 0) return;

  // Done?
  if (g_burstFlashesRemaining == 0) {
    g_burstActive = false;
    g_nextHeartbeatMs = now + heartbeatIntervalMs(mode);

    ledOff();
    pulseBacklightEndIfNeeded();
    if (s_screenIsOff) SET_BACKLIGHT(0);
    return;
  }

  // Toggle
  g_burstLedOn = !g_burstLedOn;

  if (g_burstLedOn) {
    uint8_t r, g, b;
    modeColor(mode, r, g, b);
    ledSetRGB(r, g, b);
    g_burstNextMs = now + onTimeMs(mode);
  } else {
    ledOff();
    g_burstFlashesRemaining--;
    g_burstNextMs = now + gapTimeMs(mode);
  }
}

bool isPetSleepingNow() {
  return pet.isSleeping ||
         g_app.isSleeping ||
         g_app.sleepingByTimer ||
         g_app.sleepUntilRested ||
         g_app.sleepUntilAwakened ||
         (g_app.uiState == UIState::PET_SLEEPING);
}

LedPetMode computeLedMode() {
  if (isPetSleepingNow()) return LED_PET_SLEEPING;

  if (pet.health <= 25) return LED_PET_DANGER;
  if (pet.hunger <= 25) return LED_PET_HUNGRY;
  if (pet.energy <= 25) return LED_PET_TIRED;

  static LedPetMode s_lastMood = LED_PET_OK;

  // Small hysteresis so the LED doesn't flicker at boundary.
  static const int MAD_ON    = 25;
  static const int MAD_OFF   = 30;
  static const int BORED_ON  = 60;
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
