#include "M5Cardputer.h"
#include "app_state.h"
#include "currency.h"
#include "display.h"
#include "graphics.h"
#include "input.h"
#include <Arduino.h>
#include <ctype.h>

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
static void readKeyboard(InputState &out);
static void applyGpioButtons(InputState &out);
static void IRAM_ATTR updateEncoderISR();

// GO button tracking (kept if your project uses it elsewhere)
static bool lastGo = false;
static uint32_t goPressMs = 0;

static inline bool isDeleteToken(uint8_t uc, char c)
{
  // Character-stream safe delete tokens only.
  return (uc == 0x7F) || (uc == 0x08) || (c == '\b');
}

// -----------------------------------------------------------------------------
// Shared input globals
// -----------------------------------------------------------------------------
static InputState g_last;

// Force-clear flag: request a latch clear on next readInput()
static volatile bool g_forceClear = false;

// If GO is used for screen power, it may also generate Keyboard Enter.
// This flag lets main() consume exactly one Enter->Select mapping.
static volatile bool g_consumeEnterOnce = false;
void inputConsumeEnterOnce() { g_consumeEnterOnce = true; }

// Text capture mode (console/text input)
bool g_textCaptureMode = false;

// Keyboard edge latches
static bool s_settingsKeyLatched = false; // ESC latch (historically ` / ~ on some layouts)
static bool s_enterLatched = false;
static bool s_mgSelectLatched = false; // mini-game Enter/G edge latch (separate from UI Enter)
static bool s_delLatched = false;
static bool s_qLatched = false; // Q -> menu edge
static bool s_cLatched = false; // '/' or '\' -> console edge
static bool s_gLatched = false; // G -> selectOnce edge

// Nav cluster latches — prevent press+release double edges
static bool s_navUpLatched = false;
static bool s_navDownLatched = false;
static bool s_navLeftLatched = false;
static bool s_navRightLatched = false;

// Mini-game / misc latches
static bool s_spaceLatched = false;

// -----------------------------------------------------------------------------
// Keyboard held probes
// -----------------------------------------------------------------------------
static inline bool kbHeldChar(char c) { return M5Cardputer.Keyboard.isKeyPressed(c); }

// NOTE: M5Cardputer::Keyboard.isKeyPressed() is character-based on many firmwares.
// Passing HID usage IDs here is NOT reliable. Keep this only for ASCII control probes.
static inline bool kbHeldKey(uint8_t ascii) { return M5Cardputer.Keyboard.isKeyPressed((char)ascii); }

// Your arrow cluster on Cardputer is usually FN-layer punctuation.
// Common mapping used in Cardputer projects:
//   Up=';'  Down='.'  Left=','  Right='/'
static inline bool kbHeldUpArrow() { return kbHeldChar(';'); }
static inline bool kbHeldDownArrow() { return kbHeldChar('.'); }
static inline bool kbHeldLeftArrow() { return kbHeldChar(','); }
static inline bool kbHeldRightArrow() { return kbHeldChar('/'); }

// Some special keys (ESC/Backspace/Delete) don't always contribute to isPressed()/isChange()
// on all Cardputer keyboard firmwares. Probe a couple of representations so edge latches behave.
static inline bool kbHeldEnterKey() { return M5Cardputer.Keyboard.keysState().enter; }

static inline bool kbHeldEscKey()
{
  // Physical "ESC" key on Cardputer is usually the ` / ~ key.
  if (kbHeldChar('`') || kbHeldChar('~'))
    return true;

  // Some firmwares expose HID usage IDs / ASCII ESC in isKeyPressed().
  if (kbHeldKey(0x1B))
    return true;

  // Some layouts/fonts report this key as degree in the word stream; held-probe is best-effort.
  if (kbHeldChar((char)0xB0))
    return true;

  return false;
}

static inline bool kbHeldBackspaceKey()
{
  // Try multiple representations.
  // Some firmwares may treat backspace as ASCII BS or DEL.
  if (kbHeldChar('\b'))
    return true;
  if (kbHeldChar((char)0x08))
    return true; // BS
  if (kbHeldChar((char)0x7F))
    return true; // DEL

  return false;
}

static inline bool kbHeldDeleteKey()
{
  // Forward delete often surfaces as ASCII DEL on some firmwares.
  if (kbHeldChar((char)0x7F))
    return true;

  return false;
}

static inline bool kbHeldAnyDeleteKey() { return kbHeldBackspaceKey() || kbHeldDeleteKey(); }

// -----------------------------------------------------------------------------
// OPTIONAL GPIO BUTTONS
// -----------------------------------------------------------------------------
static inline bool readActiveLow(int pin) { return (digitalRead(pin) == LOW); }

static constexpr unsigned long DEBOUNCE_MS = 12; // 10–15ms feels good on Cardputer buttons

struct DebBtn
{
  bool raw = false;
  bool stable = false;
  unsigned long lastChangeMs = 0;
};

#if defined(BTN_LEFT)
static DebBtn dbLeft;
#endif
#if defined(BTN_RIGHT)
static DebBtn dbRight;
#endif
#if defined(BTN_UP)
static DebBtn dbUp;
#endif
#if defined(BTN_DOWN)
static DebBtn dbDown;
#endif
#if defined(BTN_SELECT)
static DebBtn dbSel;
#endif
#if defined(BTN_MENU)
static DebBtn dbMenu;
#endif

// -----------------------------------------------------------------------------
// OPTIONAL ENCODER (kept behind defines; safe if unused)
// -----------------------------------------------------------------------------
volatile int g_detentDelta = 0;
static volatile uint8_t g_lastEncState = 0;
static volatile int8_t g_quadCount = 0;

static const int8_t QEM[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void IRAM_ATTR updateEncoderISR()

{
#if defined(ENC_A) && defined(ENC_B)
  uint8_t a = (uint8_t)digitalRead(ENC_A);
  uint8_t b = (uint8_t)digitalRead(ENC_B);
  uint8_t state = (uint8_t)((a << 1) | b);

  uint8_t idx = (uint8_t)((g_lastEncState << 2) | state);
  int8_t step = QEM[idx];

  if (step != 0)
  {
    g_quadCount += step;
    if (g_quadCount >= 4)
    {
      g_detentDelta++;
      g_quadCount = 0;
    }
    else if (g_quadCount <= -4)
    {
      g_detentDelta--;
      g_quadCount = 0;
    }
  }

  g_lastEncState = state;
#endif
}

// Backspace Helper for preventing del from opening menu
static inline bool backspaceMapsToMenuInState(UIState s)
{
  // If we're on the sleep tab or in the dedicated sleeping UI, DEL/BKSP must do nothing.
  // This prevents "open menu -> close menu -> wake pet" side effects.
  if (g_app.currentTab == Tab::TAB_SLEEP)
    return false;

  switch (s)
  {
  case UIState::INVENTORY:
  case UIState::SHOP:
  case UIState::PET_SLEEPING:
    return false; // Backspace inactive here
  default:
    return true;
  }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
void inputForceClear() { g_forceClear = true; }

void inputSetTextCapture(bool on)
{
  g_textCaptureMode = on;

  // Clear latches immediately so we don't carry UI edges into text or vice versa.
  clearInputLatch();

  // Also request a one-tick deferred clear so "cleared while held" doesn't
  // starve edges after mode switches.
  inputForceClear();
}

void inputInit()
{
  const unsigned long now = millis();

#if defined(BTN_LEFT)
  pinMode(BTN_LEFT, INPUT_PULLUP);
  dbLeft.raw = dbLeft.stable = readActiveLow(BTN_LEFT);
  dbLeft.lastChangeMs = now;
#endif
#if defined(BTN_RIGHT)
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  dbRight.raw = dbRight.stable = readActiveLow(BTN_RIGHT);
  dbRight.lastChangeMs = now;
#endif
#if defined(BTN_UP)
  pinMode(BTN_UP, INPUT_PULLUP);
  dbUp.raw = dbUp.stable = readActiveLow(BTN_UP);
  dbUp.lastChangeMs = now;
#endif
#if defined(BTN_DOWN)
  pinMode(BTN_DOWN, INPUT_PULLUP);
  dbDown.raw = dbDown.stable = readActiveLow(BTN_DOWN);
  dbDown.lastChangeMs = now;
#endif
#if defined(BTN_SELECT)
  pinMode(BTN_SELECT, INPUT_PULLUP);
  dbSel.raw = dbSel.stable = readActiveLow(BTN_SELECT);
  dbSel.lastChangeMs = now;
#endif
#if defined(BTN_MENU)
  pinMode(BTN_MENU, INPUT_PULLUP);
  dbMenu.raw = dbMenu.stable = readActiveLow(BTN_MENU);
  dbMenu.lastChangeMs = now;
#endif

#if defined(ENC_A) && defined(ENC_B)
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  g_lastEncState = (uint8_t)((digitalRead(ENC_A) << 1) | digitalRead(ENC_B));
  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoderISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoderISR, CHANGE);
#endif

  g_detentDelta = 0;
  g_quadCount = 0;

  s_settingsKeyLatched = false;
  s_enterLatched = false;
  s_mgSelectLatched = false;
  s_delLatched = false;
  s_qLatched = false;
  s_cLatched = false;
  s_gLatched = false;
  s_navUpLatched = false;
  s_navDownLatched = false;
  s_navLeftLatched = false;
  s_navRightLatched = false;
  s_spaceLatched = false;
}

void clearInputLatch()
{
  // Reset keyboard latches.
  // IMPORTANT: preserve latches for keys that are currently HELD.
  auto st = M5Cardputer.Keyboard.keysState();

  // Preserve held state so clearing latches while ESC key is held
  // doesn't synthesize a fresh escOnce next tick.
  // IMPORTANT: keysState().word can be stale unless isChange() is true on some firmwares.
  // For latch preservation, only trust the held-probe.
  s_settingsKeyLatched = kbHeldEscKey();

  s_enterLatched = kbHeldEnterKey();               // preserve only truly held enter
  s_delLatched = (st.del || kbHeldAnyDeleteKey()); // preserve held delete/backspace

  s_qLatched = false;
  s_cLatched = false;
  s_gLatched = false;

  s_navUpLatched = false;
  s_navDownLatched = false;
  s_navLeftLatched = false;
  s_navRightLatched = false;
  s_spaceLatched = false;

  // IMPORTANT:
  // Do NOT modify s_mgSelectLatched here.
  // It is derived from held state in the keyboard scan; clearing/preserving it
  // during transitions can either synthesize a fresh mgSelectOnce (restart bug)
  // or get stuck true (can't-start bug).

  // Reset edge tracking for GPIO so next tick doesn't generate "Once"
  const unsigned long now = millis();

#if defined(BTN_LEFT)
  dbLeft.raw = dbLeft.stable = readActiveLow(BTN_LEFT);
  dbLeft.lastChangeMs = now;
#endif
#if defined(BTN_RIGHT)
  dbRight.raw = dbRight.stable = readActiveLow(BTN_RIGHT);
  dbRight.lastChangeMs = now;
#endif
#if defined(BTN_UP)
  dbUp.raw = dbUp.stable = readActiveLow(BTN_UP);
  dbUp.lastChangeMs = now;
#endif
#if defined(BTN_DOWN)
  dbDown.raw = dbDown.stable = readActiveLow(BTN_DOWN);
  dbDown.lastChangeMs = now;
#endif
#if defined(BTN_SELECT)
  dbSel.raw = dbSel.stable = readActiveLow(BTN_SELECT);
  dbSel.lastChangeMs = now;
#endif
#if defined(BTN_MENU)
  dbMenu.raw = dbMenu.stable = readActiveLow(BTN_MENU);
  dbMenu.lastChangeMs = now;
#endif

  // Reset encoder movement
  g_detentDelta = 0;
  g_quadCount = 0;
}

// -----------------------------------------------------------------------------
// Main input read (matches input.h API)
// -----------------------------------------------------------------------------
void readInput(InputState &out)
{
  if (g_forceClear)
  {
    clearInputLatch();
    g_forceClear = false;
  }

  out.clearEdges();

  // Keyboard first
  readKeyboard(out);

  // Only apply physical buttons / encoder in UI mode
  if (!g_textCaptureMode)
  {
    applyGpioButtons(out);

    int d = 0;
    noInterrupts();
    d = g_detentDelta;
    g_detentDelta = 0;
    interrupts();
    out.encoderDelta = d;
  }

  g_last = out;
}

InputState readInput()
{
  InputState out;
  readInput(out);
  return out;
}

// -----------------------------------------------------------------------------
// GPIO buttons (edge pulses)
// -----------------------------------------------------------------------------
static void applyGpioButtons(InputState &out)
{
  const unsigned long now = millis();

  auto updateBtn = [&](int pin, DebBtn &b, bool &onceFlag)
  {
    const bool cur = readActiveLow(pin);

    if (cur != b.raw)
    {
      b.raw = cur;
      b.lastChangeMs = now;
    }

    if ((now - b.lastChangeMs) >= DEBOUNCE_MS && b.stable != b.raw)
    {
      b.stable = b.raw;
      if (b.stable)
        onceFlag = true; // press edge only
    }
  };

#if defined(BTN_LEFT)
  updateBtn(BTN_LEFT, dbLeft, out.leftOnce);
#endif
#if defined(BTN_RIGHT)
  updateBtn(BTN_RIGHT, dbRight, out.rightOnce);
#endif
#if defined(BTN_UP)
  updateBtn(BTN_UP, dbUp, out.upOnce);
#endif
#if defined(BTN_DOWN)
  updateBtn(BTN_DOWN, dbDown, out.downOnce);
#endif
#if defined(BTN_SELECT)
  updateBtn(BTN_SELECT, dbSel, out.selectOnce);
#endif
#if defined(BTN_MENU)
  updateBtn(BTN_MENU, dbMenu, out.menuOnce);
#endif

#if defined(BTN_SELECT)
  out.selectHeld = dbSel.stable;
#endif
#if defined(BTN_MENU)
  out.menuHeld = dbMenu.stable;
#endif
#if defined(BTN_UP)
  out.upHeld = dbUp.stable;
#endif
#if defined(BTN_DOWN)
  out.downHeld = dbDown.stable;
#endif
#if defined(BTN_LEFT)
  out.leftHeld = dbLeft.stable;
#endif
#if defined(BTN_RIGHT)
  out.rightHeld = dbRight.stable;
#endif

  const bool inMiniGame = (g_app.uiState == UIState::MINI_GAME);
  if (inMiniGame)
  {
    out.mgLeftOnce |= out.leftOnce;
    out.mgRightOnce |= out.rightOnce;
    out.mgUpOnce |= out.upOnce;
    out.mgDownOnce |= out.downOnce;

    out.mgLeftHeld |= out.leftHeld;
    out.mgRightHeld |= out.rightHeld;
    out.mgDownHeld |= out.downHeld;
    out.mgSelectOnce |= out.selectOnce;
    out.mgSelectHeld |= out.selectHeld;

    // Lock out UI-layer meanings while in mini-game
    out.leftOnce = out.rightOnce = out.upOnce = out.downOnce = false;
    out.selectOnce = out.menuOnce = false;

    out.leftHeld = out.rightHeld = out.upHeld = out.downHeld = false;
    out.selectHeld = out.menuHeld = false;
  }
}

// -----------------------------------------------------------------------------
// Keyboard read + mapping
// -----------------------------------------------------------------------------
static void readKeyboard(InputState &out)
{
  const bool changed = M5Cardputer.Keyboard.isChange();

  auto st = M5Cardputer.Keyboard.keysState();
  out.kbHeldCount = (uint8_t)M5Cardputer.Keyboard.isPressed();

  const bool inMiniGameUi = (g_app.uiState == UIState::MINI_GAME);

  // -----------------------------------------------------------------------------
  // ESC detection (must NOT depend on `changed`, and must happen BEFORE early-return)
  // -----------------------------------------------------------------------------
  bool escWordThisTick = false;

  // Scan the word stream ONLY when changed (word can be stale otherwise)
  if (changed)
  {
    uint8_t seq = 0; // 0=none, 1=saw ESC, 2=saw ESC[
    for (auto wc : st.word)
    {
      if (!wc)
        continue;

      const uint8_t uc = (uint8_t)wc;

      if (seq == 0)
      {
        if (uc == 0x1B)
        {
          // Might be a naked ESC, or the start of an ANSI sequence.
          seq = 1;
          continue;
        }

        // Non-ANSI representations of ESC key some firmwares emit
        if (wc == '`' || wc == '~' || uc == 0xB0)
          escWordThisTick = true;

        continue;
      }

      if (seq == 1)
      {
        // ANSI arrow prefix: ESC[
        if (wc == '[')
        {
          seq = 2;
          continue;
        }

        // ESC not followed by '[' -> treat as real ESC
        escWordThisTick = true;
        seq = 0;
        continue;
      }

      // seq == 2 : ESC [ <code>  (arrows). Don't mark as ESC.
      seq = 0;
    }

    // IMPORTANT FIX:
    // If the stream ended right after a lone ESC (0x1B), that IS a real ESC press.
    if (seq == 1)
      escWordThisTick = true;
  }

  // Held-probe fallback (works even when word stream doesn't report ESC)
  const bool heldEsc = kbHeldEscKey();

  // Unified ESC signal
  const bool escAnyHeld = (escWordThisTick || heldEsc);

  // If ESC isn't held, always ensure the latch can't get stuck true due to earlier clears.
  if (!escAnyHeld)
    s_settingsKeyLatched = false;

  // Always compute mini-game held controls
  const bool heldUp = kbHeldUpArrow();
  const bool heldDown = kbHeldDownArrow();
  const bool heldLeft = kbHeldLeftArrow();
  const bool heldRight = kbHeldRightArrow();

  const bool heldE = kbHeldChar('e') || kbHeldChar('E');
  const bool heldA = kbHeldChar('a') || kbHeldChar('A');
  const bool heldS = kbHeldChar('s') || kbHeldChar('S');
  const bool heldD = kbHeldChar('d') || kbHeldChar('D');

  const bool heldO = kbHeldChar('o') || kbHeldChar('O');
  const bool heldJ = kbHeldChar('j') || kbHeldChar('J');
  const bool heldK = kbHeldChar('k') || kbHeldChar('K');
  const bool heldL = kbHeldChar('l') || kbHeldChar('L');

  const bool heldEnter = st.enter;
  const bool heldG = kbHeldChar('g') || kbHeldChar('G');
  const bool heldSpace = kbHeldChar(' ');

  bool delWordThisTick = false;
  if (changed)
  {
    for (auto wc : st.word)
    {
      if (!wc)
        continue;
      if (isDeleteToken((uint8_t)wc, wc))
      {
        delWordThisTick = true;
        break;
      }
    }
  }

  const bool heldBackspaceSpecial = st.del || kbHeldAnyDeleteKey() || delWordThisTick;

  const bool mgUpHeld = (heldUp || heldE || heldO);
  const bool mgDownHeld = (heldDown || heldS || heldK);
  const bool mgLeftHeld = (heldLeft || heldA || heldJ);
  const bool mgRightHeld = (heldRight || heldD || heldL);
  const bool mgSelectHeld = (heldEnter || heldG);

  out.mgUpHeld = mgUpHeld;
  out.mgDownHeld = mgDownHeld;
  out.mgLeftHeld = mgLeftHeld;
  out.mgRightHeld = mgRightHeld;
  out.mgSelectHeld = mgSelectHeld;
  out.mgSpaceHeld = heldSpace;

  // UI held flags.
  // Normal UI gets keyboard nav clusters.
  // Console/text capture only gets punctuation arrows so E/S/etc still type normally.
  if (!inMiniGameUi)
  {
    if (!g_textCaptureMode)
    {
      out.selectHeld = st.enter;
      out.uiUpHeld = (heldUp || heldE || heldO);
      out.uiDownHeld = (heldDown || heldS || heldK);
    }
    else if (g_app.uiState == UIState::CONSOLE)
    {
      out.uiUpHeld = heldUp;
      out.uiDownHeld = heldDown;
    }
  }

  // If nothing changed AND no keys are held, we can skip work.
  // IMPORTANT: include escAnyHeld so ESC never gets skipped by this early return.
  const bool heldSpecial = heldUp || heldDown || heldLeft || heldRight || escAnyHeld || heldEnter || heldG ||
                           heldSpace || heldBackspaceSpecial;

  if (!changed && out.kbHeldCount == 0 && !heldSpecial)
    return;

  out.kbChanged = true;

  // Punctuation arrow UI edges from held-state (UI only)
  if (!inMiniGameUi && !g_textCaptureMode)
  {
    const bool pUp = kbHeldUpArrow();
    const bool pDown = kbHeldDownArrow();
    const bool pLeft = kbHeldLeftArrow();
    const bool pRight = kbHeldRightArrow();

    static bool s_pUpLatched = false;
    static bool s_pDownLatched = false;
    static bool s_pLeftLatched = false;
    static bool s_pRightLatched = false;
    static uint32_t s_pUpMs = 0;
    static uint32_t s_pDownMs = 0;
    static uint32_t s_pLeftMs = 0;
    static uint32_t s_pRightMs = 0;

    const uint32_t kPuncCooldownMs = 120;
    const uint32_t t = millis();

    auto acceptP = [&](uint32_t &lastMs) -> bool
    {
      // Allow first press after boot.
      if (lastMs == 0)
      {
        lastMs = t;
        return true;
      }
      if ((t - lastMs) < kPuncCooldownMs)
        return false;
      lastMs = t;
      return true;
    };

    if (pUp && !s_pUpLatched && acceptP(s_pUpMs))
    {
      out.upOnce = true;
      s_pUpLatched = true;
    }
    if (!pUp)
      s_pUpLatched = false;

    if (pDown && !s_pDownLatched && acceptP(s_pDownMs))
    {
      out.downOnce = true;
      s_pDownLatched = true;
    }
    if (!pDown)
      s_pDownLatched = false;

    if (pLeft && !s_pLeftLatched && acceptP(s_pLeftMs))
    {
      out.leftOnce = true;
      s_pLeftLatched = true;
    }
    if (!pLeft)
      s_pLeftLatched = false;

    if (pRight && !s_pRightLatched && acceptP(s_pRightMs))
    {
      out.rightOnce = true;
      s_pRightLatched = true;
    }
    if (!pRight)
      s_pRightLatched = false;
  }

  // Nav de-dupe / cooldown
  const uint32_t now = millis();

  static uint32_t s_navUpMs = 0;
  static uint32_t s_navDownMs = 0;
  static uint32_t s_navLeftMs = 0;
  static uint32_t s_navRightMs = 0;
  static uint32_t s_navSelMs = 0;
  static uint32_t s_navMenuMs = 0;
  static uint32_t s_navConMs = 0;

  const uint32_t kNavCooldownMs = 120;

  auto acceptNav = [&](uint32_t &lastMs) -> bool
  {
    // Allow first press after boot.
    if (lastMs == 0)
    {
      lastMs = now;
      return true;
    }
    if ((now - lastMs) < kNavCooldownMs)
      return false;
    lastMs = now;
    return true;
  };

  // Release edge latches when idle
  if (out.kbHeldCount == 0 && !heldSpecial)
  {
    s_qLatched = false;
    s_cLatched = false;
    s_gLatched = false;
    s_spaceLatched = false;

    s_enterLatched = false;
    s_delLatched = false;
  }

  bool sawUpThisTick = false;
  bool sawDownThisTick = false;
  bool sawLeftThisTick = false;
  bool sawRightThisTick = false;
  bool sawDelWordThisTick = false;

  // -----------------------------------------------------------------------------
  // ESC edge generation (uses escAnyHeld, not just st.word)
  // -----------------------------------------------------------------------------
  const bool escNow = escAnyHeld;

  // Block ESC on mandatory boot/timekeeping screens so users can't skip them.
  auto escBlockedInState = [](UIState s) -> bool
  {
    switch (s)
    {
    case UIState::NAME_PET:
      return true;
    default:
      return false;
    }
  };

  const bool escBlockedNow = escBlockedInState(g_app.uiState);

  // Cooldown: prevents menu flicker if some code clears the latch while ESC is still held.
  static uint32_t s_escOnceMs = 0;
  const uint32_t kEscCooldownMs = 250;

  if (escNow)
  {
    if (!s_settingsKeyLatched)
    {
      if (!escBlockedNow)
      {
        if ((uint32_t)(now - s_escOnceMs) >= kEscCooldownMs)
        {
          out.escOnce = true;
          s_escOnceMs = now;
        }
      }

      if (inMiniGameUi)
        out.mgQuitOnce = true;

      s_settingsKeyLatched = true;
    }
  }
  else
  {
    s_settingsKeyLatched = false;
  }

  // Mini-game "Once" edges from held states.
  static bool s_mgNavUpLatched = false;
  static bool s_mgNavDownLatched = false;
  static bool s_mgNavLeftLatched = false;
  static bool s_mgNavRightLatched = false;

  if (mgUpHeld && !s_mgNavUpLatched)
  {
    out.mgUpOnce = true;
    s_mgNavUpLatched = true;
  }
  if (!mgUpHeld)
    s_mgNavUpLatched = false;

  if (mgDownHeld && !s_mgNavDownLatched)
  {
    out.mgDownOnce = true;
    s_mgNavDownLatched = true;
  }
  if (!mgDownHeld)
    s_mgNavDownLatched = false;

  if (mgLeftHeld && !s_mgNavLeftLatched)
  {
    out.mgLeftOnce = true;
    s_mgNavLeftLatched = true;
  }
  if (!mgLeftHeld)
    s_mgNavLeftLatched = false;

  if (mgRightHeld && !s_mgNavRightLatched)
  {
    out.mgRightOnce = true;
    s_mgNavRightLatched = true;
  }
  if (!mgRightHeld)
    s_mgNavRightLatched = false;

  if (mgSelectHeld && !s_mgSelectLatched)
  {
    out.mgSelectOnce = true;
    s_mgSelectLatched = true;
  }
  if (!mgSelectHeld)
  {
    s_mgSelectLatched = false;
  }

  if (heldSpace && !s_spaceLatched)
  {
    out.mgSpaceOnce = true;
    s_spaceLatched = true;
  }
  if (!heldSpace)
  {
    s_spaceLatched = false;
  }

  // Hard lockout in mini-game
  if (inMiniGameUi)
  {
    out.tabJump = 255;
    out.consoleOnce = false;
    out.controlsOnce = false;
    out.hotSettings = false;
    out.menuOnce = false;
    out.selectOnce = false;

    s_qLatched = kbHeldChar('q') || kbHeldChar('Q');
    s_cLatched = kbHeldChar('\\');
    s_gLatched = heldG;

    out.kbQHead = out.kbQTail = 0;
    return;
  }

  // Word stream processing ONLY when changed
  if (changed)
  {
    for (auto c : st.word)
    {
      if (!c)
        continue;

      const uint8_t ucEsc = (uint8_t)c;

      // ESC should behave like the global cancel/settings key, not disappear.
      if (ucEsc == 0x1B)
      {
        out.hotSettings = true;
        continue;
      }

      // Cardputer ESC comes through the tilde/backquote family.
      // In normal UI flow this should behave like the existing global
      // cancel/settings key, not disappear.
      if (ucEsc == 0x1B || ucEsc == 0x29 || c == '`' || c == '~' || ucEsc == 0xB0)
      {
        out.hotSettings = true;
        continue;
      }

      // Backspace/Delete/ASCII DEL -> console delete OR UI menu/back
      const uint8_t uc = (uint8_t)c;
      if (isDeleteToken(uc, c))
      {
        sawDelWordThisTick = true;

        if (g_textCaptureMode)
        {
          if (!s_delLatched)
            out.kbPush((uint8_t)'\b');
          s_delLatched = true;
        }
        else
        {
          if (backspaceMapsToMenuInState(g_app.uiState))
          {
            if (!s_delLatched && acceptNav(s_navMenuMs))
              out.menuOnce = true;
          }
          s_delLatched = true;
        }
        continue;
      }

      // Backslash -> console toggle
      if (c == '\\')
      {
        if (!s_cLatched && acceptNav(s_navConMs))
        {
          out.consoleOnce = true;
          s_cLatched = true;
        }
        continue;
      }

      const char lc = (char)tolower((unsigned char)c);

      if (!g_textCaptureMode)
      {
        // Q -> HOME edge
        if (lc == 'q')
        {
          if (!s_qLatched && acceptNav(s_navMenuMs))
            out.homeOnce = true;
          s_qLatched = true;
          continue;
        }

        // G -> selectOnce edge
        if (lc == 'g')
        {
          if (!s_gLatched && acceptNav(s_navSelMs))
          {
            out.selectOnce = true;
            s_gLatched = true;
          }
          continue;
        }

        // I -> controls overlay
        if (lc == 'i')
        {
          out.controlsOnce = true;
          continue;
        }

        // Bottom row tab jumps
        switch (lc)
        {
        case 'z':
          out.tabJump = 0;
          continue;
        case 'x':
          out.tabJump = 1;
          continue;
        case 'c':
          out.tabJump = 2;
          continue;
        case 'v':
          out.tabJump = 3;
          continue;
        case 'b':
          out.tabJump = 4;
          continue;
        case 'n':
          out.tabJump = 5;
          continue;
        case 'm':
          out.tabJump = 6;
          continue;
        default:
          break;
        }

        // WASD / HJKL / EWO nav cluster
        if (lc == 'e' || lc == 'w' || lc == 'o')
        {
          sawUpThisTick = true;
          if (!s_navUpLatched && acceptNav(s_navUpMs))
            out.upOnce = true;
          s_navUpLatched = true;
          continue;
        }
        if (lc == 'a' || lc == 'j')
        {
          sawLeftThisTick = true;
          if (!s_navLeftLatched && acceptNav(s_navLeftMs))
            out.leftOnce = true;
          s_navLeftLatched = true;
          continue;
        }
        if (lc == 's' || lc == 'k')
        {
          sawDownThisTick = true;
          if (!s_navDownLatched && acceptNav(s_navDownMs))
            out.downOnce = true;
          s_navDownLatched = true;
          continue;
        }
        if (lc == 'd' || lc == 'l')
        {
          sawRightThisTick = true;
          if (!s_navRightLatched && acceptNav(s_navRightMs))
            out.rightOnce = true;
          s_navRightLatched = true;
          continue;
        }
      }

      // Default: push printable into kb queue (text capture)
      out.kbPush((uint8_t)c);
    }
  }

  // Release nav latches when key no longer present
  if (!sawUpThisTick)
    s_navUpLatched = false;
  if (!sawDownThisTick)
    s_navDownLatched = false;
  if (!sawLeftThisTick)
    s_navLeftLatched = false;
  if (!sawRightThisTick)
    s_navRightLatched = false;

  // Backspace/Delete fallback
  if (!sawDelWordThisTick)
  {
    if (heldBackspaceSpecial)
    {
      if (g_textCaptureMode)
      {
        if (!s_delLatched)
          out.kbPush((uint8_t)'\b');
        s_delLatched = true;
      }
      else
      {
        if (backspaceMapsToMenuInState(g_app.uiState))
        {
          if (!s_delLatched && acceptNav(s_navMenuMs))
            out.menuOnce = true;
        }
        s_delLatched = true;
      }
    }
    else
    {
      s_delLatched = false;
    }
  }

  // Enter edge + optional UI select mapping
  if (st.enter)
  {
    if (!s_enterLatched)
      out.kbPush((uint8_t)'\n');

    if (!s_enterLatched && !g_textCaptureMode)
    {
      if (g_consumeEnterOnce)
        g_consumeEnterOnce = false;
      else
      {
        if (acceptNav(s_navSelMs))
          out.selectOnce = true;
      }
    }

    s_enterLatched = true;
  }
  else
  {
    s_enterLatched = false;
  }

  if (st.fn)
    out.kbPush((uint8_t)KEY_FN);
  if (st.shift)
    out.kbPush((uint8_t)RH_KEY_SHIFT);
}