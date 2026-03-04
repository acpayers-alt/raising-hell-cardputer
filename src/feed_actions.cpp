#include "feed_actions.h"

#include <Arduino.h>

#include "app_state.h"
#include "graphics.h" // ui_showMessage
#include "pet.h"
#include "save_manager.h"
#include "ui_menu_state.h" // feedMenuIndex

// Apply Soul Food effect + consume exactly one item.
// Returns true only if an item was available and used.
bool consumeOneSoulFood()
{
  if (!g_app.inventory.hasItem(ITEM_SOUL_FOOD))
    return false;

  pet.hunger = constrain(pet.hunger + 20, 0, 100);
  pet.happiness = constrain(pet.happiness + 10, 0, 100);
  pet.energy = constrain(pet.energy + 10, 0, 100);

  g_app.inventory.removeItem(ITEM_SOUL_FOOD, 1);

  pet.lastFedTime = millis();
  saveManagerMarkDirty();
  return true;
}

static const char *foodLabelForCurrentPet()
{
  const char *s = g_app.inventory.getItemLabelForType(ITEM_SOUL_FOOD);
  return (s && *s) ? s : "Soul Food";
}

static void showNoFoodMessage()
{
  char buf[48];
  snprintf(buf, sizeof(buf), "No %s!", foodLabelForCurrentPet());
  ui_showMessage(buf);
}

int consumeSoulFoodUntilFull()
{
  int used = 0;
  while (pet.hunger < 100)
  {
    if (!consumeOneSoulFood())
      break;
    used++;
    if (used > 99)
      break; // safety
  }
  return used;
}

void feedUseSelected()
{
  if (feedMenuIndex == 0)
  {
    if (pet.hunger >= 100)
    {
      ui_showMessage("Already full!");
      return;
    }

    if (!consumeOneSoulFood())
      showNoFoodMessage();
    else
    {
      char buf[48];
      snprintf(buf, sizeof(buf), "Ate %s!", foodLabelForCurrentPet());
      ui_showMessage(buf);
    }
    return;
  }

  if (feedMenuIndex == 1)
  {
    if (pet.hunger >= 100)
    {
      ui_showMessage("Already full!");
      return;
    }

    int used = consumeSoulFoodUntilFull();
    if (used <= 0)
      showNoFoodMessage();
    else
    {
      String msg = "Ate x" + String(used);
      ui_showMessage(msg.c_str());
    }
    return;
  }
}