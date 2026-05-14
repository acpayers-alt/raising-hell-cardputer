#include "ui_activities_menu.h"

#include "app_state.h"
#include "input.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_menu_state.h"

namespace
{
struct MenuItem
{
  const char *label;
  void (*onSelect)(InputState &);
};

static void actFishing(InputState &in)
{
  uiActionEnterStateClean(UIState::ACTIVITY_FISHING, Tab::TAB_ACTIVITIES, true, in, 160);
}

static const MenuItem kItems[] = {
    {"Fishing", actFishing},
    {"More Soon", nullptr},
};
} // namespace

int uiActivitiesMenuCount() { return (int)(sizeof(kItems) / sizeof(kItems[0])); }

const char *uiActivitiesMenuLabel(int idx)
{
  if (idx < 0 || idx >= uiActivitiesMenuCount())
    return "";
  return kItems[idx].label;
}

bool uiActivitiesMenuActivate(int idx, InputState &in)
{
  if (idx < 0 || idx >= uiActivitiesMenuCount())
    return false;

  feedMenuIndex = idx;

  if (!kItems[idx].onSelect)
  {
    soundError();
    return false;
  }

  kItems[idx].onSelect(in);
  return true;
}