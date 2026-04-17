#include "graphics_mini_stats.h"

#include <Arduino.h>
#include <stdio.h>

#include <M5GFX.h>

#include "display.h"
#include "graphics.h"
#include "pet.h"
#include "sdcard.h"

// ----------------------------------------------------------------------------
// Mini stat icon assets / cache
// ----------------------------------------------------------------------------

static const char *PATH_INF_COIN  = "/raising_hell/graphics/ui/icons/inf_coin.png";
static const char *PATH_LIFE_ICON = "/raising_hell/graphics/ui/icons/life_icon.png";

static constexpr int MINI_STAT_ICON_W = 18;
static constexpr int MINI_STAT_ICON_H = 18;
static constexpr uint16_t MINI_STAT_ICON_TRANSPARENT = 0xF81F;

static M5Canvas s_miniStatLifeIcon(&spr);
static bool s_miniStatLifeIconReady = false;

static M5Canvas s_miniStatCoinIcon(&spr);
static bool s_miniStatCoinIconReady = false;

// ----------------------------------------------------------------------------
// Internal helpers
// ----------------------------------------------------------------------------

static bool ensureMiniStatIconCache(M5Canvas &canvas, bool &ready, const char *path)
{
  if (ready)
    return true;
  if (!g_sdReady || !path || !*path)
    return false;

  canvas.setColorDepth(16);

  if (!canvas.width() || !canvas.height())
  {
    if (!canvas.createSprite(MINI_STAT_ICON_W, MINI_STAT_ICON_H))
      return false;
  }

  canvas.fillSprite(MINI_STAT_ICON_TRANSPARENT);

  if (!canvasDrawPngFromSD(canvas, path, 1, 1))
  {
    canvas.deleteSprite();
    ready = false;
    return false;
  }

  ready = true;
  return true;
}

static bool drawMiniStatIconCached(const char *path, int x, int y)
{
  M5Canvas *canvas = nullptr;
  bool *ready = nullptr;

  if (path == PATH_LIFE_ICON)
  {
    canvas = &s_miniStatLifeIcon;
    ready = &s_miniStatLifeIconReady;
  }
  else if (path == PATH_INF_COIN)
  {
    canvas = &s_miniStatCoinIcon;
    ready = &s_miniStatCoinIconReady;
  }

  if (canvas && ready && ensureMiniStatIconCache(*canvas, *ready, path))
  {
    canvas->pushSprite(x, y, MINI_STAT_ICON_TRANSPARENT);
    return true;
  }

  if (g_sdReady)
    return sprDrawPngFromSD(path, x, y);

  return false;
}

// ============================================================================
// Tiny stat preview panel
// ============================================================================

static void drawMiniStatPreviewAt(int x0, bool showCoin)
{
  const int panelW = 72;

  // Layout
  const int headerY = PET_AREA_Y + 2;

  // Stat block
  const int barH = 14;
  const int rowGap = 4;
  const int rowH = barH + rowGap;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood   = 0x001F;
  const uint16_t colEnergy = 0x03E0;

  // Bars first
  const int y0 = headerY + 4;
  const int barX = x0 + 2;
  const int barW = panelW - 4;

  const int yHunger = y0 + 0 * rowH;
  const int yMood   = y0 + 1 * rowH;
  const int yRest   = y0 + 2 * rowH;

  drawTinyBar(barX, yHunger, barW, barH, colHunger, colHunger, pet.hunger, "Hunger");
  drawTinyBar(barX, yMood,   barW, barH, colMood,   colMood,   pet.happiness, "Mood");
  drawTinyBar(barX, yRest,   barW, barH, colEnergy, colEnergy, pet.energy, "Rest");

  // Bottom header: coin/count on left, heart/HP on right
  spr.setTextFont(2);
  spr.setTextSize(1);

  const int headerY2 = headerY + 74;
  const int headerIconY = headerY2 + 0;
  const int topTextY = headerY2 + 1;

  spr.setTextColor(TFT_WHITE, TFT_TRANSPARENT);

  // Define the right-side heart anchor first so coin text can avoid it
  const int heartIconX = x0 + panelW - 2 - 16 - 28;

  // Left side: coin + count (count grows left, icon follows it)
  if (showCoin)
  {
    char infBuf[20];
    snprintf(infBuf, sizeof(infBuf), "%d", pet.inf);

    // Fixed right edge for coin text, safely left of the heart block
    const int coinRightX = heartIconX - 6;

    spr.setTextDatum(TR_DATUM);

    // Measure count width using current font/settings
    const int coinTextW = spr.textWidth(infBuf);

    // Keep a small gap between icon and number
    const int coinGap = 6;

    // Place icon so it sits just left of the text block
    const int coinIconX = coinRightX - coinTextW - coinGap - 16;

    drawMiniStatIconCached(PATH_INF_COIN, coinIconX, headerIconY);

    // fake-bold / slightly larger-looking text
    spr.drawString(infBuf, coinRightX, topTextY);
    spr.drawString(infBuf, coinRightX - 1, topTextY);
  }

  // Right side: heart + HP
  {
    char hpBuf[16];
    snprintf(hpBuf, sizeof(hpBuf), "%d", pet.health);

    const int hpTextW = spr.textWidth(hpBuf);
    const int hpTextX = x0 + panelW - 2 - hpTextW;

    drawMiniStatIconCached(PATH_LIFE_ICON, heartIconX, headerIconY);

    spr.setTextDatum(TL_DATUM);

    // fake-bold / slightly larger-looking text
    spr.drawString(hpBuf, hpTextX, topTextY);
    spr.drawString(hpBuf, hpTextX + 1, topTextY);
  }

  spr.setTextDatum(TL_DATUM);
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

void drawMiniStatPreview()
{
  const int panelW = 72;
  const int x0 = SCREEN_W - panelW - 4;
  drawMiniStatPreviewAt(x0, true);
}

void drawMiniStatPreviewSleepLeft()
{
  const int x0 = 4;
  const int panelW = 72;

  // Layout
  const int headerY = PET_AREA_Y + 2;

  // Stat block
  const int barH = 14;
  const int rowGap = 4;
  const int rowH = barH + rowGap;

  const uint16_t colHunger = 0xF800;
  const uint16_t colMood   = 0x001F;
  const uint16_t colEnergy = 0x03E0;

  // Bars near the top
  const int y0 = headerY + 4;
  const int barX = x0 + 2;
  const int barW = panelW - 4;

  const int yHunger = y0 + 0 * rowH;
  const int yMood   = y0 + 1 * rowH;
  const int yRest   = y0 + 2 * rowH;

  drawTinyBar(barX, yHunger, barW, barH, colHunger, colHunger, pet.hunger, "Hunger");
  drawTinyBar(barX, yMood,   barW, barH, colMood,   colMood,   pet.happiness, "Mood");
  drawTinyBar(barX, yRest,   barW, barH, colEnergy, colEnergy, pet.energy, "Rest");

  // Bottom footer: heart + HP only on the sleep-left panel.
  spr.setTextFont(2);
  spr.setTextSize(1);

  const int headerY2 = headerY + 64;
  const int headerIconY = headerY2 + 0;
  const int topTextY = headerY2 + 1;

  {
    const int heartIconX = x0 + 2;

    char hpBuf[16];
    snprintf(hpBuf, sizeof(hpBuf), "%d", pet.health);

    const int hpTextX = heartIconX + 18;
    const int hpTextW = spr.textWidth(hpBuf);
    (void)hpTextW;

    drawMiniStatIconCached(PATH_LIFE_ICON, heartIconX, headerIconY);

    spr.setTextColor(TFT_WHITE, TFT_TRANSPARENT);
    spr.setTextDatum(TL_DATUM);

    // fake-bold
    spr.drawString(hpBuf, hpTextX, topTextY);
    spr.drawString(hpBuf, hpTextX + 1, topTextY);
  }

  spr.setTextDatum(TL_DATUM);
}

void graphicsReleaseMiniStatCaches()
{
  s_miniStatLifeIcon.deleteSprite();
  s_miniStatLifeIconReady = false;

  s_miniStatCoinIcon.deleteSprite();
  s_miniStatCoinIconReady = false;
}