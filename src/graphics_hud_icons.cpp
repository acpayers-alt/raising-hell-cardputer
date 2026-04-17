#include "graphics_hud_icons.h"

#include "display.h"
#include "graphics_sd_draw.h"
#include "sdcard.h"

static const char *PATH_INF_COIN  = "/raising_hell/graphics/ui/icons/inf_coin.png";
static const char *PATH_LIFE_ICON = "/raising_hell/graphics/ui/icons/life_icon.png";
static const char *PATH_FOOD_ICON = "/raising_hell/graphics/ui/icons/food_icon.png";
static const char *PATH_MOOD_ICON = "/raising_hell/graphics/ui/icons/mood_icon.png";
static const char *PATH_REST_ICON = "/raising_hell/graphics/ui/icons/rest_icon.png";

static constexpr int HUD_HEADER_ICON_W = 12;
static constexpr int HUD_HEADER_ICON_H = 12;
static constexpr int HUD_STAT_ICON_W = 10;
static constexpr int HUD_STAT_ICON_H = 10;
static constexpr uint16_t HUD_ICON_TRANSPARENT = 0xF81F;

static M5Canvas s_hudLifeIconSmall(&spr);
static bool s_hudLifeIconSmallReady = false;
static M5Canvas s_hudCoinIconSmall(&spr);
static bool s_hudCoinIconSmallReady = false;

static M5Canvas s_hudFoodIcon(&spr);
static bool s_hudFoodIconReady = false;
static M5Canvas s_hudMoodIcon(&spr);
static bool s_hudMoodIconReady = false;
static M5Canvas s_hudRestIcon(&spr);
static bool s_hudRestIconReady = false;

static bool ensureHudIconCache(M5Canvas &canvas, bool &ready, const char *path, int w, int h)
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

  canvas.fillSprite(HUD_ICON_TRANSPARENT);

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

  if (path == PATH_LIFE_ICON)
  {
    canvas = &s_hudLifeIconSmall;
    ready = &s_hudLifeIconSmallReady;
    w = HUD_HEADER_ICON_W;
    h = HUD_HEADER_ICON_H;
  }
  else if (path == PATH_INF_COIN)
  {
    canvas = &s_hudCoinIconSmall;
    ready = &s_hudCoinIconSmallReady;
    w = HUD_HEADER_ICON_W;
    h = HUD_HEADER_ICON_H;
  }
  else if (path == PATH_FOOD_ICON)
  {
    canvas = &s_hudFoodIcon;
    ready = &s_hudFoodIconReady;
    w = HUD_STAT_ICON_W;
    h = HUD_STAT_ICON_H;
  }
  else if (path == PATH_MOOD_ICON)
  {
    canvas = &s_hudMoodIcon;
    ready = &s_hudMoodIconReady;
    w = HUD_STAT_ICON_W;
    h = HUD_STAT_ICON_H;
  }
  else if (path == PATH_REST_ICON)
  {
    canvas = &s_hudRestIcon;
    ready = &s_hudRestIconReady;
    w = HUD_STAT_ICON_W;
    h = HUD_STAT_ICON_H;
  }

  if (canvas && ready && ensureHudIconCache(*canvas, *ready, path, w, h))
  {
    canvas->pushSprite(x, y, HUD_ICON_TRANSPARENT);
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

  s_hudCoinIconSmall.deleteSprite();
  s_hudCoinIconSmallReady = false;

  s_hudFoodIcon.deleteSprite();
  s_hudFoodIconReady = false;

  s_hudMoodIcon.deleteSprite();
  s_hudMoodIconReady = false;

  s_hudRestIcon.deleteSprite();
  s_hudRestIconReady = false;
}