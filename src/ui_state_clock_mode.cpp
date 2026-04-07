#include "ui_state_clock_mode.h"

#include "graphics.h"
#include "return_target.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_state_pet_sleeping.h"

static ReturnTarget s_clockModeReturn{UIState::PET_SCREEN, Tab::TAB_PET};

void uiClockModeSetReturnState(UIState state, Tab tab)
{
  s_clockModeReturn.state = state;
  s_clockModeReturn.tab = tab;
}

void uiClockModeHandle(InputState &in)
{
  if (in.escOnce || in.menuOnce)
  {
    playBeep();

    const bool returningToSleep = (s_clockModeReturn.state == UIState::PET_SLEEPING);

    uiActionEnterStateClean(s_clockModeReturn.state, s_clockModeReturn.tab, true, in, 150);

    if (returningToSleep)
    {
      uiPetSleepingBootEnter();
      requestFullUIRedraw();
      sleepBgKickNow();
      forceRenderUIOnce();
    }
    else
    {
      requestFullUIRedraw();
    }

    return;
  }

  clearInputLatch();
}