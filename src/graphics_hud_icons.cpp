#include "graphics_hud_icons.h"

#include "display.h"
#include "graphics_sd_draw.h"
#include "sdcard.h"
#include "ui_icons.h"

static constexpr uint16_t HUD_ICON_TRANSPARENT = 0xF81F;

static M5Canvas s_hudLifeIconSmall(&spr);
static bool s_hudLifeIconSmallReady = false;
static M5Canvas s_hudInfIconSmall(&spr);
static bool s_hudInfIconSmallReady = false;
static M5Canvas s_hudInfTopBarIcon(&spr);
static bool s_hudInfTopBarIconReady = false;
static M5Canvas s_hudInfLargeIcon(&spr);
static bool s_hudInfLargeIconReady = false;

static bool ensureHudIconCache(M5Canvas &canvas, bool &ready, const char *path, int w, int h, uint16_t fillColor)
{
  if (ready)
    return true;
  if (!g_sdReady || !path || !*path)
    return false;

  canvas.setColorDepth(16);

  if (!canvas.width() || !canvas.height())
  {
    if (!canvas.createSprite(w, h))
      return false;
  }

  canvas.fillSprite(fillColor);

  if (!canvasDrawPngFromSD(canvas, path, 0, 0))
  {
    canvas.deleteSprite();
    ready = false;
    return false;
  }

  ready = true;
  return true;
}

bool drawHudIconCached(const char *path, int x, int y)
{
  M5Canvas *canvas = nullptr;
  bool *ready = nullptr;
  int w = 0;
  int h = 0;
  bool useColorKey = true;
  uint16_t fillColor = HUD_ICON_TRANSPARENT;

  if (path == LIFE_ICON_PATH)
  {
    canvas = &s_hudLifeIconSmall;
    ready = &s_hudLifeIconSmallReady;
    w = HUD_HEADER_ICON_W;
    h = HUD_HEADER_ICON_H;
  }
  else if (path == INF_ICON_PATH)
  {
    canvas = &s_hudInfTopBarIcon;
    ready = &s_hudInfTopBarIconReady;
    w = INF_ICON_W;
    h = INF_ICON_H;
    useColorKey = false;
    fillColor = TFT_BLACK;
  }

  else if (path == INF_ICON_LARGE_PATH)
  {
    canvas = &s_hudInfLargeIcon;
    ready = &s_hudInfLargeIconReady;
    w = INF_ICON_LARGE_W;
    h = INF_ICON_LARGE_H;
    useColorKey = false;
    fillColor = TFT_BLACK;
  }

  if (canvas && ready && ensureHudIconCache(*canvas, *ready, path, w, h, fillColor))
  {
    if (useColorKey)
      canvas->pushSprite(x, y, HUD_ICON_TRANSPARENT);
    else
      canvas->pushSprite(x, y);

    return true;
  }

  if (g_sdReady)
    return sprDrawPngFromSD(path, x, y);

  return false;
}

void graphicsReleaseHudIconCaches()
{
  s_hudLifeIconSmall.deleteSprite();
  s_hudLifeIconSmallReady = false;

  s_hudInfTopBarIcon.deleteSprite();
  s_hudInfTopBarIconReady = false;

  s_hudInfLargeIcon.deleteSprite();
  s_hudInfLargeIconReady = false;
}