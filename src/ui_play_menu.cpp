#include "ui_play_menu.h"

#include "mini_games.h" // startFlappyFireball(), startInfernalDodger(), startCrossyRoad()

namespace
{

struct MenuItem
{
  const char *label;
  void (*onSelect)();
};

static void actFlappy() { startFlappyFireball(); }

static void actDodger() { startInfernalDodger(); }

static void actCrossy() { startCrossyRoad(); }

static void actAbduction() { startAbductionBeam(); }

static const MenuItem kItems[] = {
    {"Flappy Fireball", actFlappy},
    {"Fireball Run", actDodger},
    {"Crossy Cosmos", actCrossy},
    {"Abduction Beam", actAbduction},
};

static const char *flappyMenuLabelForPet() { return "Flappy Fireball"; }

static const char *dodgerMenuLabelForPet() { return "Fireball Run"; }

static const char *crossyMenuLabelForPet() { return "Crossy Cosmos"; }

static const char *abductionMenuLabelForPet() { return "Abduction Beam"; }
} // namespace

int uiPlayMenuCount() { return (int)(sizeof(kItems) / sizeof(kItems[0])); }

const char *uiPlayMenuLabel(int idx)
{
  if (idx < 0 || idx >= uiPlayMenuCount())
    return "";

  switch (idx)
  {
  case 0:
    return flappyMenuLabelForPet();
  case 1:
    return dodgerMenuLabelForPet();
  case 2:
    return crossyMenuLabelForPet();
  case 3:
    return abductionMenuLabelForPet();
  default:
    return kItems[idx].label;
  }
}

bool uiPlayMenuActivate(int idx)
{
  if (idx < 0 || idx >= uiPlayMenuCount())
    return false;
  if (!kItems[idx].onSelect)
    return false;

  kItems[idx].onSelect();
  return true;
}