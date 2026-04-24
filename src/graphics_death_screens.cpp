#include "graphics_death_screens.h"
#include "graphics_mini_stats.h"
#include "graphics_sd_draw.h"
#include "graphics.h"

#include "anim_clips.h"
#include "anim_engine.h"
#include "app_state.h"
#include "death_state.h"
#include "display.h"
#include "pet.h"
#include "save_manager.h"
#include "sdcard.h"
#include "ui_death_menu.h"
#include "ui_runtime.h"

#include <Arduino.h>
#include <time.h>

extern M5Canvas spr;
extern bool g_forcePetBgCache;
extern bool g_sdReady;

static constexpr int MINI_STAT_W = 56;
static constexpr int MINI_STAT_PAD = 4;

struct PetRenderProfile
{
  int w;
  int h;
  int xOff;
  int yOff;
};

// Shared helpers/state still owned by graphics.cpp
const PetRenderProfile &getPetProfile(PetType t);
bool getPngWH(const char *path, int &outW, int &outH);
bool isScreenOn();

void cachePetAreaBackgroundIfNeeded(bool needPetBg);
void restorePetAreaFromCache();
void drawTopBar();
void drawTabBar();
void drawPetPerfHud();

int deathTransitionYNudgeForPet();
AnimId deathTransitionStaticClipForPet();

static uint8_t deathTransitionStaticFrameIndex(const AnimClip *clip)
{
  if (!clip || clip->frameCount == 0)
    return 0;

  // Cheap, deterministic default: use the last frame of the sick clip.
  // If any pet looks weird, we can special-case per clip later.
  return (uint8_t)(clip->frameCount - 1);
}

static void drawDeathTransitionStaticPet()
{
  if (!g_sdReady)
    return;

  const AnimId id = deathTransitionStaticClipForPet();
  const AnimClip *clip = animGetClip(id);
  if (!clip || !clip->frames || clip->frameCount == 0)
    return;

  const uint8_t idx = deathTransitionStaticFrameIndex(clip);
  const char *path = clip->frames[idx];
  if (!path || !*path)
    return;

  const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;
  const int petAreaX = 0;

  const PetRenderProfile &prof = getPetProfile(pet.type);

  const int centerX = petAreaX + (petAreaW / 2) + prof.xOff;
  const int bottomY = (PET_AREA_Y + PET_AREA_H) + prof.yOff;

  int w = 0;
  int h = 0;

  if (getPngWH(path, w, h) && w > 0 && h > 0)
  {
    const int drawX = centerX - (w / 2);
    const int drawY = bottomY - h + deathTransitionYNudgeForPet();
    sprDrawPngFromSD(path, drawX, drawY);
  }
  else
  {
    // Fallback if WH lookup fails.
    sprDrawPngFromSD(path, centerX, bottomY);
  }
}

void drawDeathTransitionScreen(bool redrawBg)
{
  if (!isScreenOn())
    return;

  static PetType s_lastBgPetType = (PetType)255;
  static uint8_t s_lastBgEvoStage = 255;

  const bool petChanged = (s_lastBgPetType != pet.type) || (s_lastBgEvoStage != pet.evoStage);

  // redrawBg should restore from cache, not force a fresh SD/JPEG rebuild.
  const bool needPetBg = petChanged || g_forcePetBgCache;
  
  s_lastBgPetType = pet.type;
  s_lastBgEvoStage = pet.evoStage;

  cachePetAreaBackgroundIfNeeded(needPetBg);
  g_forcePetBgCache = false;

  if (needPetBg)
  {
    restorePetAreaFromCache();
  }

  drawTopBar();
  drawDeathTransitionStaticPet();
  drawMiniStatPreview();
  drawTabBar();
  drawPetPerfHud();
}
