#include "ui_state_tab_driven.h"

#include "app_state.h"
#include "ui_defs.h"

// These handlers already exist (some migrated, some still legacy).
// Keep this list minimal and expand as you migrate more.
void uiPetScreenHandle(InputState& in);
void uiSleepMenuHandle(InputState& in);

void uiInventoryHandle(InputState& in);
void uiShopHandle(InputState& in);

void uiTabDrivenHandle(InputState& in)
{
  switch (g_app.currentTab)
  {
    case Tab::TAB_SLEEP:
      uiSleepMenuHandle(in);
      return;

    case Tab::TAB_INV:
      uiInventoryHandle(in);
      return;

    case Tab::TAB_SHOP:
      uiShopHandle(in);
      return;

    case Tab::TAB_PET:
    case Tab::TAB_ACTIVITIES:
    case Tab::TAB_PLAY:
    default:
      uiPetScreenHandle(in);
      return;
  }
}