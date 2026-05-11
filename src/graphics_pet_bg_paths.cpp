#include "graphics_pet_bg_paths.h"

static const char *PATH_BG_PET = "/raising_hell/graphics/bg/hell_bg.jpg";

static const char *PATH_BG_DEVIL_BABY  = "/raising_hell/graphics/background/dev/hell_bg.jpg";
static const char *PATH_BG_DEVIL_TEEN  = "/raising_hell/graphics/background/dev/dev_teen_bg.jpg";
static const char *PATH_BG_DEVIL_ADULT = "/raising_hell/graphics/background/dev/dev_ad_bg.jpg";
static const char *PATH_BG_DEVIL_ELDER = "/raising_hell/graphics/background/dev/dev_el_bg.jpg";

static const char *PATH_BG_ELDRITCH_BABY  = "/raising_hell/graphics/background/eld/eld_bg.jpg";
static const char *PATH_BG_ELDRITCH_TEEN  = "/raising_hell/graphics/background/eld/eld_teen_bg.jpg";
static const char *PATH_BG_ELDRITCH_ADULT = "/raising_hell/graphics/background/eld/eld_ad_bg.jpg";
static const char *PATH_BG_ELDRITCH_ELDER = "/raising_hell/graphics/background/eld/eld_el_bg.jpg";

static const char *PATH_BG_ALIEN_BABY = "/raising_hell/graphics/background/al/al_bb_bg.jpg";

const char *bgPathForPet(PetType t)
{
  switch (t)
  {
  case PET_ELDRITCH:
    return PATH_BG_ELDRITCH_BABY;

  case PET_ALIEN:
    return PATH_BG_ALIEN_BABY;

  case PET_DEVIL:
  default:
    return PATH_BG_PET;
  }
}

const char *bgPathForPetWithStage(PetType t, int evoStage)
{
  if (t == PET_DEVIL)
  {
    if (evoStage >= 3)
      return PATH_BG_DEVIL_ELDER;
    if (evoStage == 2)
      return PATH_BG_DEVIL_ADULT;
    if (evoStage == 1)
      return PATH_BG_DEVIL_TEEN;
    return PATH_BG_DEVIL_BABY;
  }

  if (t == PET_ELDRITCH)
  {
    if (evoStage >= 3)
      return PATH_BG_ELDRITCH_ELDER;
    if (evoStage == 2)
      return PATH_BG_ELDRITCH_ADULT;
    if (evoStage == 1)
      return PATH_BG_ELDRITCH_TEEN;
    return PATH_BG_ELDRITCH_BABY;
  }

  if (t == PET_ALIEN)
  {
    return PATH_BG_ALIEN_BABY;
  }

  return bgPathForPet(t);
}