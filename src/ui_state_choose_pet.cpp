#include "ui_state_choose_pet.h"
#include <Arduino.h>

#include <string.h>
#include "build_flags.h"
#include "app_state.h"
#include "input.h"
#include "name_entry_state.h"
#include "new_pet_flow_state.h"
#include "pet.h"
#include "sound.h"
#include "ui_new_pet_flow.h"
#include "ui_runtime.h"
#include "graphics.h"
#include "ui_actions.h"

static bool s_prevSelectHeld = false;

void uiChoosePetOnEnter(InputState& in)
{
  // Reset derived Enter-edge state so we do not create a fake edge
  // from whatever the previous state was doing.
  s_prevSelectHeld = in.selectHeld;

  // Any fresh entry into CHOOSE_PET should require a clean release first.
  g_choosePetBlockHatchUntilRelease = true;

  // Give the state a clean input surface.
  while (in.kbHasEvent()) (void)in.kbPop();
  in.clearEdges();
  inputForceClear();
  clearInputLatch();

  Serial.printf("[EGG] onEnter selectHeld=%d unlockMs=%lu blockUntilRelease=%d\n",
    (int)in.selectHeld,
    (unsigned long)g_choosePetInputUnlockMs,
    (int)g_choosePetBlockHatchUntilRelease);
}

void uiChoosePetHandle(InputState& in)
{
  // Reliable Enter edge (press-level -> edge) for Cardputer:
  // Sometimes selectOnce is missed depending on keyboard change detection.
  g_app.newPetFlowActive = true;

  // ---------------------------------------------------------------------------
  // Time gate to prevent instant hatch on first draw/entry
  // ---------------------------------------------------------------------------
  const uint32_t now = millis();
  if (g_choosePetInputUnlockMs != 0)
  {
    if ((int32_t)(g_choosePetInputUnlockMs - now) > 0)
    {
      while (in.kbHasEvent()) (void)in.kbPop();
      in.clearEdges();
      clearInputLatch();
      Serial.printf("[EGG] BLOCKED unlockMs still active now=%lu unlockMs=%lu\n",
        (unsigned long)now,
        (unsigned long)g_choosePetInputUnlockMs);
      return;
    }

    g_choosePetInputUnlockMs = 0;
  }

  // ---------------------------------------------------------------------------
  // Entry gate to prevent instant hatch
  // ---------------------------------------------------------------------------
  if (g_choosePetBlockHatchUntilRelease)
  {
    if (!in.selectHeld)
    {
      g_choosePetBlockHatchUntilRelease = false;
    }
    else
    {
      // swallow everything while held
      while (in.kbHasEvent()) (void)in.kbPop();
      in.clearEdges();
      inputForceClear();
      clearInputLatch();
      Serial.println("[EGG] BLOCKED waiting for release");
      return;
    }
  }

  // Enter edge derived from held state (more reliable than selectOnce)
  const bool enterEdge = (in.selectHeld && !s_prevSelectHeld);
  if (enterEdge) {
    Serial.printf("[EGG] enterEdge DETECTED selectHeld=%d prev=%d\n",
                  (int)in.selectHeld,
                  (int)s_prevSelectHeld);
  }

  s_prevSelectHeld = in.selectHeld;

  int move = 0;

  // Choose-egg should support left/right + up/down + encoder
  if (in.leftOnce)  move = -1;
  if (in.rightOnce) move = +1;

  if (in.upOnce)    move = -1;
  if (in.downOnce)  move = +1;

  if (in.encoderDelta < 0) move = -1;
  if (in.encoderDelta > 0) move = +1;

  // This is your existing choice list ordering.
  // Keep identical to legacy:
  #if PUBLIC_BUILD
  static const PetType kChoices[] = {
    PET_DEVIL,
    PET_ELDRITCH,
  };
#else
  static const PetType kChoices[] = {
    PET_DEVIL,
    PET_ELDRITCH,
    PET_ALIEN,
    PET_KAIJU,
    PET_ANUBIS,
    PET_AXOLOTL,
  };
#endif

  const int choiceCount = (int)(sizeof(kChoices) / sizeof(kChoices[0]));

  // Find current index from pending type
  int curIdx = 0;
  for (int i = 0; i < choiceCount; ++i)
  {
    if (kChoices[i] == g_pendingPetType)
    {
      curIdx = i;
      break;
    }
  }

  if (move != 0)
  {
    int nextIdx = curIdx + (move > 0 ? 1 : -1);
    if (nextIdx < 0) nextIdx = choiceCount - 1;
    if (nextIdx >= choiceCount) nextIdx = 0;

    const PetType nextType = kChoices[nextIdx];
    if (nextType != g_pendingPetType)
    {
      g_pendingPetType = nextType;
      pet.type = g_pendingPetType; // keep previews consistent

      // Full redraw here prevents ghosting because drawChoosePetScreen()
      // only clears the background when redrawBg == true.
      requestFullUIRedraw();

      playBeep();
    }

    clearInputLatch();
    return;
  }

  // Select to proceed to HATCHING (NOT directly to name)
  if (enterEdge)
  {
    // Ensure text capture is off while hatching (name flow enables it later)
    inputSetTextCapture(false);
    g_textCaptureMode = false;

    // Reset hatch flow state so updateHatching() performs its one-time init
    g_app.flow.hatch.active = false;
    g_app.flow.hatch.startMs = 0;
    g_app.flow.hatch.showingMsg = false;
    g_app.flow.hatch.msgStartMs = 0;
    g_app.flow.hatch.frame = 0;
    g_app.flow.hatch.flashWhite = false;
    g_app.flow.hatch.flashStartMs = 0;

    // Enter the modal hatching state
    uiActionEnterStateClean(UIState::HATCHING, Tab::TAB_PET, false, in, 150);
    requestFullUIRedraw();

    // Swallow any stray edges/typing so we don't instantly skip phases
    while (in.kbHasEvent()) (void)in.kbPop();
    inputForceClear();
    clearInputLatch();
    return;
  }

  // swallow typing
  while (in.kbHasEvent()) (void)in.kbPop();
  clearInputLatch();
}