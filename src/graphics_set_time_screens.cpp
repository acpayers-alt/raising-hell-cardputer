#include "graphics_set_time_screens.h"

#include "display.h"
#include "graphics.h"
#include "graphics_render_utils.h"
#include "graphics_ui_common.h"

#include "app_state.h"

#include "pet.h"
#include "time_editor_state.h"
#include "timezone.h"

extern Pet pet;

static void drawSetDateTimePanel(int x, int y, int w, int h, int selectedField)
{
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

  const int baseY = y + 2;

  int totalW = 0;
  totalW += spr.textWidth(yy) + 4;
  totalW += spr.textWidth("-") + 4;
  totalW += spr.textWidth(mo) + 4;
  totalW += spr.textWidth("-") + 4;
  totalW += spr.textWidth(dd) + 10;
  totalW += spr.textWidth(th) + 4;
  totalW += spr.textWidth(":") + 4;
  totalW += spr.textWidth(tmBuf);

  int cx = x + (w - totalW) / 2;

  auto drawField = [&](const char *s, int fid)
  {
    const int tw = spr.textWidth(s);
    const bool selected = (selectedField == fid);

    if (selected)
      spr.setTextColor(TFT_YELLOW, TFT_BLACK);

    spr.drawString(s, cx, baseY);

    if (selected)
    {
      const int uy = baseY + 16;
      spr.drawFastHLine(cx, uy, tw, TFT_YELLOW);
      spr.drawFastHLine(cx, uy + 1, tw, TFT_YELLOW);
    }

    if (selected)
      spr.setTextColor(TFT_WHITE, TFT_BLACK);

    cx += tw + 4;
  };

  drawField(yy, 0);
  spr.drawString("-", cx, baseY);
  cx += spr.textWidth("-") + 4;

  drawField(mo, 1);
  spr.drawString("-", cx, baseY);
  cx += spr.textWidth("-") + 4;

  drawField(dd, 2);

  cx += 10;

  drawField(th, 3);
  spr.drawString(":", cx, baseY);
  cx += spr.textWidth(":") + 4;

  drawField(tmBuf, 4);

  const int tzY = baseY + 23;
  const bool tzSel = (selectedField == 5);

  const char *tzLabel = tzName((uint8_t)tzIndex);
  if (!tzLabel || !tzLabel[0])
    tzLabel = "Timezone";

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(tzSel ? TFT_YELLOW : TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(tzLabel, x + w / 2, tzY);

  if (tzSel)
  {
    const int tw = spr.textWidth(tzLabel);
    const int ux = x + (w - tw) / 2;
    spr.drawFastHLine(ux, tzY + 16, tw, TFT_YELLOW);
    spr.drawFastHLine(ux, tzY + 17, tw, TFT_YELLOW);
  }

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
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

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.drawString("Set Date & Time", cx + cw / 2, contentY + 4);
  spr.setTextDatum(TL_DATUM);

  const int panelX = cx + 10;
  const int panelY = contentY + 22;
  const int panelW = cw - 20;
  const int panelH = 42;

  drawSetDateTimePanel(panelX, panelY, panelW, panelH, g_setTimeField);

  const int okW = 84;
  const int okH = 22;
  const int okX = cx + (cw - okW) / 2;

  const int okY = panelY + panelH + 2;
  const bool okSel = (g_setTimeField == 6);
  drawButton(okX, okY, okW, okH, "OK", okSel);

  spr.setTextDatum(BC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString("L/R select | U/D adjust", cx + cw / 2, contentY + ch - 2);
  spr.setTextDatum(TL_DATUM);
}