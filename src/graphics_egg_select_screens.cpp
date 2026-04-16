#include "graphics_egg_select_screens.h"
#include "graphics.h"

#include <M5GFX.h>
#include <Arduino.h>

#include "display.h"
#include "pet.h"
#include "save_manager.h"
#include "graphics_render_utils.h"

extern M5Canvas spr;
extern bool g_sdReady;

static const char *eggPreviewPathForType(PetType type);
static const char *eggLabelForType(PetType type);

bool sprDrawPngFromSD(const char *path, int x, int y);
bool isScreenOn();

static constexpr const char *DEV_EGG_PNG = "/raising_hell/graphics/pet/egg/dev_egg.png";
static constexpr const char *ELD_EGG_PNG = "/raising_hell/graphics/pet/egg/eld_egg.png";

void drawChoosePetScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;
  if (redrawBg)
    spr.fillSprite(TFT_BLACK);

  drawCenteredLine("Choose Your Egg", 18, 2, 1);

  const int eggW = 64;
  const int eggH = 64;
  const int eggX = (SCREEN_W - eggW) / 2;
  const int eggY = 38;

  const char *eggPath = nullptr;
  switch (pet.type)
  {
  case PET_ELDRITCH:
    eggPath = ELD_EGG_PNG;
    break;
  case PET_DEVIL:
  default:
    eggPath = DEV_EGG_PNG;
    break;
  }

  bool ok = false;
  if (g_sdReady && eggPath)
  {
    ok = sprDrawPngFromSD(eggPath, eggX, eggY);
  }

  if (!ok)
  {
    spr.fillEllipse(eggX + eggW / 2, eggY + eggH / 2, eggW / 2, eggH / 2, TFT_WHITE);
    spr.drawEllipse(eggX + eggW / 2, eggY + eggH / 2, eggW / 2, eggH / 2, TFT_RED);
  }

  const int arrowOffsetX = 14;
  const int arrowY = eggY + (eggH / 2) - 4;

  spr.setTextDatum(TL_DATUM);
  spr.drawString("<", eggX - arrowOffsetX, arrowY);
  spr.drawString(">", eggX + eggW + arrowOffsetX - 6, arrowY);

  const char *label = "Unknown Egg";
  switch (pet.type)
  {
  case PET_ELDRITCH:
    label = "Eldritch Egg";
    break;
  case PET_DEVIL:
  default:
    label = "Devil Egg";
    break;
  }
  
  const int eggBottomY = eggY + eggH;
  const int EGG_LABEL_Y = eggBottomY + 2;
  int EGG_PROMPT_Y = screenH - 10; // push down

  // Bigger, more prominent label
  drawCenteredLine(label, EGG_LABEL_Y, 2, 1);

  // Keep prompt smaller
  drawCenteredLine("Press ENTER to hatch", EGG_PROMPT_Y, 1, 1);

#if SAVE_DIAG_ENABLED
  {
    const uint8_t e = saveManagerLastLoadErr();
    const uint32_t sz = saveManagerLastLoadSize();

    char buf[96];
    snprintf(buf, sizeof(buf), "ERR=%u FS=%lu SP=%u SV2=%u", (unsigned)e, (unsigned long)sz,
             (unsigned)sizeof(SavePayload), (unsigned)sizeof(SavePayloadV2));

    spr.setTextSize(1);
    spr.setTextColor(TFT_YELLOW, TFT_BLACK);
    spr.drawString(buf, 2, SCREEN_H - 10);
  }
#endif
}
