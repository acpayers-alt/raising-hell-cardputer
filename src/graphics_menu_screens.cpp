#include "graphics_menu_screens.h"
#include "graphics.h"
#include <M5GFX.h>

#include "asset_ota.h"
#include "app_state.h"
#include "pet.h"
#include "save_manager.h"
#include "ui_defs.h"
#include "ui_state_backup_pet_list.h"
#include "ui_state_import_pet_list.h"
#include "ui_state_title_menu.h"
#include "version.h"

#include <Arduino.h>
#include <ctype.h>
#include <time.h>

#include "display.h"
#include "graphics_sd_draw.h"
#include "graphics_shared_utils.h"

// Pull in shared graphics globals from graphics.cpp
extern M5Canvas spr;

extern bool g_sdReady;

// Local copy for title background.
// Keep this local for now rather than pulling shared path constants around.
static const char *PATH_BG_SPLASH = "/raising_hell/graphics/background/flow/rh_splash.jpg";

// Provided by graphics.cpp
bool isScreenOn();

static void drawTitleMenuText(M5Canvas &dst, const char *text, int x, int y, uint8_t font, uint16_t fg,
  textdatum_t datum)
{
if (!text || !*text)
return;

dst.setTextFont(font);
dst.setTextSize(1);
dst.setTextDatum(datum);
dst.setTextColor(fg);
dst.drawString(text, x, y, font);
dst.setTextDatum(TL_DATUM);
}

void drawImportPetListScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  if (uiImportPetListConfirmDeleteActive())
  {
    const int idx = uiImportPetListConfirmDeleteIndex();
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Delete this stored pet?", SCREEN_W / 2, SCREEN_H / 2 - 10, 2);

    spr.setTextColor(idx == 0 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("YES", SCREEN_W / 2 - 30, SCREEN_H / 2 + 10, 2);

    spr.setTextColor(idx == 1 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("NO", SCREEN_W / 2 + 30, SCREEN_H / 2 + 10, 2);
    return;
  }

  if (uiImportPetListActionMenuActive())
  {
    const int idx = uiImportPetListActionIndex();
    const char *items[3] = {"Retrieve", "Delete", "Cancel"};

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Stored Pet Options", SCREEN_W / 2, 24, 2);

    for (int i = 0; i < 3; ++i)
    {
      spr.setTextColor(i == idx ? TFT_YELLOW : TFT_WHITE);
      spr.drawString(items[i], SCREEN_W / 2, 52 + (i * 18), 2);
    }
    return;
  }

  const int rowH = 18;
  const int startY = 20;

  const int count = uiImportPetListCount();
  const int visibleCount = uiImportPetListVisibleCount();
  const int selectedIdx = uiImportPetListSelected();
  const int windowStart = uiImportPetListWindowStart();

  if (count <= 0)
  {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("No stored pets found", SCREEN_W / 2, SCREEN_H / 2, 2);
    return;
  }

  for (int i = 0; i < visibleCount; ++i)
  {
    const int y = startY + (i * rowH);
    const bool selected = ((windowStart + i) == selectedIdx);
    const PetExportEntry &e = uiImportPetListGetVisible(i);

    bool isCurrent = false;
    if (pet.getName()[0] && strcmp(e.name, pet.getName()) == 0)
    {
      isCurrent = true;
    }

    spr.setTextDatum(TL_DATUM);

    uint16_t nameColor = TFT_WHITE;
    if (selected)
      nameColor = TFT_YELLOW;
    else if (isCurrent)
      nameColor = TFT_GREEN;

    char typePretty[16];
    snprintf(typePretty, sizeof(typePretty), "%s", e.petType);
    typePretty[0] = (char)toupper((unsigned char)typePretty[0]);
    for (int j = 1; typePretty[j]; ++j)
      typePretty[j] = (char)tolower((unsigned char)typePretty[j]);

    char nameWithSep[48];
    snprintf(nameWithSep, sizeof(nameWithSep), "%s - ", e.name);

    spr.setTextColor(nameColor);
    int nameWidth = spr.drawString(nameWithSep, 6, y, 2);

    char meta[48];
    time_t t = (time_t)e.createdAtEpoch;
    struct tm tmBuf{};
    localtime_r(&t, &tmBuf);
    snprintf(meta, sizeof(meta), "%s  %02d/%02d %02d:%02d", typePretty, tmBuf.tm_mon + 1, tmBuf.tm_mday, tmBuf.tm_hour,
             tmBuf.tm_min);

    uint16_t metaColor = TFT_LIGHTGREY;
    if (selected)
      metaColor = TFT_YELLOW;
    else if (isCurrent)
      metaColor = TFT_GREEN;

    spr.setTextColor(metaColor);
    spr.drawString(meta, 6 + nameWidth, y, 2);
  }
}

void drawBackupPetListScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  if (uiBackupPetListConfirmRestoreActive())
  {
    const int idx = uiBackupPetListConfirmRestoreIndex();

    const int selectedIdx = uiBackupPetListSelected();
    const int windowStart = uiBackupPetListWindowStart();
    const int visibleIdx = selectedIdx - windowStart;

    char titleBuf[64];
    titleBuf[0] = '\0';

    if (visibleIdx >= 0 && visibleIdx < uiBackupPetListVisibleCount())
    {
      const PetExportEntry &e = uiBackupPetListGetVisible(visibleIdx);

      time_t t = (time_t)e.createdAtEpoch;
      struct tm tmBuf{};
      localtime_r(&t, &tmBuf);

      static const char *kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

      const char *mon = "???";
      if (tmBuf.tm_mon >= 0 && tmBuf.tm_mon < 12)
        mon = kMonths[tmBuf.tm_mon];

      snprintf(titleBuf, sizeof(titleBuf), "Restore %s (%s %d, %02d:%02d)?", e.name[0] ? e.name : "backup", mon,
               tmBuf.tm_mday, tmBuf.tm_hour, tmBuf.tm_min);
    }
    else
    {
      snprintf(titleBuf, sizeof(titleBuf), "Restore backup?");
    }

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(titleBuf, SCREEN_W / 2, SCREEN_H / 2 - 30, 2);
    spr.drawString("Store Current Pet First?", SCREEN_W / 2, SCREEN_H / 2 - 12, 2);

    spr.setTextColor(idx == 0 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("YES", SCREEN_W / 2 - 40, SCREEN_H / 2 + 12, 2);

    spr.setTextColor(idx == 1 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("CANCEL", SCREEN_W / 2 + 40, SCREEN_H / 2 + 12, 2);
    return;
  }

  if (uiBackupPetListConfirmDeleteActive())
  {
    const int idx = uiBackupPetListConfirmDeleteIndex();

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Delete this backup?", SCREEN_W / 2, SCREEN_H / 2 - 10, 2);

    spr.setTextColor(idx == 0 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("YES", SCREEN_W / 2 - 30, SCREEN_H / 2 + 10, 2);

    spr.setTextColor(idx == 1 ? TFT_YELLOW : TFT_WHITE);
    spr.drawString("NO", SCREEN_W / 2 + 30, SCREEN_H / 2 + 10, 2);

    return;
  }

  if (uiBackupPetListActionMenuActive())
  {
    const int idx = uiBackupPetListActionIndex();
    const char *items[3] = {"Restore", "Delete Backup", "Cancel"};

    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_WHITE);
    spr.drawString("Backup Options", SCREEN_W / 2, 24, 2);

    for (int i = 0; i < 3; ++i)
    {
      spr.setTextColor(i == idx ? TFT_YELLOW : TFT_WHITE);
      spr.drawString(items[i], SCREEN_W / 2, 52 + (i * 18), 2);
    }
    return;
  }

  const int count = uiBackupPetListCount();
  if (count <= 0)
  {
    spr.setTextDatum(TC_DATUM);
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString("No backups found", SCREEN_W / 2, SCREEN_H / 2, 2);
    return;
  }

  const int rowH = 18;
  const int startY = 20;
  const int visibleCount = uiBackupPetListVisibleCount();

  for (int i = 0; i < visibleCount; ++i)
  {
    const int y = startY + (i * rowH);
    const bool selected = ((uiBackupPetListWindowStart() + i) == uiBackupPetListSelected());
    const PetExportEntry &e = uiBackupPetListGetVisible(i);

    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(selected ? TFT_YELLOW : TFT_WHITE);
    int nameWidth = spr.drawString(e.name, 6, y, 2);

    char meta[48];
    time_t t = (time_t)e.createdAtEpoch;
    struct tm tmBuf{};
    localtime_r(&t, &tmBuf);
    snprintf(meta, sizeof(meta), "%s  %02d/%02d %02d:%02d", e.petType, tmBuf.tm_mon + 1, tmBuf.tm_mday, tmBuf.tm_hour,
             tmBuf.tm_min);

    spr.setTextColor(selected ? TFT_YELLOW : TFT_LIGHTGREY);
    spr.drawString(meta, 6 + nameWidth + 6, y + 5, 1);
  }
}

void drawTitleMenuScreen(bool redrawBg)
{
  (void)redrawBg;

  if (!isScreenOn())
    return;

  // Title splash must draw full-screen no matter what happened previously.
  // Clear any lingering clip state before drawing the JPG.
  spr.clearClipRect();

  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  bool ok = false;
  if (g_sdReady)
    ok = sprDrawJpgFromSD(PATH_BG_SPLASH, 0, 0);

  if (!ok)
    spr.fillSprite(TFT_BLACK);

  const bool hasSave = uiTitleMenuHasSave();
  const bool hasImport = uiTitleMenuHasImport();
  const bool hasBirth = (saveManagerGetBirthEpoch() != 0);
  const bool hasName = (pet.getName()[0] != '\0');
  const bool hasRuntimePet = (hasBirth && hasName);

  char row0Buf[80];
  if (hasRuntimePet)
  {
    const char *typePretty = "Devil";
    switch (pet.type)
    {
    case PET_ELDRITCH:
      typePretty = "Eldritch";
      break;
    case PET_DEVIL:
    default:
      typePretty = "Devil";
      break;
    }

    snprintf(row0Buf, sizeof(row0Buf), "%s - lvl %u %s", pet.getName(), (unsigned)pet.level, typePretty);
  }
  else if (hasSave && hasBirth)
  {
    // Runtime state is partially present but the name is missing.
    // Show a more specific fallback so row 0 never goes "mysteriously blank."
    const char *typePretty = "Devil";
    switch (pet.type)
    {
    case PET_ELDRITCH:
      typePretty = "Eldritch";
      break;
    case PET_DEVIL:
    default:
      typePretty = "Devil";
      break;
    }

    snprintf(row0Buf, sizeof(row0Buf), "(unnamed) - lvl %u %s", (unsigned)pet.level, typePretty);
  }
  else if (hasSave)
  {
    snprintf(row0Buf, sizeof(row0Buf), "Continue");
  }
  else
  {
    snprintf(row0Buf, sizeof(row0Buf), "New Pet");
  }

  const char *storageLabel = hasImport ? "Pet Storage" : "Pet Storage Empty";
  const char *labels[3] = {row0Buf, storageLabel, "Settings"};
  const bool enabled[3] = {true, true, true};

  const int rowH = 18;
  const int itemCount = 3;
  const int menuTopY = (SCREEN_H / 2) + 12;
  const int menuPadX = 12;
  const int menuPadY = 8;
  const int menuBoxY = menuTopY - menuPadY;
  const int menuBoxH = (itemCount * rowH) + (menuPadY * 2);

  const int panelX = menuPadX;
  const int panelY = menuBoxY;
  const int panelW = SCREEN_W - (menuPadX * 2);
  const int panelH = menuBoxH;

  for (int yy = panelY; yy < panelY + panelH; ++yy)
  {
    const int xStart = panelX + ((yy & 1) ? 1 : 0);
    for (int xx = xStart; xx < panelX + panelW; xx += 2)
    {
      spr.drawPixel(xx, yy, TFT_BLACK);
    }
  }

  for (int i = 0; i < itemCount; ++i)
  {
    const int rowY = menuTopY + (i * rowH);
    const bool selected = (i == g_titleMenuIndex);

    uint16_t fg = TFT_WHITE;
    if (!enabled[i])
      fg = TFT_DARKGREY;
    else if (selected)
      fg = TFT_YELLOW;

    spr.setTextFont(2);
    spr.setTextSize(1);
    const int textW = spr.textWidth(labels[i]);

    if (selected)
    {
      const int arrowGap = 6;
      const int leftArrowX = (SCREEN_W / 2) - (textW / 2) - arrowGap;
      const int rightArrowX = (SCREEN_W / 2) + (textW / 2) + arrowGap;

      drawTitleMenuText(spr, "<", leftArrowX, rowY, 2, TFT_YELLOW, textdatum_t::top_right);
      drawTitleMenuText(spr, ">", rightArrowX, rowY, 2, TFT_YELLOW, textdatum_t::top_left);
    }

    drawTitleMenuText(spr, labels[i], SCREEN_W / 2, rowY, 2, fg, textdatum_t::top_center);
  }

  #if !PUBLIC_BUILD
  const char *assetVer = assetOtaInstalledVersion();
  const AssetOtaChannel ch = (AssetOtaChannel)assetOtaGetConfig().channel;

  char assetBuf[32];
  char buildBuf[32];

  snprintf(assetBuf, sizeof(assetBuf), "%s", (assetVer && assetVer[0]) ? assetVer : "none");
  snprintf(buildBuf, sizeof(buildBuf), "%s %s", (ch == AssetOtaChannel::DEV) ? "DEV" : "PUB", RH_VERSION_STRING);

  spr.setTextDatum(TL_DATUM);
  spr.setTextColor(TFT_BLACK);
  spr.drawString(buildBuf, 5, 3, 1);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString(buildBuf, 4, 2, 1);

  spr.setTextDatum(TR_DATUM);
  spr.setTextColor(TFT_BLACK);
  spr.drawString(assetBuf, SCREEN_W - 3, 3, 1);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString(assetBuf, SCREEN_W - 4, 2, 1);
#endif
}