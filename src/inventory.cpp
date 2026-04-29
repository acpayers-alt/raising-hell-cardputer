#include "inventory.h"
#include "display.h"        // ui_showMessage
#include "pet.h"            // pet reference
#include <EEPROM.h>
#include "graphics.h"
#include "savegame.h"
#include "save_manager.h"
#include "app_state.h"
#include "evolution_flow.h"

// EEPROM mirror of inventory persists across power cycles.
// IMPORTANT: This must be explicitly cleared on factory reset and death,
// otherwise a new pet can inherit the previous pet's inventory.
static constexpr int INVENTORY_EEPROM_ADDR  = 40;
static constexpr int INVENTORY_EEPROM_BYTES = Inventory::MAX_ITEMS * 2; // type + qty per slot

// Forward declarations for theme helpers
static const char* itemNameForPet(ItemType type, PetType petType);
static const char* itemDescForPet(ItemType type, PetType petType);

const char* Inventory::getItemLabelForType(ItemType type) const {
  return itemNameForPet(type, pet.type);
}

// ---------------------------------------------------------------------
// Pet-aware item description accessor (used by shop + future UI)
// ---------------------------------------------------------------------
const char* Inventory::getItemDescForType(ItemType type) const {
  return itemDescForPet(type, pet.type);
}

// =====================================================================
// THEME-AWARE ITEM LABELS (DEVIL vs ELDRITCH)
// =====================================================================
static const char* itemNameForPet(ItemType type, PetType petType) {
  switch (petType) {

    case PET_ELDRITCH:
      switch (type) {
        case ITEM_SOUL_FOOD:     return "Brine Bites";
        case ITEM_CURSED_RELIC:  return "Sunken Idol";
        case ITEM_DEMON_BONE:    return "Abyssal Bone";
        case ITEM_RITUAL_CHALK:  return "Ink Sigil Chalk";
        case ITEM_ELDRITCH_EYE:  return "Staring Pearl";
        default: return "";
      }

    case PET_DEVIL:
    default:
      switch (type) {
        case ITEM_SOUL_FOOD:     return "Soul Food";
        case ITEM_CURSED_RELIC:  return "Cursed Relic";
        case ITEM_DEMON_BONE:    return "Demon Bone";
        case ITEM_RITUAL_CHALK:  return "Ritual Chalk";
        case ITEM_ELDRITCH_EYE:  return "Eldritch Eye";
        default: return "";
      }
  }
}

static const char* itemDescForPet(ItemType type, PetType petType) {
  const bool eld = (petType == PET_ELDRITCH);

  switch (type) {
    case ITEM_SOUL_FOOD:
      if (eld) return "Salt-soaked bites that quiet the deep hunger.";
      return "A small meal that restores hunger.";

    case ITEM_CURSED_RELIC:
      if (eld) return "An idol dredged from ruins that whispers at night.";
      return "A relic steeped in dark energy.";

    case ITEM_DEMON_BONE:
      if (eld) return "A bone pulled from the abyss—slick with brine.";
      return "A bone fragment radiating infernal heat.";

    case ITEM_RITUAL_CHALK:
      if (eld) return "Inky sigil-chalk for circles drawn in seawater.";
      return "Chalk used to draw ritual circles.";

    case ITEM_ELDRITCH_EYE:
      if (eld) return "A pearl that stares back—do not blink.";
      return "A forbidden eye artifact. It watches.";

    case ITEM_NONE:
    default:
      return "";
  }
}

static void applyItemMeta(Item &it, PetType petType) {
  it.name = itemNameForPet(it.type, petType);
  it.description = itemDescForPet(it.type, petType);
}

// Convenience overload for older call sites.
static void applyItemMeta(Item &it) {
  applyItemMeta(it, pet.type);
}

ItemDeltas inventoryPreviewDeltas(ItemType type)
{
  ItemDeltas d;
  d.hunger = 0;
  d.happiness = 0;
  d.energy = 0;
  d.health = 0;
  d.xp = 0;

  switch (type)
  {
    case ITEM_SOUL_FOOD:
      d.hunger    = +30;
      d.happiness = +10;
      d.energy    = +10;
      break;

    // Keep the rest matching your actual effects:
    case ITEM_DEMON_BONE:
      d.energy = +30;   // example - adjust to your real value
      break;

    case ITEM_CURSED_RELIC:
      d.happiness = +30; // example - adjust
      break;

    case ITEM_RITUAL_CHALK:
      d.health = +30;   // example - adjust
      break;

    case ITEM_ELDRITCH_EYE:
      d.xp = +10;       // example - adjust
      break;

    default:
      break;
  }

  return d;
}

// =====================================================================
// INIT
// =====================================================================
void Inventory::init() {
  load();

  // rebuild itemCount AFTER loading
  itemCount = 0;
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0)
      itemCount++;
  }
}

void Inventory::clear() {
  for (int i = 0; i < MAX_ITEMS; ++i) {
    items[i] = Item();
  }
  selectedIndex = 0;
  itemCount = 0;
}

// =====================================================================
// SAVE (2 bytes per slot: type, qty)
// NOTE: If you fully migrate to SD saves, you can stop calling this,
// but leaving it intact keeps legacy EEPROM fallback working.
// =====================================================================
void Inventory::save() {
  saveManagerMarkDirty();

  // Optional EEPROM sync during transition (same idea as pet.save)
  int addr = INVENTORY_EEPROM_ADDR;
  for (int i = 0; i < MAX_ITEMS; i++) {
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

// =====================================================================
// LOAD
// =====================================================================
void Inventory::load() {
  int addr = INVENTORY_EEPROM_ADDR;
  for (int i = 0; i < MAX_ITEMS; i++) {
    items[i].type = (ItemType)EEPROM.read(addr++);
    items[i].quantity = EEPROM.read(addr++);
  }

  // Apply metadata based on pet type
  for (int i = 0; i < MAX_ITEMS; i++) {
    applyItemMeta(items[i], pet.type);
  }
}

void Inventory::wipePersistedEeprom()
{
  // Keep it safe even if another module didn't call EEPROM.begin yet.
  EEPROM.begin(512);
  for (int i = 0; i < INVENTORY_EEPROM_BYTES; i++)
  {
    EEPROM.write(INVENTORY_EEPROM_ADDR + i, 0);
  }
  EEPROM.commit();
}

void Inventory::toPersist(InvPersist &out) const {
  // Write exactly what the save struct can hold.
  // SAVE_INV_MAX_ITEMS comes from savegame.h (InvPersist slots size).
  for (int i = 0; i < SAVE_INV_MAX_ITEMS; i++) {
    if (i < MAX_ITEMS) {
      out.slots[i].type = (uint8_t)items[i].type;
      out.slots[i].qty  = (uint8_t)constrain(items[i].quantity, 0, 255);
    } else {
      // If your runtime inventory is smaller than the save format, pad.
      out.slots[i].type = (uint8_t)ITEM_NONE;
      out.slots[i].qty  = 0;
    }
  }

  out.selectedIndex = (int16_t)selectedIndex;
}

void Inventory::fromPersist(const InvPersist &in) {
  // Clear everything first (important if save has fewer/empty slots)
  for (int i = 0; i < MAX_ITEMS; i++) {
    items[i] = Item(); // resets type/name/desc/qty/etc.
  }

  // Restore only what fits in the runtime inventory
  const int n = (MAX_ITEMS < SAVE_INV_MAX_ITEMS) ? MAX_ITEMS : SAVE_INV_MAX_ITEMS;

  for (int i = 0; i < n; i++) {
    uint8_t t = in.slots[i].type;
    uint8_t q = in.slots[i].qty;

    // Validate type range (use your last enum value)
    if (t > (uint8_t)ITEM_ELDRITCH_EYE) {
      items[i] = Item();
      continue;
    }

    items[i].type     = (ItemType)t;
    items[i].quantity = (int)q;

    if (items[i].type == ITEM_NONE || items[i].quantity <= 0) {
      items[i] = Item();
      continue;
    }

    applyItemMeta(items[i], pet.type);
  }

  // Restore selection safely
  selectedIndex = (int)in.selectedIndex;
  if (selectedIndex < 0) selectedIndex = 0;

  // Rebuild visible itemCount (your init() does this too, but do it here so load works)
  itemCount = 0;
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0) itemCount++;
  }

  // Clamp selectedIndex to visible range
  const int visible = countItems();
  if (visible <= 0) selectedIndex = 0;
  else if (selectedIndex >= visible) selectedIndex = visible - 1;
}

// =====================================================================
// ADD ITEM (stack first)
// =====================================================================
bool Inventory::addItem(ItemType type, int qty) {
  if (qty <= 0) return false;

  // Try to stack
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type == type && items[i].quantity > 0) {
      items[i].quantity += qty;

      // Keep meta consistent with current pet theme
      applyItemMeta(items[i], pet.type);

      saveManagerMarkDirty();   // SD save
      save();                   // EEPROM mirror (optional)
      return true;
    }
  }

  // Otherwise, find empty slot
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type == ITEM_NONE || items[i].quantity == 0) {

      items[i].type     = type;
      items[i].quantity = qty;

      applyItemMeta(items[i], pet.type);

      saveManagerMarkDirty();   // SD save
      save();                   // EEPROM mirror (optional)
      return true;
    }
  }

  return false; // full
}

// =====================================================================
// REMOVE ITEM
// =====================================================================
bool Inventory::removeItem(ItemType type, int qty) {
  if (qty <= 0) return false;

  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type == type && items[i].quantity > 0) {

      items[i].quantity -= qty;

      if (items[i].quantity <= 0) {
        items[i] = Item();   // reset to default
      } else {
        // Keep meta consistent if still present
        applyItemMeta(items[i], pet.type);
      }

      saveManagerMarkDirty();  // SD save
      save();                  // EEPROM mirror (optional)

      return true;
    }
  }
  return false;
}

// =====================================================================
// GET VISIBLE ITEM COUNT
// =====================================================================
int Inventory::countItems() const {
  int c = 0;
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0)
      c++;
  }
  return c;
}

int Inventory::countType(ItemType type) const {
  int total = 0;

  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type == type && items[i].quantity > 0) {
      total += items[i].quantity;
    }
  }

  return total;
}

// =====================================================================
// GET ITEM NAME AND QUANTITY (visible index)
// =====================================================================
String Inventory::getItemName(int visibleIndex) const {
  int visible = 0;
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0) {
      if (visible == visibleIndex) {
        return String(itemNameForPet(items[i].type, pet.type));
      }
      visible++;
    }
  }
  return String("");
}

int Inventory::getItemQty(int index) const {
  int visible = 0;

  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0) {
      if (visible == index)
        return items[i].quantity;
      visible++;
    }
  }
  return 0;
}

// =====================================================================
// USE ITEM (applies stat change & removes one)
// =====================================================================
void Inventory::useSelectedItem() {
  int visible = 0;
  int realIndex = -1;

  // Map visible index to actual slot
  for (int i = 0; i < MAX_ITEMS; i++) {
    if (items[i].type != ITEM_NONE && items[i].quantity > 0) {
      if (visible == selectedIndex) {
        realIndex = i;
        break;
      }
      visible++;
    }
  }

  if (realIndex < 0) return;

  Item &it = items[realIndex];
  if (it.quantity <= 0) return;

  bool changedPet = false;

  // ------------------------------
  // APPLY ITEM EFFECT
  // ------------------------------
  switch (it.type) {
case ITEM_SOUL_FOOD: {
  pet.hunger    = constrain(pet.hunger + 30, 0, 100);
  pet.happiness = constrain(pet.happiness + 10, 0, 100);
  pet.energy    = constrain(pet.energy + 10, 0, 100);

  char msg[48];
  snprintf(msg, sizeof(msg), "Fed %s!", itemNameForPet(it.type, pet.type));
  ui_showMessage(msg);

  changedPet = true;
  break;
}

    case ITEM_CURSED_RELIC:
      pet.happiness = constrain(pet.happiness + 30, 0, 100);
      ui_showMessage("Happiness +20");
      changedPet = true;
      break;

    case ITEM_DEMON_BONE:
      pet.energy = constrain(pet.energy + 30, 0, 100);
      ui_showMessage("Energy +30");
      changedPet = true;
      break;

    case ITEM_RITUAL_CHALK:
      pet.health = constrain(pet.health + 30, 0, 100);
      ui_showMessage("Health +20");
      changedPet = true;
      break;

case ITEM_ELDRITCH_EYE: {
  // Evolution gating:
  // - must not already be max stage
  // - must meet level requirement
  // - mood must be HAPPY or BORED (bored is fine)
  //   (meaning: not sick, not tired, not hungry, not angry)

  if (pet.evoStage >= 3) {
    ui_showMessage("Already at max evolution");
    return; // do NOT consume
  }

  if (!pet.canEvolveNext()) {
    char msg[48];
    snprintf(msg, sizeof(msg), "Need Level %u", (unsigned)pet.nextEvoMinLevel());
    ui_showMessage(msg);
    return; // do NOT consume
  }

  const PetMood mood = pet.getMood();

  if (mood == MOOD_SICK) {
    ui_showMessage("Too sick to evolve");
    return; // do NOT consume
  }

  if (mood != MOOD_HAPPY && mood != MOOD_BORED) {
    ui_showMessage("Must be happy or bored");
    return; // do NOT consume
  }

  // Start evolution sequence (commit happens at the end of the sequence)
  const uint8_t fromStage = pet.evoStage;
  const uint8_t toStage   = (uint8_t)(pet.evoStage + 1);

  ui_showMessage("Evolution has started");
  beginEvolution(fromStage, toStage);

  changedPet = true; // ensures we’ll consume + mark dirty
  break;
}

    default:
      return;
  }

  // consume
  it.quantity--;
  if (it.quantity <= 0) {
    items[realIndex] = Item();
  } else {
    // keep names/descriptions consistent
    applyItemMeta(it, pet.type);
  }

  // SD save (and debounce will handle the actual write)
  saveManagerMarkDirty();

  // Optional EEPROM mirror:
  if (changedPet) pet.save();
  save();
}

bool inventoryUseOne(ItemType type)
{
  if (!g_app.inventory.hasItem(type)) return false;

  // Apply effects using the same pipeline Inventory uses internally
  Item tmp;
  tmp.type     = type;
  tmp.quantity = 1;

  applyItemMeta(tmp, pet.type);

  g_app.inventory.removeItem(type, 1);
  saveManagerMarkDirty();
  return true;
}

// ------------------------------
// Stock new pet inventory with basics
// ------------------------------
void Inventory::resetToDefaults()
{
  // Canonical default loadout (keep in sync with makeDefaultSavePayload()).
  for (int i = 0; i < MAX_ITEMS; i++)
  {
    items[i] = Item();
  }

  items[0].type     = ITEM_SOUL_FOOD;
  items[0].quantity = 3;
  applyItemMeta(items[0], pet.type);

  items[1].type     = ITEM_CURSED_RELIC;
  items[1].quantity = 1;
  applyItemMeta(items[1], pet.type);

  // Persist to EEPROM and mark the save dirty so SD will be updated.
  save();
}

// -----------------------------------------------------------------------------
// Global helper: use exactly one item by type (Feed tab, quick actions, etc.)
// Applies the SAME effects as Inventory::useSelectedItem(), but without UI text.
// -----------------------------------------------------------------------------
static bool applyItemEffect_NoUi(ItemType type)
{
  switch (type)
  {
    case ITEM_SOUL_FOOD:
      // Soul Food: +30 Hunger, +10 Mood, +10 Rest
      pet.hunger    = constrain(pet.hunger + 30, 0, 100);
      pet.happiness = constrain(pet.happiness + 10, 0, 100);
      pet.energy    = constrain(pet.energy + 10, 0, 100);
      return true;

    case ITEM_CURSED_RELIC:
      pet.happiness = constrain(pet.happiness + 20, 0, 100);
      return true;

    case ITEM_DEMON_BONE:
      pet.energy    = constrain(pet.energy + 20, 0, 100);
      return true;

    case ITEM_RITUAL_CHALK:
      pet.health    = constrain(pet.health + 20, 0, 100);
      return true;

case ITEM_ELDRITCH_EYE:
  // Evolution items are handled elsewhere (Inventory UI / evolve flow).
  // Do not consume here.
  return false;

    default:
      return false;
  }
}
