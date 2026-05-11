#include "graphics_nonpet_bg.h"

#include <string.h>
#include <strings.h>

#include "display.h"
#include "graphics_sd_draw.h"
#include "pet.h"
#include "sdcard.h"

static const char *PATH_BG_NONPET_TILE_DEV = "/raising_hell/graphics/background/flow/dev_tab_bg.png";
static const char *PATH_BG_NONPET_TILE_ELD = "/raising_hell/graphics/background/flow/eld_tab_bg.png";
static const char *PATH_BG_NONPET_TILE_AL = "/raising_hell/graphics/background/flow/al_tab_bg.png";
static M5Canvas s_nonPetTile(&M5.Display);
static bool s_nonPetTileReady = false;
static bool s_nonPetTileHardFail = false;
static uint32_t s_nonPetTileRetryAfterMs = 0;
static int s_nonPetTileW = 0;
static int s_nonPetTileH = 0;
static PetType s_nonPetTileCachedType = (PetType)255;

static constexpr int NONPET_TILE_W = 35;
static constexpr int NONPET_TILE_H = 70;

static inline const char *nonPetTilePathForPet(PetType t)
{
  switch (t)
  {
  case PET_ALIEN:
    return PATH_BG_NONPET_TILE_AL;

  case PET_ELDRITCH:
    return PATH_BG_NONPET_TILE_ELD;

  case PET_DEVIL:
  default:
    return PATH_BG_NONPET_TILE_DEV;
  }
}

static bool ensureNonPetTileReady()
{
  const uint32_t now = millis();

  if (s_nonPetTileHardFail)
    return false;

  if (s_nonPetTileRetryAfterMs != 0 && now < s_nonPetTileRetryAfterMs)
    return false;

  const PetType desiredType = pet.type;
  const char *path = nonPetTilePathForPet(desiredType);

  if (s_nonPetTileReady && s_nonPetTileW > 0 && s_nonPetTileH > 0 && s_nonPetTileCachedType == desiredType)
  {
    return true;
  }

  s_nonPetTile.deleteSprite();
  s_nonPetTileReady = false;
  s_nonPetTileW = 0;
  s_nonPetTileH = 0;
  s_nonPetTileCachedType = (PetType)255;

  if (!g_sdReady)
    return false;

  s_nonPetTile.setColorDepth(16);
  if (!s_nonPetTile.createSprite(NONPET_TILE_W, NONPET_TILE_H))
  {
    s_nonPetTileRetryAfterMs = now + 30000;

    static uint32_t s_lastCreateFailLogMs = 0;
    if (now - s_lastCreateFailLogMs > 5000)
    {
      Serial.println("[NONPET TILE] createSprite failed; retry delayed");
      s_lastCreateFailLogMs = now;
    }

    return false;
  }

  s_nonPetTile.fillSprite(TFT_BLACK);

  bool ok = false;
  const char *ext = strrchr(path, '.');

  if (ext && (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0))
    ok = canvasDrawJpgFromSD(s_nonPetTile, path, 0, 0);
  else
    ok = canvasDrawPngFromSD(s_nonPetTile, path, 0, 0);

  if (!ok)
  {
    // Do not hard-fail forever on a transient SD/decode miss.
    // Back off retries and fall back to a cheap generated background.
    s_nonPetTileHardFail = false;
    s_nonPetTileRetryAfterMs = now + 30000;

    static char s_lastNonPetTileFailPath[160] = {0};

    const char *failPath = path ? path : "(null)";
    if (strcmp(s_lastNonPetTileFailPath, failPath) != 0)
    {
      strncpy(s_lastNonPetTileFailPath, failPath, sizeof(s_lastNonPetTileFailPath) - 1);
      s_lastNonPetTileFailPath[sizeof(s_lastNonPetTileFailPath) - 1] = '\0';
      Serial.printf("[NONPET TILE] load failed path='%s'\n", failPath);
    }

    s_nonPetTile.deleteSprite();
    return false;
  }

  s_nonPetTileW = s_nonPetTile.width();
  s_nonPetTileH = s_nonPetTile.height();
  s_nonPetTileReady = (s_nonPetTileW > 0 && s_nonPetTileH > 0);

  if (s_nonPetTileReady)
    s_nonPetTileCachedType = desiredType;

  s_nonPetTileHardFail = false;
  s_nonPetTileRetryAfterMs = 0;

  if (s_nonPetTileW != NONPET_TILE_W || s_nonPetTileH != NONPET_TILE_H)
  {
    Serial.printf("[NONPET TILE] unexpected cache size %dx%d expected %dx%d\n", s_nonPetTileW, s_nonPetTileH,
                  NONPET_TILE_W, NONPET_TILE_H);
  }

  return s_nonPetTileReady;
}

void drawNonPetTabBackground()
{
  spr.fillScreen(TFT_BLACK);

  if (!ensureNonPetTileReady())
  {
    spr.fillScreen(TFT_BLACK);
    return;
  }
    
  for (int y = 0; y < SCREEN_H; y += s_nonPetTileH)
  {
    for (int x = 0; x < SCREEN_W; x += s_nonPetTileW)
    {
      s_nonPetTile.pushSprite(&spr, x, y);
    }
  }
}

void graphicsReleaseNonPetTileCache()
{
  s_nonPetTile.deleteSprite();
  s_nonPetTileReady = false;
  s_nonPetTileHardFail = false;
  s_nonPetTileRetryAfterMs = 0;
  s_nonPetTileW = 0;
  s_nonPetTileH = 0;
  s_nonPetTileCachedType = (PetType)255;
}