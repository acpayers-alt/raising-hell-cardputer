#include "graphics_hatching_screens.h"
#include "graphics.h"

#include <M5GFX.h>
#include <Arduino.h>

#include "app_state.h"
#include "display.h"
#include "new_pet_flow_state.h"
#include "pet.h"

extern M5Canvas spr;
extern bool g_sdReady;

bool sprDrawPngFromSD(const char *path, int x, int y);
bool getPngWH(const char *path, int &outW, int &outH);

static const char *pendingEggCrackedPng()
{
  if (g_pendingPetType == PET_ELDRITCH)
    return "/raising_hell/graphics/pet/egg/eld_egg_cracked.png";

  return "/raising_hell/graphics/pet/egg/dev_egg_cracked.png";
}

static const char *const *pendingEggCrackFrames()
{
  static const char *const devFrames[4] = {
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack1.png",
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack2.png",
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack3.png",
      "/raising_hell/graphics/pet/egg/anim/dev/devil_crack4.png",
  };

  static const char *const eldFrames[4] = {
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack1.png",
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack2.png",
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack3.png",
      "/raising_hell/graphics/pet/egg/anim/eld/eld_crack4.png",
  };

  return (g_pendingPetType == PET_ELDRITCH) ? eldFrames : devFrames;
}

static const char *pendingHatchMessage()
{
  switch (g_pendingPetType)
  {
  case PET_ELDRITCH:
    return "You hatched a baby eldritch";
  case PET_DEVIL:
  default:
    return "You hatched a baby devil";
  }
}

static void drawCenteredImageSpr(const char *path, int cx, int cy)
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
  {
    ok = sprDrawPngFromSD(path, x, y);
  }

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

static void drawCrackedEggBig(int cx, int topY, const char *path)
{
  if (!path || !path[0] || !g_sdReady)
    return;

  int w = 0;
  int h = 0;
  const bool gotWH = getPngWH(path, w, h);

  const int x = gotWH ? (cx - (w / 2)) : cx;
  const int y = topY;

  sprDrawPngFromSD(path, x, y);
}

void drawHatchingScreen(bool redrawBg)
{
  (void)redrawBg;

  spr.fillSprite(TFT_BLACK);

  if (g_app.flow.hatch.flashWhite && !g_app.flow.hatch.showingMsg)
  {
    spr.fillSprite(TFT_WHITE);
    return;
  }

  const int centerX = screenW / 2;
  const int animEggY = screenH / 2;
  const int crackedEggTopY = 4;

  const char *const *crackFrames = pendingEggCrackFrames();

  if (!g_app.flow.hatch.showingMsg)
  {
    if (g_app.flow.hatch.frame < 4)
      drawCenteredImageSpr(crackFrames[g_app.flow.hatch.frame], centerX, animEggY);
    else
      drawCrackedEggBig(centerX, crackedEggTopY, pendingEggCrackedPng());
    return;
  }

  drawCrackedEggBig(centerX, crackedEggTopY, pendingEggCrackedPng());

  spr.setTextDatum(MC_DATUM);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.drawString(pendingHatchMessage(), centerX, 122);
}