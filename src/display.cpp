#include "display.h"

// --- Standard / core ----------------------------------------------------------
#include <Arduino.h>

// --- Hardware / platform ------------------------------------------------------
#include "M5Cardputer.h"

// --- Core app systems ---------------------------------------------------------
#include "app_state.h"
#include "system_status_state.h"

// --- Display / UI -------------------------------------------------------------
#include "display_dims_state.h"
#include "display_state.h"
#include "ui_menu_state.h"
#include "ui_runtime.h"

// --- Rendering / animation ----------------------------------------------------
#include "anim_engine.h"
#include "graphics.h"

// --- Input / interaction ------------------------------------------------------
#include "input.h"

// --- Gameplay -----------------------------------------------------------------
#include "feed.h"
#include "pet.h"

// --- Audio / feedback ---------------------------------------------------------
#include "sound.h"

// --- Power / system control ---------------------------------------------------
#include "brightness_state.h"
#include "flow_power_menu.h"
#include "wifi_power.h"

// --- Debug --------------------------------------------------------------------
#include "debug_state.h"

// end of includes

static bool g_backlightPulseActive = false;

static uint8_t s_lastUserBrightnessLevel = 1;
static bool s_batteryDimActive = false;
static uint8_t s_batteryDimLevel = 0;       // protective dim target
static bool s_batteryWifiForcedOff = false; // only restore Wi-Fi if we disabled it

// --- Battery smoothing + curve helpers --------------------------------------

static int voltageToPercent(int mv)
{
  struct Point
  {
    int mv;
    int pct;
  };
  static const Point kCurve[] = {
      {4200, 100}, {4160, 95}, {4110, 88}, {4060, 80}, {4010, 72}, {3960, 64}, {3910, 56}, {3870, 48}, {3830, 40},
      {3790, 32},  {3750, 24}, {3710, 17}, {3670, 11}, {3630, 6},  {3590, 3},  {3550, 1},  {3500, 0},
  };

  if (mv >= kCurve[0].mv)
    return 100;
  const int last = (int)(sizeof(kCurve) / sizeof(kCurve[0])) - 1;
  if (mv <= kCurve[last].mv)
    return 0;

  for (int i = 0; i < last; ++i)
  {
    if (mv <= kCurve[i].mv && mv >= kCurve[i + 1].mv)
    {
      const int x0 = kCurve[i].mv, y0 = kCurve[i].pct;
      const int x1 = kCurve[i + 1].mv, y1 = kCurve[i + 1].pct;
      return y0 + (mv - x0) * (y1 - y0) / (x1 - x0);
    }
  }
  return 0;
}

static int median5(int a, int b, int c, int d, int e)
{
  int v[5] = {a, b, c, d, e};
  for (int i = 0; i < 5; ++i)
    for (int j = i + 1; j < 5; ++j)
      if (v[j] < v[i])
      {
        int t = v[i];
        v[i] = v[j];
        v[j] = t;
      }
  return v[2];
}

static uint32_t s_lastManualScreenToggleMs = 0;

// Canvas framebuffer
M5Canvas spr(&M5Cardputer.Display);

void toggleScreenPower() { SET_SCREEN_POWER(!isScreenOn()); }

bool isScreenOn() { return g_app.screenOn; }

uint32_t screenPowerLastManualToggleMs() { return s_lastManualScreenToggleMs; }

void markScreenPowerManualToggle(uint32_t now) { s_lastManualScreenToggleMs = now; }

static inline uint8_t clampU8(int v)
{
  if (v < 0)
    return 0;
  if (v > 255)
    return 255;
  return (uint8_t)v;
}

void initBacklight()
{
  // Cardputer backlight is handled by M5GFX; no pin init needed.
}

static void applyBacklightRaw(uint8_t level)
{
  static uint8_t s_lastLevel = 255;

  if (level == s_lastLevel)
    return;

  s_lastLevel = level;

  if (Serial && Serial.availableForWrite() >= 96)
  {
    Serial.printf("[BL] setBacklight(%u) screenOn=%d pulse=%d ui=%d\n",
                  (unsigned)level,
                  isScreenOn() ? 1 : 0,
                  g_backlightPulseActive ? 1 : 0,
                  (int)g_app.uiState);
  }

  M5Cardputer.Display.setBrightness(level);
}

void forceBacklightDuringFade(uint8_t level)
{
  applyBacklightRaw(level);
}

void setBacklight(uint8_t level)
{
  // During the pet intro fade, block outside attempts to brighten the screen.
  // The fade code itself must use forceBacklightDuringFade().
  if (g_app.uiState == UIState::PET_SCREEN && isPetScreenIntroFadeActive())
  {
    if (level > 0)
      return;
  }

  applyBacklightRaw(level);
}

void setScreenPower(bool on)
{
  const bool wasOn = g_app.screenOn;
  if (wasOn == on)
  {
    return;
  }

  // Update the single source of truth FIRST.
  g_app.screenOn = on;

  if (!on)
  {
    // --- going OFF ---
    g_backlightPulseActive = false;

    M5Cardputer.Display.fillScreen(TFT_BLACK);
    SET_BACKLIGHT(0);
    delay(10);
    M5Cardputer.Display.sleep();

    screenJustWentOff = true;
    return;
  }

  // --- going ON / waking ---
  M5Cardputer.Display.wakeup();

  // Apply brightness immediately unless the pet-screen intro fade is about to own
  // the backlight ramp.
  if (!(g_app.uiState == UIState::PET_SCREEN && (g_app.petScreenIntroFadePending || isPetScreenIntroFadeActive())))
  {
    int b = brightnessValues[brightnessLevel];
    if (b < 0)
      b = 0;
    if (b > 255)
      b = 255;
    setBacklight((uint16_t)b);
  }

  // Force redraw after wake
  requestUIRedraw();

  // Clear edge inputs so a lingering press doesn't immediately re-toggle
  inputForceClear();
  clearInputLatch();

  // Rebase animation timers so long screen-off intervals don't stall animation.
  animNotifyScreenWake();
  sleepBgNotifyScreenWake();
}

static uint8_t effectiveBrightnessLevel() { return s_batteryDimActive ? s_batteryDimLevel : brightnessLevel; }

static void applyEffectiveBrightness()
{
  if (!isScreenOn())
    return;

  const uint8_t level = effectiveBrightnessLevel();
  applyBrightnessLevel(level);
}

static void setBatteryDimActive(bool active, uint8_t dimLevel = 0)
{
  s_batteryDimActive = active;
  s_batteryDimLevel = dimLevel;
  applyEffectiveBrightness();
}

void displayRememberUserBrightness(uint8_t level) { s_lastUserBrightnessLevel = level; }

uint8_t displayGetUserBrightnessLevel() { return s_lastUserBrightnessLevel; }

void setScreenPowerTagged(bool on, const char *file, int line)
{
  if (Serial && Serial.availableForWrite() >= 120)
  {
    Serial.printf("[PWR] setScreenPower(%d) @ %s:%d t=%lu ui=%d tab=%d\n", (int)on, file, line, (unsigned long)millis(),
                  (int)g_app.uiState, (int)g_app.currentTab);
  }
  setScreenPower(on); // IMPORTANT: call the real function, not the macro
}

static inline bool bootCanPrint(size_t need) { return g_debugEnabled && (Serial.availableForWrite() >= (int)need); }

void displayInit()
{
  static bool s_inited = false;
  if (s_inited)
    return;
  s_inited = true;

  if (bootCanPrint(60))
    Serial.println("[displayInit] Cardputer backend start");

  int w = M5Cardputer.Display.width();
  int h = M5Cardputer.Display.height();

  screenW = w;
  screenH = h;

  spr.createSprite(w, h);
  spr.setTextScroll(false);

  spr.fillScreen(TFT_BLACK);
  spr.pushSprite(0, 0);

  // FORCE a real power-on pass on boot (state may say "on" but hardware isn't)
  g_app.screenOn = false;
  SET_SCREEN_POWER(true);
  requestUIRedraw();

  if (bootCanPrint(80))
    Serial.printf("[displayInit] Cardputer backend done (%dx%d)\n", w, h);
}

void backlightPulseBegin(uint8_t level)
{
  (void)level;
  return;
}

void backlightPulseEnd()
{
  return;
}

// ============================================================================
// Battery polling (Cardputer) - robust (voltage -> percent)
// ============================================================================

// --- Battery filter state ----------------------------------------------------
static bool s_batLogPrintedOnce = false;
static int s_lastLoggedPct = -999;
static int s_lastLoggedMv = -999;
static bool s_lastLoggedUsb = false;

static int s_hist[5] = {0};
static uint8_t s_histN = 0;
static uint8_t s_histIdx = 0;
static float s_mvEma = 0.0f;

// USB heuristic (fast, non-sticky)
static int s_prevMv = 0;
static bool s_havePrevMv = false;
static int s_attachEvidence = 0;
static int s_detachEvidence = 0;
static uint32_t s_lastUsbFlipMs = 0;

void updateBattery()
{
  static uint32_t nextMs = 0;
  const uint32_t now = millis();
  if (now < nextMs)
    return;
  nextMs = now + 250; // 4 Hz

  const int rawMv = (int)M5Cardputer.Power.getBatteryVoltage();
  const bool mvValid = (rawMv >= 2500 && rawMv <= 5000);
  if (!mvValid)
    return;

  // --- smoothing pipeline ----------------------------------------------------
  s_hist[s_histIdx] = rawMv;
  s_histIdx = (s_histIdx + 1) % 5;
  if (s_histN < 5)
    s_histN++;

  int mvMed = rawMv;
  if (s_histN == 5)
  {
    mvMed = median5(s_hist[0], s_hist[1], s_hist[2], s_hist[3], s_hist[4]);
  }

  if (s_mvEma <= 0.0f)
    s_mvEma = (float)mvMed;
  s_mvEma = s_mvEma * 0.75f + (float)mvMed * 0.25f;

  const int mvFilt = (int)(s_mvEma + 0.5f);

  // --- publish voltage -------------------------------------------------------
  batteryVoltageMv = mvFilt;

  // --- percent from curve ----------------------------------------------------
  const int pct = voltageToPercent(mvFilt);

  // --- USB / external power heuristic ---------------------------------------
  if (!s_havePrevMv)
  {
    s_prevMv = mvFilt;
    s_havePrevMv = true;
  }

  const int dv = mvFilt - s_prevMv;
  s_prevMv = mvFilt;

  bool usb = usbPowered;

  const bool nearTop = (mvFilt >= 4140);
  const bool risingHard = (dv >= 6);
  const bool risingSoft = (dv >= 3);
  const bool fallingHard = (dv <= -8);
  const bool fallingSoft = (dv <= -4);

  if (nearTop || risingHard)
  {
    if (s_attachEvidence < 8)
      s_attachEvidence += 2;
  }
  else if (risingSoft)
  {
    if (s_attachEvidence < 8)
      s_attachEvidence += 1;
  }
  else if (s_attachEvidence > 0)
  {
    s_attachEvidence -= 1;
  }

  if (fallingHard)
  {
    if (s_detachEvidence < 8)
      s_detachEvidence += 2;
  }
  else if (fallingSoft)
  {
    if (s_detachEvidence < 8)
      s_detachEvidence += 1;
  }
  else if (s_detachEvidence > 0)
  {
    s_detachEvidence -= 1;
  }

  if (!usb)
  {
    if (s_attachEvidence >= 4)
    {
      usb = true;
      s_lastUsbFlipMs = now;
      s_attachEvidence = 0;
      s_detachEvidence = 0;
    }
  }
  else
  {
    const bool holdElapsed = (now - s_lastUsbFlipMs) >= 1500;
    if (holdElapsed && s_detachEvidence >= 4 && !nearTop)
    {
      usb = false;
      s_lastUsbFlipMs = now;
      s_attachEvidence = 0;
      s_detachEvidence = 0;
    }
  }

  // boot-time guess so first reading is sane
  if (!s_batLogPrintedOnce)
  {
    usb = (mvFilt >= 4140);
    usbPowered = usb;

    if (Serial.availableForWrite() >= 160)
    {
      Serial.printf("[BAT] init raw=%d med=%d filt=%d dv=%d pct=%d usb=%d attach=%d detach=%d\n", rawMv, mvMed, mvFilt,
                    dv, pct, (int)usb, s_attachEvidence, s_detachEvidence);
    }

    s_batLogPrintedOnce = true;
    s_lastLoggedPct = pct;
    s_lastLoggedMv = mvFilt;
    s_lastLoggedUsb = usb;
  }

  // --- commit changes / redraw ----------------------------------------------
  static int lastPct = -999;
  static int lastMv = -999;
  static bool lastUsb = false;

  const bool changed = (pct != lastPct) || (mvFilt != lastMv) || (usb != lastUsb);
  if (changed)
  {
    batteryPercent = pct;
    usbPowered = usb;

    requestUIRedraw();

    lastPct = pct;
    lastMv = mvFilt;
    lastUsb = usb;
  }

  // --- quiet logging ---------------------------------------------------------
  const bool usbChanged = (usb != s_lastLoggedUsb);
  const int pctDelta = pct - s_lastLoggedPct;
  const int mvDelta = mvFilt - s_lastLoggedMv;

  const bool pctChangedOnBattery = (!usb && (pctDelta >= 1 || pctDelta <= -1));
  const bool pctChangedOnUsb = (usb && pct < 95 && (pctDelta >= 2 || pctDelta <= -2));
  const bool meaningfulMvChange = (mvDelta >= 12 || mvDelta <= -12);

  bool shouldLog = false;
  const char *reason = "";

  if (usbChanged)
  {
    shouldLog = true;
    reason = "usb";
  }
  else if (pctChangedOnBattery)
  {
    shouldLog = true;
    reason = "pct";
  }
  else if (pctChangedOnUsb)
  {
    shouldLog = true;
    reason = "pct";
  }
  else if (meaningfulMvChange)
  {
    shouldLog = true;
    reason = "mv";
  }

  if (shouldLog)
  {
    if (Serial.availableForWrite() >= 160)
    {
      Serial.printf("[BAT] change raw=%d med=%d filt=%d dv=%d pct=%d usb=%d attach=%d detach=%d reason=%s\n", rawMv,
                    mvMed, mvFilt, dv, pct, (int)usb, s_attachEvidence, s_detachEvidence, reason);
    }

    s_lastLoggedPct = pct;
    s_lastLoggedMv = mvFilt;
    s_lastLoggedUsb = usb;
  }
}

void batteryProtectionTick(uint32_t now)
{
  static uint32_t lowSinceMs = 0;
  static uint32_t critSinceMs = 0;

  const int mv = batteryVoltageMv;

  const bool lowNow = (!usbPowered && mv > 0 && mv <= 3550);
  const bool critNow = (!usbPowered && mv > 0 && mv <= 3475);

  if (lowNow)
  {
    if (!lowSinceMs)
      lowSinceMs = now;
  }
  else
  {
    lowSinceMs = 0;
  }

  if (critNow)
  {
    if (!critSinceMs)
      critSinceMs = now;
  }
  else
  {
    critSinceMs = 0;
  }

  batteryLow = lowSinceMs && (now - lowSinceMs >= 5000);
  batteryCritical = critSinceMs && (now - critSinceMs >= 2000);

  if (batteryLow)
  {
    setBatteryDimActive(true, 0);

    if (!s_batteryWifiForcedOff)
    {
      applyWifiPower(false);
      s_batteryWifiForcedOff = true;
    }
  }

  if (batteryCritical)
  {
    emergencyBatteryShutdown();
  }
  if (!batteryLow && (now - lowSinceMs > 2000))
  {
    // Restore brightness
    setBatteryDimActive(false);

    // Restore Wi-Fi ONLY if we turned it off
    if (s_batteryWifiForcedOff)
    {
      applyWifiPower(true);
      s_batteryWifiForcedOff = false;
    }
  }
}

void setBacklightTagged(uint8_t level, const char *file, int line)
{
  static uint8_t s_lastLevel = 255; // impossible value so first call always applies

  // If we’re already at this brightness, do nothing (prevents spam + extra work).
  if (level == s_lastLevel)
    return;
  s_lastLevel = level;

  if (Serial && Serial.availableForWrite() >= 120)
  {
    Serial.printf("[BL] setBacklight(%u) @ %s:%d t=%lu ui=%d tab=%d\n", (unsigned)level, file, line,
                  (unsigned long)millis(), (int)g_app.uiState, (int)g_app.currentTab);
  }

  setBacklight(level);
}
