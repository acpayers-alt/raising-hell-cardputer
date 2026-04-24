#include "graphics_raw_stream.h"

#include <Arduino.h>
#include <SD.h>

#include "display.h"
#include "sdcard.h"

extern M5Canvas spr;
extern int screenW;
extern int screenH;

static const int MAX_LINE_WIDTH = 240;
static uint16_t lineBuf[MAX_LINE_WIDTH];

static bool clipRectToScreen(int &x, int &y, int &w, int &h)
{
  if (w <= 0 || h <= 0)
    return false;
  if (x >= screenW || y >= screenH)
    return false;
  if (x + w <= 0 || y + h <= 0)
    return false;

  if (x < 0)
  {
    w += x;
    x = 0;
  }

  if (y < 0)
  {
    h += y;
    y = 0;
  }

  if (x + w > screenW)
    w = screenW - x;
  if (y + h > screenH)
    h = screenH - y;

  return (w > 0 && h > 0);
}

bool streamRawImage(const char *path, int x, int y, int w, int h)
{
  if (!g_sdReady || !path)
    return false;
  if (!clipRectToScreen(x, y, w, h))
    return false;

  File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  if (w > MAX_LINE_WIDTH)
    w = MAX_LINE_WIDTH;

  const size_t rowBytes = (size_t)w * 2;

  for (int row = 0; row < h; ++row)
  {
    const size_t n = f.read((uint8_t *)lineBuf, rowBytes);
    if (n != rowBytes)
    {
      f.close();
      return false;
    }

    spr.pushImage(x, y + row, w, 1, lineBuf);
  }

  f.close();
  return true;
}

bool streamRawImageFast(const char *path, int x, int y, int w, int h)
{
  return streamRawImage(path, x, y, w, h);
}