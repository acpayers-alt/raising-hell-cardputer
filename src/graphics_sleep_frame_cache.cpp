#include "graphics_sleep_frame_cache.h"

#include <stdlib.h>
#include <string.h>

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "display.h"
#include "graphics_sd_draw.h"
#include "sdcard.h"
#include "tft_compat.h"

static uint16_t **s_sleepAnimFrameCache = nullptr;
static uint8_t s_sleepAnimFrameCacheCnt = 0;
static uint8_t s_sleepAnimFrameCacheMode = 0; // 1=baby,2=teen,3=adult,4=elder
static bool s_sleepAnimFrameCacheReady = false;

void freeSleepAnimFrameCache()
{
  if (s_sleepAnimFrameCache)
  {
    for (uint8_t i = 0; i < s_sleepAnimFrameCacheCnt; ++i)
    {
      if (s_sleepAnimFrameCache[i])
      {
        free(s_sleepAnimFrameCache[i]);
        s_sleepAnimFrameCache[i] = nullptr;
      }
    }
    free(s_sleepAnimFrameCache);
    s_sleepAnimFrameCache = nullptr;
  }

  s_sleepAnimFrameCacheCnt = 0;
  s_sleepAnimFrameCacheMode = 0;
  s_sleepAnimFrameCacheReady = false;
}

bool ensureSleepAnimFrameCache(uint8_t mode, const char *const *frames, uint8_t frameCount, int drawX, int drawY)
{
  if (mode == 0 || !frames || frameCount == 0)
    return false;

  // No PSRAM on this hardware. Full-screen cached sleep frames are too large
  // and can starve later graphics/WiFi allocations.
  // Fall back to drawing sleep frames live instead of caching snapshots.
  return false;

  if (s_sleepAnimFrameCacheReady && s_sleepAnimFrameCache && s_sleepAnimFrameCacheMode == mode &&
      s_sleepAnimFrameCacheCnt == frameCount)
  {
    return true;
  }

  const size_t pxCount = (size_t)SCREEN_W * (size_t)SCREEN_H;
  const size_t bufBytes = pxCount * sizeof(uint16_t);
  const size_t totalNeeded = bufBytes * frameCount;

  if (ESP.getPsramSize() == 0 || heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < (totalNeeded + 4096))
  {
    return false;
  }

  freeSleepAnimFrameCache();

  uint16_t *sprBuf = (uint16_t *)spr.getBuffer();
  if (!sprBuf)
    return false;

  s_sleepAnimFrameCache = (uint16_t **)calloc(frameCount, sizeof(uint16_t *));
  if (!s_sleepAnimFrameCache)
    return false;

  for (uint8_t i = 0; i < frameCount; ++i)
  {
    s_sleepAnimFrameCache[i] = (uint16_t *)malloc(bufBytes);
    if (!s_sleepAnimFrameCache[i])
    {
      freeSleepAnimFrameCache();
      return false;
    }

    spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);

    bool ok = false;
    if (g_sdReady && frames[i])
    {
      const char *ext = strrchr(frames[i], '.');
      const bool isPng = (ext && (strcasecmp(ext, ".png") == 0));
      if (isPng)
        ok = sprDrawPngFromSD(frames[i], drawX, drawY);
      else
        ok = sprDrawJpgFromSD(frames[i], drawX, drawY);
    }

    if (!ok)
    {
      freeSleepAnimFrameCache();
      return false;
    }

    memcpy(s_sleepAnimFrameCache[i], sprBuf, bufBytes);
  }

  s_sleepAnimFrameCacheCnt = frameCount;
  s_sleepAnimFrameCacheMode = mode;
  s_sleepAnimFrameCacheReady = true;
  return true;
}