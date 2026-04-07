#include "ui_state_clock_mode.h"

#include "graphics.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"

void uiClockModeHandle(InputState &in)
{
  if (in.escOnce || in.menuOnce)
  {
    playBeep();
    uiActionEnterStateClean(UIState::PET_SCREEN, Tab::TAB_PET, true, in, 150);    requestFullUIRedraw();
    return;
  }

  // Keep the mode passive. Ignore navigation / tab movement here.
  clearInputLatch();
}