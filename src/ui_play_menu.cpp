#include "ui_play_menu.h"

#include "mini_games.h" // startFlappyFireball(), startInfernalDodger(), startCrossyRoad()
#include "pet.h"

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
    {"Crossy Hell", actCrossy},
    {"Abduction Beam", actAbduction},
};

static const char *flappyMenuLabelForPet()
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

static const char *dodgerMenuLabelForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Submarine Run";
  case PET_DEVIL:
  default:
    return "Fireball Run";
  }
}

static const char *crossyMenuLabelForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Crossy Cosmos";
  case PET_DEVIL:
  default:
    return "Crossy Hell";
  }
}

static const char *abductionMenuLabelForPet()
{
  switch (pet.type)
  {
  case PET_ELDRITCH:
    return "Harvest Beam";

  case PET_DEVIL:
  default:
    return "Soul Beam";
  }
}

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