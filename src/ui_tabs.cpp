#include "ui_tabs.h"

#include "app_state.h"
#include "ui_defs.h"
#include "ui_runtime.h"
#include "ui_actions.h"

static inline int tabCountInt()
{
  return (int)Tab::TAB_COUNT;
}

static inline UIState uiStateForTab(Tab t)
{
  switch (t)
  {
    case Tab::TAB_SLEEP: return UIState::SLEEP_MENU;
    case Tab::TAB_INV:   return UIState::INVENTORY;
    case Tab::TAB_SHOP:  return UIState::SHOP;

    // PET/Stats/Feed/Play all ride the pet screen handler
    case Tab::TAB_PET:
    case Tab::TAB_STATS:
    case Tab::TAB_ACTIVITIES:
    case Tab::TAB_PLAY:
    default:
      return UIState::PET_SCREEN;
  }
}

void tabNext()
{
  int t = (int)g_app.currentTab;
  const int n = tabCountInt();
  t = (t + 1) % n;

  const Tab nt = (Tab)t;
  uiActionEnterState(uiStateForTab(nt), nt, false);
}

void tabPrev()
{
  int t = (int)g_app.currentTab;
  const int n = tabCountInt();
  t = (t + n - 1) % n;

  const Tab nt = (Tab)t;
  uiActionEnterState(uiStateForTab(nt), nt, false);
}

void syncUiToTab()
{
  uiActionEnterState(uiStateForTab(g_app.currentTab), g_app.currentTab, false);
}