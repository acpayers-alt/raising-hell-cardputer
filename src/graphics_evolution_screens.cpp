#include "graphics_evolution_screens.h"
#include "graphics_sd_draw.h"

#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "pet.h"

#include "anim_clips.h"
#include "anim_engine.h"

extern bool g_sdReady;
extern AppState g_app;
extern Pet pet;

bool getPngWH(const char *path, int &w, int &h);

static AnimId evoHappyClipFor(PetType type, uint8_t stage)
{
  if (stage > 3)
    stage = 3;

  switch (type)
  {
  case PET_DEVIL:
    switch (stage)
    {
    case 0:
      return ANIM_DEV_BABY_HAPPY_BALL;
    case 1:
      return ANIM_DEV_TEEN_HAPPY_POSE;
    case 2:
      return ANIM_DEV_ADULT_HAPPY_TAIL;
    default:
      return ANIM_DEV_ELDER_HAPPY_SHAKE;
    }

  case PET_ELDRITCH:
    switch (stage)
    {
    case 0:
      return ANIM_ELD_BABY_HAPPY_SIT;
    case 1:
      return ANIM_ELD_TEEN_HAPPY_THUMB;
    case 2:
      return ANIM_ELD_ADULT_HAPPY_SPIN;
    default:
      return ANIM_ELD_ELDER_HAPPY_PASS;
    }

  case PET_ALIEN:
    switch (stage)
    {
    case 0:
      return ANIM_ALIEN_BABY_HAPPY;

    case 1:
      return ANIM_ALIEN_TEEN_HAPPY_SWAY;

    case 2:
      return ANIM_ALIEN_ADULT_HAPPY_STANCE;

    case 3:
    default:
      return ANIM_ALIEN_ELDER_HAPPY_SMILE;
    }
  }

  return ANIM_DEV_BABY_HAPPY_BALL;
}

void drawEvolutionScreen()
{
  spr.fillSprite(TFT_BLACK);

  if (g_app.flow.evo.flashWhite)
  {
    spr.fillSprite(TFT_WHITE);
    return;
  }

  const uint8_t stageShown = (g_app.flow.evo.phase >= 2) ? g_app.flow.evo.toStage : g_app.flow.evo.fromStage;

  const AnimId id = evoHappyClipFor(pet.type, stageShown);
  const AnimClip *clip = animGetClip(id);
  if (!clip || !clip->frames || clip->frameCount == 0)
    return;

  const uint32_t now = millis();
  const uint32_t t = (g_app.flow.evo.phaseStartMs == 0) ? 0 : (now - g_app.flow.evo.phaseStartMs);

  uint32_t idx = 0;
  if (clip->frameMs > 0)
    idx = t / clip->frameMs;
  if (clip->loop && clip->frameCount > 0)
    idx %= clip->frameCount;
  if (!clip->loop && idx >= clip->frameCount)
    idx = clip->frameCount - 1;

  const char *path = clip->frames[idx];
  if (!path || !path[0] || !g_sdReady)
    return;

  int w = 0;
  int h = 0;
  const bool gotWH = getPngWH(path, w, h);

  const int cx = screenW / 2;
  const int cy = screenH / 2 + 10;

  const int x = gotWH ? (cx - (w / 2)) : cx;
  const int y = gotWH ? (cy - (h / 2)) : cy;

  sprDrawPngFromSD(path, x, y);
}