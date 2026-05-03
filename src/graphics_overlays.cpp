#include "graphics_overlays.h"

#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "graphics_ui_common.h"
#include "pet.h"
#include "ui_invalidate.h"
#include "ui_power_menu.h"
#include "ui_runtime.h"

#include <cstring>

extern Pet pet;

static bool g_levelUpPopupActive = false;
static uint16_t g_levelUpPopupLevel = 0;

static bool g_toastActive = false;
static uint32_t g_toastUntilMs = 0;
static char g_toastMsg[128] = {0};

static bool g_alertScreenFlashActive = false;
static uint32_t g_alertScreenFlashUntilMs = 0;
static uint16_t g_alertScreenFlashColor = TFT_BLACK;
static bool g_alertScreenFlashLatched = false;

void uiResetLevelUpPopupState()
{
  g_levelUpPopupActive = false;
  g_levelUpPopupLevel = 0;
}

void ui_drawMessageWindow(const char *title, const char *line1, const char *line2, bool maskLine2, bool showCursor)
{
  if (!isScreenOn())
    return;

  spr.fillRect(0, 0, screenW, screenH, TFT_BLACK);

  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);
  const int boxH = 74;
  const int x = pad;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(title ? title : "", screenW / 2, y + 8);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(line1 ? line1 : "", screenW / 2, y + 28);

  char shown[40];
  shown[0] = '\0';

  if (line2)
  {
    if (maskLine2)
    {
      size_t n = strnlen(line2, 32);
      if (n > 32)
        n = 32;
      for (size_t i = 0; i < n; i++)
        shown[i] = '*';
      shown[n] = '\0';
    }
    else
    {
      strncpy(shown, line2, sizeof(shown) - 1);
      shown[sizeof(shown) - 1] = '\0';
    }
  }

  if (showCursor)
  {
    const int inX = x + 12;
    const int inY = y + 40;
    const int inW = boxW - 24;
    const int inH = 20;

    const uint16_t inputOutline = uiModalOutline(pet.type);
    spr.drawRoundRect(inX, inY, inW, inH, 6, inputOutline);

    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);
    spr.drawString(shown, inX + 6, inY + 4);

    int cx = inX + 6 + spr.textWidth(shown);
    spr.fillRect(cx, inY + 4, 2, 12, TFT_WHITE);

    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(BC_DATUM);
    spr.drawString("ENTER: Next   MENU/ESC: Cancel", screenW / 2, y + boxH - 6);
  }
  else
  {
    spr.setTextFont(2);
    spr.setTextSize(1);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextDatum(TC_DATUM);
    spr.drawString(shown, screenW / 2, y + 46);

    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(BC_DATUM);
    spr.drawString("ENTER: Next   MENU/ESC: Cancel", screenW / 2, y + boxH - 6);
  }

  spr.setTextDatum(TL_DATUM);
}

void uiShowLevelUpPopup(uint16_t newLevel)
{
  g_levelUpPopupActive = true;
  g_levelUpPopupLevel = newLevel;
  invalidateBackgroundCache();
  requestUIRedraw();
}

bool uiIsLevelUpPopupActive()
{
  return g_levelUpPopupActive;
}

void uiDismissLevelUpPopup()
{
  g_levelUpPopupActive = false;
  invalidateBackgroundCache();
  requestUIRedraw();
}

void uiDrawLevelUpPopup()
{
  if (!g_levelUpPopupActive)
    return;

  const uint16_t outline = uiModalOutline(pet.type);

  const int boxW = 168;
  const int boxH = 56;
  const int x = (screenW - boxW) / 2;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, outline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("LEVEL UP!", screenW / 2, y + 6);

  char line1[32];
  snprintf(line1, sizeof(line1), "Reached Level %u", (unsigned)g_levelUpPopupLevel);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  spr.drawString(line1, screenW / 2, y + 28);

  spr.setTextDatum(TL_DATUM);
}

static void uiShowToastInternal(const char *msg, uint32_t durationMs)
{
  if (!msg)
    return;

  strncpy(g_toastMsg, msg, sizeof(g_toastMsg) - 1);
  g_toastMsg[sizeof(g_toastMsg) - 1] = '\0';

  g_toastActive = true;
  g_toastUntilMs = durationMs ? (millis() + durationMs) : 0;

  requestUIRedraw();
}

void ui_showMessage(const char *msg)
{
  uiShowToastInternal(msg, 900);
}

void ui_showTimedMessage(const char *msg, uint32_t durationMs)
{
  uiShowToastInternal(msg, durationMs);
}

bool uiToastIsActive()
{
  return g_toastActive;
}

bool uiToastIsPersistent()
{
  return g_toastActive && g_toastUntilMs == 0;
}

void uiDismissToast()
{
  if (!g_toastActive)
    return;

  g_toastActive = false;
  g_toastUntilMs = 0;
  g_toastMsg[0] = '\0';
  requestFullUIRedraw();
}

void ui_showSuccessMessage(const char *msg)
{
  uiShowToastInternal(msg, 1200);
}

void uiBeginAlertScreenFlash(uint8_t r, uint8_t g, uint8_t b)
{
  g_alertScreenFlashColor = spr.color565(r, g, b);
  g_alertScreenFlashActive = true;
  g_alertScreenFlashLatched = true;
  g_alertScreenFlashUntilMs = 0;
  requestUIRedraw();
}

void uiEndAlertScreenFlash()
{
  g_alertScreenFlashActive = false;
  g_alertScreenFlashLatched = false;
  g_alertScreenFlashUntilMs = 0;
  requestFullUIRedraw();
}

void uiDrawAlertScreenFlashOverlay()
{
  if (!g_alertScreenFlashActive)
    return;

  if (!g_alertScreenFlashLatched)
  {
    const uint32_t now = millis();
    if ((int32_t)(now - g_alertScreenFlashUntilMs) >= 0)
    {
      g_alertScreenFlashActive = false;
      g_alertScreenFlashUntilMs = 0;
      requestFullUIRedraw();
      return;
    }
  }

  if (!isScreenOn())
    return;

  spr.fillRect(0, 0, SCREEN_W, SCREEN_H, g_alertScreenFlashColor);
}

void uiDrawToastOverlay()
{
  if (!g_toastActive)
    return;

  const uint32_t now = millis();
  if (g_toastUntilMs != 0 && (int32_t)(now - g_toastUntilMs) >= 0)
  {
    g_toastActive = false;
    g_toastUntilMs = 0;
    g_toastMsg[0] = '\0';
    requestFullUIRedraw();
    return;
  }

  if (!isScreenOn())
    return;

  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = screenW - (pad * 2);

  int lineCount = 1;
  for (const char *p = g_toastMsg; *p; ++p)
  {
    if (*p == '\n')
      lineCount++;
  }

  if (lineCount < 1)
    lineCount = 1;
  if (lineCount > 4)
    lineCount = 4;

  const int lineH = 16;
  const int boxH = 18 + (lineCount * lineH);
  const int x = pad;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);

  char line[96];
  const char *start = g_toastMsg;
  int drawLine = 0;
  int textY = y + 8;

  while (*start && drawLine < lineCount)
  {
    const char *end = strchr(start, '\n');
    const size_t len = end ? (size_t)(end - start) : strlen(start);
    const size_t copyLen = (len < sizeof(line) - 1) ? len : sizeof(line) - 1;

    memcpy(line, start, copyLen);
    line[copyLen] = '\0';

    spr.drawString(line, screenW / 2, textY + (drawLine * lineH));

    if (!end)
      break;

    start = end + 1;
    drawLine++;
  }

  spr.setTextDatum(TL_DATUM);
  
  requestUIRedraw();
}

static void drawPowerMenuOverlay()
{
  const uint16_t modalOutline = uiModalOutline(pet.type);
  const uint16_t selFill = uiPillOutline(pet.type);
  const uint16_t selText = TFT_BLACK;

  const int boxW = 200;
  const int boxH = 92;
  const int x = (screenW - boxW) / 2;
  const int y = (screenH - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 10, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 10, modalOutline);

  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString("POWER MENU", screenW / 2, y + 8);

  const int itemCount = uiPowerMenuCount();

  const int listX = x + 16;
  int yy = y + 26;

  for (int i = 0; i < itemCount; i++)
  {
    const bool sel = (i == g_app.powerMenuIndex);

    if (sel)
    {
      spr.fillRoundRect(listX - 6, yy - 2, boxW - 32, 18, 6, selFill);
      spr.setTextColor(selText, selFill);
    }
    else
    {
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
    }

    spr.setTextDatum(TL_DATUM);
    spr.drawString(uiPowerMenuLabel(i), listX, yy);
    yy += 20;
  }

  spr.setTextDatum(TL_DATUM);
}

void drawPowerMenu()
{
  drawPowerMenuOverlay();
}