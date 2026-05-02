#include "graphics_set_time_screens.h"

#include "display.h"
#include "graphics.h"
#include "graphics_render_utils.h"
#include "graphics_ui_common.h"

#include "app_state.h"

#include "pet.h"

extern Pet pet;

// externs (already defined elsewhere)
extern int g_setTimeField;
extern bool g_setTimeMode;

extern tm g_setTimeTm;

static void drawSetDateTimePanel(int x, int y, int w, int h, int selectedField)
{
  const uint16_t outline = uiPillOutline(pet.type);

  spr.drawRoundRect(x, y, w, h, 8, outline);
  spr.fillRoundRect(x + 1, y + 1, w - 2, h - 2, 8, TFT_BLACK);

  const int year = g_setTimeTm.tm_year + 1900;
  const int mon = g_setTimeTm.tm_mon + 1;
  const int day = g_setTimeTm.tm_mday;
  const int hh = g_setTimeTm.tm_hour;
  const int mm = g_setTimeTm.tm_min;

  char yy[8], mo[4], dd[4], th[4], tmBuf[4];
  snprintf(yy, sizeof(yy), "%04d", year);
  snprintf(mo, sizeof(mo), "%02d", mon);
  snprintf(dd, sizeof(dd), "%02d", day);
  snprintf(th, sizeof(th), "%02d", hh);
  snprintf(tmBuf, sizeof(tmBuf), "%02d", mm);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  const int baseY = y + 18;
  int cx = x + 8;

  auto drawField = [&](const char *s, int fid)
  {
    spr.drawString(s, cx, baseY);
    const int tw = spr.textWidth(s);

    if (selectedField == fid)
    {
      const int uy = baseY + 15;
      spr.drawFastHLine(cx, uy, tw, TFT_YELLOW);
      spr.drawFastHLine(cx, uy + 1, tw, TFT_YELLOW);
    }

    cx += tw + 4;
  };
  
  // Date
  drawField(yy, 0);
  spr.drawString("-", cx, baseY);
  cx += spr.textWidth("-") + 4;
  drawField(mo, 1);
  spr.drawString("-", cx, baseY);
  cx += spr.textWidth("-") + 4;
  drawField(dd, 2);

  // Spacer
  cx += 10;

  // Time
  drawField(th, 3);
  spr.drawString(":", cx, baseY);
  cx += spr.textWidth(":") + 4;
  drawField(tmBuf, 4);
}

void drawSetTimeScreen()
{
  if (!isScreenOn())
    return;

  drawTopBar();

  const int cx = 0;
  const int cw = screenW;
  const int contentY = TOP_BAR_H;
  const int ch = screenH - TOP_BAR_H;

  spr.fillRect(0, contentY, cw, ch, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.drawString("Set Date & Time", cx + 8, contentY + 6);

  const int panelX = cx + 10;
  const int panelY = contentY + 28;
  const int panelW = cw - 20;
  const int panelH = 42;

  drawSetDateTimePanel(panelX, panelY, panelW, panelH, g_setTimeField);

  const int okW = 84;
  const int okH = 22;
  const int okX = cx + (cw - okW) / 2;
  const int okY = panelY + panelH + 12;

  const bool okSel = (g_setTimeField == 5);
  drawButton(okX, okY, okW, okH, "OK", okSel);

  spr.setTextDatum(BC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("Enter: next | Arrows: +/-", cx + cw / 2, contentY + ch - 2);
  spr.setTextDatum(TL_DATUM);
}