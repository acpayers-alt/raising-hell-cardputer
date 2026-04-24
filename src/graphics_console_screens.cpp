#include "graphics.h"

#include "console.h"
#include "display.h"
#include "graphics_chrome.h"
#include "pet.h"

extern M5Canvas spr;

static constexpr int CONSOLE_INPUT_H = TAB_BAR_H;
static constexpr int CONSOLE_PAD_X = 4;
static constexpr int CONSOLE_PAD_Y = 2;
static constexpr int CONSOLE_INPUT_FONT = 2;

void drawConsoleMenu()
{
  drawTopBar();
  spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);

  spr.drawString("CONSOLE", 6, PET_AREA_Y + 6);
  spr.drawString("Type in Serial Monitor", 6, PET_AREA_Y + 18);
  spr.drawString("ESC: Back", 6, PET_AREA_Y + 30);

  drawTabBar();
}

void drawConsoleScreen()
{
  drawTopBar();

  const int outY = TOP_BAR_H;
  const int outH = SCREEN_H - TOP_BAR_H - CONSOLE_INPUT_H;
  const int inY = TOP_BAR_H + outH;

  const PetUIColorScheme ui = uiSchemeForPet(pet.type);
  const uint16_t inputBg = ui.topBg;
  const uint16_t inputLine = ui.topOutline;

  spr.fillRect(0, outY, SCREEN_W, outH, TFT_BLACK);
  spr.fillRect(0, inY, SCREEN_W, CONSOLE_INPUT_H, inputBg);
  spr.drawFastHLine(0, inY, SCREEN_W, inputLine);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextDatum(TL_DATUM);

  const int lineH = 10;
  const int maxLinesVisible = outH / lineH;

  const int total = consoleGetLineCount();
  const int first = consoleGetFirstVisibleLine(maxLinesVisible);
  int last = first + maxLinesVisible;
  if (last > total)
    last = total;

  int y = outY + 2;
  for (int i = first; i < last; i++)
  {
    const char *s = consoleGetLine(i);
    if (s && *s)
      spr.drawString(s, CONSOLE_PAD_X, y);
    y += lineH;
  }

  if (consoleIsScrolledUp())
  {
    char scrollBuf[24];
    snprintf(scrollBuf, sizeof(scrollBuf), "scroll:%d", consoleGetScrollOffset());

    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.setTextDatum(TR_DATUM);
    spr.drawString(scrollBuf, SCREEN_W - 4, outY + 2);

    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
  }
      
  spr.setTextFont(CONSOLE_INPUT_FONT);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, inputBg);
  spr.setTextDatum(TL_DATUM);

  const char *in = consoleGetInputLine();
  if (!in)
    in = "";

  char full[256];
  snprintf(full, sizeof(full), "> %s", in);

  const int x0 = CONSOLE_PAD_X;
  const int y0 = inY + CONSOLE_PAD_Y;
  const int maxPx = SCREEN_W - (CONSOLE_PAD_X * 2);

  const char *shown = full;
  while (*shown && spr.textWidth(shown) > maxPx)
    shown++;

  spr.drawString(shown, x0, y0);

  spr.setTextFont(1);
  spr.setTextSize(1);
}