#include "pet_action_gate.h"

#include "graphics.h"
#include "pet.h"
#include "sound.h"

extern Pet pet;

bool uiBlockIfPetConditionBarsAction()
{
  const PetMood mood = pet.getMood();

  switch (mood)
  {
  case MOOD_TIRED:
    ui_showMessage("Too tired for this");
    soundError();
    return true;

  case MOOD_HUNGRY:
    ui_showMessage("Too hungry for this");
    soundError();
    return true;

  case MOOD_SICK:
    ui_showMessage("You are sick, get help!");
    soundError();
    return true;

  default:
    return false;
  }
}