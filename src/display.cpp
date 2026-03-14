#include "display.h"

#include <Arduino.h>
#include "M5Cardputer.h"
#include "feed.h"          
#include "pet.h"           
#include "input.h"         
#include "sound.h"         
#include "graphics.h"      
#include "ui_runtime.h"    
#include "ui_menu_state.h" 
#include "display_state.h"
#include "debug_state.h"
#include "brightness_state.h"
#include "app_state.h"           
#include "app_state.h"
#include "system_status_state.h" 
#include "display_dims_state.h" 
#include "anim_engine.h"

static uint32_t s_lastManualScreenToggleMs = 0;

// Canvas framebuffer
M5Canvas spr(&M5Cardputer.Display);

void toggleScreenPower() {
  SET_SCREEN_POWER(!isScreenOn());
}

bool isScreenOn() {
  return g_app.screenOn;
}

uint32_t screenPowerLastManualToggleMs() {
  return s_lastManualScreenToggleMs;
}

void markScreenPowerManualToggle(uint32_t now) {
  s_lastManualScreenToggleMs = now;
}

static inline uint8_t clampU8(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return (uint8_t)v;
}

void initBacklight() {
  // Cardputer backlight is handled by M5GFX; no pin init needed.
}

void setBacklight(uint8_t level) {
  M5Cardputer.Display.setBrightness(level);
}

void setScreenPower(bool on) {
  const bool wasOn = g_app.screenOn;
  if (wasOn == on) {
    return;
  }

  // Update the single source of truth FIRST.
  g_app.screenOn = on;

  if (!on) {
    // --- going OFF ---
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    SET_BACKLIGHT(0);
    M5Cardputer.Display.sleep();
    screenJustWentOff = true;
    return;
  }

  // --- going ON / waking ---
  M5Cardputer.Display.wakeup();

  // Apply brightness immediately
  int b = brightnessValues[brightnessLevel];
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  setBacklight((uint16_t)b);

  // Force redraw after wake
  requestUIRedraw();

  // Clear edge inputs so a lingering press doesn't immediately re-toggle
  inputForceClear();
  clearInputLatch();

  // Rebase animation timers so long screen-off intervals don't stall animation.
  animNotifyScreenWake();
  sleepBgNotifyScreenWake();
}

void setScreenPowerTagged(bool on, const char* file, int line) {
  if (Serial && Serial.availableForWrite() >= 120) {
    Serial.printf("[PWR] setScreenPower(%d) @ %s:%d t=%lu ui=%d tab=%d\n",
                  (int)on, file, line, (unsigned long)millis(),
                  (int)g_app.uiState, (int)g_app.currentTab);
  }
  setScreenPower(on); // IMPORTANT: call the real function, not the macro
}

static inline bool bootCanPrint(size_t need) {
  return g_debugEnabled && (Serial.availableForWrite() >= (int)need);
}

void displayInit() {
  static bool s_inited = false;
  if (s_inited) return;
  s_inited = true;

  if (bootCanPrint(60)) Serial.println("[displayInit] Cardputer backend start");

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

  if (bootCanPrint(80)) Serial.printf("[displayInit] Cardputer backend done (%dx%d)\n", w, h);
}

static bool g_backlightPulseActive = false;

void backlightPulseBegin(uint8_t level) {
  // Pulse only when screen is OFF (rail-power hack for LED)
  if (isScreenOn()) return;

  if (g_backlightPulseActive) return;
  g_backlightPulseActive = true;

  M5Cardputer.Display.wakeup();
  setBacklight(level);
}

void backlightPulseEnd() {
  if (!g_backlightPulseActive) return;
  g_backlightPulseActive = false;

// Only end pulse / sleep LCD when screen is OFF
if (isScreenOn()) return;

setBacklight(0);
M5Cardputer.Display.sleep();
}

// ============================================================================
// Battery polling (Cardputer) - robust (voltage -> percent)
// ============================================================================
void updateBattery() {
  static uint32_t nextMs = 0;
  const uint32_t now = millis();
  if (now < nextMs) return;
  nextMs = now + 1000; // 1 Hz

  int rawPct = (int)M5Cardputer.Power.getBatteryLevel();
  int rawMv  = (int)M5Cardputer.Power.getBatteryVoltage();

  const bool mvValid = (rawMv >= 2500 && rawMv <= 5000);

  int pct = batteryPercent;

  if (rawPct > 0 && rawPct <= 100) {
    pct = rawPct;
  } else if (mvValid) {
    int mapped = (int)((rawMv - 3300) * 100L / (4200 - 3300));
    if (mapped < 0) mapped = 0;
    if (mapped > 100) mapped = 100;
    pct = mapped;
  } else {
    if (pct < 0 || pct > 100) pct = -1;
  }

  if (pct > 100) pct = 100;

  // ---------------------------------------------------------------------------
  // USB heuristic via voltage trend (smoothed + debounced)
  // ---------------------------------------------------------------------------
  // Note: Cardputer's power API doesn't expose a stable "USB present" flag,
  // so we infer it from battery voltage behavior. Raw voltage can be noisy,
  // so we smooth it and require sustained evidence before toggling the icon.

  // Exponential moving average to tame 1 Hz noise.
  static float mvEma = 0.0f;
  int mvFilt = rawMv;
  if (mvValid) {
    if (mvEma <= 0.0f) mvEma = (float)rawMv;
    mvEma = mvEma * 0.85f + (float)rawMv * 0.15f;   // ~6–7s time constant at 1 Hz
    mvFilt = (int)(mvEma + 0.5f);
  }

  // 10-sample ring buffer (10 seconds at 1 Hz)
  static int mvHist[10] = {0};
  static uint8_t mvHistN = 0;
  static uint8_t mvHistIdx = 0;

  static bool usb = false;
  static uint32_t usbHoldUntilMs = 0;
  static uint32_t usbLastFlipMs  = 0;

  static uint8_t riseCount = 0;
  static uint8_t fallCount = 0;

  const uint32_t MIN_ON_MS  = 15000;   // don't drop icon immediately after it appears
  const uint32_t MIN_OFF_MS = 5000;    // don't re-assert instantly after clearing

  if (mvValid) {
    mvHist[mvHistIdx] = mvFilt;
    mvHistIdx = (uint8_t)((mvHistIdx + 1) % 10);
    if (mvHistN < 10) mvHistN++;

    if (mvHistN >= 10) {
      const int oldest = mvHist[mvHistIdx];
      const int newest = mvFilt;
      const int delta  = newest - oldest; // mV over ~10s

      // Thresholds (with hysteresis + debounce)
      const int RISE_HARD_MV = +25;  // immediate "USB present"
      const int RISE_SOFT_MV = +15;  // count-up evidence

      const int FALL_HARD_MV = -70;  // immediate "USB absent" evidence
      const int FALL_SOFT_MV = -45;  // count-up evidence

      const uint8_t NEED_COUNT = 3;  // 3 consecutive checks

      // --- detect insert / keep ---
      if (!usb) {
        // avoid flicker on quick re-assert after a clear
        if (now - usbLastFlipMs < MIN_OFF_MS) {
          riseCount = 0;
        } else if (delta >= RISE_HARD_MV) {
          usb = true;
          usbHoldUntilMs = now + 180000; // 3 minutes
          usbLastFlipMs = now;
          riseCount = 0;
          fallCount = 0;
        } else {
          if (delta >= RISE_SOFT_MV) riseCount++;
          else riseCount = 0;

          if (riseCount >= NEED_COUNT) {
            usb = true;
            usbHoldUntilMs = now + 180000;
            usbLastFlipMs = now;
            riseCount = 0;
            fallCount = 0;
          }
        }
      } else {
        // usb currently true: extend hold while stable/slightly rising
        const int KEEP_DROP_MV = -10;
        if (delta > KEEP_DROP_MV) {
          usbHoldUntilMs = now + 180000;
          fallCount = 0;
        } else {
          // consider clearing only after minimum on-time
          const bool minOnSatisfied = (now - usbLastFlipMs) >= MIN_ON_MS;

          if (delta <= FALL_HARD_MV && minOnSatisfied) {
            usb = false;
            usbLastFlipMs = now;
            riseCount = 0;
            fallCount = 0;
          } else {
            if (delta <= FALL_SOFT_MV) fallCount++;
            else fallCount = 0;

            // sustained fall + hold expired => clear
            if (minOnSatisfied && now >= usbHoldUntilMs && fallCount >= NEED_COUNT) {
              usb = false;
              usbLastFlipMs = now;
              riseCount = 0;
              fallCount = 0;
            }
          }
        }
      }
    }
  } else {
    // invalid voltage -> don't assert usb; let it expire
    if (usb && now >= usbHoldUntilMs) {
      usb = false;
      usbLastFlipMs = now;
      riseCount = 0;
      fallCount = 0;
    }
  }

  // ---- commit changes / redraw ----
  static int  lastPct = -999;
  static int  lastMv  = -999;
  static bool lastUsb = false;

  int mvForState = mvValid ? rawMv : lastMv;

  const bool changed = (pct != lastPct) || (mvForState != lastMv) || (usb != lastUsb);
  if (changed) {
    batteryPercent = pct;
    usbPowered     = usb;

    requestUIRedraw();

    lastPct = pct;
    lastMv  = mvForState;
    lastUsb = usb;
  }

  // ---- periodic diag ----
  static uint32_t nextDiag = 0;
  if (now >= nextDiag) {
    nextDiag = now + 5000;
    if (Serial.availableForWrite() >= 110) {
      int d = 0;
      if (mvHistN >= 10) {
        const int oldest = mvHist[mvHistIdx];
        d = rawMv - oldest;
      }
      //Serial.printf("[BAT] rawPct=%d rawMv=%d pct=%d usb=%d mv10sDelta=%d\n",
      //              rawPct, rawMv, batteryPercent, (int)usbPowered, d);
    }
  }
}

void setBacklightTagged(uint8_t level, const char* file, int line) {
  static uint8_t s_lastLevel = 255; // impossible value so first call always applies

  // If we’re already at this brightness, do nothing (prevents spam + extra work).
  if (level == s_lastLevel) return;
  s_lastLevel = level;

  if (Serial && Serial.availableForWrite() >= 120) {
    Serial.printf("[BL] setBacklight(%u) @ %s:%d t=%lu ui=%d tab=%d\n",
                  (unsigned)level, file, line, (unsigned long)millis(),
                  (int)g_app.uiState, (int)g_app.currentTab);
  }

  setBacklight(level);
}
