#include "hatching_flow.h"

#include <Arduino.h>
#include <cstring>

#include "app_state.h"
#include "graphics.h"
#include "input.h"
#include "name_entry_state.h"
#include "sound.h"
#include "time_state.h"
#include "ui_actions.h"
#include "ui_invalidate.h"
#include "ui_runtime.h"

void updateHatching()
{
  const uint32_t now = millis();

  HatchFlowState &h = g_app.flow.hatch;

  // Scripted sequence:
  // wobble = 1(hold) -> 2 -> 3(hold) -> 4 -> 3
  // then hold, repeat wobble, then flash pulses, then cracked egg, then message.
  //
  // Frames indices: 0=crack1, 1=crack2, 2=crack3, 3=crack4, 4=cracked egg

  // Timing (ms) - tuned: faster steps, shorter holds
  const uint32_t kHoldCrack1Ms = 1300;
  const uint32_t kStepMs = 120;
  const uint32_t kHoldCrack4Ms = 420; // (your crack3 hold)
  const uint32_t kHoldBetweenMs = 520;
  const uint32_t kHoldCrackedMs = 900;
  const uint32_t kMessageHoldMs = 3600;

  // Flash pulses (reduces tearing vs one long white fill)
  const uint32_t kFlashPulseMs = 38; // duration of each white/black slice
  const uint8_t kFlashPulses = 3;    // number of white pulses

  auto startStep = [&](uint8_t newStep)
  {
    h.step = newStep;
    h.stepStartMs = now;
  };

  // ---------------------------------------------------------------------------
  // Flash pulse driver (uses hatch flow state)
  // ---------------------------------------------------------------------------
  auto flashBegin = [&]()
  {
    h.flashActive = true;
    h.flashPhase = 0;
    h.flashPhaseStartMs = now;

    h.flashWhite = true;
    h.flashStartMs = now;
  };

  auto flashTick = [&]()
  {
    if (!h.flashActive)
      return;

    const uint32_t fdt = now - h.flashPhaseStartMs;
    if (fdt < kFlashPulseMs)
      return;

    h.flashPhaseStartMs = now;
    h.flashPhase++;

    const uint8_t totalSlices = (uint8_t)(kFlashPulses * 2);
    if (h.flashPhase >= totalSlices)
    {
      h.flashActive = false;
      h.flashWhite = false;
      return;
    }

    // Even phases = white, odd phases = black
    h.flashWhite = ((h.flashPhase % 2) == 0);
  };

  // ---------------------------------------------------------------------------
  // One-shot step sound hooks
  // ---------------------------------------------------------------------------

  auto onStepEnter = [&](uint8_t newStep)
  {
    // crack2 moments
    if (newStep == 1 || newStep == 7)
    {
      soundClick();
    }

    // crack4 moments
    if (newStep == 3 || newStep == 9)
    {
      soundClick();
    }

    // flash trigger moment
    if (newStep == 11)
    {
      soundConfirm();
    }

    // message/birth moment
    if (newStep == 12)
    {
      soundWake();
    }
  };

  // ---------------------------------------------------------------------------
  // Init on entering hatching
  // ---------------------------------------------------------------------------
  if (!h.active)
  {
    h.active = true;
    h.startMs = now;

    h.showingMsg = false;
    h.msgStartMs = 0;
    h.frame = 0;
    h.inited = true;

    h.flashActive = false;
    h.flashPhase = 0;
    h.flashWhite = false;
    h.flashStartMs = 0;
    h.flashPhaseStartMs = 0;

    startStep(0);
    h.lastStep = 255; // force step-enter hook
  }
  else if (!h.inited)
  {
    // Safety: if state got weird, re-init deterministically
    h.showingMsg = false;
    h.msgStartMs = 0;
    h.frame = 0;
    h.inited = true;

    h.flashActive = false;
    h.flashPhase = 0;
    h.flashWhite = false;
    h.flashStartMs = 0;
    h.flashPhaseStartMs = 0;

    startStep(0);
    h.lastStep = 255;
  }

  // Fire step-enter sound hooks
  if (h.step != h.lastStep)
  {
    h.lastStep = h.step;
    onStepEnter(h.step);
  }

  // Keep flash progressing regardless of step (but draw code will ignore during msg)
  flashTick();

  const uint32_t dt = now - h.stepStartMs;

  switch (h.step)
  {
    // ---- WOBBLE 1 ----
  case 0: // crack1 HOLD
    h.frame = 0;
    if (dt >= kHoldCrack1Ms)
      startStep(1);
    break;

  case 1: // crack2
    g_app.flow.hatch.frame = 1;
    if (dt >= kStepMs)
      startStep(2);
    break;

  case 2: // crack3 HOLD
    g_app.flow.hatch.frame = 2;
    if (dt >= kHoldCrack4Ms)
      startStep(3);
    break;

  case 3: // crack4
    g_app.flow.hatch.frame = 3;
    if (dt >= kStepMs)
      startStep(4);
    break;

  case 4: // back to crack3 (end of wobble)
    g_app.flow.hatch.frame = 2;
    if (dt >= kStepMs)
      startStep(5);
    break;

  case 5:                       // hold between wobbles
    h.frame = 0; // holding crack1 reads best visually
    if (dt >= kHoldBetweenMs)
      startStep(6);
    break;

  // ---- WOBBLE 2 ----
  case 6: // crack1 HOLD
    h.frame = 0;
    if (dt >= kHoldCrack1Ms)
      startStep(7);
    break;

  case 7: // crack2
    g_app.flow.hatch.frame = 1;
    if (dt >= kStepMs)
      startStep(8);
    break;

  case 8: // crack3 HOLD
    g_app.flow.hatch.frame = 2;
    if (dt >= kHoldCrack4Ms)
      startStep(9);
    break;

  case 9: // crack4
    g_app.flow.hatch.frame = 3;
    if (dt >= kStepMs)
      startStep(10);
    break;

  case 10: // back to crack3 (end of wobble)
    g_app.flow.hatch.frame = 2;
    if (dt >= kStepMs)
    {
      // begin multi-pulse flash sequence, then move to cracked egg hold
      flashBegin();
      startStep(11);
    }
    break;

  // ---- CRACKED EGG ----
  case 11: // cracked egg hold (flash may still be pulsing briefly)
    g_app.flow.hatch.frame = 4;

    // Ensure we never remain stuck white if something odd happens
    if (!h.flashActive)
    {
      h.flashWhite = false;
    }

    if (dt >= kHoldCrackedMs)
      startStep(12);
    break;

  // ---- MESSAGE ----
  case 12:
      // never allow flash during message phase
      h.flashActive = false;
      h.flashWhite = false;

      h.frame = 4;
      h.showingMsg = true;
      if (h.msgStartMs == 0)
        h.msgStartMs = now;

      if (dt >= kMessageHoldMs)
      {
        // Reset hatching state
        h.active = false;
        h.startMs = 0;
        h.showingMsg = false;
        h.msgStartMs = 0;
        h.frame = 0;
        h.inited = false;

        h.step = 0;
        h.stepStartMs = 0;
        h.lastStep = 255;

        h.flashActive = false;
        h.flashPhase = 0;
        h.flashWhite = false;
        h.flashStartMs = 0;
        h.flashPhaseStartMs = 0;
        
      // Go to naming
      memset(g_pendingPetName, 0, sizeof(g_pendingPetName));

      // Flush any stale keyboard events / latches from CHOOSE_PET or DEATH flow
      inputForceClear();
      clearInputLatch();

      // Now enable text capture for name entry
      inputSetTextCapture(true);

      uiActionEnterState(UIState::NAME_PET, Tab::TAB_PET, true);
      requestFullUIRedraw();

      // One more latch clear for safety (cheap and avoids double-enter)
      clearInputLatch();
    }
    break;
  }
}
