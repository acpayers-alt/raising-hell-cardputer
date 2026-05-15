#include "shop_items.h"

// IMPORTANT: order MUST match graphics.cpp shopItemTypeForIndexLocal():
// 0 Soul Food, 1 Cursed Relic, 2 Demon Bone, 3 Ritual Chalk, 4 Eldritch Eye
const ShopItem availableItems[] = {
    {ITEM_SOUL_FOOD, 40},     {ITEM_CURSED_RELIC, 40},  {ITEM_DEMON_BONE, 40},
    {ITEM_RITUAL_CHALK, 100}, {ITEM_ELDRITCH_EYE, 500}, {ITEM_FISHING_BAIT, 50},
};

const int SHOP_ITEM_COUNT = (int)(sizeof(availableItems) / sizeof(availableItems[0]));
