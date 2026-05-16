#ifndef RH_INPUT_H
#define RH_INPUT_H

#include <Arduino.h>

// ----------------------------------------------------------------------------
// Text capture (console / input fields) — disables most global hotkeys
// ----------------------------------------------------------------------------
extern bool g_textCaptureMode;

void inputSetTextCapture(bool on);

// Force-clear any queued/latched input state (implementation in input_cardputer.cpp)
void inputForceClear();

// Clears "once" edge latches + encoder delta + keyboard queue (soft flush)
void clearInputLatch();

// ----------------------------------------------------------------------------
// Special key tokens (avoid colliding with ASCII 0–127)
// IMPORTANT: Do NOT use KEY_* names (M5Cardputer defines them too).
// ----------------------------------------------------------------------------
static constexpr uint8_t RH_KEY_BACKSPACE = 0x80;
static constexpr uint8_t RH_KEY_ENTER = 0x81;
static constexpr uint8_t RH_KEY_FN = 0x82;
static constexpr uint8_t RH_KEY_SHIFT = 0x83;

struct KeyEvent
{
  uint8_t code; // ASCII for normal keys, RH_KEY_* for specials
};

struct InputState
{
  // --------------------------------------------------------------------------
  // LEGACY "Once" EDGE FLAGS (true for one tick)
  // These match what your current code expects: menuOnce, upOnce, etc.
  // --------------------------------------------------------------------------
  uint8_t tabJump = 255;

  bool menuOnce = false;
  bool homeOnce = false;
  bool selectOnce = false;
  bool upOnce = false;
  bool downOnce = false;
  bool leftOnce = false;
  bool rightOnce = false;
  bool encoderPressOnce = false;
  bool photoOnce = false;

  bool screenOnce = false;

  bool escOnce = false;     // Esc toggles Settings (open / return)
  bool hotSettings = false; // optional: direct settings open

  bool consoleOnce = false;
  bool controlsOnce = false;
  bool goShortRelease = false; // short press → screen toggle
  bool goLongHold = false;     // long hold → power menu

  // --------------------------------------------------------------------------
  // HELD LEVEL FLAGS (true while held)
  // --------------------------------------------------------------------------
  bool menuHeld = false;
  bool selectHeld = false;
  bool upHeld = false;
  bool downHeld = false;
  bool leftHeld = false;
  bool rightHeld = false;
  bool encoderHeld = false;

  // UI navigation held state, including keyboard nav clusters.
  // Used by scrollable UI like Care Guide and Console.
  bool uiUpHeld = false;
  bool uiDownHeld = false;

  // --------------------------------------------------------------------------
  // MINI-GAME MAPPED CONTROLS (active only while uiState == UIState::MINI_GAME)
  // While a mini-game is running, we LOCK OUT all other hotkeys (tabs, console,
  // settings, etc.) so no accidental force-quit or UI beeps.
  // --------------------------------------------------------------------------
  bool mgQuitOnce = false;   // ESC (mapped to ` / ~ in Cardputer)
  bool mgSelectOnce = false; // ENTER / G
  bool mgSelectHeld = false;

  bool mgUpOnce = false;
  bool mgDownOnce = false;
  bool mgLeftOnce = false;
  bool mgRightOnce = false;

  bool mgUpHeld = false;
  bool mgDownHeld = false;
  bool mgLeftHeld = false;
  bool mgRightHeld = false;
  bool mgQuitHeld = false; // ESC/back held in mini-game (pause/exit handling)

  bool mgSpaceOnce = false; // SPACE (duck / drop / brake depending on game)
  bool mgSpaceHeld = false;

  // Mini-game action keys (keyboard)
  bool keyEOnce = false;
  bool keyEHeld = false;
  bool keySOnce = false;
  bool keySHeld = false;

  // --------------------------------------------------------------------------
  // Encoder movement (detents since last tick)
  // --------------------------------------------------------------------------
  int encoderDelta = 0;

  // --------------------------------------------------------------------------
  // Keyboard (entire keyboard) — event queue per tick
  // --------------------------------------------------------------------------
  bool kbChanged = false;
  uint8_t kbHeldCount = 0;

  static constexpr uint8_t KBQ = 24;
  KeyEvent kbQueue[KBQ];
  uint8_t kbQHead = 0;
  uint8_t kbQTail = 0;

  inline void clearEdges()
  {
    // one-tick edge pulses
    menuOnce = selectOnce = upOnce = downOnce = leftOnce = rightOnce = false;
    homeOnce = false;
    encoderPressOnce = false;
    screenOnce = false;
    escOnce = false;
    hotSettings = false;
    consoleOnce = false;
    controlsOnce = false;
    photoOnce = false;
    keyEOnce = false;
    keyEHeld = false;
    keySOnce = false;
    keySHeld = false;
    mgQuitOnce = false;
    mgSelectOnce = false;
    mgSelectHeld = false;
    mgUpOnce = mgDownOnce = mgLeftOnce = mgRightOnce = false;
    mgUpHeld = mgDownHeld = mgLeftHeld = mgRightHeld = false;
    mgSpaceOnce = false;
    mgSpaceHeld = false;
    mgUpHeld = mgDownHeld = mgLeftHeld = mgRightHeld = false;
    mgQuitHeld = false;
    uiUpHeld = false;
    uiDownHeld = false;
    goShortRelease = false;
    goLongHold = false;

    // per-tick deltas
    encoderDelta = 0;
    tabJump = 255;

    // keyboard snapshot/queue
    kbChanged = false;
    kbHeldCount = 0;
    kbQHead = kbQTail = 0;
  }

  inline bool kbHasEvent() const { return kbQHead != kbQTail; }

  inline KeyEvent kbPop()
  {
    KeyEvent e{0};
    if (kbQHead == kbQTail)
      return e;
    e = kbQueue[kbQTail];
    kbQTail = (uint8_t)((kbQTail + 1) % KBQ);
    return e;
  }

  inline void kbPush(uint8_t code)
  {
    uint8_t next = (uint8_t)((kbQHead + 1) % KBQ);
    if (next == kbQTail)
      return; // drop if full
    kbQueue[kbQHead] = {code};
    kbQHead = next;
  }
};

// API
void inputInit();
void readInput(InputState &out);
void inputConsumeEnterOnce();

// -----------------------------------------------------------------------------
// Legacy compatibility wrappers (old code expects these)
// -----------------------------------------------------------------------------
inline void initInput() { inputInit(); }

// Old code: InputState input = readInput();
InputState readInput();

#endif
