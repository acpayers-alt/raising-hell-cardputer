#include "graphics_sleep_anim_assets.h"

static const char *PATH_BG_SLEEP_DEVIL = "/raising_hell/graphics/background/sleep_bg.jpg";
static const char *PATH_BG_SLEEP_ELDRITCH = "/raising_hell/graphics/background/eld_sleep.jpg";

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

SleepAnimSelection selectSleepAnimForPet(PetType type, int evoStage)
{
  SleepAnimSelection out = {};

  if (type == PET_DEVIL)
  {
    switch (evoStage)
    {
    case 0:
      out.mode = 1;
      out.frames = DEV_BABY_SLEEP_FRAMES;
      out.frameCount = sizeof(DEV_BABY_SLEEP_FRAMES) / sizeof(DEV_BABY_SLEEP_FRAMES[0]);
      out.frameMs = DEV_BABY_SLEEP_FRAME_MS;
      return out;
    case 1:
      out.mode = 2;
      out.frames = DEV_TEEN_SLEEP_FRAMES;
      out.frameCount = sizeof(DEV_TEEN_SLEEP_FRAMES) / sizeof(DEV_TEEN_SLEEP_FRAMES[0]);
      out.frameMs = DEV_TEEN_SLEEP_FRAME_MS;
      return out;
    case 2:
      out.mode = 3;
      out.frames = DEV_ADULT_SLEEP_FRAMES;
      out.frameCount = sizeof(DEV_ADULT_SLEEP_FRAMES) / sizeof(DEV_ADULT_SLEEP_FRAMES[0]);
      out.frameMs = DEV_ADULT_SLEEP_FRAME_MS;
      return out;
    case 3:
      out.mode = 4;
      out.frames = DEV_ELDER_SLEEP_FRAMES;
      out.frameCount = sizeof(DEV_ELDER_SLEEP_FRAMES) / sizeof(DEV_ELDER_SLEEP_FRAMES[0]);
      out.frameMs = DEV_ELDER_SLEEP_FRAME_MS;
      return out;
    default:
      out.bgPath = PATH_BG_SLEEP_DEVIL;
      return out;
    }
  }

  if (type == PET_ELDRITCH)
  {
    switch (evoStage)
    {
    case 0:
      out.mode = 5;
      out.frames = ELD_BABY_SLEEP_FRAMES;
      out.frameCount = sizeof(ELD_BABY_SLEEP_FRAMES) / sizeof(ELD_BABY_SLEEP_FRAMES[0]);
      out.frameMs = ELD_BABY_SLEEP_FRAME_MS;
      return out;
    case 1:
      out.mode = 6;
      out.frames = ELD_TEEN_SLEEP_FRAMES;
      out.frameCount = sizeof(ELD_TEEN_SLEEP_FRAMES) / sizeof(ELD_TEEN_SLEEP_FRAMES[0]);
      out.frameMs = ELD_TEEN_SLEEP_FRAME_MS;
      return out;
    case 2:
      out.mode = 7;
      out.frames = ELD_ADULT_SLEEP_FRAMES;
      out.frameCount = sizeof(ELD_ADULT_SLEEP_FRAMES) / sizeof(ELD_ADULT_SLEEP_FRAMES[0]);
      out.frameMs = ELD_ADULT_SLEEP_FRAME_MS;
      return out;
    case 3:
      out.mode = 8;
      out.frames = ELD_ELDER_SLEEP_FRAMES;
      out.frameCount = sizeof(ELD_ELDER_SLEEP_FRAMES) / sizeof(ELD_ELDER_SLEEP_FRAMES[0]);
      out.frameMs = ELD_ELDER_SLEEP_FRAME_MS;
      return out;
    default:
      out.bgPath = PATH_BG_SLEEP_ELDRITCH;
      return out;
    }
  }

  out.bgPath = PATH_BG_SLEEP_DEVIL;
  return out;
}