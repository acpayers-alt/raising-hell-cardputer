#include "mg_pause_menu.h"

#include "display.h"
#include "mg_pause_core.h" // mgPauseIsPaused / mgPauseChoice
#include "graphics.h"

void mgDrawPauseOverlay()
{
  if (!mgPauseIsPaused()) return;

  const uint8_t choice = mgPauseChoice();

  const int gW = (screenW > 0) ? screenW : 240;
  const int gH = (screenH > 0) ? screenH : 135;

  const int bw = gW - 40;
  const int bh = 70;
  const int bx = 20;
  const int by = (gH - bh) / 2;

  spr.fillRect(bx, by, bw, bh, TFT_BLACK);
  spr.drawRect(bx, by, bw, bh, TFT_DARKGREY);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawCentreString("PAUSED", gW / 2, by + 8, 2);

  spr.setTextDatum(TL_DATUM);
  const int ox = bx + 12;
  const int oy = by + 28;

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString((choice == 0) ? "> Continue" : "  Continue", ox, oy, 2);
  spr.drawString((choice == 1) ? "> Exit"     : "  Exit",     ox, oy + 20, 2);

  spr.setTextDatum(BC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
}