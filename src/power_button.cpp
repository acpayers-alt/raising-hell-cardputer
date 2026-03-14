#include "power_button.h"

#include "display.h" // isScreenOn(), toggleScreenPower(), requestUIRedraw()
#include "flow_power_menu.h"
#include "input.h"                // inputForceClear(), clearInputLatch()
#include "input_activity_state.h" // setLastInputActivityMs(now)
#include "menu_actions.h"         // openPowerMenuFromHere(now)
#include "ui_invalidate.h"
#include <Arduino.h>
#include "motion.h"

static constexpr int GO_BTN_PIN = 0;
static constexpr bool GO_ACTIVE_LOW = true;

static volatile bool g_goEdgeFlag = false;
static volatile bool g_goEdgeLevel = false;
static volatile uint32_t g_goEdgeMs = 0;

static void IRAM_ATTR onGoEdge()
{
  const bool raw = GO_ACTIVE_LOW ? (digitalRead(GO_BTN_PIN) == LOW) : (digitalRead(GO_BTN_PIN) == HIGH);
  g_goEdgeLevel = raw;
  g_goEdgeMs = millis();
  g_goEdgeFlag = true;
}

// --- GO button (Cardputer spec: GPIO0) ---
static inline bool readGoRaw()
{
  return GO_ACTIVE_LOW ? (digitalRead(GO_BTN_PIN) == LOW) : (digitalRead(GO_BTN_PIN) == HIGH);
}

void powerButtonInit()
{
  pinMode(GO_BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(GO_BTN_PIN), onGoEdge, CHANGE);
}

void powerButtonTick(uint32_t now)
{
  const uint32_t kEdgeGuardMs = 4;
  const uint32_t kMinPressMs = 20;
  const uint32_t kLongPressMs = 650;
  const uint32_t kToggleLockoutMs = 90;

  static bool s_inited = false;
  static bool s_lastRaw = false;
  static uint32_t s_lastEdgeMs = 0;

  static bool s_pressLatched = false;
  static uint32_t s_pressStartMs = 0;
  static bool s_shortArmed = false;
  static bool s_longFired = false;

  static uint32_t s_lastToggleMs = 0;

  const bool raw = readGoRaw();

  // --- consume interrupt edge if one happened ---
  bool irqEdge = false;
  bool irqLevel = raw;
  uint32_t irqMs = now;

  noInterrupts();
  if (g_goEdgeFlag)
  {
    irqEdge = true;
    irqLevel = g_goEdgeLevel;
    irqMs = g_goEdgeMs;
    g_goEdgeFlag = false;
  }
  interrupts();

  if (irqEdge)
  {
    s_lastRaw = irqLevel;
    s_lastEdgeMs = irqMs;
  }

  if (!s_inited)
  {
    s_inited = true;
    s_lastRaw = raw;
    s_lastEdgeMs = now;
    return;
  }

  if (!irqEdge && raw != s_lastRaw)
  {
    s_lastRaw = raw;
    s_lastEdgeMs = now;
  }

  // ----------------------------
  // Latch press immediately
  // ----------------------------
  if (raw && !s_pressLatched)
  {
    s_pressLatched = true;
    s_pressStartMs = now;
    s_shortArmed = false;
    s_longFired = false;
  }

  if (s_pressLatched && raw)
  {
    const uint32_t heldMs = now - s_pressStartMs;

    if (heldMs >= kMinPressMs)
      s_shortArmed = true;

    if (!s_longFired && heldMs >= kLongPressMs)
    {
      s_longFired = true;
      s_shortArmed = false;

      openPowerMenuFromHere(now);
      return;
    }
  }

  // ----------------------------
  // Fire short press on FIRST release
  // ----------------------------
  if (!raw && s_pressLatched)
  {
    if (now - s_lastEdgeMs < kEdgeGuardMs)
      return;

    s_pressLatched = false;

    if (s_longFired)
      return;

    if (!s_shortArmed)
      return;

    if (now - s_lastToggleMs < kToggleLockoutMs)
      return;

    s_lastToggleMs = now;
    s_shortArmed = false;

    const bool wasOn = isScreenOn();

    markScreenPowerManualToggle(now);
    toggleScreenPower();

    if (!isScreenOn())
  motionResetShakeDetector(1200);
  
    inputForceClear();
    clearInputLatch();

    if (!wasOn && isScreenOn())
    {
      setLastInputActivityMs(now);
      requestUIRedraw();
    }
  }
}