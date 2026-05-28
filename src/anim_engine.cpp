#include "anim_engine.h"

#include "M5Cardputer.h"

#include <FS.h>
#include <SD.h>
#include <lgfx/v1/misc/DataWrapper.hpp>

#include "anim_clips.h"
#include "app_state.h"
#include "display.h"
#include "graphics.h"
#include "graphics_pet_presentation.h"
#include "runtime_flags_state.h"
#include "sdcard.h"
#include "ui_invalidate.h"

static PetPerfStats s_petPerfStats;

const PetPerfStats &petPerfStats() { return s_petPerfStats; }

static bool s_frameChanged = false;

static void applyThoughtBubbleOffset(AnimId id, bool healthBubble, bool restBubble, int &bubbleX, int &bubbleY);

void petPerfResetStats()
{
  s_petPerfStats.petFrameMs = 0;
  s_petPerfStats.petSpriteDrawMs = 0;
  s_petPerfStats.animStepMs = 0;
}

static uint16_t smoothPerfMs(uint16_t oldValue, uint16_t sample) { return (uint16_t)((oldValue * 3u + sample) / 4u); }

// -----------------------------------------------------------------------------
// Arduino File -> LovyanGFX DataWrapper (same as pet_anim.cpp, but centralized)
// -----------------------------------------------------------------------------
class ArduinoFileDataWrapper : public lgfx::v1::DataWrapper
{
public:
  explicit ArduinoFileDataWrapper(File *f) : _f(f) {}

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
    uint32_t pos = (uint32_t)_f->position();
    _f->seek(pos + offset);
  }

  bool seek(uint32_t offset) override
  {
    if (!_f)
      return false;
    return _f->seek(offset);
  }

  void close() override
  {
    // No-op
  }

  int32_t tell() override
  {
    if (!_f)
      return 0;
    return (int32_t)_f->position();
  }

private:
  File *_f = nullptr;
};

static bool drawPngPathAnim(const char *path, int x, int y)
{
  if (!g_sdReady || !path)
    return false;

  const uint32_t sdStartMs = millis();
  const bool ok = sprDrawPngFromSD(path, x, y);
  const uint16_t sample = (uint16_t)(millis() - sdStartMs);
  s_petPerfStats.petSpriteDrawMs = smoothPerfMs(s_petPerfStats.petSpriteDrawMs, sample);
  return ok;
}

static const char *kThoughtRestFrames[] = {
    "/raising_hell/graphics/ui/thought_bubbles/thought_rest1.png",
    "/raising_hell/graphics/ui/thought_bubbles/thought_rest2.png",
};

static uint8_t s_restThoughtFrame = 0;
static uint32_t s_restThoughtNextMs = 0;

static const char *kThoughtBurgerFrames[] = {
    "/raising_hell/graphics/ui/thought_bubbles/thought_burger1.png",
    "/raising_hell/graphics/ui/thought_bubbles/thought_burger2.png",
};

static uint8_t s_burgerThoughtFrame = 0;
static uint32_t s_burgerThoughtNextMs = 0;

static bool animUsesRestThought(AnimId id)
{
  return id == ANIM_DEV_BABY_SLEEPY_NOD || id == ANIM_DEV_TEEN_SLEEPY_BOB || id == ANIM_DEV_ADULT_TIRED_CHAIR_IDLE ||
         id == ANIM_DEV_ADULT_TIRED_CHAIR_BLINK || id == ANIM_DEV_ELDER_TIRED_SIT || id == ANIM_ELD_BABY_SLEEPY_YAWN ||
         id == ANIM_ELD_TEEN_TIRED_NOD || id == ANIM_ELD_ADULT_SLEEPY_DRINK || id == ANIM_ELD_ELDER_SLEEPY_HOLD ||
         id == ANIM_ALIEN_BABY_TIRED_LAY || id == ANIM_ALIEN_BABY_TIRED_HOLD || id == ANIM_ALIEN_TEEN_TIRED_SNORE ||
         id == ANIM_ALIEN_TEEN_TIRED_BLINK || id == ANIM_ALIEN_ADULT_TIRED_NOD;
}

static bool animUsesBurgerThought(AnimId id)
{
  return id == ANIM_DEV_BABY_HUNGRY_RUB || id == ANIM_DEV_TEEN_HUNGRY_RUB || id == ANIM_DEV_ADULT_HUNGRY_BEND ||
         id == ANIM_DEV_ELDER_HUNGRY_RUB || id == ANIM_ELD_BABY_HUNGRY_RUB || id == ANIM_ELD_TEEN_HUNGRY_BITE ||
         id == ANIM_ELD_ADULT_HUNGRY_SHAKE || id == ANIM_ELD_ELDER_HUNGRY_EAT || id == ANIM_ALIEN_BABY_HUNGRY_STAND ||
         id == ANIM_ALIEN_TEEN_HUNGRY_FORK || id == ANIM_ALIEN_ADULT_HUNGRY_BEND;
}

static bool animUsesHealthThought(AnimId id)
{
  return id == ANIM_DEV_BABY_SICK_CRAWL || id == ANIM_DEV_TEEN_SICK_BOB || id == ANIM_DEV_ADULT_SICK_LAY ||
         id == ANIM_DEV_ELDER_SICK_COUGH || id == ANIM_ELD_BABY_SICK_BOB || id == ANIM_ELD_TEEN_SICK_SNEEZE ||
         id == ANIM_ELD_ADULT_SICK_HUNCH || id == ANIM_ELD_ELDER_SICK_SNEEZE || id == ANIM_ALIEN_BABY_SICK_MOAN ||
         id == ANIM_ALIEN_BABY_SICK_SNEEZE || id == ANIM_ALIEN_TEEN_SICK_LOOP || id == ANIM_ALIEN_ADULT_SICK_HEAVE;
}

static void resetRestThought()
{
  s_restThoughtFrame = 0;
  s_restThoughtNextMs = 0;
}

static uint8_t tickRestThoughtFrame()
{
  const uint32_t now = millis();

  if (s_restThoughtNextMs == 0)
  {
    s_restThoughtFrame = 0;
    s_restThoughtNextMs = now + 260;
    return s_restThoughtFrame;
  }

  if ((int32_t)(now - s_restThoughtNextMs) < 0)
    return s_restThoughtFrame;

  s_restThoughtFrame ^= 1;
  s_restThoughtNextMs = now + 260;

  return s_restThoughtFrame;
}

static void drawRestThoughtBubbleForTiredPet(int petDrawX, int petDrawY, int petW, AnimId id)
{
  const uint8_t frame = tickRestThoughtFrame();
  const char *path = kThoughtRestFrames[frame];

  if (!path || !path[0])
    return;

  int bubbleX = petDrawX + petW - 10;
  int bubbleY = petDrawY - 4;

  applyThoughtBubbleOffset(id, false, true, bubbleX, bubbleY); // burger

  (void)drawPngPathAnim(path, bubbleX, bubbleY);
}

static void resetBurgerThought()
{
  s_burgerThoughtFrame = 0;
  s_burgerThoughtNextMs = 0;
}

static uint8_t tickBurgerThoughtFrame()
{
  const uint32_t now = millis();

  if (s_burgerThoughtNextMs == 0)
  {
    s_burgerThoughtFrame = 0;
    s_burgerThoughtNextMs = now + 260;
    return s_burgerThoughtFrame;
  }

  if ((int32_t)(now - s_burgerThoughtNextMs) < 0)
    return s_burgerThoughtFrame;

  s_burgerThoughtFrame ^= 1;
  s_burgerThoughtNextMs = now + 260;

  return s_burgerThoughtFrame;
}

static void applyThoughtBubbleOffset(AnimId id, bool healthBubble, bool restBubble, int &bubbleX, int &bubbleY)
{
  if (healthBubble && id == ANIM_ALIEN_TEEN_SICK_LOOP)
  {
    bubbleX -= 80;
    bubbleY -= 30;
    return;
  }

  if (restBubble)
  {
    if (id == ANIM_DEV_TEEN_SLEEPY_BOB)
    {
      bubbleX -= 8;
      return;
    }

    if (id == ANIM_DEV_ELDER_TIRED_SIT)
    {
      bubbleX -= 8;
      return;
    }

    if (id == ANIM_ELD_BABY_SLEEPY_YAWN)
    {
      bubbleX -= 16;
      return;
    }

    if (id == ANIM_ELD_TEEN_TIRED_NOD)
    {
      bubbleX -= 8;
      return;
    }

    if (id == ANIM_ELD_ADULT_SLEEPY_DRINK)
    {
      bubbleX -= 16;
      return;
    }

    if (id == ANIM_ELD_ELDER_SLEEPY_HOLD)
    {
      bubbleX -= 12;
      return;
    }

    return;
  }

  if (healthBubble)
  {
    if (id == ANIM_DEV_BABY_SICK_CRAWL)
    {
      bubbleX -= 72;
      bubbleY -= 8;
      return;
    }
    if (id == ANIM_DEV_ADULT_SICK_LAY)
    {
      bubbleX -= 14;
      bubbleY -= 4;
      return;
    }
    if (id == ANIM_DEV_ELDER_SICK_COUGH)
    {
      bubbleX -= 14;
      bubbleY -= 4;
      return;
    }
    if (id == ANIM_ELD_ADULT_SICK_HUNCH)
    {
      bubbleX -= 10;
      return;
    }

    if (id == ANIM_ELD_ELDER_SICK_SNEEZE)
    {
      bubbleX -= 10;
      return;
    }
  }

  if (!healthBubble)
  {
    if (id == ANIM_DEV_TEEN_HUNGRY_RUB)
    {
      bubbleX -= 8;
      return;
    }

    if (id == ANIM_ELD_TEEN_HUNGRY_BITE)
    {
      bubbleX -= 8;
      return;
    }

    if (id == ANIM_ELD_ADULT_HUNGRY_SHAKE)
    {
      bubbleY += 10;
      return;
    }

    if (id == ANIM_ELD_ELDER_HUNGRY_EAT)
    {
      bubbleX -= 6;
      return;
    }

    if (id == ANIM_DEV_ADULT_HUNGRY_BEND || id == ANIM_DEV_ELDER_HUNGRY_RUB)
    {
      bubbleX -= 18;
      return;
    }
  }
}

static void drawBurgerThoughtBubbleForHungryAlienBaby(int petDrawX, int petDrawY, int petW, AnimId id)
{
  const uint8_t frame = tickBurgerThoughtFrame();
  const char *path = kThoughtBurgerFrames[frame];

  if (!path || !path[0])
    return;

  // Position relative to the sprite so this can be reused/tuned later.
  int bubbleX = petDrawX + petW - 10;
  int bubbleY = petDrawY - 4;

  applyThoughtBubbleOffset(id, false, false, bubbleX, bubbleY);

  (void)drawPngPathAnim(path, bubbleX, bubbleY);
}

// Read PNG width/height from IHDR. Fast and avoids drawing just to measure.
static bool pngReadWH(const char *path, int *outW, int *outH)
{
  if (!g_sdReady || !path || !outW || !outH)
    return false;

  File f = SD.open(path, FILE_READ);
  if (!f)
    return false;

  uint8_t buf[24];
  const int n = (int)f.read(buf, sizeof(buf));
  f.close();
  if (n < (int)sizeof(buf))
    return false;

  // PNG signature
  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  for (int i = 0; i < 8; i++)
  {
    if (buf[i] != sig[i])
      return false;
  }

  // Chunk type should be IHDR at bytes 12..15
  if (buf[12] != 'I' || buf[13] != 'H' || buf[14] != 'D' || buf[15] != 'R')
    return false;

  auto be32 = [&](int off) -> int
  {
    return (int)((uint32_t)buf[off] << 24 | (uint32_t)buf[off + 1] << 16 | (uint32_t)buf[off + 2] << 8 |
                 (uint32_t)buf[off + 3]);
  };

  const int w = be32(16);
  const int h = be32(20);
  if (w <= 0 || h <= 0)
    return false;

  *outW = w;
  *outH = h;
  return true;
}

struct PngSizeCacheEntry
{
  const char *path;
  int16_t w;
  int16_t h;
};

static PngSizeCacheEntry s_pngSizeCache[24] = {};
static uint8_t s_pngSizeCacheNext = 0;

static bool pngReadWHCached(const char *path, int *outW, int *outH)
{
  if (!path || !outW || !outH)
    return false;

  for (const auto &e : s_pngSizeCache)
  {
    if (e.path == path && e.w > 0 && e.h > 0)
    {
      *outW = e.w;
      *outH = e.h;
      return true;
    }
  }

  int w = 0;
  int h = 0;
  if (!pngReadWH(path, &w, &h))
    return false;

  s_pngSizeCache[s_pngSizeCacheNext].path = path;
  s_pngSizeCache[s_pngSizeCacheNext].w = (int16_t)w;
  s_pngSizeCache[s_pngSizeCacheNext].h = (int16_t)h;
  s_pngSizeCacheNext = (uint8_t)((s_pngSizeCacheNext + 1) % (sizeof(s_pngSizeCache) / sizeof(s_pngSizeCache[0])));

  *outW = w;
  *outH = h;
  return true;
}

static const char *kThoughtHealthFrames[] = {
    "/raising_hell/graphics/ui/thought_bubbles/thought_health1.png",
    "/raising_hell/graphics/ui/thought_bubbles/thought_health2.png",
};

static uint8_t s_healthThoughtFrame = 0;
static uint32_t s_healthThoughtNextMs = 0;

static void resetHealthThought()
{
  s_healthThoughtFrame = 0;
  s_healthThoughtNextMs = 0;
}

static uint8_t tickHealthThoughtFrame()
{
  const uint32_t now = millis();

  if (s_healthThoughtNextMs == 0)
  {
    s_healthThoughtFrame = 0;
    s_healthThoughtNextMs = now + 260;
    return s_healthThoughtFrame;
  }

  if ((int32_t)(now - s_healthThoughtNextMs) < 0)
    return s_healthThoughtFrame;

  s_healthThoughtFrame ^= 1;
  s_healthThoughtNextMs = now + 260;

  return s_healthThoughtFrame;
}

static void drawHealthThoughtBubbleForSickAlienBaby(int petDrawX, int petDrawY, int petW, AnimId id)
{
  const uint8_t frame = tickHealthThoughtFrame();
  const char *path = kThoughtHealthFrames[frame];

  if (!path || !path[0])
    return;

  int bubbleX = petDrawX + petW - 10;
  int bubbleY = petDrawY - 4;

  applyThoughtBubbleOffset(id, true, false, bubbleX, bubbleY);

  (void)drawPngPathAnim(path, bubbleX, bubbleY);
}

// -----------------------------------------------------------------------------
// Engine state
// -----------------------------------------------------------------------------
static AnimId s_baseId = ANIM_NONE;
static uint8_t s_baseIdx = 0;
static uint32_t s_baseNextMs = 0;

static AnimId s_overrideId = ANIM_NONE;
static uint8_t s_overrideIdx = 0;
static uint32_t s_overrideNextMs = 0;
static bool s_overridePlaying = false;

static uint32_t s_nextTriggerMs = 0;

static uint32_t randRangeInclusive(uint32_t lo, uint32_t hi)
{
  if (hi <= lo)
    return lo;
  return (uint32_t)random((long)lo, (long)(hi + 1));
}

static bool canStartTriggerOverride(AnimId baseId, AnimId triggerId)
{
  if (baseId == ANIM_ALIEN_ELDER_BORED_BLINK && triggerId == ANIM_ALIEN_ELDER_BORED_BEAM)
  {
    // Do not let the beam animation run invisibly behind wander/intro walking.
    // If it does, the completion hook still fires and causes a random walk-on.
    return !petPresentationAnimating();
  }

  return true;
}

static uint16_t frameMsForAnimFrame(AnimId id, uint8_t idx, uint16_t fallbackMs)
{
  if (id == ANIM_ALIEN_TEEN_SICK_LOOP)
  {
    // Sick animation pacing:
    // frame 1 = quick motion
    // frame 2 = weak pause
    // frame 3 = long exhausted hold

    if (idx == 1)
      return 2000;

    if (idx == 2)
      return 5000;

    return 180;
  }

  if (id == ANIM_ALIEN_TEEN_BORED_STOMP)
  {
    // Hold stomp landing frame.
    if (idx == 2)
      return 3000;

    return 180;
  }

  if (id == ANIM_ALIEN_TEEN_ANGRY_SABER)
  {
    // Hold final saber pose dramatically.
    if (idx == 3)
      return 3000;

    return 120;
  }

  if (id == ANIM_ALIEN_ADULT_HAPPY_STANCE)
  {
    // Adult alien happy stance:
    // frame 1 holds as the main pose, then frames 2/3 animate normally.
    if (idx == 0)
      return 3000;

    return 220;
  }

  if (id == ANIM_ALIEN_ADULT_BORED_SIGH)
  {
    // Adult alien bored sigh:
    // 1 -> 2 -> 3, hold frame 3, then 2 -> repeat.
    if (idx == 2)
      return 2000;

    return 220;
  }

  if (id == ANIM_ALIEN_ADULT_ANGRY_SHOOT)
  {
    // Adult alien angry shoot:
    // 1 hold -> 2 -> 3 hold -> 4 -> 3 hold -> 4 -> 3 hold -> 4 -> 2 -> repeat.
    if (idx == 0)
      return 3000;

    if (idx == 2 || idx == 4 || idx == 6)
      return 1000;

    return 120;
  }

  if (id == ANIM_ALIEN_ELDER_ANGRY_SHAKE)
  {
    // Elder alien angry shake:
    // 1 hold -> 2 -> 3 -> 2 -> 3 -> 2 -> 3 -> repeat.
    if (idx == 0)
      return 3000;

    return fallbackMs;
  }

  if (id == ANIM_ALIEN_ADULT_HUNGRY_BEND)
  {
    // Adult alien hungry bend:
    // frame 1 = 1 second, frame 2 = 3 seconds.
    if (idx == 0)
      return 1000;

    if (idx == 1)
      return 3000;

    return fallbackMs;
  }

  if (id == ANIM_ALIEN_ELDER_HAPPY_SMILE)
  {
    // Elder alien happy smile:
    // 1 hold -> 2 -> 3 -> 2 -> repeat.
    if (idx == 0)
      return 2000;

    return fallbackMs;
  }

  return fallbackMs;
}

static void markFrameChanged()
{
  s_frameChanged = true;
  requestUIRedraw();
}

static void resetBaseTo(AnimId id, uint32_t now)
{
  s_baseId = id;
  s_baseIdx = 0;

  const AnimClip *clip = animGetClip(s_baseId);
  uint16_t ms = clip ? clip->frameMs : 1000;
  ms = frameMsForAnimFrame(s_baseId, s_baseIdx, ms);
  if (ms < 40)
    ms = 40;

  s_baseNextMs = now + ms;

  // reset override
  s_overridePlaying = false;
  s_overrideId = ANIM_NONE;
  s_overrideIdx = 0;
  s_overrideNextMs = 0;

  // reset trigger schedule
  s_nextTriggerMs = 0;
  const AnimBehavior *beh = animGetBehavior(s_baseId);
  if (beh && beh->triggerId != ANIM_NONE)
  {
    s_nextTriggerMs = now + randRangeInclusive(beh->triggerMinMs, beh->triggerMaxMs);
  }

  markFrameChanged();
}

bool animEnsurePetScreenReady()
{
  if (!g_sdReady)
    return false;

  const uint32_t now = millis();
  const AnimId desired = animSelectPetScreen();

  if (desired == ANIM_NONE)
    return false;

  if (desired != s_baseId)
    resetBaseTo(desired, now);

  const AnimClip *clip = animGetClip(s_baseId);
  return clip && clip->frameCount > 0;
}

static void startOverride(AnimId id, uint32_t now)
{
  s_overridePlaying = true;
  s_overrideId = id;
  s_overrideIdx = 0;

  const AnimClip *clip = animGetClip(s_overrideId);
  uint16_t ms = clip ? clip->frameMs : 90;
  ms = frameMsForAnimFrame(s_overrideId, s_overrideIdx, ms);
  if (ms < 40)
    ms = 40;

  s_overrideNextMs = now + ms;

  markFrameChanged();
}

void animRequestPetGesture(AnimId id)
{
  if (!g_sdReady || id == ANIM_NONE)
    return;

  const uint32_t now = millis();
  const AnimId desired = animSelectPetScreen();

  if (desired == ANIM_NONE)
    return;

  if (desired != s_baseId)
    resetBaseTo(desired, now);

  if (s_overridePlaying)
    return;

  startOverride(id, now);
}

static void stopOverrideAndScheduleNext(uint32_t now)
{
  const AnimId completedOverrideId = s_overrideId;

  s_overridePlaying = false;
  s_overrideId = ANIM_NONE;
  s_overrideIdx = 0;
  s_overrideNextMs = 0;

  const AnimBehavior *beh = animGetBehavior(s_baseId);
  if (beh && beh->triggerId != ANIM_NONE)
  {
    s_nextTriggerMs = now + randRangeInclusive(beh->triggerMinMs, beh->triggerMaxMs);
  }
  else
  {
    s_nextTriggerMs = 0;
  }

  if (completedOverrideId == ANIM_ALIEN_ELDER_BORED_BEAM)
  {
    startPetIntroWalkFromLeft();
  }

  markFrameChanged();
}

void animTick()
{
  const uint32_t animStartMs = millis();

  if (!g_sdReady)
  {
    const uint16_t sample = (uint16_t)(millis() - animStartMs);
    s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
    return;
  }

  const uint32_t now = millis();

  // Choose base clip for the pet screen
  const AnimId desired = animSelectPetScreen();

  if (!animUsesBurgerThought(desired))
    resetBurgerThought();

  if (!animUsesHealthThought(desired))
    resetHealthThought();

  if (!animUsesRestThought(desired))
    resetRestThought();

  // Leaving pet tab/screen => ANIM_NONE
  if (desired == ANIM_NONE)
  {
    // IMPORTANT:
    // When leaving the pet tab/screen, clear animation state WITHOUT requesting a redraw.
    // Otherwise we can force extra work during navigation and it feels like a brief hang.
    if (s_baseId != ANIM_NONE)
    {
      s_baseId = ANIM_NONE;
      s_baseIdx = 0;
      s_baseNextMs = 0;

      s_overridePlaying = false;
      s_overrideId = ANIM_NONE;
      s_overrideIdx = 0;
      s_overrideNextMs = 0;

      s_nextTriggerMs = 0;

      // Dump any pending change; we don't want animation-driven restores off-tab.
      s_frameChanged = false;
    }

    const uint16_t sample = (uint16_t)(millis() - animStartMs);
    s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
    return;
  }

  // Base changed?
  if (desired != s_baseId)
  {
    resetBaseTo(desired, now);
    const uint16_t sample = (uint16_t)(millis() - animStartMs);
    s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
    return;
  }

  // Gesture trigger?
  const AnimBehavior *beh = animGetBehavior(s_baseId);
  if (beh && beh->triggerId != ANIM_NONE)
  {
    if (!s_overridePlaying && s_nextTriggerMs != 0 && (int32_t)(now - s_nextTriggerMs) >= 0)
    {
      if (!canStartTriggerOverride(s_baseId, beh->triggerId))
      {
        // Do not queue the beam to fire immediately after walking ends.
        // Reschedule it into the normal rare window instead.
        s_nextTriggerMs = now + randRangeInclusive(beh->triggerMinMs, beh->triggerMaxMs);
      }
      else
      {
        startOverride(beh->triggerId, now);

        const uint16_t sample = (uint16_t)(millis() - animStartMs);
        s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
        return;
      }
    }
  }

  // Advance override if playing
  if (s_overridePlaying)
  {
    const AnimClip *clip = animGetClip(s_overrideId);
    if (!clip || clip->frameCount == 0)
    {
      stopOverrideAndScheduleNext(now);
      const uint16_t sample = (uint16_t)(millis() - animStartMs);
      s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
      return;
    }

    if ((int32_t)(now - s_overrideNextMs) < 0)
    {
      const uint16_t sample = (uint16_t)(millis() - animStartMs);
      s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
      return;
    }

    if (s_overrideIdx + 1 < clip->frameCount)
    {
      s_overrideIdx++;

      uint16_t ms = frameMsForAnimFrame(s_overrideId, s_overrideIdx, clip->frameMs);
      if (ms < 40)
        ms = 40;

      s_overrideNextMs = now + ms;
      markFrameChanged();
    }
    else
    {
      // one-shot done after the current frame's duration has elapsed.
      stopOverrideAndScheduleNext(now);
    }

    const uint16_t sample = (uint16_t)(millis() - animStartMs);
    s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
    return;
  }

  // Advance base
  const AnimClip *base = animGetClip(s_baseId);
  if (!base || base->frameCount == 0)
  {
    const uint16_t sample = (uint16_t)(millis() - animStartMs);
    s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
    return;
  }

  if ((int32_t)(now - s_baseNextMs) < 0)
  {
    const uint16_t sample = (uint16_t)(millis() - animStartMs);
    s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
    return;
  }

  uint8_t nextIdx = s_baseIdx;

  if (base->frameCount > 1)
  {
    if (base->loop)
      nextIdx = (uint8_t)((s_baseIdx + 1) % base->frameCount);
    else if (s_baseIdx + 1 < base->frameCount)
      nextIdx = s_baseIdx + 1;
  }

  uint16_t ms = frameMsForAnimFrame(s_baseId, nextIdx, base->frameMs);
  if (ms < 40)
    ms = 40;
  s_baseNextMs = now + ms;

  if (base->frameCount <= 1)
  {
    const uint16_t sample = (uint16_t)(millis() - animStartMs);
    s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
    return;
  }

  if (base->loop)
  {
    s_baseIdx = (uint8_t)((s_baseIdx + 1) % base->frameCount);
    markFrameChanged();
  }
  else
  {
    // non-loop base clips aren’t expected here; clamp at end
    if (s_baseIdx + 1 < base->frameCount)
    {
      s_baseIdx++;
      markFrameChanged();
    }
  }

  const uint16_t sample = (uint16_t)(millis() - animStartMs);
  s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
}

void animNotifyScreenWake()
{
  if (!g_sdReady)
    return;

  const uint32_t now = millis();

  // Re-evaluate what should be playing now that we're awake.
  const AnimId desired = animSelectPetScreen();

  // If we shouldn't be animating anymore, stop cleanly.
  if (desired == ANIM_NONE)
  {
    animForceStop();
    return;
  }

  // If the base clip should change (mood/stage changed while asleep), reset it.
  if (desired != s_baseId)
  {
    resetBaseTo(desired, now);
    return;
  }

  // HARD RESUME:
  // millis() may have paused during light sleep. Force deadlines to "now"
  // so animTick advances immediately after wake.
  s_baseNextMs = now;

  // Nudge a frame immediately so the first wake render isn't "same frame".
  // Nudge whichever clip is currently visible (override wins).
  if (s_overridePlaying)
  {
    const AnimClip *o = animGetClip(s_overrideId);
    if (o && o->frameCount > 1)
    {
      if (s_overrideIdx + 1 < o->frameCount)
        s_overrideIdx++;
      else
        s_overrideIdx = 0;
    }
  }
  else
  {
    const AnimClip *base = animGetClip(s_baseId);
    if (base && base->frameCount > 1)
    {
      if (base->loop)
        s_baseIdx = (uint8_t)((s_baseIdx + 1) % base->frameCount);
      else if (s_baseIdx + 1 < base->frameCount)
        s_baseIdx++;
    }
  }

  markFrameChanged();
}

bool animConsumeFrameChanged()
{
  const bool v = s_frameChanged;
  s_frameChanged = false;
  return v;
}

bool animCurrentFrame(AnimId &outId, uint8_t &outIdx)
{
  outId = s_baseId;
  outIdx = s_baseIdx;

  if (s_overridePlaying)
  {
    outId = s_overrideId;
    outIdx = s_overrideIdx;
  }

  const AnimClip *clip = animGetClip(outId);
  if (!clip || clip->frameCount == 0)
    return false;

  if (outIdx >= clip->frameCount)
    outIdx = 0;

  return outId != ANIM_NONE;
}

void animDrawPetFrame(int x, int y)
{
  const uint32_t frameStartMs = millis();

  if (!g_sdReady)
  {
    const uint16_t sample = (uint16_t)(millis() - frameStartMs);
    s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
    return;
  }

  AnimId id = s_baseId;
  uint8_t idx = s_baseIdx;

  if (s_overridePlaying)
  {
    id = s_overrideId;
    idx = s_overrideIdx;
  }

  const AnimClip *clip = animGetClip(id);
  if (!clip || clip->frameCount == 0)
  {
    const uint16_t sample = (uint16_t)(millis() - frameStartMs);
    s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
    return;
  }

  if (idx >= clip->frameCount)
    idx = 0;

  const char *path = clip->frames[idx];
  (void)drawPngPathAnim(path, x, y);

  const uint16_t sample = (uint16_t)(millis() - frameStartMs);
  s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
}

void animDrawPetFrameAnchoredNominalBottom(int centerX, int nominalBottomY, int nominalH, int yAdjust)
{
  const uint32_t frameStartMs = millis();

  if (!g_sdReady)
  {
    const uint16_t sample = (uint16_t)(millis() - frameStartMs);
    s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
    return;
  }

  // Clock Mode still has a footer area for the ESC hint.
  // Reserve that strip so tall sprites never draw into it.
  const int petBottom = SCREEN_H - TAB_BAR_H;

  if (nominalBottomY > petBottom)
    nominalBottomY = petBottom;

  AnimId id = s_baseId;
  uint8_t idx = s_baseIdx;

  if (s_overridePlaying)
  {
    id = s_overrideId;
    idx = s_overrideIdx;
  }

  const AnimClip *clip = animGetClip(id);
  if (!clip || clip->frameCount == 0)
  {
    const uint16_t sample = (uint16_t)(millis() - frameStartMs);
    s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
    return;
  }

  if (idx >= clip->frameCount)
    idx = 0;
  const char *path = clip->frames[idx];

  int w = 0;
  int h = 0;
  const int drawY = nominalBottomY - nominalH + yAdjust;

  if (pngReadWHCached(path, &w, &h))
  {
    const int drawX = centerX - (w / 2);
    (void)drawPngPathAnim(path, drawX, drawY);
  }
  else
  {
    (void)drawPngPathAnim(path, centerX, drawY);
  }

  const uint16_t sample = (uint16_t)(millis() - frameStartMs);
  s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
}

void animDrawPetFrameAnchoredBottom(int centerX, int bottomY)
{
  const uint32_t frameStartMs = millis();

  if (!g_sdReady)
  {
    const uint16_t sample = (uint16_t)(millis() - frameStartMs);
    s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
    return;
  }

  // Clamp anchor so sprite bottoms never go under the tab bar.
  // This is the actual bottom of the pet area.
  // Clamp anchor so sprite bottoms never go under the active bottom UI.
  // Clock Mode has no tab bar, so let it use the full screen height.
  // Clock Mode still has a footer area for the ESC hint.
  // Reserve that strip so tall sprites never draw into it.
  const int petBottom = SCREEN_H - TAB_BAR_H;
  if (bottomY > petBottom)
    bottomY = petBottom;

  AnimId id = s_baseId;
  uint8_t idx = s_baseIdx;

  if (s_overridePlaying)
  {
    id = s_overrideId;
    idx = s_overrideIdx;
  }

  const AnimClip *clip = animGetClip(id);
  if (!clip || clip->frameCount == 0)
  {
    const uint16_t sample = (uint16_t)(millis() - frameStartMs);
    s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
    return;
  }

  if (idx >= clip->frameCount)
    idx = 0;
  const char *path = clip->frames[idx];

  int w = 0;
  int h = 0;
  if (pngReadWHCached(path, &w, &h))
  {
    int drawX = centerX - (w / 2);

    // Nudge only specific oversized/offset Alien animations.
    if (id == ANIM_ALIEN_BABY_TIRED_LAY || id == ANIM_ALIEN_BABY_TIRED_HOLD)
    {
      drawX += (g_app.uiState == UIState::CLOCK_MODE) ? 12 : -8;
    }

    if (id == ANIM_ALIEN_TEEN_SICK_LOOP)
    {
      drawX += (g_app.uiState == UIState::CLOCK_MODE) ? 5 : -15;
    }

    const int drawY = bottomY - h;
    (void)drawPngPathAnim(path, drawX, drawY);

    if (animUsesRestThought(id))
      drawRestThoughtBubbleForTiredPet(drawX, drawY, w, id);

    if (animUsesBurgerThought(id))
      drawBurgerThoughtBubbleForHungryAlienBaby(drawX, drawY, w, id);

    if (animUsesHealthThought(id))
      drawHealthThoughtBubbleForSickAlienBaby(drawX, drawY, w, id);
  }
  else
  {
    // Best-effort fallback: at least honor the bottom anchor on Y.
    // (Without width/height we can't center accurately.)
    (void)drawPngPathAnim(path, centerX, bottomY);
  }

  const uint16_t sample = (uint16_t)(millis() - frameStartMs);
  s_petPerfStats.petFrameMs = smoothPerfMs(s_petPerfStats.petFrameMs, sample);
}

void animForceStop()
{
  s_baseId = ANIM_NONE;
  s_baseIdx = 0;
  s_baseNextMs = 0;

  s_overridePlaying = false;
  s_overrideId = ANIM_NONE;
  s_overrideIdx = 0;
  s_overrideNextMs = 0;

  s_nextTriggerMs = 0;
  s_frameChanged = false;
}