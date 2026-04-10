#include "input_activity_state.h"
#include "input.h"

// If you already define this somewhere else, DELETE one of the definitions.
// (But based on your current linker error, the getter is missing, not this.)
uint32_t g_lastInputActivityMs = 0;

uint32_t getLastInputActivityMs() {
  return g_lastInputActivityMs;
}

void setLastInputActivityMs(uint32_t ms) {
  g_lastInputActivityMs = ms;
}

// Any meaningful user input counts as activity.
bool hasUserActivity(const InputState& in) {
  return
      // Core UI one-shot actions
      in.menuOnce || in.homeOnce || in.selectOnce ||
      in.upOnce || in.downOnce || in.leftOnce || in.rightOnce ||
      in.encoderPressOnce || in.escOnce ||
      in.consoleOnce || in.controlsOnce ||
      in.screenOnce || in.goShortRelease || in.goLongHold ||

      // Held navigation / buttons
      in.menuHeld || in.selectHeld ||
      in.upHeld || in.downHeld || in.leftHeld || in.rightHeld ||
      in.encoderHeld ||

      // Encoder / tab switching
      (in.encoderDelta != 0) ||
      (in.tabJump != 255) ||

      // Mini-game controls
      in.mgQuitOnce || in.mgSelectOnce ||
      in.mgUpOnce || in.mgDownOnce || in.mgLeftOnce || in.mgRightOnce ||
      in.mgSpaceOnce ||
      in.mgQuitHeld || in.mgSelectHeld ||
      in.mgUpHeld || in.mgDownHeld || in.mgLeftHeld || in.mgRightHeld ||
      in.mgSpaceHeld ||
      in.keyEOnce || in.keyEHeld || in.keySOnce || in.keySHeld ||

      // Keyboard activity
      in.kbChanged || (in.kbHeldCount > 0) || in.kbHasEvent();
}