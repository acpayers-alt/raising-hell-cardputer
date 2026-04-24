#include "graphics_sd_draw.h"

#include <FS.h>
#include <SD.h>
#include <lgfx/v1/misc/DataWrapper.hpp>

#include "display.h"
#include "sdcard.h"

class RH_FileDataWrapper : public lgfx::v1::DataWrapper
{
public:
  explicit RH_FileDataWrapper(fs::File &f) : _f(&f) {}

  int read(uint8_t *buf, uint32_t len) override
  {
    if (!_f)
      return 0;
    return (int)_f->read(buf, len);
  }

  void skip(int32_t offset) override
  {
    if (!_f)
      return;

    int32_t pos = (int32_t)_f->position();
    int32_t next = pos + offset;
    if (next < 0)
      next = 0;

    const uint32_t size = (uint32_t)_f->size();
    if ((uint32_t)next > size)
      next = (int32_t)size;

    _f->seek((uint32_t)next);
  }

  bool seek(uint32_t offset) override
  {
    if (!_f)
      return false;

    const uint32_t size = (uint32_t)_f->size();
    if (offset > size)
      offset = size;

    return _f->seek(offset);
  }

  void close(void) override
  {
    if (_f)
    {
      _f->close();
      _f = nullptr;
    }
  }

  int32_t tell(void) override
  {
    if (!_f)
      return 0;
    return (int32_t)_f->position();
  }

private:
  fs::File *_f = nullptr;
};

bool sprDrawJpgFromSD(const char *path, int x, int y)
{
  if (!g_sdReady || !path || !*path)
    return false;

  fs::File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  RH_FileDataWrapper dw(f);
  const bool ok = spr.drawJpg(&dw, x, y);
  dw.close();
  return ok;
}

bool sprDrawPngFromSD(const char *path, int x, int y)
{
  if (!g_sdReady || !path || !*path)
    return false;

  fs::File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  RH_FileDataWrapper dw(f);
  const bool ok = spr.drawPng(&dw, x, y);
  dw.close();
  return ok;
}

bool canvasDrawPngFromSD(LGFX_Sprite &canvas, const char *path, int x, int y)
{
  if (!g_sdReady || !path || !*path)
    return false;

  fs::File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  RH_FileDataWrapper dw(f);
  const bool ok = canvas.drawPng(&dw, x, y);
  dw.close();
  return ok;
}

bool canvasDrawJpgFromSD(M5Canvas &canvas, const char *path, int x, int y)
{
  if (!g_sdReady || !path || !*path)
    return false;

  fs::File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  RH_FileDataWrapper dw(f);
  const bool ok = canvas.drawJpg(&dw, x, y);
  dw.close();
  return ok;
}

bool canvasDrawPngFromSD(M5Canvas &canvas, const char *path, int x, int y)
{
  if (!g_sdReady || !path || !*path)
    return false;

  fs::File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  RH_FileDataWrapper dw(f);
  const bool ok = canvas.drawPng(&dw, x, y);
  dw.close();
  return ok;
}

bool canvasDrawJpgFromSD(LGFX_Sprite &canvas, const char *path, int x, int y)
{
  if (!g_sdReady || !path || !*path)
    return false;

  fs::File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  RH_FileDataWrapper dw(f);
  const bool ok = canvas.drawJpg(&dw, x, y);
  dw.close();
  return ok;
}