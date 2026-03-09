#include "mini_game_assets.h"

#include <FS.h>
#include <SD.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"
#include "graphics.h"   // spr
#include "sdcard.h"     // g_sdReady

namespace
{
  static M5Canvas s_sharedBgSpr(&M5.Display);
  static bool s_sharedBgReady = false;
  static MiniGame s_sharedBgOwner = MiniGame::NONE;
  static char s_sharedBgPath[128] = {0};
  static int s_sharedBgW = 0;
  static int s_sharedBgH = 0;

  static bool resolveSdPath(const char* path, const char** outUsePath)
  {
    if (!path || !path[0])
      return false;

    if (SD.exists(path))
    {
      if (outUsePath) *outUsePath = path;
      return true;
    }

    if (path[0] == '/' && SD.exists(path + 1))
    {
      if (outUsePath) *outUsePath = path + 1;
      return true;
    }

    return false;
  }

  static bool isPngPath(const char* path)
  {
    if (!path || !path[0])
      return false;

    const char* ext = strrchr(path, '.');
    if (!ext)
      return false;

    return (strcasecmp(ext, ".png") == 0);
  }
}

void mgAssetsLogHeap(const char* tag)
{
  Serial.printf(
      "[MG HEAP] %s free=%u largest=%u\n",
      tag ? tag : "(null)",
      (unsigned)ESP.getFreeHeap(),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void mgAssetsBeginSession(MiniGame game, const char* tag)
{
  Serial.printf("[MG ASSET] begin game=%d tag=%s\n", (int)game, tag ? tag : "(null)");
  mgAssetsLogHeap(tag ? tag : "mgAssetsBeginSession");
}

void mgAssetsEndSession(MiniGame game, const char* tag)
{
  Serial.printf("[MG ASSET] end game=%d tag=%s\n", (int)game, tag ? tag : "(null)");
  mgAssetsLogHeap(tag ? tag : "mgAssetsEndSession");
}

bool mgAssetsEnsureSharedBg(MiniGame owner, const char* path)
{
  if (!path || !path[0])
    return false;

  if (!g_sdReady)
  {
    Serial.println("[MG ASSET] shared bg load skipped: SD not ready");
    return false;
  }

  // Already loaded and matches exactly.
  if (s_sharedBgReady &&
      s_sharedBgOwner == owner &&
      s_sharedBgPath[0] &&
      strcmp(s_sharedBgPath, path) == 0)
  {
    return true;
  }

  const int w = (int)spr.width();
  const int h = (int)spr.height();
  if (w <= 0 || h <= 0)
    return false;

  const char* usePath = path;
  if (!resolveSdPath(path, &usePath))
  {
    Serial.printf("[MG ASSET] shared bg missing path='%s'\n", path);
    return false;
  }

  const bool isPng = isPngPath(path);

  mgAssetsLogHeap("shared-bg-before-load");

  if (!s_sharedBgReady || s_sharedBgW != w || s_sharedBgH != h)
  {
    if (s_sharedBgReady)
    {
      s_sharedBgSpr.deleteSprite();
      s_sharedBgReady = false;
    }

    s_sharedBgSpr.setColorDepth(8);
    if (!s_sharedBgSpr.createSprite(w, h))
    {
      Serial.printf("[MG ASSET] shared bg create FAIL w=%d h=%d\n", w, h);
      mgAssetsLogHeap("shared-bg-create-fail");
      return false;
    }

    s_sharedBgW = w;
    s_sharedBgH = h;
    s_sharedBgReady = true;
  }

  s_sharedBgSpr.fillSprite(TFT_BLACK);

  bool ok = false;
  if (isPng)
    ok = s_sharedBgSpr.drawPngFile(SD, usePath, 0, 0);
  else
    ok = s_sharedBgSpr.drawJpgFile(SD, usePath, 0, 0);

  if (!ok)
  {
    Serial.printf("[MG ASSET] shared bg draw FAIL path='%s' use='%s'\n", path, usePath);
    mgAssetsLogHeap("shared-bg-draw-fail");
    return false;
  }

  s_sharedBgOwner = owner;
  strlcpy(s_sharedBgPath, path, sizeof(s_sharedBgPath));

  Serial.printf("[MG ASSET] shared bg ready owner=%d path='%s'\n", (int)owner, s_sharedBgPath);
  mgAssetsLogHeap("shared-bg-after-load");
  return true;
}

void mgAssetsReleaseSharedBgIfOwner(MiniGame owner)
{
  if (!s_sharedBgReady)
    return;

  if (s_sharedBgOwner != owner)
    return;

  s_sharedBgSpr.deleteSprite();
  s_sharedBgReady = false;
  s_sharedBgOwner = MiniGame::NONE;
  s_sharedBgPath[0] = 0;
  s_sharedBgW = 0;
  s_sharedBgH = 0;

  mgAssetsLogHeap("shared-bg-release-owner");
}

void mgAssetsReleaseSharedBg()
{
  if (!s_sharedBgReady)
    return;

  s_sharedBgSpr.deleteSprite();
  s_sharedBgReady = false;
  s_sharedBgOwner = MiniGame::NONE;
  s_sharedBgPath[0] = 0;
  s_sharedBgW = 0;
  s_sharedBgH = 0;

  mgAssetsLogHeap("shared-bg-release-all");
}

bool mgAssetsHasSharedBg()
{
  return s_sharedBgReady;
}

MiniGame mgAssetsSharedBgOwner()
{
  return s_sharedBgOwner;
}

const char* mgAssetsSharedBgPath()
{
  return s_sharedBgPath;
}

M5Canvas* mgAssetsSharedBg()
{
  return s_sharedBgReady ? &s_sharedBgSpr : nullptr;
}

int mgAssetsSharedBgW()
{
  return s_sharedBgW;
}

int mgAssetsSharedBgH()
{
  return s_sharedBgH;
}

void mgAssetsReleaseSprite(M5Canvas& dst, const char* tag)
{
  dst.deleteSprite();
  if (tag && tag[0])
    mgAssetsLogHeap(tag);
}

bool mgAssetsLoadSprite(
    M5Canvas& dst,
    const char* path,
    int colorDepth,
    uint16_t fillColor,
    const char* tag)
{
  if (!path || !path[0])
  {
    Serial.println("[MG ASSET] sprite load skipped: empty path");
    return false;
  }

  if (!g_sdReady)
  {
    Serial.println("[MG ASSET] sprite load skipped: SD not ready");
    return false;
  }

  const char* usePath = path;
  if (!resolveSdPath(path, &usePath))
  {
    Serial.printf("[MG ASSET] sprite missing path='%s'\n", path);
    return false;
  }

  int w = 0;
  int h = 0;

  if (isPngPath(usePath))
  {
    File f = SD.open(usePath, "r");
    if (!f)
      return false;

    uint8_t b[24];
    const int n = f.read(b, sizeof(b));
    f.close();

    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (n != (int)sizeof(b) || memcmp(b, sig, 8) != 0)
      return false;
    if (!(b[12] == 'I' && b[13] == 'H' && b[14] == 'D' && b[15] == 'R'))
      return false;

    w = (int)((uint32_t)b[16] << 24 | (uint32_t)b[17] << 16 | (uint32_t)b[18] << 8 | (uint32_t)b[19]);
    h = (int)((uint32_t)b[20] << 24 | (uint32_t)b[21] << 16 | (uint32_t)b[22] << 8 | (uint32_t)b[23]);
  }
  else
  {
    // JPG path: let drawJpgFile decode into a display-sized sprite only after allocation.
    // For now, use the screen sprite dimensions as a conservative fallback.
    // We can improve JPG dimension probing later if needed.
    w = (int)spr.width();
    h = (int)spr.height();
  }

  if (w <= 0 || h <= 0)
  {
    Serial.printf("[MG ASSET] invalid sprite dims path='%s' w=%d h=%d\n", usePath, w, h);
    return false;
  }

  mgAssetsLogHeap(tag ? tag : "sprite-before-load");

  dst.deleteSprite();
  dst.setColorDepth(colorDepth);

  if (!dst.createSprite(w, h))
  {
    Serial.printf("[MG ASSET] createSprite FAIL path='%s' w=%d h=%d depth=%d\n",
                  usePath, w, h, colorDepth);
    mgAssetsLogHeap("sprite-create-fail");
    return false;
  }

  dst.fillSprite(fillColor);

  bool ok = false;
  if (isPngPath(usePath))
    ok = dst.drawPngFile(SD, usePath, 0, 0);
  else
    ok = dst.drawJpgFile(SD, usePath, 0, 0);

  if (!ok)
  {
    Serial.printf("[MG ASSET] draw FAIL path='%s'\n", usePath);
    dst.deleteSprite();
    mgAssetsLogHeap("sprite-draw-fail");
    return false;
  }

  mgAssetsLogHeap(tag ? tag : "sprite-after-load");
  return true;
}

bool mgAssetsReadPngDims(const char *path, int *outW, int *outH, const char **outUsePath)
{
  if (outW) *outW = 0;
  if (outH) *outH = 0;
  if (outUsePath) *outUsePath = nullptr;

  if (!g_sdReady || !path || !path[0])
    return false;

  const char *usePath = path;
  if (SD.exists(path))
  {
    usePath = path;
  }
  else if (path[0] == '/' && SD.exists(path + 1))
  {
    usePath = path + 1;
  }
  else
  {
    return false;
  }

  File f = SD.open(usePath, "r");
  if (!f)
    return false;

  uint8_t b[24];
  const int n = f.read(b, sizeof(b));
  f.close();
  if (n != (int)sizeof(b))
    return false;

  static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (memcmp(b, sig, 8) != 0)
    return false;

  if (!(b[12] == 'I' && b[13] == 'H' && b[14] == 'D' && b[15] == 'R'))
    return false;

  const int w = (int)((uint32_t)b[16] << 24 | (uint32_t)b[17] << 16 | (uint32_t)b[18] << 8 | (uint32_t)b[19]);
  const int h = (int)((uint32_t)b[20] << 24 | (uint32_t)b[21] << 16 | (uint32_t)b[22] << 8 | (uint32_t)b[23]);

  if (w <= 0 || h <= 0)
    return false;

  if (outW) *outW = w;
  if (outH) *outH = h;
  if (outUsePath) *outUsePath = usePath;
  return true;
}