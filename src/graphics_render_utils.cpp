#include "graphics_render_utils.h"
#include "graphics.h"
#include "graphics_sd_draw.h"

#include <M5GFX.h>
#include <Arduino.h>
#include <SD.h>
#include <string.h>

#include "display.h"

extern M5Canvas spr;
extern bool g_sdReady;


void drawCenteredLine(const char *s, int y, int font, int size)
{
  spr.setTextDatum(TC_DATUM);
  spr.setTextFont(font);
  spr.setTextSize(size);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(s ? s : "", SCREEN_W / 2, y);
}

bool getPngWH(const char *path, int &outW, int &outH)
{
  outW = 0;
  outH = 0;

  if (!path || !path[0] || !g_sdReady)
    return false;

  File fp = SD.open(path, FILE_READ);
  if (!fp)
    return false;

  uint8_t hdr[24];
  const size_t got = fp.read(hdr, sizeof(hdr));
  fp.close();

  if (got < sizeof(hdr))
    return false;

  static const uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  if (memcmp(hdr, kPngSig, sizeof(kPngSig)) != 0)
    return false;

  outW = (int)(((uint32_t)hdr[16] << 24) |
               ((uint32_t)hdr[17] << 16) |
               ((uint32_t)hdr[18] << 8)  |
               ((uint32_t)hdr[19]));

  outH = (int)(((uint32_t)hdr[20] << 24) |
               ((uint32_t)hdr[21] << 16) |
               ((uint32_t)hdr[22] << 8)  |
               ((uint32_t)hdr[23]));

  return (outW > 0 && outH > 0);
}

void drawCenteredImageSpr(const char *path, int cx, int cy)
{
  if (!path || !*path)
    return;

  int w = 0;
  int h = 0;
  const bool gotWH = getPngWH(path, w, h);

  const int x = gotWH ? (cx - (w / 2)) : cx;
  const int y = gotWH ? (cy - (h / 2)) : cy;

  bool ok = false;
  if (g_sdReady)
    ok = sprDrawPngFromSD(path, x, y);

  if (!ok)
  {
    const int boxW = gotWH ? w : 140;
    const int boxH = gotWH ? h : 40;
    const int boxX = gotWH ? x : (cx - boxW / 2);
    const int boxY = gotWH ? y : (cy - boxH / 2);

    spr.drawRect(boxX, boxY, boxW, boxH, TFT_DARKGREY);
    spr.setTextDatum(MC_DATUM);
    spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
    spr.setTextFont(1);
    spr.setTextSize(1);
    spr.drawString("IMG FAIL", cx, cy);
  }
}