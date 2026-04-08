#include "anim_engine.h"

#include "M5Cardputer.h"

#include <SD.h>
#include <FS.h>
#include <lgfx/v1/misc/DataWrapper.hpp>

#include "sdcard.h"
#include "display.h"
#include "graphics.h"
#include "anim_clips.h"
#include "ui_invalidate.h"
#include "runtime_flags_state.h"
#include "app_state.h"

static PetPerfStats s_petPerfStats;

const PetPerfStats &petPerfStats()
{
  return s_petPerfStats;
}

void petPerfResetStats()
{
  s_petPerfStats.petFrameMs = 0;
  s_petPerfStats.petSpriteDrawMs = 0;
  s_petPerfStats.animStepMs = 0;
}

static uint16_t smoothPerfMs(uint16_t oldValue, uint16_t sample)
{
  return (uint16_t)((oldValue * 3u + sample) / 4u);
}

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
    return (int)((uint32_t)buf[off] << 24 |
                 (uint32_t)buf[off + 1] << 16 |
                 (uint32_t)buf[off + 2] << 8 |
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

static bool s_frameChanged = false;

static uint32_t randRangeInclusive(uint32_t lo, uint32_t hi)
{
  if (hi <= lo)
    return lo;
  return (uint32_t)random((long)lo, (long)(hi + 1));
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

static void startOverride(AnimId id, uint32_t now)
{
  s_overridePlaying = true;
  s_overrideId = id;
  s_overrideIdx = 0;

  const AnimClip *clip = animGetClip(s_overrideId);
  uint16_t ms = clip ? clip->frameMs : 90;
  if (ms < 40)
    ms = 40;

  s_overrideNextMs = now + ms;

  markFrameChanged();
}

static void stopOverrideAndScheduleNext(uint32_t now)
{
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
      startOverride(beh->triggerId, now);
      const uint16_t sample = (uint16_t)(millis() - animStartMs);
      s_petPerfStats.animStepMs = smoothPerfMs(s_petPerfStats.animStepMs, sample);
      return;
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

    uint16_t ms = clip->frameMs;
    if (ms < 40)
      ms = 40;
    s_overrideNextMs = now + ms;

    if (s_overrideIdx + 1 < clip->frameCount)
    {
      s_overrideIdx++;
      markFrameChanged();
    }
    else
    {
      // one-shot done
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

  uint16_t ms = base->frameMs;
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

  const int petBottom = (g_app.uiState == UIState::CLOCK_MODE)
                            ? SCREEN_H
                            : (SCREEN_H - TAB_BAR_H);
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
  const int petBottom = (g_app.uiState == UIState::CLOCK_MODE)
                            ? SCREEN_H
                            : (SCREEN_H - TAB_BAR_H);
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
    const int drawX = centerX - (w / 2);
    const int drawY = bottomY - h;
    (void)drawPngPathAnim(path, drawX, drawY);
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