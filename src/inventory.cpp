#include "inventory.h"

#include <EEPROM.h>

#include "app_state.h"
#include "display.h"
#include "evolution_flow.h"
#include "graphics.h"
#include "pet.h"
#include "pet_action_gate.h"
#include "save_manager.h"
#include "savegame.h"

static constexpr int INVENTORY_EEPROM_ADDR = 40;
static constexpr int INVENTORY_EEPROM_BYTES = Inventory::MAX_ITEMS * 2;

static const char *itemNameForPet(ItemType type, PetType petType);
static const char *itemDescForPet(ItemType type, PetType petType);
static void applyItemMeta(Item &it, PetType petType);
static bool applyItemEffect_NoUi(ItemType type);

const char *Inventory::getItemLabelForType(ItemType type) const { return itemNameForPet(type, pet.type); }

const char *Inventory::getItemDescForType(ItemType type) const { return itemDescForPet(type, pet.type); }

static const char *itemNameForPet(ItemType type, PetType petType)
{
  switch (petType)
  {
  case PET_ELDRITCH:
    switch (type)
    {
    case ITEM_SOUL_FOOD:
      return "Brine Bites";
    case ITEM_CURSED_RELIC:
      return "Sunken Idol";
    case ITEM_DEMON_BONE:
      return "Abyssal Bone";
    case ITEM_RITUAL_CHALK:
      return "Ink Sigil Chalk";
    case ITEM_ELDRITCH_EYE:
      return "Staring Pearl";
    case ITEM_FISHING_BAIT:
      return "Fishing Bait";
    case ITEM_INFERNAL_PACIFIER:
      return "Infernal Pacifier";
    default:
      return "";
    }

  case PET_DEVIL:
  default:
    switch (type)
    {
    case ITEM_SOUL_FOOD:
      return "Soul Food";
    case ITEM_CURSED_RELIC:
      return "Cursed Relic";
    case ITEM_DEMON_BONE:
      return "Demon Bone";
    case ITEM_RITUAL_CHALK:
      return "Ritual Chalk";
    case ITEM_ELDRITCH_EYE:
      return "Eldritch Eye";
    case ITEM_FISHING_BAIT:
      return "Fishing Bait";
    case ITEM_INFERNAL_PACIFIER:
      return "Infernal Pacifier";
    default:
      return "";
    }
  }
}

static const char *itemDescForPet(ItemType type, PetType petType)
{
  const bool eld = (petType == PET_ELDRITCH);

  switch (type)
  {
  case ITEM_SOUL_FOOD:
    return eld ? "Salt-soaked bites that quiet the deep hunger." : "A small meal that restores hunger.";

  case ITEM_CURSED_RELIC:
    return eld ? "An idol dredged from ruins that whispers at night." : "A relic steeped in dark energy.";

  case ITEM_DEMON_BONE:
    return eld ? "A bone pulled from the abyss—slick with brine." : "A bone fragment radiating infernal heat.";

  case ITEM_RITUAL_CHALK:
    return eld ? "Inky sigil-chalk for circles drawn in seawater." : "Chalk used to draw ritual circles.";

  case ITEM_ELDRITCH_EYE:
    return eld ? "A pearl that stares back—do not blink." : "A forbidden eye artifact. It watches.";

  case ITEM_FISHING_BAIT:
    return "Used to cast a line while fishing.";

  case ITEM_INFERNAL_PACIFIER:
    return "Returns your pet to baby form.";

  case ITEM_NONE:
  default:
    return "";
  }
}

static void applyItemMeta(Item &it, PetType petType)
{
  it.name = itemNameForPet(it.type, petType);
  it.description = itemDescForPet(it.type, petType);
}

static void applyItemMeta(Item &it) { applyItemMeta(it, pet.type); }

ItemDeltas inventoryPreviewDeltas(ItemType type)
{
  ItemDeltas d = {};

  switch (type)
  {
  case ITEM_SOUL_FOOD:
    d.hunger = 30;
    d.happiness = 10;
    break;

  case ITEM_CURSED_RELIC:
    d.happiness = 30;
    break;

  case ITEM_DEMON_BONE:
    d.energy = 30;
    break;

  case ITEM_RITUAL_CHALK:
    d.health = max(0, 100 - pet.health);
    break;

  case ITEM_ELDRITCH_EYE:
    d.xp = 10;
    break;

  case ITEM_FISHING_BAIT:
    break;

  default:
    break;
  }

  return d;
}

void Inventory::init()
{
  load();

  itemCount = 0;
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0)
      itemCount++;
  }
}

void Inventory::clear()
{
  for (int i = 0; i < MAX_ITEMS; ++i)
  {
    items[i] = Item();
  }

  selectedIndex = 0;
  itemCount = 0;
}

void Inventory::save()
{
  saveManagerMarkDirty();

  int addr = INVENTORY_EEPROM_ADDR;
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    EEPROM.write(addr++, (uint8_t)items[i].type);
    EEPROM.write(addr++, (uint8_t)items[i].quantity);
  }

  EEPROM.commit();
}

void Inventory::syncEepromNoDirty()
{
  int addr = INVENTORY_EEPROM_ADDR;
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    EEPROM.write(addr++, (uint8_t)items[i].type);
    EEPROM.write(addr++, (uint8_t)items[i].quantity);
  }

  EEPROM.commit();
}

void Inventory::load()
{
  int addr = INVENTORY_EEPROM_ADDR;
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    items[i].type = (ItemType)EEPROM.read(addr++);
    items[i].quantity = EEPROM.read(addr++);
    applyItemMeta(items[i], pet.type);
  }
}

void Inventory::wipePersistedEeprom()
{
  EEPROM.begin(512);

  for (int i = 0; i < INVENTORY_EEPROM_BYTES; i++)
  {
    EEPROM.write(INVENTORY_EEPROM_ADDR + i, 0);
  }

  EEPROM.commit();
}

void Inventory::toPersist(InvPersist &out) const
{
  for (int i = 0; i < SAVE_INV_MAX_ITEMS; i++)
  {
    if (i < MAX_ITEMS)
    {
      out.slots[i].type = (uint8_t)items[i].type;
      out.slots[i].qty = (uint8_t)constrain(items[i].quantity, 0, 255);
    }
    else
    {
      out.slots[i].type = (uint8_t)ITEM_NONE;
      out.slots[i].qty = 0;
    }
  }

  out.selectedIndex = (int16_t)selectedIndex;
}

void Inventory::fromPersist(const InvPersist &in)
{
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    items[i] = Item();
  }

  const int n = (MAX_ITEMS < SAVE_INV_MAX_ITEMS) ? MAX_ITEMS : SAVE_INV_MAX_ITEMS;

  for (int i = 0; i < n; i++)
  {
    const uint8_t t = in.slots[i].type;
    const uint8_t q = in.slots[i].qty;

    if (t > (uint8_t)ITEM_INFERNAL_PACIFIER)
      continue;

    items[i].type = (ItemType)t;
    items[i].quantity = (int)q;

    if (items[i].type == ITEM_NONE || items[i].quantity <= 0)
    {
      items[i] = Item();
      continue;
    }

    applyItemMeta(items[i], pet.type);
  }

  selectedIndex = (int)in.selectedIndex;
  if (selectedIndex < 0)
    selectedIndex = 0;

  itemCount = countItems();

  if (itemCount <= 0)
    selectedIndex = 0;
  else if (selectedIndex >= itemCount)
    selectedIndex = itemCount - 1;
}

bool Inventory::addItem(ItemType type, int qty)
{
  if (qty <= 0)
    return false;

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type == type && items[i].quantity > 0)
    {
      items[i].quantity += qty;
      applyItemMeta(items[i], pet.type);
      save();
      return true;
    }
  }

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type == ITEM_NONE || items[i].quantity == 0)
    {
      items[i].type = type;
      items[i].quantity = qty;
      applyItemMeta(items[i], pet.type);
      save();
      return true;
    }
  }

  return false;
}

bool Inventory::removeItem(ItemType type, int qty)
{
  if (qty <= 0)
    return false;

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type == type && items[i].quantity > 0)
    {
      items[i].quantity -= qty;

      if (items[i].quantity <= 0)
        items[i] = Item();
      else
        applyItemMeta(items[i], pet.type);

      save();
      return true;
    }
  }

  return false;
}

int Inventory::countItems() const
{
  int c = 0;

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0)
      c++;
  }

  return c;
}

int Inventory::countType(ItemType type) const
{
  int total = 0;

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type == type && items[i].quantity > 0)
      total += items[i].quantity;
  }

  return total;
}

String Inventory::getItemName(int visibleIndex) const
{
  int visible = 0;

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0)
    {
      if (visible == visibleIndex)
        return String(itemNameForPet(items[i].type, pet.type));

      visible++;
    }
  }

  return String("");
}

int Inventory::getItemQty(int index) const
{
  int visible = 0;

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0)
    {
      if (visible == index)
        return items[i].quantity;

      visible++;
    }
  }

  return 0;
}

void Inventory::useSelectedItem()
{
  int visible = 0;
  int realIndex = -1;

  for (int i = 0; i < MAX_ITEMS; i++)
  {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0)
    {
      if (visible == selectedIndex)
      {
        realIndex = i;
        break;
      }

      visible++;
    }
  }

  if (realIndex < 0)
    return;

  Item &it = items[realIndex];
  if (it.quantity <= 0)
    return;

  bool changedPet = false;

  switch (it.type)
  {
  case ITEM_SOUL_FOOD:
  {
    pet.hunger = constrain(pet.hunger + 30, 0, 100);
    pet.happiness = constrain(pet.happiness + 10, 0, 100);
    char msg[48];
    snprintf(msg, sizeof(msg), "Fed %s!", itemNameForPet(it.type, pet.type));
    ui_showMessage(msg);

    changedPet = true;
    break;
  }

  case ITEM_CURSED_RELIC:
    pet.happiness = constrain(pet.happiness + 30, 0, 100);
    ui_showMessage("Happiness +30");
    changedPet = true;
    break;

  case ITEM_DEMON_BONE:
    pet.energy = constrain(pet.energy + 30, 0, 100);
    ui_showMessage("Energy +30");
    changedPet = true;
    break;

  case ITEM_RITUAL_CHALK:
  {
    const int oldHealth = pet.health;
    pet.health = 100;

    ui_showMessage("Restore Full Health");
    Serial.printf("[ITEM] Ritual Chalk %d->%d\n", oldHealth, pet.health);

    changedPet = true;
    break;
  }

  case ITEM_ELDRITCH_EYE:
  {
    if (pet.evoStage >= 3)
    {
      ui_showMessage("Already at max evolution");
      return;
    }

    if (!pet.canEvolveNext())
    {
      char msg[48];
      snprintf(msg, sizeof(msg), "Need Level %u", (unsigned)pet.nextEvoMinLevel());
      ui_showMessage(msg);
      return;
    }

    const PetMood mood = pet.getMood();

    if (uiBlockIfPetConditionBarsAction())
      return;

    const uint8_t fromStage = pet.evoStage;
    const uint8_t toStage = (uint8_t)(pet.evoStage + 1);

    ui_showMessage("Evolution has started");
    beginEvolution(fromStage, toStage);

    changedPet = true;
    break;
  }

  case ITEM_INFERNAL_PACIFIER:
  {
    if (pet.evoStage == 0)
    {
      ui_showMessage("Already baby");
      return;
    }

    const uint8_t oldStage = pet.evoStage;
    pet.setEvoStage(0);

    Serial.printf("[ITEM] Infernal Pacifier evo %u->0\n", (unsigned)oldStage);
    ui_showMessage("Returned to baby form");

    changedPet = true;
    break;
  }

  case ITEM_FISHING_BAIT:
    ui_showMessage("Use this while fishing.");
    return;

  default:
    return;
  }

  it.quantity--;
  if (it.quantity <= 0)
    items[realIndex] = Item();
  else
    applyItemMeta(it, pet.type);

  saveManagerMarkDirty();

  if (changedPet)
    pet.save();

  save();
}

bool inventoryUseOne(ItemType type)
{
  if (!g_app.inventory.hasItem(type))
    return false;

  if (!applyItemEffect_NoUi(type))
    return false;

  g_app.inventory.removeItem(type, 1);
  pet.save();
  saveManagerMarkDirty();
  return true;
}

void Inventory::resetToDefaults()
{
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    items[i] = Item();
  }

  items[0].type = ITEM_SOUL_FOOD;
  items[0].quantity = 3;
  applyItemMeta(items[0], pet.type);

  items[1].type = ITEM_CURSED_RELIC;
  items[1].quantity = 1;
  applyItemMeta(items[1], pet.type);

  save();
}

static bool applyItemEffect_NoUi(ItemType type)
{
  switch (type)
  {
  case ITEM_SOUL_FOOD:
    pet.hunger = constrain(pet.hunger + 30, 0, 100);
    pet.happiness = constrain(pet.happiness + 10, 0, 100);
    return true;

  case ITEM_CURSED_RELIC:
    pet.happiness = constrain(pet.happiness + 30, 0, 100);
    return true;

  case ITEM_DEMON_BONE:
    pet.energy = constrain(pet.energy + 30, 0, 100);
    return true;

  case ITEM_RITUAL_CHALK:
    pet.health = 100;
    return true;

  case ITEM_ELDRITCH_EYE:

  case ITEM_INFERNAL_PACIFIER:
    if (pet.evoStage == 0)
      return false;
    pet.setEvoStage(0);
    return true;

  default:
    return false;
  }
}