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
#include "graphics_sleep_anim_assets.h"
#include "graphics_sleep_frame_cache.h"
#include "graphics_ui_common.h"
#include "pet.h"
#include "pet_autonomy.h"
#include "save_manager.h"
#include "wardrive_steps.h"

// -----------------------------------------------------------------------------
// External state (owned elsewhere)
// -----------------------------------------------------------------------------
extern Pet pet;
extern bool g_sdReady;

// Sleep background kick (owned by graphics.cpp)
extern volatile bool g_sleepBgKick;

// Shared path + cache (owned by graphics.cpp for now)
extern const char *PATH_BG_SLEEP;

// -----------------------------------------------------------------------------
// External systems (owned elsewhere)
// -----------------------------------------------------------------------------

// UI / render loop
void requestUIRedraw();
bool isScreenOn();

// Shared drawing helpers
void drawTopBar();
void drawSleepMeterBar();

// -----------------------------------------------------------------------------
// Sleep module state (owned here)
// -----------------------------------------------------------------------------
static volatile bool g_sleepBgWakeKick = false;

static uint32_t g_sleepAnimNextFrameMs = 0;
static bool g_sleepAnimActive = false;

// -----------------------------------------------------------------------------
// Forward declarations (private to this module)
// -----------------------------------------------------------------------------
static void drawSleepStepCounterBadge()
{
  if (!isStepCounterEnabled())
    return;

  const uint32_t steps = wardriveStepsToday();

  char buf[16];
  snprintf(buf, sizeof(buf), "%lu", (unsigned long)steps);
  
  spr.setTextDatum(TL_DATUM);
  spr.setTextFont(1);
  spr.setTextSize(1);

  const int textW = spr.textWidth(buf);
  const int boxW = 18 + textW + 6;
  const int boxH = 13;

  // Right anchored. As the text grows, the badge expands left.
  const int x = SCREEN_W - boxW - 4;
  const int y = TOP_BAR_H + 2;

  spr.fillRoundRect(x, y, boxW, boxH, 5, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 5, TFT_DARKGREY);

  const int iconX = x + 4;
  const int iconY = y + 3;

  spr.drawCircle(iconX + 4, iconY + 4, 2, TFT_GREEN);
  spr.drawCircle(iconX + 4, iconY + 4, 5, TFT_DARKGREEN);
  spr.fillCircle(iconX + 4, iconY + 4, 1, TFT_GREEN);

  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.drawString(buf, x + 15, y + 3);
}

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

static void drawPassOutNotice()
{
  if (!petAutonomyPassOutNoticePending())
    return;

  const uint16_t modalOutline = uiModalOutline(pet.type);

  const int pad = 10;
  const int boxW = SCREEN_W - (pad * 2);
  const int boxH = 42;
  const int x = pad;
  const int y = (SCREEN_H - boxH) / 2;

  spr.fillRoundRect(x, y, boxW, boxH, 8, TFT_BLACK);
  spr.drawRoundRect(x, y, boxW, boxH, 8, modalOutline);

  spr.setTextDatum(MC_DATUM);
  spr.setTextFont(2);
  spr.setTextSize(1);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  char msg[64];
  const char *name = pet.name && pet.name[0] ? pet.name : "Pet";
  
  snprintf(msg, sizeof(msg), "%s passed out", name);
  
  spr.drawString(msg, SCREEN_W / 2, y + (boxH / 2));
  
  spr.setTextDatum(TL_DATUM);
}

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

  const SleepAnimSelection sel = selectSleepAnimForPet(pet.type, pet.evoStage);
  const uint8_t newMode = sel.mode;

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

  const char *bgPath = sel.bgPath;

  static uint8_t s_lastMode = 0;
  static bool s_animInited = false;

  const bool modeChanged = (s_mode != s_lastMode);

  const char *const *frames = sel.frames;
  uint8_t frameCount = sel.frameCount;
  uint32_t frameMs = sel.frameMs;

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
      // Cache system currently disabled (no PSRAM). Keep call for structure.
      (void)ensureSleepAnimFrameCache(s_mode, frames, frameCount, 0, 18);

      // Fall through to live draw path below
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
  drawSleepStepCounterBadge();
  drawPassOutNotice();
  drawSleepMeterBar();
}

void drawSleepScreen() { drawSleepScreenImpl(true); }
