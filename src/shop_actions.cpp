#include "shop_actions.h"

#include <cstdio>

#include "app_state.h"
#include "currency.h"
#include "graphics.h" // ui_showMessage
#include "save_manager.h"
#include "shop_items.h"

bool shopBuyItem(int idx)
{
  if (SHOP_ITEM_COUNT <= 0)
    return false;

  if (idx < 0)
    idx = 0;
  if (idx >= SHOP_ITEM_COUNT)
    idx = SHOP_ITEM_COUNT - 1;

  const ShopItem &it = availableItems[idx];

  if (!spendInf(it.price))
  {
    ui_showMessage("Not enough INF!");
    return false;
  }

  const int qty = (it.type == ITEM_FISHING_BAIT) ? 3 : 1;
  g_app.inventory.addItem(it.type, qty);
  saveManagerMarkDirty();

  char msg[48];
  const char *nm = g_app.inventory.getItemLabelForType(it.type);
  if (!nm)
    nm = "";
  if (qty > 1)
    std::snprintf(msg, sizeof(msg), "Purchased %s x%d", nm, qty);
  else
    std::snprintf(msg, sizeof(msg), "Purchased %s", nm);
  ui_showMessage(msg);

  return true;
}