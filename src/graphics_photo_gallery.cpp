#include "ui_state_photo_gallery.h"

#include "display.h"
#include "graphics.h"
#include "graphics_sd_draw.h"

extern M5Canvas spr;

bool isScreenOn();

void drawPhotoGalleryScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  if (photoGalleryViewingPhoto())
  {
    spr.fillSprite(TFT_BLACK);

    const char *path = photoGallerySelectedPath();
    const bool ok = path && sprDrawPngFromSD(path, 0, 0);

    if (!ok)
    {
      spr.setTextDatum(MC_DATUM);
      spr.setTextColor(TFT_WHITE);
      spr.drawString("Photo failed", SCREEN_W / 2, SCREEN_H / 2, 2);
      spr.setTextDatum(TL_DATUM);
    }

    return;
  }

  spr.fillSprite(TFT_BLACK);

  spr.setTextDatum(TC_DATUM);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("Photo Gallery", SCREEN_W / 2, 6, 2);

  const int count = photoGalleryCount();
  if (count <= 0)
  {
    spr.setTextColor(TFT_DARKGREY);
    spr.drawString("No photos found", SCREEN_W / 2, SCREEN_H / 2 - 8, 2);
    spr.drawString("ESC: Back", SCREEN_W / 2, SCREEN_H - 14, 1);
    spr.setTextDatum(TL_DATUM);
    return;
  }

  const int rowH = 18;
  const int startY = 28;
  const int visibleCount = photoGalleryVisibleCount();
  const int selected = photoGallerySelected();
  const int windowStart = photoGalleryWindowStart();

  for (int i = 0; i < visibleCount; ++i)
  {
    const int idx = windowStart + i;
    const int y = startY + (i * rowH);
    const bool isSelected = idx == selected;

    spr.setTextDatum(TL_DATUM);
    spr.setTextColor(isSelected ? TFT_YELLOW : TFT_WHITE);
    spr.drawString(photoGalleryVisibleName(i), 8, y, 1);
  }

  char footer[48];
  snprintf(footer, sizeof(footer), "%d/%d  ENTER: View  ESC: Back", selected + 1, count);

  spr.setTextDatum(BC_DATUM);
  spr.setTextColor(TFT_LIGHTGREY);
  spr.drawString(footer, SCREEN_W / 2, SCREEN_H - 3, 1);
  spr.setTextDatum(TL_DATUM);
}