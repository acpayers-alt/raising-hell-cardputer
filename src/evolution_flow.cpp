#include "evolution_flow.h"

#include <Arduino.h>

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "pet.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_invalidate.h"
#include "ui_runtime.h"

static bool s_evoFlashSoundPlayed = false;

void beginEvolution(uint8_t fromStage, uint8_t toStage)
{
  const uint32_t now = millis();

  g_app.flow.evo.active = true;
  g_app.flow.evo.showingMsg = true;
  g_app.flow.evo.msgStartMs = now;
  g_app.flow.evo.fromStage = fromStage;
  g_app.flow.evo.toStage = toStage;
  g_app.flow.evo.phase = 0;
  g_app.flow.evo.phaseStartMs = 0;
  g_app.flow.evo.flashWhite = false;
  s_evoFlashSoundPlayed = false;

  clearInputLatch();
  requestUIRedraw();
}

void updateEvolution() {
  const uint32_t now = millis();

  // Tunables
  const uint32_t kMsgHoldMs   = 1100;  // message phase
  const uint32_t kPreHoldMs   = 1500;  // BEFORE sprite hold
  const uint32_t kFlashMs     = 140;   // longer flash
  const uint32_t kPostHoldMs  = 1700;  // AFTER sprite hold

  // Phase -1: message phase (still in whatever UIState you were in)
  if (g_app.flow.evo.showingMsg) {
    if (now - g_app.flow.evo.msgStartMs < kMsgHoldMs) {
      return;
    }

    // Switch into the black evolution screen
    g_app.flow.evo.showingMsg = false;
    uiActionEnterState(UIState::EVOLUTION, Tab::TAB_PET, true);

    g_app.flow.evo.phase        = 0;
    g_app.flow.evo.phaseStartMs = now;
    g_app.flow.evo.flashWhite   = false;

    requestFullUIRedraw();
    clearInputLatch();
    return;
  }

  // If we somehow got here without being in EVOLUTION state, force it
  if (g_app.uiState != UIState::EVOLUTION) {
    uiActionEnterState(UIState::EVOLUTION, Tab::TAB_PET, true);

    g_app.flow.evo.phase        = 0;
    g_app.flow.evo.phaseStartMs = now;
    g_app.flow.evo.flashWhite   = false;

    requestFullUIRedraw();
    clearInputLatch();
    return;
  }

  // Normal phases
  const uint32_t dt = now - g_app.flow.evo.phaseStartMs;

  switch (g_app.flow.evo.phase) {
    case 0: // pre
      if (dt >= kPreHoldMs) {
        g_app.flow.evo.phase        = 1;
        g_app.flow.evo.phaseStartMs = now;
        g_app.flow.evo.flashWhite   = true;

        if (!s_evoFlashSoundPlayed) {
          soundEvoZap();
          s_evoFlashSoundPlayed = true;
        }
      }
      break;

    case 1: // flash
      if (dt >= kFlashMs) {
        g_app.flow.evo.phase        = 2;
        g_app.flow.evo.phaseStartMs = now;
        g_app.flow.evo.flashWhite   = false;
      }
      break;

    case 2: // post
      if (dt >= kPostHoldMs) {
        g_app.flow.evo.phase = 3;
      }
      break;

    default:
    case 3: { // done → commit + return
      // Commit the stage change here (so the reveal is “real”)
      pet.setEvoStage(g_app.flow.evo.toStage);

      g_app.flow.evo.active     = false;
      g_app.flow.evo.flashWhite = false;

      uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);

      requestFullUIRedraw();
      clearInputLatch();
      return;
    }
  }
}
