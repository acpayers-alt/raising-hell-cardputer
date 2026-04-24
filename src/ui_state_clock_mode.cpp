#include "ui_state_clock_mode.h"

#include "graphics.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "ui_state_pet_sleeping.h"

void openClockModeWithReturn(UIState state, Tab tab, InputState &in, uint16_t drainMs)
{
  uiPushReturnTarget(state, tab);
  uiActionEnterStateClean(UIState::CLOCK_MODE, Tab::TAB_PET, true, in, drainMs);
  requestFullUIRedraw();
}

void openClockModeWithReturnNoInput(UIState state, Tab tab)
{
  uiPushReturnTarget(state, tab);
  uiActionEnterState(UIState::CLOCK_MODE, Tab::TAB_PET, true);
  inputForceClear();
  clearInputLatch();
  requestFullUIRedraw();
}

void uiClockModeExitToReturn(InputState &in, uint16_t drainMs)
{
  const UIReturnTarget target = uiGetReturnTarget();
  uiPopReturnTarget();

  const bool returningToSleep = (target.state == UIState::PET_SLEEPING);

  uiActionEnterStateClean(target.state, target.tab, true, in, drainMs);

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