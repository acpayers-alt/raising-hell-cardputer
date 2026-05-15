// inventory.h
#pragma once
#include "savegame.h"
#include <Arduino.h>

// ------------------------------------
// Item Types (non-food items included)
// ------------------------------------
enum ItemType
{
  ITEM_NONE = 0,
  ITEM_SOUL_FOOD,
  ITEM_CURSED_RELIC,
  ITEM_DEMON_BONE,
  ITEM_RITUAL_CHALK,
  ITEM_ELDRITCH_EYE,
  ITEM_FISHING_BAIT
};

bool inventoryUseOne(ItemType type);

// ------------------------------------
// Item Struct
// ------------------------------------
struct Item
{
  ItemType type;
  int quantity;
  String name;
  String description;

  Item() : type(ITEM_NONE), quantity(0), name(""), description("") {}
};

struct ItemDeltas
{
  int hunger;
  int happiness;
  int energy;
  int health;
  int xp;
};

ItemDeltas inventoryPreviewDeltas(ItemType type);

// ------------------------------------
// Inventory Class
// ------------------------------------
class Inventory
{
public:
  static constexpr int MAX_ITEMS = 16;

  Inventory() : selectedIndex(0), itemCount(0), inf(0) {}

  // Pet-specific label/desc helpers (implemented in inventory.cpp)
  const char *getItemLabelForType(ItemType type) const;
  const char *getItemDescForType(ItemType type) const;

  Item items[MAX_ITEMS];
  int selectedIndex;
  int itemCount; // Needed for correct indexing

  void init();
  void save();
  void load();
  void resetToDefaults();
  void clear();

  // Clears the EEPROM-backed inventory mirror so a new pet cannot
  // inherit items after a factory reset or death.
  void wipePersistedEeprom();

  // Writes the current in-memory inventory to EEPROM without marking
  // the SD save "dirty". Useful after loading/unpacking a save.
  void syncEepromNoDirty();

  // ------------------------------------
  // Add & Remove
  // ------------------------------------
  bool addItem(ItemType type, int qty);
  bool removeItem(ItemType type, int qty);

  // ------------------------------------
  // Query
  // ------------------------------------
  int countItems() const;

  bool hasItem(ItemType type) const { return getQuantity(type) > 0; }

  int getQuantity(ItemType type) const
  {
    int total = 0;
    for (int i = 0; i < MAX_ITEMS; ++i)
    {
      if (items[i].type == type && items[i].quantity > 0)
      {
        total += items[i].quantity;
      }
    }
    return total;
  }

  String getItemName(int visibleIndex) const;
  int getItemQty(int index) const;

  // ------------------------------------
  // Use Selected
  // ------------------------------------
  void useSelectedItem();
  int countType(ItemType type) const;

  void toPersist(InvPersist &out) const;
  void fromPersist(const InvPersist &in);

  // Optional if you already have currency elsewhere:
  uint32_t getInf() const { return inf; }
  void setInf(uint32_t v) { inf = v; }

  // Use exactly one item by type (applies the same effects as the Inventory UI)
  bool inventoryUseOne(ItemType type);

private:
  uint32_t inf;
};
