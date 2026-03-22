#include "ui_play_menu.h"

#include "input.h"
#include "mini_games.h" // startFlappyFireball(), startInfernalDodger(), startCrossyRoad()
#include "pet.h"

namespace {

struct MenuItem {
  const char* label;
  void (*onSelect)(InputState&);
};

static void actFlappy(InputState& in)
{
  (void)in;
  startFlappyFireball();
}

static void actDodger(InputState& in)
{
  (void)in;
  startInfernalDodger();
}

static void actCrossy(InputState& in)
{
  (void)in;
  startCrossyRoad();
}

static const MenuItem kItems[] = {
  {"Flappy Fireball",  actFlappy},
  {"Fireball Run",  actDodger},
  {"Crossy Hell",      actCrossy},
};

static const char* flappyMenuLabelForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Flappy Curse";
  case PET_DEVIL:
  default:
    return "Flappy Fireball";
  }
}

} // namespace

int uiPlayMenuCount()
{
  return (int)(sizeof(kItems) / sizeof(kItems[0]));
}

const char* uiPlayMenuLabel(int idx)
{
  if (idx < 0 || idx >= uiPlayMenuCount())
    return "";

  if (idx == 0)
    return flappyMenuLabelForPet();

  return kItems[idx].label;
}

bool uiPlayMenuActivate(int idx, InputState& in)
{
  if (idx < 0 || idx >= uiPlayMenuCount()) return false;
  if (!kItems[idx].onSelect) return false;
  kItems[idx].onSelect(in);
  return true;
}