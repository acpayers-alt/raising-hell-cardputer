#include "graphics_sleep_screens.h"

// -----------------------------------------------------------------------------
// Core includes
// -----------------------------------------------------------------------------
#include "anim_clips.h"
#include "display.h"
#include "graphics.h"
#include "graphics_assets.h"
#include "graphics_mini_stats.h"
#include "graphics_sd_draw.h"
#include "pet.h"

// -----------------------------------------------------------------------------
// External state (owned elsewhere)
// -----------------------------------------------------------------------------
extern Pet pet;
extern bool g_sdReady;

// Sleep background kick (owned by graphics.cpp)
extern volatile bool g_sleepBgKick;

// Shared path + cache (owned by graphics.cpp for now)
extern const char *PATH_BG_SLEEP;
extern uint16_t **s_sleepAnimFrameCache;

// -----------------------------------------------------------------------------
// External systems (owned elsewhere)
// -----------------------------------------------------------------------------

// UI / render loop
void requestUIRedraw();
bool isScreenOn();

// Shared drawing helpers
void drawTopBar();
void drawSleepMeterBar();

// Sleep frame cache subsystem
void freeSleepAnimFrameCache();
bool ensureSleepAnimFrameCache(uint8_t mode, const char *const *frames, uint8_t frameCount, int x, int y);

// -----------------------------------------------------------------------------
// Sleep module state (owned here)
// -----------------------------------------------------------------------------
static volatile bool g_sleepBgWakeKick = false;

static uint32_t g_sleepAnimNextFrameMs = 0;
static bool g_sleepAnimActive = false;

// -----------------------------------------------------------------------------
// Forward declarations (private to this module)
// -----------------------------------------------------------------------------
static void drawSleepScreenImpl(bool redrawBg);

void sleepBgNotifyScreenWake()
{
  g_sleepBgWakeKick = true;
  requestUIRedraw();
}

void sleepAnimHeartbeat(uint32_t now)
{
  if (!g_sleepAnimActive)
    return;
  if (g_sleepAnimNextFrameMs == 0)
    return;

  if ((int32_t)(now - g_sleepAnimNextFrameMs) >= 0)
  {
    requestUIRedraw();
  }
}

static inline const char *sleepBgForPet(PetType type)
{
  switch (type)
  {
  case PET_ELDRITCH:
    return "/raising_hell/graphics/background/eld_sleep.jpg";
  case PET_DEVIL:
  default:
    return PATH_BG_SLEEP;
  }
}

// -----------------------------------------------------------------------------
// DEVIL BABY sleep background animation (4 JPG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_BABY_SLEEP_FRAME_MS = 200;

static const char *DEV_BABY_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/bb/sleep/dev_baby_sleepbk4.jpg",
};

static inline bool useDevBabySleepAnim() { return (pet.type == PET_DEVIL) && (pet.evoStage == 0); }

// -----------------------------------------------------------------------------
// DEVIL TEEN sleep background animation (4 JPG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_TEEN_SLEEP_FRAME_MS = 180;

static const char *DEV_TEEN_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/tn/sleep/dev_teen_sleepbk4.jpg",
};

static inline bool useDevTeenSleepAnim() { return (pet.type == PET_DEVIL) && (pet.evoStage == 1); }

// -----------------------------------------------------------------------------
// DEVIL ADULT sleep background animation (4 JPG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_ADULT_SLEEP_FRAME_MS = 160;

static const char *DEV_ADULT_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/ad/sleep/dev_adult_sleepbk4.jpg",
};

static inline bool useDevAdultSleepAnim() { return (pet.type == PET_DEVIL) && (pet.evoStage == 2); }

// -----------------------------------------------------------------------------
// DEVIL ELDER sleep background animation (4 JPG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t DEV_ELDER_SLEEP_FRAME_MS = 200;
static constexpr uint8_t DEV_ELDER_SLEEP_FRAME_COUNT = 4;

static const char *DEV_ELDER_SLEEP_FRAMES[DEV_ELDER_SLEEP_FRAME_COUNT] = {
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk1.jpg",
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk2.jpg",
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk3.jpg",
    "/raising_hell/graphics/pet/anim/dev/ed/sleep/dev_el_sleepbk4.jpg",
};

static inline bool useDevElderSleepAnim() { return (pet.type == PET_DEVIL) && (pet.evoStage == 3); }

// -----------------------------------------------------------------------------
// ELDRITCH BABY sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_BABY_SLEEP_FRAME_MS = 200;

static const char *ELD_BABY_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk3.png",
    "/raising_hell/graphics/pet/anim/eld/bb/sleep/eld_bb_sleepbk4.png",
};

static inline bool useEldBabySleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 0); }

// -----------------------------------------------------------------------------
// ELDRITCH TEEN sleep background animation (3 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_TEEN_SLEEP_FRAME_MS = 200;
static constexpr uint8_t ELD_TEEN_SLEEP_FRAME_COUNT = 3;

static const char *ELD_TEEN_SLEEP_FRAMES[ELD_TEEN_SLEEP_FRAME_COUNT] = {
    "/raising_hell/graphics/pet/anim/eld/tn/sleep/eld_tn_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sleep/eld_tn_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/tn/sleep/eld_tn_sleepbk3.png",
};

static inline bool useEldTeenSleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 1); }

// -----------------------------------------------------------------------------
// ELDRITCH ADULT sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_ADULT_SLEEP_FRAME_MS = 180;

static const char *ELD_ADULT_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk3.png",
    "/raising_hell/graphics/pet/anim/eld/ad/sleep/eld_ad_sleepbk4.png",
};

static inline bool useEldAdultSleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 2); }

// -----------------------------------------------------------------------------
// ELDRITCH ELDER sleep background animation (4 PNG frames)
// -----------------------------------------------------------------------------
static constexpr uint32_t ELD_ELDER_SLEEP_FRAME_MS = 180;

static const char *ELD_ELDER_SLEEP_FRAMES[4] = {
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk1.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk2.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk3.png",
    "/raising_hell/graphics/pet/anim/eld/ed/sleep/eld_ed_sleepbk4.png",
};

static inline bool useEldElderSleepAnim() { return (pet.type == PET_ELDRITCH) && (pet.evoStage == 3); }

static void drawSleepScreenImpl(bool redrawBg)
{
  if (!isScreenOn())
    return;

  static uint8_t s_frame = 0;
  static uint32_t s_nextFrameMs = 0;
  static bool s_hasBg = false;

  static uint8_t s_mode = 0;

  const uint32_t now = millis();

  const bool kick = g_sleepBgKick;
  if (kick)
    g_sleepBgKick = false;

  const bool wakeKick = g_sleepBgWakeKick;
  if (wakeKick)
    g_sleepBgWakeKick = false;

  const bool babyAnim = useDevBabySleepAnim();
  const bool teenAnim = useDevTeenSleepAnim();
  const bool adultAnim = useDevAdultSleepAnim();
  const bool elderAnim = useDevElderSleepAnim();
  const bool eldBabyAnim = useEldBabySleepAnim();
  const bool eldTeenAnim = useEldTeenSleepAnim();
  const bool eldAdultAnim = useEldAdultSleepAnim();
  const bool eldElderAnim = useEldElderSleepAnim();

  uint8_t newMode = 0;

  if (babyAnim)
    newMode = 1;
  else if (teenAnim)
    newMode = 2;
  else if (adultAnim)
    newMode = 3;
  else if (elderAnim)
    newMode = 4;
  else if (eldBabyAnim)
    newMode = 5;
  else if (eldTeenAnim)
    newMode = 6;
  else if (eldAdultAnim)
    newMode = 7;
  else if (eldElderAnim)
    newMode = 8;

  if (newMode != s_mode)
  {
    s_mode = newMode;
    s_frame = 0;
    s_nextFrameMs = 0;
    s_hasBg = false;
    redrawBg = true;
    freeSleepAnimFrameCache();
  }

  bool frameChanged = false;

  const char *bgPath = nullptr;

  static uint8_t s_lastMode = 0;
  static bool s_animInited = false;

  const bool modeChanged = (s_mode != s_lastMode);

  const char *const *frames = nullptr;
  uint8_t frameCount = 0;
  uint32_t frameMs = 0;

  switch (s_mode)
  {
  case 1:
    frames = DEV_BABY_SLEEP_FRAMES;
    frameCount = sizeof(DEV_BABY_SLEEP_FRAMES) / sizeof(DEV_BABY_SLEEP_FRAMES[0]);
    frameMs = DEV_BABY_SLEEP_FRAME_MS;
    break;
  case 2:
    frames = DEV_TEEN_SLEEP_FRAMES;
    frameCount = sizeof(DEV_TEEN_SLEEP_FRAMES) / sizeof(DEV_TEEN_SLEEP_FRAMES[0]);
    frameMs = DEV_TEEN_SLEEP_FRAME_MS;
    break;
  case 3:
    frames = DEV_ADULT_SLEEP_FRAMES;
    frameCount = sizeof(DEV_ADULT_SLEEP_FRAMES) / sizeof(DEV_ADULT_SLEEP_FRAMES[0]);
    frameMs = DEV_ADULT_SLEEP_FRAME_MS;
    break;
  case 4:
    frames = DEV_ELDER_SLEEP_FRAMES;
    frameCount = sizeof(DEV_ELDER_SLEEP_FRAMES) / sizeof(DEV_ELDER_SLEEP_FRAMES[0]);
    frameMs = DEV_ELDER_SLEEP_FRAME_MS;
    break;
  case 5:
    frames = ELD_BABY_SLEEP_FRAMES;
    frameCount = sizeof(ELD_BABY_SLEEP_FRAMES) / sizeof(ELD_BABY_SLEEP_FRAMES[0]);
    frameMs = ELD_BABY_SLEEP_FRAME_MS;
    break;
  case 6:
    frames = ELD_TEEN_SLEEP_FRAMES;
    frameCount = sizeof(ELD_TEEN_SLEEP_FRAMES) / sizeof(ELD_TEEN_SLEEP_FRAMES[0]);
    frameMs = ELD_TEEN_SLEEP_FRAME_MS;
    break;
  case 7:
    frames = ELD_ADULT_SLEEP_FRAMES;
    frameCount = sizeof(ELD_ADULT_SLEEP_FRAMES) / sizeof(ELD_ADULT_SLEEP_FRAMES[0]);
    frameMs = ELD_ADULT_SLEEP_FRAME_MS;
    break;
  case 8:
    frames = ELD_ELDER_SLEEP_FRAMES;
    frameCount = sizeof(ELD_ELDER_SLEEP_FRAMES) / sizeof(ELD_ELDER_SLEEP_FRAMES[0]);
    frameMs = ELD_ELDER_SLEEP_FRAME_MS;
    break;
  default:
    bgPath = sleepBgForPet(pet.type);
    s_lastMode = s_mode;
    break;
  }

  const bool anyKick = (kick || wakeKick);

  if (anyKick && frames && frameCount > 0 && frameMs > 0)
  {
    s_animInited = true;
    s_nextFrameMs = now;

    if (frameCount > 1)
    {
      s_frame = (uint8_t)((s_frame + 1) % frameCount);
      frameChanged = true;
    }

    s_hasBg = false;
  }

  if (frames && frameCount > 0 && frameMs > 0)
  {
    if (!s_animInited || modeChanged)
    {
      s_animInited = true;

      if (s_nextFrameMs == 0)
        s_frame = 0;

      s_nextFrameMs = now;
      frameChanged = true;
      s_hasBg = false;

      freeSleepAnimFrameCache();
    }
    else
    {
      const int32_t late = (int32_t)(now - s_nextFrameMs);
      if (late >= 0)
      {
        uint32_t steps = 1u + (uint32_t)late / (uint32_t)frameMs;
        if (steps > frameCount)
          steps = frameCount;

        s_frame = (uint8_t)((s_frame + steps) % frameCount);
        s_nextFrameMs += steps * frameMs;
        frameChanged = true;
      }
    }

    bgPath = frames[s_frame];
  }

  s_lastMode = s_mode;

  g_sleepAnimActive = (frames && frameCount > 0 && frameMs > 0);
  g_sleepAnimNextFrameMs = (g_sleepAnimActive ? s_nextFrameMs : 0);

  const bool needBgDraw = redrawBg || frameChanged || !s_hasBg;

  if (needBgDraw)
  {
    bool ok = false;

    if (s_mode != 0 && frames && frameCount > 0)
    {
      if (ensureSleepAnimFrameCache(s_mode, frames, frameCount, 0, 18))
      {
        uint16_t *sprBuf = (uint16_t *)spr.getBuffer();
        if (sprBuf && s_sleepAnimFrameCache && s_sleepAnimFrameCache[s_frame])
        {
          const size_t pxCount = (size_t)SCREEN_W * (size_t)SCREEN_H;
          memcpy(sprBuf, s_sleepAnimFrameCache[s_frame], pxCount * sizeof(uint16_t));
          ok = true;
        }
      }
    }

    if (!ok)
    {
      if (g_sdReady && bgPath)
      {
        const char *ext = strrchr(bgPath, '.');
        const bool isPng = (ext && (strcasecmp(ext, ".png") == 0));
        if (isPng)
          ok = sprDrawPngFromSD(bgPath, 0, 18);
        else
          ok = sprDrawJpgFromSD(bgPath, 0, 18);
      }
    }

    if (!ok)
    {
      spr.fillRect(0, 0, SCREEN_W, SCREEN_H, TFT_BLACK);
      s_hasBg = false;
    }
    else
    {
      s_hasBg = true;
    }
  }

  drawTopBar();
  drawMiniStatPreviewSleepLeft();
  drawSleepMeterBar();
}

void drawSleepScreen() { drawSleepScreenImpl(true); }
