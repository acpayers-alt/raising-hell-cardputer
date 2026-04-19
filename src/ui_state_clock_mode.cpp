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

void openClockModeWithReturn(UIState state, Tab tab, InputState &in, uint16_t drainMs)
{
  uiClockModeSetReturnState(state, tab);
  uiActionEnterStateClean(UIState::CLOCK_MODE, Tab::TAB_PET, true, in, drainMs);
  requestFullUIRedraw();
}

void uiClockModeExitToReturn(InputState &in, uint16_t drainMs)
{
  const ReturnTarget target = s_clockModeReturn;
  const bool returningToSleep = (target.state == UIState::PET_SLEEPING);

  uiActionEnterStateClean(target.state, target.tab, true, in, drainMs);

  // Reset after use, not before.
  s_clockModeReturn = {UIState::PET_SCREEN, Tab::TAB_PET};

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
}

void uiClockModeHandle(InputState &in)
{
  if (in.escOnce || in.menuOnce)
  {
    playBeep();
    uiClockModeExitToReturn(in, 150);
    return;
  }
}