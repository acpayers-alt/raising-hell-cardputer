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
#include "time_persist.h"

// --- Debug --------------------------------------------------------------------
#include "debug_state.h"
#include "support_logging_state.h"

// end of includes

static bool g_backlightPulseActive = false;

static uint8_t s_lastUserBrightnessLevel = 1;

// --- Battery smoothing + curve helpers --------------------------------------

static int sanitizeBatteryPercent(int pct)
{
<<<<<<< HEAD
  if (pct < 0)
    return -1;
  if (pct > 100)
=======
  struct Point
  {
    int mv;
    int pct;
  };

  // Cardputer ADV reports battery voltage lower than the original Cardputer.
  // Field data shows a full ADV pack can report around ~4.04V and appear
  // roughly 20% low with the old curve. Keep 0% aligned near the real boot/
  // protection floor so the UI does not sit at 0% for hours.
  static const Point kCurve[] = {
      {4050, 100}, {4025, 96}, {4000, 92}, {3975, 88}, {3950, 83}, {3925, 78}, {3900, 73}, {3875, 67}, {3850, 61},
      {3825, 55},  {3800, 49}, {3775, 43}, {3750, 37}, {3725, 31}, {3700, 26}, {3675, 21}, {3650, 17}, {3625, 13},
      {3600, 10},  {3550, 7},  {3500, 5},  {3450, 3},  {3400, 2},  {3350, 1},  {3300, 0},
  };

  if (mv >= kCurve[0].mv)
>>>>>>> dev/3.0.0
    return 100;
  return pct;
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

bool isBacklightPulseActive() { return g_backlightPulseActive; }

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
  static uint8_t s_lastLevel = 255; // impossible value so first call always applies
  static constexpr bool kLogBacklightChanges = false;

  const uint8_t finalLevel = clampU8((int)level);

  if (finalLevel == s_lastLevel)
    return;

  s_lastLevel = finalLevel;

  if (kLogBacklightChanges && Serial && Serial.availableForWrite() >= 120)
  {
    Serial.printf("[POWER][BL] req=%u final=%u screenOn=%d pulse=%d ui=%d\n", (unsigned)level, (unsigned)finalLevel,
                  isScreenOn() ? 1 : 0, g_backlightPulseActive ? 1 : 0, (int)g_app.uiState);
  }

  M5Cardputer.Display.setBrightness(finalLevel);
}

void forceBacklightDuringFade(uint8_t level) { applyBacklightRaw(level); }

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
    SET_BACKLIGHT((uint8_t)b);
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

void displayRememberUserBrightness(uint8_t level) { s_lastUserBrightnessLevel = level; }

uint8_t displayGetUserBrightnessLevel() { return s_lastUserBrightnessLevel; }

void setScreenPowerTagged(bool on, const char *file, int line)
{
  static constexpr bool kLogScreenPower = false;
  static int s_last = -1;

  if (kLogScreenPower && s_last != (int)on)
  {
    Serial.printf("[POWER] screen=%d ui=%d tab=%d\n", (int)on, (int)g_app.uiState, (int)g_app.currentTab);
    s_last = (int)on;
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

static bool s_backlightPulseWasScreenOn = false;
static int s_backlightPulsePrevLevel = -1;

static bool s_backlightRailPulseActive = false;

void backlightRailPulseBegin(uint8_t level)
{
  if (s_backlightRailPulseActive)
    return;

  s_backlightRailPulseActive = true;
  g_backlightPulseActive = true;

  // Hardware-only wake for the shared LED/display rail.
  // Do not change g_app.screenOn and do not request UI redraw.
  M5Cardputer.Display.wakeup();
  applyBacklightRaw(clampU8((int)level));
}

void backlightRailPulseShowColor(uint8_t r, uint8_t g, uint8_t b)
{
  if (!s_backlightRailPulseActive)
    return;

  const uint16_t color = M5Cardputer.Display.color565(r, g, b);
  M5Cardputer.Display.fillScreen(color);
}

void backlightRailPulseEnd()
{
  if (!s_backlightRailPulseActive)
    return;

  applyBacklightRaw(0);
  delay(10);
  M5Cardputer.Display.sleep();
  s_backlightRailPulseActive = false;
  g_backlightPulseActive = false;
}

void backlightRailPulseAdoptScreenOn()
{
  if (!s_backlightRailPulseActive)
    return;

  // A real logical screen wake happened while the rail pulse was active.
  // Do not sleep the display or force brightness to 0; just clear pulse ownership.
  s_backlightRailPulseActive = false;
  g_backlightPulseActive = false;
}

void backlightPulseBegin(uint8_t level)
{
  s_backlightPulseWasScreenOn = isScreenOn();
  g_backlightPulseActive = true;

  // Make sure the shared rail is actually powered for LED alerts.
  if (!s_backlightPulseWasScreenOn)
    SET_SCREEN_POWER(true);

  SET_BACKLIGHT(level);
}

void backlightPulseEnd()
{
  g_backlightPulseActive = false;

  if (s_backlightPulseWasScreenOn)
  {
    // Restore the user's configured brightness level.
    SET_BACKLIGHT((uint8_t)brightnessValues[brightnessLevel]);
  }
  else
  {
    // This pulse only existed to power the LED/shared rail while the screen was off.
    SET_BACKLIGHT(0);
    SET_SCREEN_POWER(false);
  }

  s_backlightPulseWasScreenOn = false;
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

bool displayUsbPowerLikely()
{
  const int mv = batteryVoltageMv;

  // Only trust committed USB state, or a clearly-high battery voltage.
  // Do NOT treat one weak piece of attach evidence as USB-present.
  if (usbPowered)
    return true;

  if (mv >= 4140)
    return true;

  return false;
}

void updateBattery()
{
  static uint32_t nextMs = 0;
  const uint32_t now = millis();
  if (now < nextMs)
    return;
  nextMs = now + 250; // 4 Hz

  const int rawMv = (int)M5Cardputer.Power.getBatteryVoltage();
  const int rawPct = sanitizeBatteryPercent((int)M5Cardputer.Power.getBatteryLevel());

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

  // --- percent from platform API --------------------------------------------
  // Launcher/Bruce use the M5 power path for presentation. Keep our filtered
  // voltage for diagnostics/protection, but do not hand-map display percent.
  const int pct = (rawPct >= 0) ? rawPct : batteryPercent;

  // --- USB / external power heuristic ---------------------------------------
  if (!s_havePrevMv)
  {
    s_prevMv = mvFilt;
    s_havePrevMv = true;
  }

  const int dv = mvFilt - s_prevMv;
  s_prevMv = mvFilt;

  bool usb = usbPowered || (mvFilt >= 4140);

  const bool nearTop = (mvFilt >= 4140);
  const bool lowBand = (mvFilt > 0 && mvFilt <= 3800);
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

  const int attachThreshold = 4;

  if (!usb)
  {
    if (s_attachEvidence >= attachThreshold)
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

  // First pass: keep the full heuristic result. Do not clobber it back down to
  // only "mv >= 4140", because that breaks low-battery-on-USB cases.
  if (!s_batLogPrintedOnce)
  {
    usbPowered = usb;

    if (Serial.availableForWrite() >= 160)
    {
      if (supportLoggingEnabled())
      {
        Serial.printf("[BAT] init raw=%d med=%d filt=%d dv=%d pct=%d usb=%d attach=%d detach=%d\n", rawMv, mvMed,
                      (int)s_mvEma, dv, pct, (int)usb, s_attachEvidence, s_detachEvidence);
      }
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

  const bool pctValid = (pct >= 0 && pct <= 100);
  const bool changed = (pctValid && pct != lastPct) || (mvFilt != lastMv) || (usb != lastUsb);

  if (changed)
  {
    if (pctValid)
      batteryPercent = pct;

    usbPowered = usb;

    requestUIRedraw();

    if (pctValid)
      lastPct = pct;

    lastMv = mvFilt;
    lastUsb = usb;
  }

  // --- quiet logging ---------------------------------------------------------
  const bool usbLogChanged = (usb != s_lastLoggedUsb);
  const int pctDelta = pct - s_lastLoggedPct;
  const int mvDelta = mvFilt - s_lastLoggedMv;

  const bool pctChangedOnBattery = (!usb && (pctDelta >= 2 || pctDelta <= -2));
  const bool pctChangedOnUsb = (usb && pct < 95 && (pctDelta >= 3 || pctDelta <= -3));
  const bool meaningfulMvChange = (mvDelta >= 25 || mvDelta <= -25);

  bool shouldLog = false;
  const char *reason = "";

  if (usbLogChanged)
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
  const bool usbLikely = displayUsbPowerLikely();

  // On external power, battery protection stands down.
  if (usbLikely)
  {
    lowSinceMs = 0;
    critSinceMs = 0;
    batteryLow = false;
    batteryCritical = false;
    return;
  }

  const bool lowNow = (mv > 0 && mv <= 3500);
  const bool critNow = (mv > 0 && mv <= 3250);

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

  // No low-battery Wi-Fi shutoff. Just track battery state and only hard-stop on critical.
  if (batteryCritical)
  {
    emergencyBatteryShutdown();
  }
}

void setBacklightTagged(uint8_t level, const char *file, int line)
{
  static uint8_t s_lastLevel = 255; // impossible value so first call always applies

  if (level == s_lastLevel)
    return;
  s_lastLevel = level;

  if (Serial && Serial.availableForWrite() >= 160)
  {
    if (supportLoggingEnabled())
    {
      Serial.printf("[BL SRC] req=%u @ %s:%d t=%lu ui=%d tab=%d\n", (unsigned)level, file ? file : "?", line,
                    (unsigned long)millis(), (int)g_app.uiState, (int)g_app.currentTab);
    }
  }
  setBacklight(level);
}