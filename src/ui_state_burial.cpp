#include "ui_state_burial.h"

#include <Arduino.h>

#include "input.h"
#include "save_manager.h"
#include "sound.h"
#include "ui_runtime.h"
#include "ui_input_common.h"

void ui_showMessage(const char* msg);
void forceRenderUIOnce();

static bool s_burialDirgeStarted = false;

void uiBurialHandle(InputState& in)
{
  if (!s_burialDirgeStarted)
  {
    soundResetDeathDirgeLatch();
    soundFuneralDirge();
    s_burialDirgeStarted = true;
  }

  if (!(in.selectOnce || in.encoderPressOnce)) {
    uiDrainKb(in);
    clearInputLatch();
    return;
  }

  // Show toast and force at least one render before reboot
  ui_showMessage("Rest in peace...");
  forceRenderUIOnce();

  delay(950);

  soundResetDeathDirgeLatch();
  saveManagerDeletePetOnly();

  s_burialDirgeStarted = false;

  delay(50);
  ESP.restart();
}