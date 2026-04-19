#include "ui_power_menu.h"

#include "flow_power_menu.h"
#include "input.h"
#include "ui_actions.h"
#include "ui_runtime.h"
#include "graphics.h"
#include "ui_state_clock_mode.h"
#include "app_state.h"
#include "pet.h"
#include "settings_flow_state.h"
#include "save_manager.h"

namespace {

struct MenuItem {
  const char* label;
  void (*onSelect)(InputState&);
};

static void actClockMode(InputState &in)
{
  resetClockModePetPresentation();

  // Return to whatever screen was underneath the power menu.
  openClockModeWithReturn(g_settingsFlow.powerMenuReturn.state,
                          g_settingsFlow.powerMenuReturn.tab,
                          in,
                          120);
}

static void actReboot(InputState& in)
{
  (void)in;
  powerMenuActReboot();
}

static void actShutdown(InputState& in)
{
  (void)in;
  powerMenuActShutdown();
}

static const MenuItem kItems[] = {
  {"Clock Mode", actClockMode},
  {"Reboot",     actReboot},
  {"Shut Down",  actShutdown},
};

} // namespace

int uiPowerMenuCount()
{
  return (int)(sizeof(kItems) / sizeof(kItems[0]));
}

const char* uiPowerMenuLabel(int idx)
{
  if (idx < 0 || idx >= uiPowerMenuCount()) return "";
  return kItems[idx].label;
}

bool uiPowerMenuActivate(int idx, InputState& in)
{
  if (idx < 0 || idx >= uiPowerMenuCount()) return false;
  if (!kItems[idx].onSelect) return false;
  kItems[idx].onSelect(in);
  return true;
}