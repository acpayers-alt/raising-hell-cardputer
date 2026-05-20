#include "ui_play_menu.h"

#include "mini_games.h" // startFlappyFireball(), startInfernalDodger(), startCrossyRoad()

namespace
{

struct MenuItem
{
  const char *label;
  PetType ownerType;
  void (*onSelect)();
};

static void actFlappy() { startFlappyFireball(); }

static void actDodger() { startInfernalDodger(); }

static void actCrossy() { startCrossyRoad(); }

static void actAbduction() { startAbductionBeam(); }

static const MenuItem kItems[] = {
    {"Flappy Fireball", PET_DEVIL, actFlappy},
    {"Fireball Run", PET_DEVIL, actDodger},
    {"Crossy Cosmos", PET_ELDRITCH, actCrossy},
    {"Abduction Beam", PET_ALIEN, actAbduction},
};

} // namespace

int uiPlayMenuCount() { return (int)(sizeof(kItems) / sizeof(kItems[0])); }

const char *uiPlayMenuLabel(int idx)
{
  if (idx < 0 || idx >= uiPlayMenuCount())
    return "";

  return kItems[idx].label;
}

PetType uiPlayMenuOwnerPetType(int idx)
{
  if (idx < 0 || idx >= uiPlayMenuCount())
    return PET_DEVIL;

  return kItems[idx].ownerType;
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