#include "graphics_tab_screens.h"

#include "graphics.h"

#include <time.h>

#include "display.h"
#include "graphics_nonpet_bg.h"
#include "graphics_sd_draw.h"
#include "graphics_shared_utils.h"
#include "graphics_ui_common.h"
#include "pet.h"
#include "pet_age.h"
#include "save_manager.h"
#include "sdcard.h"
#include "ui_play_menu.h"
#include "ui_menu_state.h"

extern M5Canvas spr;

const char *getBioStatusImagePath();

void drawStatsTab(bool redrawBg)
{
  (void)redrawBg;
  if (!isScreenOn())
    return;

  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int pad = 6;
  const int cardX = pad;
  const int cardY = contentY + 2;
  const int cardW = SCREEN_W - pad * 2;
  const int cardH = contentH - 4;

  spr.fillRoundRect(cardX, cardY, cardW, cardH, 8, TFT_BLACK);
  spr.drawRoundRect(cardX, cardY, cardW, cardH, 8, TFT_DARKGREY);

  const int nameX = cardX + 10;
  const int nameY = cardY + 8;

  char nmBuf[64];
  pet.buildDisplayName(nmBuf, sizeof(nmBuf));

  String titleLine = String(nmBuf);
  titleLine.trim();
  if (titleLine.length() == 0)
    titleLine = "(NO NAME)";

  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_YELLOW, TFT_BLACK);
  spr.drawString(titleLine, nameX, nameY);

  const int dividerY = nameY + spr.fontHeight() + 4;
  spr.drawFastHLine(cardX + 10, dividerY, cardW - 20, TFT_DARKGREY);

  spr.setTextFont(1);
  spr.setTextSize(1);
  spr.setTextDatum(TL_DATUM);

  const int bodyPad = 8;
  const int bodyX = cardX + bodyPad;
  const int bodyY = dividerY + bodyPad;
  const int bodyW = cardW - (bodyPad * 2);
  const int bodyH = (cardY + cardH) - bodyY - bodyPad;

  const int desiredBio = 48;
  const int bioSize = (bodyH < desiredBio) ? bodyH : desiredBio;
  const int bioX = bodyX;
  const int bioY = bodyY + (bodyH - bioSize) / 2;

  spr.drawRoundRect(bioX - 1, bioY - 1, bioSize + 2, bioSize + 2, 6, TFT_DARKGREY);

  const char *bioPath = getBioStatusImagePath();

  if (g_sdReady)
  {
    sprDrawPngFromSD(bioPath, bioX, bioY);
  }
  else
  {
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.drawString("NO IMG", bioX + 8, bioY + (bioSize / 2) - 4);
  }

  const int textX = bioX + bioSize + 12;
  const int textY = bodyY;
  const int rowH = 13;

  auto drawKV = [&](int px, int py, const char *key, const char *val)
  {
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);

    char kbuf[24];
    snprintf(kbuf, sizeof(kbuf), "%s:", key);
    spr.drawString(kbuf, px, py, 1);

    int vx = px + spr.textWidth(kbuf) + 4;
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.drawString(val ? val : "", vx, py, 1);
  };

  char buf[40];

  {
    uint32_t birth = saveManagerGetBirthEpoch();
    int64_t now = (int64_t)time(nullptr);
    AgeParts a = calcAgeParts((int64_t)birth, now);
    formatAgeString(buf, sizeof(buf), a, false);
  }
  drawKV(textX, textY + 0 * rowH, "Age", buf);

  {
    const int curLevel = pet.level;
    const uint16_t evolveLevel = pet.nextEvoMinLevel();
    const bool evolutionAvailable = pet.canEvolveNext();
    const int y = textY + 1 * rowH;

    spr.setTextDatum(TL_DATUM);
    spr.setTextFont(1);
    spr.setTextSize(1);

    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    char kbuf[24];
    snprintf(kbuf, sizeof(kbuf), "%s:", "Level");
    spr.drawString(kbuf, textX, y, 1);

    const int vx = textX + spr.textWidth(kbuf) + 4;

    if (evolveLevel == 0)
    {
      char vbuf[32];
      snprintf(vbuf, sizeof(vbuf), "%d", curLevel);
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(vbuf, vx, y, 1);
    }
    else if (evolutionAvailable)
    {
      char left[16];
      snprintf(left, sizeof(left), "%d (", curLevel);

      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(left, vx, y, 1);

      int w = spr.textWidth(left);

      spr.setTextColor(TFT_YELLOW, TFT_BLACK);
      spr.drawString("Evo Ready!", vx + w, y, 1);

      w += spr.textWidth("Evo Ready!");

      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(")", vx + w, y, 1);
    }
    else
    {
      char vbuf[64];
      snprintf(vbuf, sizeof(vbuf), "%d (%u to evolve)", curLevel, (unsigned)evolveLevel);
      spr.setTextColor(TFT_WHITE, TFT_BLACK);
      spr.drawString(vbuf, vx, y, 1);
    }
  }

  {
    const uint32_t need = pet.xpForNextLevel();
    if (need > 0)
      snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)pet.xp, (unsigned long)need);
    else
      snprintf(buf, sizeof(buf), "%lu", (unsigned long)pet.xp);
  }
  drawKV(textX, textY + 2 * rowH, "XP", buf);

  {
    const char *cond = "Happy";
    uint16_t condColor = TFT_GREEN;

    const int SICK_HP = 60;
    const int HUNGRY_LEVEL = 30;
    const int TIRED_EN = 30;
    const int ANGRY_HAPPY = 30;
    const int BORED_HAPPY = 60;

    if (pet.health < SICK_HP)
    {
      cond = "Sick";
      condColor = TFT_RED;
    }
    else if (pet.hunger <= HUNGRY_LEVEL)
    {
      cond = "Hungry";
      condColor = TFT_YELLOW;
    }
    else if (pet.energy <= TIRED_EN)
    {
      cond = "Tired";
      condColor = TFT_YELLOW;
    }
    else if (pet.happiness <= ANGRY_HAPPY)
    {
      cond = "Angry";
      condColor = TFT_YELLOW;
    }
    else if (pet.happiness < BORED_HAPPY)
    {
      cond = "Bored";
      condColor = TFT_GREEN;
    }

    spr.setTextDatum(TL_DATUM);
    spr.setTextFont(1);
    spr.setTextSize(1);

    char kbuf[24];
    snprintf(kbuf, sizeof(kbuf), "%s:", "Condition");
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.drawString(kbuf, textX, textY + 3 * rowH, 1);

    const int vx = textX + spr.textWidth(kbuf) + 4;
    spr.setTextColor(condColor, TFT_BLACK);
    spr.drawString(cond, vx, textY + 3 * rowH, 1);
  }
}

void drawPlayTab(bool redrawBg)
{
  if (!isScreenOn())
    return;

  (void)redrawBg;

  drawNonPetTabBackground();
  drawTopBar();
  drawTabBar();

  const int contentY = TOP_BAR_H;
  const int contentH = SCREEN_H - TOP_BAR_H - TAB_BAR_H;
  const int contentBottom = contentY + contentH;

  const int totalItems = uiPlayMenuCount();

  if (totalItems <= 0)
  {
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(TFT_RED, TFT_BLACK);
    spr.drawString("No games available", SCREEN_W / 2, SCREEN_H / 2);
    return;
  }

  const int selectedIndex = clampi(playMenuIndex, 0, totalItems - 1);

  constexpr int MAX_VISIBLE = 3;
  int start = 0, visCount = 0;
  listWindow(totalItems, selectedIndex, MAX_VISIBLE, start, visCount);
  
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
    int index = start + row;
    int y = startY + row * (itemH + gap);
    bool sel = (index == selectedIndex);

    uint16_t outline = sel ? uiPillOutline(pet.type) : TFT_DARKGREY;
    uint16_t fill = sel ? uiPillFillSelected(pet.type) : TFT_BLACK;
    uint16_t textCol = sel ? TFT_WHITE : TFT_LIGHTGREY;

    spr.fillRoundRect(boxX, y, boxW, itemH, radius, fill);
    spr.drawRoundRect(boxX, y, boxW, itemH, radius, outline);

    int cx = boxX + boxW / 2;
    int th = spr.fontHeight();
    int ty = y + (itemH - th) / 2;

    spr.setTextColor(textCol, fill);
    spr.drawCentreString(uiPlayMenuLabel(index), cx, ty, 2);
  }

  if (start > 0 || (start + visCount < totalItems))
  {
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    spr.setTextDatum(TL_DATUM);

    const int arrowX = boxX + boxW + 6;
    const int arrowUpY = startY - 2;
    const int arrowDownY = startY + totalH - 10;

    if (start > 0)
      spr.drawString("^", arrowX, arrowUpY);
    if (start + visCount < totalItems)
      spr.drawString("v", arrowX, arrowDownY);
  }

  spr.setTextDatum(TL_DATUM);
}