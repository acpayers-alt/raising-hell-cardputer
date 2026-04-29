#include "graphics_tab_menus.h"

#include "graphics.h"

#include <Arduino.h>

#include "app_state.h"
#include "brightness_state.h"
#include "display.h"
#include "feed_menu_state.h"
#include "graphics_chrome.h"
#include "graphics_hud_icons.h"
#include "graphics_nonpet_bg.h"
#include "graphics_shared_utils.h"
#include "graphics_ui_common.h"
#include "inventory.h"
#include "inventory_state.h"
#include "pet.h"
#include "pet_age.h"
#include "save_manager.h"
#include "sdcard.h"
#include "shop_items.h"
#include "shop_screen_state.h"
#include "sound.h"
#include "ui_feed_menu.h"
#include "ui_menu_state.h"
#include "ui_sleep_menu.h"
#include "ui_icons.h"

// Shop item icons (per-pet theme)
static const char *PATH_SHOP_DEV_FOOD = "/raising_hell/graphics/ui/shop_items/dev/dev_food.png";
static const char *PATH_SHOP_DEV_MOOD = "/raising_hell/graphics/ui/shop_items/dev/dev_mood.png";
static const char *PATH_SHOP_DEV_REST = "/raising_hell/graphics/ui/shop_items/dev/dev_rest.png";
static const char *PATH_SHOP_DEV_HEALTH = "/raising_hell/graphics/ui/shop_items/dev/dev_health.png";
static const char *PATH_SHOP_DEV_EVO = "/raising_hell/graphics/ui/shop_items/dev/dev_evo.png";

static const char *PATH_SHOP_ELD_FOOD = "/raising_hell/graphics/ui/shop_items/eld/eld_food.png";
static const char *PATH_SHOP_ELD_MOOD = "/raising_hell/graphics/ui/shop_items/eld/eld_mood.png";
static const char *PATH_SHOP_ELD_REST = "/raising_hell/graphics/ui/shop_items/eld/eld_rest.png";
static const char *PATH_SHOP_ELD_HEALTH = "/raising_hell/graphics/ui/shop_items/eld/eld_health.png";
static const char *PATH_SHOP_ELD_EVO = "/raising_hell/graphics/ui/shop_items/eld/eld_evo.png";

// ============================================================================
// Shop / Sleep / Inventory / Feed
// ============================================================================
// Shop list index -> item type (0..SHOP_ITEM_COUNT-1). SHOP_ITEM_COUNT is Exit.
static ItemType shopItemTypeForIndexLocal(int idx)
{
  switch (idx)
  {
  case 0:
    return ITEM_SOUL_FOOD;
  case 1:
    return ITEM_CURSED_RELIC;
  case 2:
    return ITEM_DEMON_BONE;
  case 3:
    return ITEM_RITUAL_CHALK;
  case 4:
    return ITEM_ELDRITCH_EYE;
  default:
    return ITEM_NONE;
  }
}

void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100, const char *label)
{
  value01_100 = clampi(value01_100, 0, 100);

  const int r = h / 2;
  const int innerX = x + 1;
  const int innerY = y + 1;
  const int innerW = w - 2;
  const int innerH = h - 2;
  const int fillW = (innerW * value01_100) / 100;

  // Outer pill
  spr.fillRoundRect(x, y, w, h, r, outline);

  // Inner dark track
  spr.fillRoundRect(innerX, innerY, innerW, innerH, (innerH / 2), TFT_BLACK);

  // Fill with flat right edge
  if (fillW > 0)
  {
    int fw = fillW;
    if (fw < innerH)
      fw = innerH; // keep tiny values visible as a nub
    if (fw > innerW)
      fw = innerW;

    spr.fillRect(innerX, innerY, fw, innerH, fill);
  }

  // Centered label
  if (label && label[0])
  {
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_WHITE);
    spr.setTextDatum(MC_DATUM);
    spr.drawString(label, x + w / 2, y + h / 2);
    spr.setTextDatum(TL_DATUM);
  }
}

void drawTinyBar(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100)
{
  drawTinyBar(x, y, w, h, fill, outline, value01_100, nullptr);
}

void drawTinyBarV(int x, int y, int w, int h, uint16_t fill, uint16_t outline, int value01_100)
{
  value01_100 = clampi(value01_100, 0, 100);

  spr.drawRect(x, y, w, h, outline);

  const int innerW = w - 2;
  const int innerH = h - 2;
  const int fillH = (innerH * value01_100) / 100;

  spr.fillRect(x + 1, y + 1, innerW, innerH, TFT_BLACK);

  const int fy = y + 1 + (innerH - fillH);
  spr.fillRect(x + 1, fy, innerW, fillH, fill);
}

void drawShopScreen()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = SHOP_ITEM_COUNT;

  if (g_shopScreen.selectedIndex < 0)
    g_shopScreen.selectedIndex = 0;
  if (g_shopScreen.selectedIndex >= SHOP_ITEM_COUNT)
    g_shopScreen.selectedIndex = SHOP_ITEM_COUNT - 1;

  // Windowing for visible rows
  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_shopScreen.selectedIndex, MAX_VISIBLE, start, visCount);

  // Safety: never draw more rows than actually exist
  if (visCount < 0)
    visCount = 0;
  if (visCount > totalItems - start)
    visCount = totalItems - start;

  // ---------------------------------------------------------------------------
  // Layout: left list pills + right detail panel (image + price + effects)
  // ---------------------------------------------------------------------------
  const int margin = 6;
  const int gapLR = 8;

  const int listX = margin;
  const int listW = 118; // match Inventory pills width
  const int listRight = listX + listW;

  const int panelX = listRight + gapLR;
  const int panelW = SCREEN_W - panelX - margin;

  // Pill sizing: match Inventory list style
  int itemH = 20;
  int gapY = 5;

  int totalListH = visCount * itemH + (visCount - 1) * gapY;
  if (totalListH > contentH)
  {
    itemH = 18;
    gapY = 4;
    totalListH = visCount * itemH + (visCount - 1) * gapY;
  }

  int startY = contentY + (contentH - totalListH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalListH);

  const int radius = 10;

  // Draw list pills (name only; no price text here)
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    const int i = start + row;

    if (i < 0 || i >= totalItems)
      continue;

    const int y = startY + row * (itemH + gapY);
    const bool sel = (i == g_shopScreen.selectedIndex);

    const uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    const uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    const uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(listX, y, listW, itemH, radius, fill);
    spr.drawRoundRect(listX, y, listW, itemH, radius, outline);

    const ItemType t = availableItems[i].type;
    const char *itemName = g_app.inventory.getItemLabelForType(t);
    if (!itemName)
      itemName = "";

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextDatum(TL_DATUM);

    const int th = spr.fontHeight();
    const int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(itemName, listX + 8, ty);
  }

  // Scroll arrows (to the right of the left list)
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  const int arrowX = listRight + 2;
  const int arrowUpY = startY - 2;
  const int arrowDownY = startY + totalListH - 10;

  if (start > 0)
    spr.drawString("^", arrowX, arrowUpY);
  if (start + visCount < totalItems)
    spr.drawString("v", arrowX, arrowDownY);

  // ---------------------------------------------------------------------------
  // Right detail panel for selected item
  // ---------------------------------------------------------------------------
  const int panelY = contentY + 6;
  const int panelH = contentH - 12;

  spr.fillRoundRect(panelX, panelY, panelW, panelH, 10, TFT_BLACK);
  spr.drawRoundRect(panelX, panelY, panelW, panelH, 10, TFT_DARKGREY);

  const int pad = 8;

  // Image pinned near the top of the panel
  const int imgW = 64;
  const int imgH = 64;
  const int imgX = panelX + pad;
  const int imgY = panelY + pad - 4;

  const ItemType selType = availableItems[g_shopScreen.selectedIndex].type;

  const bool eldTheme = (pet.type == PET_ELDRITCH);

  const char *imgPath = nullptr;
  switch (selType)
  {
  case ITEM_SOUL_FOOD:
    imgPath = eldTheme ? PATH_SHOP_ELD_FOOD : PATH_SHOP_DEV_FOOD;
    break;
  case ITEM_CURSED_RELIC:
    imgPath = eldTheme ? PATH_SHOP_ELD_MOOD : PATH_SHOP_DEV_MOOD;
    break;
  case ITEM_DEMON_BONE:
    imgPath = eldTheme ? PATH_SHOP_ELD_REST : PATH_SHOP_DEV_REST;
    break;
  case ITEM_RITUAL_CHALK:
    imgPath = eldTheme ? PATH_SHOP_ELD_HEALTH : PATH_SHOP_DEV_HEALTH;
    break;
  case ITEM_ELDRITCH_EYE:
    imgPath = eldTheme ? PATH_SHOP_ELD_EVO : PATH_SHOP_DEV_EVO;
    break;
  default:
    imgPath = nullptr;
    break;
  }

  bool okImg = false;
  if (g_sdReady && imgPath)
    okImg = sprDrawPngFromSD(imgPath, imgX, imgY);
  if (!okImg)
  {
    spr.fillEllipse(imgX + imgW / 2, imgY + imgH / 2, imgW / 2, imgH / 2, TFT_WHITE);
    spr.drawEllipse(imgX + imgW / 2, imgY + imgH / 2, imgW / 2, imgH / 2, TFT_RED);
  }

  // Price (icon + value)
  const int cost = availableItems[g_shopScreen.selectedIndex].price;
  
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Right-aligned anchor
  const int priceRightX = panelX + panelW - 8;
  const int priceY = imgY + (imgH - spr.fontHeight()) / 2;
  
  // Measure text first so we can right-align the whole block
  String priceStr = String(cost);
  const int textW = spr.textWidth(priceStr);
  
  // Icon placement (to the left of text)
  const int iconW = INF_ICON_W;
  const int iconH = INF_ICON_H;
  
  // total width = icon + spacing + text
  const int totalW = iconW + 3 + textW;
  
  // starting X so the whole thing is right-aligned
  const int startX = priceRightX - totalW;
  
  // vertically center icon relative to image block
  const int iconY = imgY + (imgH - iconH) / 2;
  
  // draw icon
  drawHudIconCached(INF_ICON_PATH, startX, iconY);
  
  // draw number
  spr.setTextDatum(TL_DATUM);
  spr.drawString(priceStr, startX + iconW + 3, priceY);
  
  // Effects at the bottom
  String eff;
  switch (selType)
  {
  case ITEM_SOUL_FOOD:
    eff = "-30 Hunger";
    break;
  case ITEM_CURSED_RELIC:
    eff = "+30 Mood";
    break;
  case ITEM_DEMON_BONE:
    eff = "+30 Energy";
    break;
  case ITEM_RITUAL_CHALK:
    eff = "Full Health";
    break;
  case ITEM_ELDRITCH_EYE:
    eff = "Evolve Now";
    break;
  default:
    eff = "";
    break;
  }

  if (eff.length() > 0)
  {
    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    spr.setTextDatum(BC_DATUM);
    const int effectsX = panelX + panelW / 2;
    const int effectsY = panelY + panelH - 2;
    spr.drawString(eff, effectsX, effectsY);
    spr.setTextDatum(TL_DATUM);
  }
}

void drawFeedMenu()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = uiFeedMenuCount();

  g_feedMenu.selectedIndex = clampi(g_feedMenu.selectedIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, g_feedMenu.selectedIndex, MAX_VISIBLE, start, visCount);
  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  if (totalH > contentH)
  {
    itemH = 20;
    gap = 5;
    totalH = visCount * itemH + (visCount - 1) * gap;
  }

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  const int shiftDown = 14;
  const int meterH = 10;
  const int meterGap = 6;

  int startY = contentY + (contentH - totalH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalH);
  startY += shiftDown;

  const int gapTop = startY - contentY;
  int meterY = contentY + (gapTop - meterH) / 2;
  if (meterY < contentY + 2)
    meterY = contentY + 2;

  if (meterY < contentY)
  {
    meterY = contentY;
    startY = meterY + meterH + meterGap;
  }

  if (startY + totalH > contentBottom)
  {
    startY = contentBottom - totalH;
    meterY = startY - meterGap - meterH;
    if (meterY < contentY)
      meterY = contentY;
  }

  const uint16_t colHunger = 0xF800;
  const int meterInset = 16;
  const int meterW = boxW - (meterInset * 2);
  const int meterX = boxX + (boxW - meterW) / 2;

  drawTinyBar(meterX, meterY, meterW, meterH, colHunger, colHunger, pet.hunger);

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    int i = start + row;
    int y = startY + row * (itemH + gap);
    bool sel = (i == g_feedMenu.selectedIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    int cx = boxX + boxW / 2;
    int th = spr.fontHeight();
    int ty = y + (itemH - th) / 2;

    String line = uiFeedMenuLabel(i);
    if (i == 1)
    {
      const int SOUL_FOOD_HUNGER_GAIN = 20;
      int missing = 100 - pet.hunger;
      if (missing < 0)
        missing = 0;
      int needed = (missing + SOUL_FOOD_HUNGER_GAIN - 1) / SOUL_FOOD_HUNGER_GAIN;
      line += " (" + String(needed) + ")";
    }

    spr.setTextColor(textCol, fill);
    spr.drawCentreString(line.c_str(), cx, ty, 2);
  }

  spr.setTextDatum(TL_DATUM);
}

void drawSleepMenu()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = uiSleepMenuCount();
  if (totalItems <= 0)
    return;

  const int selectedIndex = clampi(sleepMenuIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, sleepMenuIndex, MAX_VISIBLE, start, visCount);

  int itemH = 22;
  int gap = 6;

  int totalH = visCount * itemH + (visCount - 1) * gap;
  if (totalH > contentH)
  {
    itemH = 20;
    gap = 5;
    totalH = visCount * itemH + (visCount - 1) * gap;
  }

  int startY = contentY + (contentH - totalH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalH);

  const int boxW = (SCREEN_W * 3) / 4;
  const int boxX = (SCREEN_W - boxW) / 2;
  const int radius = 10;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    int i = start + row;
    int y = startY + row * (itemH + gap);
    bool sel = (i == sleepMenuIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    int cx = boxX + boxW / 2;
    int th = spr.fontHeight();
    int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawCentreString(uiSleepMenuLabel(i), cx, ty, 2);
  }

  spr.setTextDatum(TL_DATUM);
}

static void drawInventoryLeftStatsPanel(int contentY, int contentH, int boxX)
{
  const int panelX = 2;
  const int panelW = boxX - panelX - 2;
  if (panelW < 12 || contentH < 30)
    return;

  const int gapY = 6;
  int barH = (contentH - 2 * gapY) / 3;
  barH = clampi(barH, 14, 20);

  const int totalBarsH = (3 * barH) + (2 * gapY);
  int y0 = contentY + (contentH - totalBarsH) / 2;
  if (y0 < contentY)
    y0 = contentY;

  const int barPadX = 7;
  const int barX = panelX + barPadX;
  const int barW = panelW - (barPadX * 2);
  if (barW < 8)
    return;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood = 0x001F;
  const uint16_t colEnergy = 0x07E0;

  const int rowGap = 10;
  const int rowH = barH + rowGap;

  const int yHunger = y0 + 0 * rowH;
  const int yMood = y0 + 1 * rowH;
  const int yRest = y0 + 2 * rowH;

  drawTinyBar(barX, yHunger, barW, barH, colHunger, colHunger, pet.hunger, "Hunger");
  drawTinyBar(barX, yMood, barW, barH, colMood, colMood, pet.happiness, "Mood");
  drawTinyBar(barX, yRest, barW, barH, colEnergy, colEnergy, pet.energy, "Rest");

  spr.setTextDatum(TL_DATUM);
}

void drawInventoryMenu()
{
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = g_app.inventory.countItems();
  const bool empty = (totalItems <= 0);

  if (g_app.inventory.selectedIndex < 0)
    g_app.inventory.selectedIndex = 0;
  if (g_app.inventory.selectedIndex >= totalItems && !empty)
    g_app.inventory.selectedIndex = totalItems - 1;

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  if (empty)
  {
    start = 0;
    visCount = 1;
  }
  else
  {
    listWindow(totalItems, g_app.inventory.selectedIndex, MAX_VISIBLE, start, visCount);
  }

  // ---------------------------------------------------------------------------
  // Layout: left list pills + right stat readout panel
  // ---------------------------------------------------------------------------
  const int margin = 6;
  const int gapLR = 8;

  const int listX = margin;
  const int listW = 118; // narrower pills like shop
  const int listRight = listX + listW;

  const int panelX = listRight + gapLR;
  const int panelW = SCREEN_W - panelX - margin;

  // Pill sizing
  int itemH = 20;
  int gapY = 5;

  int totalListH = visCount * itemH + (visCount - 1) * gapY;
  if (totalListH > contentH)
  {
    itemH = 18;
    gapY = 4;
    totalListH = visCount * itemH + (visCount - 1) * gapY;
  }

  int startY = contentY + (contentH - totalListH) / 2;
  startY = clampi(startY, contentY, contentBottom - totalListH);

  const int radius = 10;

  // ---------------------------------------------------------------------------
  // Draw list pills (left)
  // ---------------------------------------------------------------------------
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  for (int row = 0; row < visCount; row++)
  {
    int index = empty ? 0 : (start + row);
    int yy = startY + row * (itemH + gapY);
    bool sel = (!empty && index == g_app.inventory.selectedIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(listX, yy, listW, itemH, radius, fill);
    spr.drawRoundRect(listX, yy, listW, itemH, radius, outline);

    String label;
    if (empty)
      label = "(Empty)";
    else
    {
      String name = g_app.inventory.getItemName(index);
      int qty = g_app.inventory.getItemQty(index);
      label = name + " x" + String(qty);
    }

    int th = spr.fontHeight();
    int ty = yy + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawString(label.c_str(), listX + 8, ty);
  }

  // Scroll arrows (to the right of the left list)
  if (!empty)
  {
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);

    const int arrowX = listRight + 2;
    const int arrowUpY = startY - 2;
    const int arrowDownY = startY + totalListH - 10;

    if (start > 0)
      spr.drawString("^", arrowX, arrowUpY);
    if (start + visCount < totalItems)
      spr.drawString("v", arrowX, arrowDownY);
  }

  // ---------------------------------------------------------------------------
  // Right stat readout panel
  // ---------------------------------------------------------------------------
  const int panelY = contentY + 6;
  const int panelH = contentH - 12;

  spr.fillRoundRect(panelX, panelY, panelW, panelH, 10, TFT_BLACK);
  spr.drawRoundRect(panelX, panelY, panelW, panelH, 10, TFT_DARKGREY);

  // Determine hovered item type (and compute stat deltas)
  ItemType hoveredType = ITEM_NONE;

  if (!empty)
  {
    int visible = 0;
    int realIndex = -1;
    for (int i = 0; i < Inventory::MAX_ITEMS; i++)
    {
      if (g_app.inventory.items[i].type != ITEM_NONE && g_app.inventory.items[i].quantity > 0)
      {
        if (visible == g_app.inventory.selectedIndex)
        {
          realIndex = i;
          break;
        }
        visible++;
      }
    }
    if (realIndex >= 0)
      hoveredType = g_app.inventory.items[realIndex].type;
  }

  const bool isEvoItem = (!empty && hoveredType == ITEM_ELDRITCH_EYE);
  const uint16_t evoLevel = pet.nextEvoMinLevel(); // 0 if no further evolution
  const bool evoReady = (evoLevel != 0) && pet.canEvolveNext();

  const ItemDeltas deltas = inventoryPreviewDeltas(hoveredType);

  const int dhunger = deltas.hunger;
  const int dmood = deltas.happiness;
  const int drest = deltas.energy;
  const int dhp = deltas.health;

  // Draw stats as integers, optionally with "+X" appended
  const int pad = 8;
  int x = panelX + pad;
  int y = panelY + pad;

  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  // Eldritch Eye: show evolution message instead of stat lines
  if (isEvoItem)
  {
    // 3-line evolve readout: Title / Level / Availability (colored)
    const int lineGap = 1;

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextDatum(TL_DATUM);

    // Line 1: Title
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString("Evolve Now", x, y);
    y += spr.fontHeight() + lineGap;

    // Line 2: Level requirement
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    if (evoLevel == 0)
    {
      spr.drawString("Level: --", x, y);
    }
    else
    {
      char lvl[24];
      snprintf(lvl, sizeof(lvl), "Level: %u", (unsigned)evoLevel);
      spr.drawString(lvl, x, y);
    }
    y += spr.fontHeight() + lineGap;

    // Line 3: Availability (colored)
    if (evoLevel == 0)
    {
      spr.setTextColor(TFT_RED, TFT_BLACK);
      spr.drawString("Not Available", x, y);
    }
    else
    {
      spr.setTextColor(evoReady ? TFT_YELLOW : TFT_RED, TFT_BLACK);
      spr.drawString(evoReady ? "Available" : "Not Available", x, y);
    }

    spr.setTextDatum(TL_DATUM);
  }
  else
  {
    auto drawLine = [&](const char *label, int base, int delta)
    {
      // Draw base portion first: "Hunger 20"
      char baseBuf[32];
      snprintf(baseBuf, sizeof(baseBuf), "%s %d", label, base);

      spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      spr.drawString(baseBuf, x, y);

      if (!empty && delta != 0)
      {

        // Clamp displayed delta so base+delta never exceeds 100
        int shownDelta = delta;
        if (base >= 100)
        {
          shownDelta = 0;
        }
        else if (base + shownDelta > 100)
        {
          shownDelta = 100 - base;
        }

        if (shownDelta != 0)
        {
          // Compute X position right after base text
          int deltaX = x + spr.textWidth(baseBuf);

          // Yellow if it will do something, red if maxed (base==100)
          uint16_t deltaColor = (base < 100) ? TFT_YELLOW : TFT_RED;

          spr.setTextColor(deltaColor, TFT_BLACK);

          char deltaBuf[16];
          snprintf(deltaBuf, sizeof(deltaBuf), "+%d", shownDelta);

          spr.drawString(deltaBuf, deltaX, y);
        }
        else
        {
          // Optional: if you still want to show "+0" in red when maxed, uncomment:
          int deltaX = x + spr.textWidth(baseBuf);
          spr.setTextColor(TFT_RED, TFT_BLACK);
          spr.drawString("+0", deltaX, y);
        }
      }

      y += spr.fontHeight() + 1; // tight spacing
    };

    drawLine("Hunger", pet.hunger, dhunger);
    drawLine("Mood", pet.happiness, dmood);
    drawLine("Rest", pet.energy, drest);
    drawLine("Health", pet.health, dhp);
  }

  spr.setTextDatum(TL_DATUM);
}
