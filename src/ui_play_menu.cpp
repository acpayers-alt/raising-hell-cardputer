#include "ui_play_menu.h"

#include "mini_games.h" // startFlappyFireball(), startInfernalDodger(), startCrossyRoad()
#include "pet.h"

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

static int uiPlayMenuPhysicalIndexForDisplayIndex(int displayIdx)
{
  if (displayIdx < 0 || displayIdx >= uiPlayMenuCount())
    return -1;

  const PetType current = pet.type;
  int n = 0;

  for (int i = 0; i < uiPlayMenuCount(); ++i)
  {
    if (kItems[i].ownerType == current)
    {
      if (n == displayIdx)
        return i;
      ++n;
    }
  }

  for (int i = 0; i < uiPlayMenuCount(); ++i)
  {
    if (kItems[i].ownerType != current)
    {
      if (n == displayIdx)
        return i;
      ++n;
    }
  }

  return -1;
}

const char *uiPlayMenuLabel(int idx)
{
  const int physicalIdx = uiPlayMenuPhysicalIndexForDisplayIndex(idx);
  if (physicalIdx < 0)
    return "";

  return kItems[physicalIdx].label;
}

PetType uiPlayMenuOwnerPetType(int idx)
{
  const int physicalIdx = uiPlayMenuPhysicalIndexForDisplayIndex(idx);
  if (physicalIdx < 0)
    return PET_DEVIL;

  return kItems[physicalIdx].ownerType;
}

bool uiPlayMenuActivate(int idx)
{
  const int physicalIdx = uiPlayMenuPhysicalIndexForDisplayIndex(idx);
  if (physicalIdx < 0)
    return false;
  if (!kItems[physicalIdx].onSelect)
    return false;

  kItems[physicalIdx].onSelect();
  return true;
}