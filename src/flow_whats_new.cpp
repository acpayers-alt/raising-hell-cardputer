#include "flow_whats_new.h"

#include "graphics.h"
#include "input.h"
#include "sound.h"
#include "ui_actions.h"
#include "whats_new_state.h"

void uiWhatsNewHandle(InputState &in)
{
  if (!whatsNewDismissAllowed())
  {
    uiActionSwallowAll(in);
    return;
  }

  int move = in.encoderDelta;
  if (in.upOnce)
    move = -1;
  if (in.downOnce)
    move = 1;

  if (move < 0)
  {
    uiActionSwallowAll(in);
    if (whatsNewScrollUp())
      playBeep();
    return;
  }

  if (move > 0)
  {
    uiActionSwallowAll(in);
    if (whatsNewScrollDown())
      playBeep();
    return;
  }

  if (in.selectOnce || in.menuOnce || in.escOnce)
  {
    uiActionSwallowAll(in);
    whatsNewDismiss();
    return;
  }
}