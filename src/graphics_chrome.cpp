#include "graphics_chrome.h"

#include "app_state.h"
#include "display.h"
#include "graphics_hud_icons.h"
#include "graphics_shared_utils.h"
#include "pet.h"
#include "system_status_state.h"
#include "time_persist.h"
#include "time_state.h"
#include "ui_icons.h"
#include "wifi_time.h"

extern Pet pet;

PetUIColorScheme uiSchemeForPet(PetType t)
{
  switch (t)
  {
  case PET_ELDRITCH:
    return PetUIColorScheme{0x0010, 0x001F, 0xFFFF,

                            0x0010, 0x001F, 0x07FF, 0xFFFF, 0x0000};

  case PET_DEVIL:
  default:
    return PetUIColorScheme{0x2000, 0xF800, 0xFFFF,

                            0x2000, 0xF800, 0xFBE0, 0xFFFF, 0x0000};
  }
}

String formatTime()
{
  if (!timeIsValid())
    return "! --:--";

  if (currentHour < 0 || currentMinute < 0 || currentHour > 23 || currentMinute > 59)
    return "! --:--";

  int h = currentHour;
  bool pm = false;

  if (h == 0)
  {
    h = 12;
    pm = false;
  }
  else if (h == 12)
  {
    pm = true;
  }
  else if (h > 12)
  {
    h -= 12;
    pm = true;
  }

  char buf[12];
  const char *prefix = timeIsDirty() ? "* " : "";
  snprintf(buf, sizeof(buf), "%s%d:%02d %s", prefix, h, currentMinute, pm ? "PM" : "AM");
  return String(buf);
}

static int wifiBarsFromRssi(int rssi)
{
  if (rssi >= -55)
    return 4;
  if (rssi >= -67)
    return 3;
  if (rssi >= -75)
    return 2;
  if (rssi >= -85)
    return 1;
  return 0;
}

static void drawWifiIcon(int x, int y)
{
  const int w = 14;
  const int h = 10;
  const uint16_t col = TFT_WHITE;

  if (!wifiIsEnabled() || !wifiIsConnected())
  {
    int cx = x + (w / 2);
    int cy = y + (h / 2);
    int r = 3;
    spr.drawLine(cx - r, cy - r, cx + r, cy + r, col);
    spr.drawLine(cx + r, cy - r, cx - r, cy + r, col);
    return;
  }

  int bars = wifiBarsFromRssi(wifiRssi());
  for (int i = 0; i < 4; i++)
  {
    int barH = (i + 1) * 2;
    int bx = x + i * 3;
    int by = y + (h - barH);
    if (i < bars)
      spr.fillRect(bx, by, 2, barH, col);
    else
      spr.drawRect(bx, by, 2, barH, col);
  }
}

void drawTopBar()
{
  const PetUIColorScheme ui = uiSchemeForPet(pet.type);
  const uint16_t bg = ui.topBg;
  const uint16_t outline = ui.topOutline;
  const uint16_t text = ui.topText;

  const int padL = 10;
  const int padR = 4;

  const int batW = 18;
  const int batH = 8;
  const int wifiW = 14;
  const int wifiH = 10;

  const int gapTimeToWifi = 4;
  const int gapWifiToBat = 4;

  const int boltW = 8;
  const int gapWifiToBolt = 4;
  const int gapBoltToBat = 4;

  spr.fillRect(0, 0, SCREEN_W, TOP_BAR_H, bg);
  spr.drawFastHLine(0, TOP_BAR_H - 1, SCREEN_W, outline);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(text, bg);

  String t = formatTime();
  int tw = spr.textWidth(t.c_str());

  int batX = SCREEN_W - padR - batW;
  int batY = (TOP_BAR_H - batH) / 2;

  const int boltX = batX - gapBoltToBat - boltW;
  const int boltY = batY + 1;

  int wifiX = batX - gapWifiToBat - wifiW;
  if (usbPowered)
    wifiX = boltX - gapWifiToBolt - wifiW;
  int wifiY = (TOP_BAR_H - wifiH) / 2;

  int timeRightEdge = wifiX - gapTimeToWifi;
  int timeX = timeRightEdge - tw;
  if (timeX < 0)
    timeX = 0;

  spr.setTextDatum(TL_DATUM);
  spr.drawString(t, timeX, (TOP_BAR_H - 8) / 2);

  drawWifiIcon(wifiX, wifiY);

  int pct = batteryPercent;
  if (pct > 100)
    pct = 100;

  spr.drawRect(batX, batY, batW, batH, text);
  spr.drawRect(batX + batW, batY + 2, 2, batH - 4, text);
  spr.fillRect(batX + 1, batY + 1, batW - 2, batH - 2, bg);

  if (pct >= 0)
  {
    int fillW = (batW - 2) * pct / 100;
    fillW = clampi(fillW, 0, batW - 2);
    spr.fillRect(batX + 1, batY + 1, fillW, batH - 2, text);
  }

  if (usbPowered)
  {
    const uint16_t boltCol = 0xFFE0;
    for (int dx = 0; dx <= 1; dx++)
    {
      spr.drawLine(boltX + dx, boltY + 0, boltX + dx + 4, boltY + 3, boltCol);
      spr.drawLine(boltX + dx + 4, boltY + 3, boltX + dx + 1, boltY + 3, boltCol);
      spr.drawLine(boltX + dx + 1, boltY + 3, boltX + dx + 5, boltY + 6, boltCol);
    }
  }

  const int titleMaxRight = timeX - 6;
  const int titleY = (TOP_BAR_H - 8) / 2;

  const char *petName = pet.name;
  if (!petName || !petName[0])
    petName = "Pet";

  const unsigned int inf = (unsigned int)pet.inf;

  static constexpr int TOP_BAR_INF_ICON_GAP = 1;

  char infBuf[16];
  snprintf(infBuf, sizeof(infBuf), "%u", inf);

  char nameBuf[48];
  snprintf(nameBuf, sizeof(nameBuf), "%s - ", petName);

  const int minRight = (titleMaxRight < padL + 10) ? (padL + 10) : titleMaxRight;

  const int nameW = spr.textWidth(nameBuf);
  const int infW = spr.textWidth(infBuf);
  const int iconY = (TOP_BAR_H - INF_ICON_H) / 2;

  const int fullW = nameW + INF_ICON_W + TOP_BAR_INF_ICON_GAP + infW;
  const int shortW = INF_ICON_W + TOP_BAR_INF_ICON_GAP + infW;

  spr.setTextDatum(TL_DATUM);

  if (padL + fullW <= minRight)
  {
    int x = padL;

    // name
    spr.drawString(nameBuf, x, titleY);
    x += nameW;

    // icon
    const int iconX = x;
    drawHudIconCached(INF_ICON_PATH, iconX, iconY);
    // number (locked to icon)
    const int infX = iconX + INF_ICON_W + TOP_BAR_INF_ICON_GAP;
    spr.drawString(infBuf, infX, titleY);
  }
  else if (padL + shortW <= minRight)
  {
    const int iconX = padL;

    drawHudIconCached(INF_ICON_PATH, iconX, iconY);

    const int infX = iconX + INF_ICON_W + TOP_BAR_INF_ICON_GAP;
    spr.drawString(infBuf, infX, titleY);
  }

  spr.setTextDatum(TL_DATUM);
}

static void tabWindow(int total, int current, int maxVisible, int &start, int &count)
{
  count = (total < maxVisible) ? total : maxVisible;
  int half = count / 2;
  start = current - half;
  start = clampi(start, 0, total - count);
}

void drawTabBar()
{
  const int y = SCREEN_H - TAB_BAR_H;

  const PetUIColorScheme ui = uiSchemeForPet(pet.type);

  const uint16_t bg = ui.tabBg;
  const uint16_t outline = ui.tabOutline;
  const uint16_t fillSel = ui.tabFillSel;
  const uint16_t textOff = ui.tabTextOff;
  const uint16_t textOn = ui.tabTextOn;

  constexpr int MAX_VISIBLE_TABS = 5;

  static const char *labels[] = {"PET", "STAT", "FEED", "PLAY", "SLEEP", "INV", "SHOP"};
  const int labelsCount = (int)(sizeof(labels) / sizeof(labels[0]));

  spr.fillRect(0, y, SCREEN_W, TAB_BAR_H, bg);
  spr.drawFastHLine(0, y, SCREEN_W, outline);

  int start = 0, visCount = 0;
  const int totalTabs = TAB_COUNT_INT();
  tabWindow(totalTabs, (int)g_app.currentTab, MAX_VISIBLE_TABS, start, visCount);

  const int slotW = SCREEN_W / visCount;
  const int padX = 2;
  const int padY = 2;
  const int tabW = slotW - padX * 2;
  const int tabH = TAB_BAR_H - padY * 2;
  const int r = 4;

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);

  for (int i = 0; i < visCount; ++i)
  {
    const int tabIndex = start + i;
    const bool selected = (tabIndex == (int)g_app.currentTab);

    const int x = i * slotW + padX;
    const int ty = y + padY;
    const int cx = x + tabW / 2;
    const int cy = ty + tabH / 2;

    if (selected)
    {
      spr.fillRoundRect(x, ty, tabW, tabH, r, fillSel);
      spr.drawRoundRect(x, ty, tabW, tabH, r, outline);
      spr.setTextColor(textOn, fillSel);
    }
    else
    {
      spr.drawRoundRect(x, ty, tabW, tabH, r, outline);
      spr.setTextColor(textOff, bg);
    }

    const char *s = (tabIndex >= 0 && tabIndex < labelsCount) ? labels[tabIndex] : "?";

    int tw = spr.textWidth(s);
    const int th = 8;

    int tx = cx - (tw / 2);
    int tyText = cy - (th / 2);

    spr.setTextDatum(TL_DATUM);
    spr.drawString(s, tx, tyText);
    spr.setTextDatum(MC_DATUM);
  }

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(textOff, bg);

  const int arrowY = y + (TAB_BAR_H / 2) - 4;

  if (start > 0)
  {
    spr.setTextDatum(TL_DATUM);
    spr.drawString("<", 2, arrowY);
  }

  if (start + visCount < totalTabs)
  {
    spr.setTextDatum(TR_DATUM);
    spr.drawString(">", SCREEN_W - 2, arrowY);
  }

  spr.setTextDatum(MC_DATUM);
}