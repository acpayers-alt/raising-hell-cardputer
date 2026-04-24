#include "ui_sleep_menu.h"

#include <Arduino.h>

#include "input.h"
#include "ui_sleep_menu_actions.h"

namespace {

// Minimal embedded menu model (mirrors Settings pattern, but single-page)
struct MenuItem {
  const char* label;
  void (*onSelect)(InputState&);
};

static void actUntilAwakened(InputState& in)
{
  (void)in;
  const uint32_t nowMs = millis();
  uiSleepMenuSetUntilAwakened(nowMs);
  uiSleepMenuEnterSleep(nowMs, in);
}

static void actUntilRested(InputState& in)
{
  (void)in;
  const uint32_t nowMs = millis();
  uiSleepMenuSetUntilRested(nowMs);
  uiSleepMenuEnterSleep(nowMs, in);
}

static void act4Hours(InputState& in)
{
  (void)in;
  const uint32_t nowMs = millis();
  uiSleepMenuSetForHours(nowMs, 4);
  uiSleepMenuEnterSleep(nowMs, in);
}

static void act8Hours(InputState& in)
{
  (void)in;
  const uint32_t nowMs = millis();
  uiSleepMenuSetForHours(nowMs, 8);
  uiSleepMenuEnterSleep(nowMs, in);
}

static const MenuItem kItems[] = {
  {"Until Awakened", actUntilAwakened},
  {"Until Rested",   actUntilRested},
  {"4 hours",        act4Hours},
  {"8 hours",        act8Hours},
};

} // namespace

int uiSleepMenuCount()
{
  return (int)(sizeof(kItems) / sizeof(kItems[0]));
}

const char* uiSleepMenuLabel(int idx)
{
  if (idx < 0 || idx >= uiSleepMenuCount()) return "";
  return kItems[idx].label;
}

bool uiSleepMenuActivate(int idx, InputState& in)
{
  if (idx < 0 || idx >= uiSleepMenuCount()) return false;
  if (!kItems[idx].onSelect) return false;

  kItems[idx].onSelect(in);
  return true;
}