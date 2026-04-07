#include "ui_state_clock_mode.h"

#include "graphics.h"
#include "pet.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

void uiClockModeHandle(InputState &in)
{
  if (in.escOnce || in.menuOnce)
  {
    playBeep();

    const UIState returnState = pet.isSleeping ? UIState::PET_SLEEPING : UIState::PET_SCREEN;

    uiActionEnterStateClean(returnState, Tab::TAB_PET, true, in, 150);
    requestFullUIRedraw();
    return;
  }

  // Keep the mode passive. Ignore navigation / tab movement here.
  clearInputLatch();
}