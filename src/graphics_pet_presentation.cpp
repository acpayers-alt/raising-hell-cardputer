#include "graphics_pet_presentation.h"
#include "graphics_mini_stats.h"
#include "graphics_pet_bg_paths.h"
#include "graphics_sd_draw.h"
#include <string.h>

#include <Arduino.h>
#include <stdlib.h>

#include "anim_clips.h"
#include "anim_engine.h"
#include "display.h"
#include "graphics.h"
#include "graphics_render_utils.h"
#include "graphics_shared_utils.h"

#include "save_manager.h"
#include "wardrive_steps.h"

#include "app_state.h"
#include "pet.h"

struct PetRenderProfile
{
  int w;
  int h;
  int xOff;
  int yOff;
};

// ============================================================================
// EXTERNALS
// ============================================================================

extern Pet pet;
extern AppState g_app;
extern bool g_sdReady;

extern bool g_forcePetBgCache;

// ============================================================================
// PET LAYER / BACKGROUND CACHE
// ============================================================================

static M5Canvas petLayer(&spr);
static bool petLayerReady = false;

static const char *s_petBgCachedPath = nullptr;
static PetType s_petBgCachedType = (PetType)255;
static uint8_t s_petBgCachedStage = 255;
static bool s_petBgHardFail = false;

static bool s_petFacingLeft = false;

static bool ensurePetLayer();
static bool alienBabyBoredTeleportActive();
static void cancelAlienBabyBoredTeleport();

// UI / rendering hooks
void requestUIRedraw();
bool isScreenOn();

void drawTopBar();
void drawTabBar();
void drawPetPerfHud();
void resetPetWanderToHome();

// Animation
bool animConsumeFrameChanged();
void animDrawPetFrameAnchoredBottom(int anchorCenterX, int anchorBottomY);

// Assets
bool getPngWH(const char *path, int &w, int &h);

void graphicsReleasePetLayerForOta()
{
  petLayer.deleteSprite();
  petLayerReady = false;

  s_petBgCachedPath = nullptr;
  s_petBgCachedType = (PetType)255;
  s_petBgCachedStage = 255;
  g_forcePetBgCache = true;
}

void graphicsRecoverAfterOta()
{
  petLayer.deleteSprite();
  petLayerReady = false;

  s_petBgCachedPath = nullptr;
  s_petBgCachedType = (PetType)255;
  s_petBgCachedStage = 255;
  g_forcePetBgCache = true;

  invalidateBackgroundCache();
  requestUIRedraw();
}

void graphicsReleasePetBackgroundCache()
{
  petLayer.deleteSprite();
  petLayerReady = false;

  s_petBgCachedPath = nullptr;
  s_petBgCachedType = (PetType)255;
  s_petBgCachedStage = 255;
  s_petBgHardFail = false;
}

static bool ensurePetLayer()
{
  if (petLayerReady)
    return true;

  petLayer.setColorDepth(16);
  if (!petLayer.createSprite(SCREEN_W, PET_AREA_H))
  {
    petLayerReady = false;
    return false;
  }

  petLayerReady = true;
  return true;
}

static bool buildPetLayerCacheViaSpr(const char *bgPath)
{
  if (!bgPath || !bgPath[0])
    return false;

  if (!ensurePetLayer())
    return false;

  uint16_t *dst = (uint16_t *)petLayer.getBuffer();
  uint16_t *src = (uint16_t *)spr.getBuffer();
  if (!dst || !src)
    return false;

  // Decode using the main sprite path, then copy just the pet area into petLayer.
  spr.clearClipRect();
  spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);

  const bool ok = sprDrawJpgFromSD(bgPath, 0, PET_AREA_Y);
  if (!ok)
    return false;

  memcpy(dst, src + (PET_AREA_Y * SCREEN_W), SCREEN_W * PET_AREA_H * sizeof(uint16_t));
  return true;
}

void graphicsPrewarmPetBackgroundCache()
{
  if (!g_sdReady)
    return;

  const char *bgPath = bgPathForPetWithStage(pet.type, pet.evoStage);
  if (!bgPath || !bgPath[0])
    return;

  if (petLayerReady && s_petBgCachedPath && strcmp(s_petBgCachedPath, bgPath) == 0 && s_petBgCachedType == pet.type &&
      s_petBgCachedStage == pet.evoStage)
  {
    return;
  }

  if (!ensurePetLayer())
    return;

  petLayer.fillSprite(TFT_BLACK);

  const bool ok = buildPetLayerCacheViaSpr(bgPath);

  if (!ok)
  {
    // Best-effort prewarm only: leave the normal draw path free to retry.
    petLayerReady = false;
    s_petBgCachedPath = nullptr;
    s_petBgCachedType = (PetType)255;
    s_petBgCachedStage = 255;
    return;
  }

  s_petBgCachedPath = bgPath;
  s_petBgCachedType = pet.type;
  s_petBgCachedStage = pet.evoStage;
  s_petBgHardFail = false;
}

void cachePetAreaBackgroundIfNeeded(bool force)
{
  if (!g_sdReady)
  {
    spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);
    invalidateBackgroundCache();
    requestUIRedraw();
    return;
  }

  if (s_petBgHardFail && !force)
  {
    static uint32_t s_lastPetBgHardFailRetryMs = 0;
    const uint32_t now = millis();

    // Avoid retry storm, but do not make the failure permanent.
    if ((uint32_t)(now - s_lastPetBgHardFailRetryMs) < 3000UL)
    {
      spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);
      return;
    }

    s_lastPetBgHardFailRetryMs = now;
    force = true;
  }

  const char *bgPath = bgPathForPetWithStage(pet.type, pet.evoStage);

  if (!ensurePetLayer())
  {
    spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);
    if (bgPath)
    {
      (void)sprDrawJpgFromSD(bgPath, 0, PET_AREA_Y);
    }
    invalidateBackgroundCache();
    requestUIRedraw();
    return;
  }

  if (!petLayerReady)
    force = true;

  if (!force && (s_petBgCachedPath && bgPath && strcmp(s_petBgCachedPath, bgPath) == 0) &&
      (s_petBgCachedType == pet.type) && (s_petBgCachedStage == pet.evoStage))
  {
    return;
  }

  petLayer.fillSprite(TFT_BLACK);

  bool ok = true;
  if (bgPath)
  {
    ok = buildPetLayerCacheViaSpr(bgPath);
  }

  if (!ok)
  {
    petLayerReady = false;
    s_petBgCachedPath = nullptr;
    s_petBgCachedType = (PetType)255;
    s_petBgCachedStage = 255;

    bool directOk = false;
    if (bgPath)
      directOk = sprDrawJpgFromSD(bgPath, 0, PET_AREA_Y);

    s_petBgHardFail = !directOk;

    if (!directOk)
      spr.fillRect(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, TFT_BLACK);

    invalidateBackgroundCache();
    requestUIRedraw();
    return;
  }

  s_petBgCachedPath = bgPath;
  s_petBgCachedType = pet.type;
  s_petBgCachedStage = pet.evoStage;
  petLayerReady = true;
  s_petBgHardFail = false;
}

void restorePetAreaFromCache()
{
  if (!petLayerReady)
    return;

  spr.pushImage(0, PET_AREA_Y, SCREEN_W, PET_AREA_H, (uint16_t *)petLayer.getBuffer());
}

// ============================================================================
// PET SCREEN POSITION (ANCHOR-BASED)
// ============================================================================

static int s_petScreenX = 0;
static int s_petScreenY = 0;
static bool s_petScreenPosInitialized = false;

static int s_petHomeX = 0;
static int s_petHomeY = 0;

// ============================================================================
// INTRO SEQUENCE STATE
// ============================================================================

static bool s_petIntroWalkActive = false;
static bool s_petIntroArriveTurnActive = false;
static bool s_petIntroStandHoldActive = false;
static bool s_petIntroHandoffActive = false;

static uint32_t s_petIntroWalkLastStepMs = 0;
static uint32_t s_petIntroArriveTurnStartMs = 0;
static uint32_t s_petIntroStandHoldStartMs = 0;

static constexpr int kPetIntroWalkStepPx = 3;
static constexpr uint32_t kPetIntroWalkStepMs = 40;
static constexpr uint32_t kPetIntroArriveTurnMs = 180;
static constexpr uint32_t kPetIntroStandHoldMs = 300;
static constexpr uint32_t kPetIntroWalkFrameMs = 45;

static constexpr int kPetIntroYOffset = 0;

// ============================================================================
// WALKING SPRITE PATHS
// ============================================================================

// ---------------- DEVIL ----------------

// Baby
static const char *PATH_DEV_BB_WALK1 = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walk1.png";
static const char *PATH_DEV_BB_WALK2 = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walk2.png";
static const char *PATH_DEV_BB_WALK1_L = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walkleft1.png";
static const char *PATH_DEV_BB_WALK2_L = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walkleft2.png";

// Teen
static const char *PATH_DEV_TN_WALK1 = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walk1.png";
static const char *PATH_DEV_TN_WALK2 = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walk2.png";
static const char *PATH_DEV_TN_WALK1_L = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walkleft1.png";
static const char *PATH_DEV_TN_WALK2_L = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walkleft2.png";

// Adult
static const char *PATH_DEV_AD_WALK1 = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walk1.png";
static const char *PATH_DEV_AD_WALK2 = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walk2.png";
static const char *PATH_DEV_AD_WALK1_L = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walkleft1.png";
static const char *PATH_DEV_AD_WALK2_L = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walkleft2.png";

// Elder
static const char *PATH_DEV_EL_WALK1 = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walk1.png";
static const char *PATH_DEV_EL_WALK2 = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walk2.png";
static const char *PATH_DEV_EL_WALK1_L = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walkleft1.png";
static const char *PATH_DEV_EL_WALK2_L = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walkleft2.png";

// ---------------- ELDRITCH ----------------

// Baby
static const char *PATH_ELD_BB_WALK1 = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walk1.png";
static const char *PATH_ELD_BB_WALK2 = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walk2.png";
static const char *PATH_ELD_BB_WALK1_L = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walkleft1.png";
static const char *PATH_ELD_BB_WALK2_L = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walkleft2.png";

// Teen
static const char *PATH_ELD_TN_WALK1 = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walk1.png";
static const char *PATH_ELD_TN_WALK2 = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walk2.png";
static const char *PATH_ELD_TN_WALK1_L = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walkleft1.png";
static const char *PATH_ELD_TN_WALK2_L = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walkleft2.png";

// Adult
static const char *PATH_ELD_AD_WALK1 = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walk1.png";
static const char *PATH_ELD_AD_WALK2 = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walk2.png";
static const char *PATH_ELD_AD_WALK1_L = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walkleft1.png";
static const char *PATH_ELD_AD_WALK2_L = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walkleft2.png";

// Elder
static const char *PATH_ELD_EL_WALK1 = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walk1.png";
static const char *PATH_ELD_EL_WALK2 = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walk2.png";
static const char *PATH_ELD_EL_WALK1_L = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walkleft1.png";
static const char *PATH_ELD_EL_WALK2_L = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walkleft2.png";

// ---------------- ALIEN ----------------

// Baby
static const char *PATH_AL_BB_WALK1 = "/raising_hell/graphics/pet/anim/al/bb/wlk/al_bb_walk1.png";
static const char *PATH_AL_BB_WALK2 = "/raising_hell/graphics/pet/anim/al/bb/wlk/al_bb_walk2.png";
static const char *PATH_AL_BB_WALK1_L = "/raising_hell/graphics/pet/anim/al/bb/wlk/al_bb_walkleft1.png";
static const char *PATH_AL_BB_WALK2_L = "/raising_hell/graphics/pet/anim/al/bb/wlk/al_bb_walkleft2.png";

static const char *PATH_AL_BB_BORED_TP[] = {
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_tele1.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_tele2.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_tele3.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_tele4.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_tele5.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_tele6.png",
    "/raising_hell/graphics/pet/anim/al/bb/brd/al_bb_brd_tele7.png",
};

// Teen
static const char *PATH_AL_TN_WALK1 = "/raising_hell/graphics/pet/anim/al/tn/wlk/al_tn_walk1.png";
static const char *PATH_AL_TN_WALK2 = "/raising_hell/graphics/pet/anim/al/tn/wlk/al_tn_walk2.png";
static const char *PATH_AL_TN_WALK1_L = "/raising_hell/graphics/pet/anim/al/tn/wlk/al_tn_walkleft1.png";
static const char *PATH_AL_TN_WALK2_L = "/raising_hell/graphics/pet/anim/al/tn/wlk/al_tn_walkleft2.png";

// ============================================================================
// WANDER SYSTEM
// ============================================================================

enum class PetWanderState : uint8_t
{
  HOME_IDLE = 0,
  MOVING_TO_SIDE_A,
  PAUSE_AWAY_1,
  MOVING_TO_SIDE_B,
  PAUSE_AWAY_2,
  RETURNING_HOME
};

static PetWanderState s_petWanderState = PetWanderState::HOME_IDLE;

static int s_petWanderTargetX = 0;
static int s_petWanderSideAX = 0;
static int s_petWanderSideBX = 0;

static uint32_t s_petWanderUntilMs = 0;
static uint32_t s_petWanderLastStepMs = 0;
static uint8_t s_petWalkFrame = 0;

enum class AlienTeleportState : uint8_t
{
  IDLE = 0,
  DISAPPEARING,
  BLANK_HOLD,
  APPEARING
};

static AlienTeleportState s_alienTeleportState = AlienTeleportState::IDLE;
static uint8_t s_alienTeleportFrame = 0;
static uint32_t s_alienTeleportLastFrameMs = 0;
static uint32_t s_alienTeleportBlankStartMs = 0;
static uint32_t s_alienTeleportNextAllowedMs = 0;
static int s_alienTeleportDestX = 0;

static constexpr uint8_t kAlienTeleportFrameCount = 7;
static constexpr uint32_t kAlienTeleportFrameMs = 90;
static constexpr uint32_t kAlienTeleportBlankMs = 320;
static constexpr uint32_t kAlienTeleportMinCooldownMs = 7000;
static constexpr uint32_t kAlienTeleportMaxCooldownMs = 12000;

static constexpr int kPetWanderRangePx = 55;
static constexpr int kPetWanderMinMovePx = 28;
static constexpr int kPetWanderStepPx = 2;

static constexpr uint32_t kPetWanderStepMs = 30;
static constexpr uint32_t kPetWanderPauseAwayMs = 5000;
static constexpr uint32_t kPetWanderMinIdleMs = 5000;
static constexpr uint32_t kPetWanderMaxIdleMs = 7000;

// ============================================================================
// PUBLIC STATE ACCESSORS
// ============================================================================

int petPresentationX() { return s_petScreenX; }
int petPresentationY() { return s_petScreenY; }

bool petPresentationHasIntroHandoff() { return s_petIntroHandoffActive; }

void clearPetPresentationIntroHandoff() { s_petIntroHandoffActive = false; }

// ============================================================================
// STATE HELPERS
// ============================================================================

bool petPresentationScriptedIntroActive()
{
  return s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive || s_petIntroHandoffActive;
}

bool petPresentationAnimating()
{
  return petPresentationScriptedIntroActive() || alienBabyBoredTeleportActive() ||
         s_petWanderState == PetWanderState::MOVING_TO_SIDE_A || s_petWanderState == PetWanderState::MOVING_TO_SIDE_B ||
         s_petWanderState == PetWanderState::RETURNING_HOME;
}

bool petWalkOverrideActive() { return petPresentationAnimating(); }

bool petPresentationFacingLeft() { return s_petFacingLeft; }

// ============================================================================
// PET PROFILE
// ============================================================================

static const PetRenderProfile kPetProfile[] = {
    {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET}, // DEVIL
    {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET}, // ELDRITCH
    {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET}, // ALIEN
};

const PetRenderProfile &getPetProfile(PetType t)
{
  int idx = (int)t;
  const int count = (int)(sizeof(kPetProfile) / sizeof(kPetProfile[0]));
  if (idx < 0 || idx >= count)
    idx = 0;
  return kPetProfile[idx];
}

// ============================================================================
// POSITIONING
// ============================================================================

static int getClockModePetHomeX(const PetRenderProfile &prof)
{
  // Park the pet to the left of the first large clock digit.
  // This keeps static mood poses from covering the time while still allowing
  // walking/wandering pets to roam from a safe starting point.
  static constexpr int kClockFirstDigitApproxX = (SCREEN_W / 2) - 52;
  static constexpr int kClockDigitClearancePx = 6;

  const int maxAnchorX = kClockFirstDigitApproxX - (PET_SPR_W / 2) - kClockDigitClearancePx;
  const int minAnchorX = PET_SPR_W / 2;

  return clampi(maxAnchorX + prof.xOff, minAnchorX, SCREEN_W - (PET_SPR_W / 2));
}

void getPetHomeScreenPosition(int &outX, int &outY)
{
  const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;

  const PetRenderProfile &prof = getPetProfile(pet.type);

  if (g_app.uiState == UIState::CLOCK_MODE && !pet.isSleeping)
    outX = getClockModePetHomeX(prof);
  else
    outX = (petAreaW / 2) + prof.xOff;

  outY = (PET_AREA_Y + PET_AREA_H) + prof.yOff;
}

// ============================================================================
// RESET / INIT
// ============================================================================

void resetPetScreenPositionToHome()
{
  getPetHomeScreenPosition(s_petHomeX, s_petHomeY);

  s_petScreenX = s_petHomeX;
  s_petScreenY = s_petHomeY;
  s_petScreenPosInitialized = true;

  s_petIntroWalkActive = false;
  s_petIntroArriveTurnActive = false;
  s_petIntroStandHoldActive = false;
  s_petIntroHandoffActive = false;
  cancelAlienBabyBoredTeleport();
}

// ============================================================================
// WANDER CONTROL
// ============================================================================

static void scheduleNextPetWander()
{
  const uint32_t now = millis();
  const uint32_t span = (kPetWanderMaxIdleMs - kPetWanderMinIdleMs);

  s_petWanderUntilMs = now + kPetWanderMinIdleMs + (span ? (uint32_t)random((long)span) : 0);
}

static bool alienBabyBoredTeleportEligible(PetMood mood)
{
  // Do not allow teleporting in Clock Mode. Clock Mode parks the pet safely
  // beside the time display, and teleport can yank it back across the clock.
  if (g_app.uiState == UIState::CLOCK_MODE)
    return false;

  return pet.type == PET_ALIEN && pet.evoStage == 0 && mood == MOOD_BORED && !pet.isSleeping &&
         s_petWanderState == PetWanderState::HOME_IDLE && !s_petIntroWalkActive && !s_petIntroArriveTurnActive &&
         !s_petIntroStandHoldActive && !s_petIntroHandoffActive;
}

static void scheduleNextAlienTeleport(uint32_t now)
{
  const uint32_t span = kAlienTeleportMaxCooldownMs - kAlienTeleportMinCooldownMs;
  s_alienTeleportNextAllowedMs = now + kAlienTeleportMinCooldownMs + (span ? (uint32_t)random((long)span) : 0);
}

static int pickAlienTeleportDestX()
{
  const bool inClockMode = (g_app.uiState == UIState::CLOCK_MODE);
  const int minAnchorX = PET_SPR_W / 2;
  const int rightClearancePx = 12;

  int maxAnchorX = 0;
  if (inClockMode)
    maxAnchorX = SCREEN_W - (PET_SPR_W / 2) - rightClearancePx;
  else
  {
    const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;
    maxAnchorX = petAreaW - (PET_SPR_W / 2) - rightClearancePx;
  }

  int destX = s_petScreenX;
  for (int tries = 0; tries < 10; ++tries)
  {
    destX = (int)random(minAnchorX, maxAnchorX + 1);
    if (abs(destX - s_petScreenX) >= 28)
      break;
  }

  return clampi(destX, minAnchorX, maxAnchorX);
}

static void startAlienBabyBoredTeleport(uint32_t now)
{
  s_alienTeleportState = AlienTeleportState::DISAPPEARING;
  s_alienTeleportFrame = 0;
  s_alienTeleportLastFrameMs = now;
  s_alienTeleportBlankStartMs = 0;
  s_alienTeleportDestX = pickAlienTeleportDestX();
  requestUIRedraw();
}

static bool alienBabyBoredTeleportActive() { return s_alienTeleportState != AlienTeleportState::IDLE; }

static void cancelAlienBabyBoredTeleport()
{
  s_alienTeleportState = AlienTeleportState::IDLE;
  s_alienTeleportFrame = 0;
  s_alienTeleportLastFrameMs = 0;
  s_alienTeleportBlankStartMs = 0;
  s_alienTeleportDestX = 0;
}

static bool tickAlienBabyBoredTeleport(uint32_t now, PetMood mood)
{
  if (!alienBabyBoredTeleportActive())
  {
    if (!alienBabyBoredTeleportEligible(mood))
    {
      if (mood != MOOD_BORED)
        s_alienTeleportNextAllowedMs = 0;
      return false;
    }

    if (s_alienTeleportNextAllowedMs == 0)
      scheduleNextAlienTeleport(now);

    if ((int32_t)(now - s_alienTeleportNextAllowedMs) < 0)
      return false;

    startAlienBabyBoredTeleport(now);
    return true;
  }

  if (!alienBabyBoredTeleportEligible(mood))
  {
    cancelAlienBabyBoredTeleport();
    return false;
  }

  switch (s_alienTeleportState)
  {
  case AlienTeleportState::DISAPPEARING:
    if ((uint32_t)(now - s_alienTeleportLastFrameMs) < kAlienTeleportFrameMs)
      return true;

    s_alienTeleportLastFrameMs = now;

    if (s_alienTeleportFrame + 1 < kAlienTeleportFrameCount)
    {
      ++s_alienTeleportFrame;
      requestUIRedraw();
      return true;
    }

    s_alienTeleportState = AlienTeleportState::BLANK_HOLD;
    s_alienTeleportBlankStartMs = now;
    requestUIRedraw();
    return true;

  case AlienTeleportState::BLANK_HOLD:
    if ((uint32_t)(now - s_alienTeleportBlankStartMs) < kAlienTeleportBlankMs)
      return true;

    s_petScreenX = s_alienTeleportDestX;
    s_alienTeleportFrame = kAlienTeleportFrameCount - 1;
    s_alienTeleportState = AlienTeleportState::APPEARING;
    s_alienTeleportLastFrameMs = now;
    requestUIRedraw();
    return true;

  case AlienTeleportState::APPEARING:
    if ((uint32_t)(now - s_alienTeleportLastFrameMs) < kAlienTeleportFrameMs)
      return true;

    s_alienTeleportLastFrameMs = now;

    if (s_alienTeleportFrame > 0)
    {
      --s_alienTeleportFrame;
      requestUIRedraw();
      return true;
    }

    cancelAlienBabyBoredTeleport();
    scheduleNextAlienTeleport(now);
    requestUIRedraw();
    return false;

  case AlienTeleportState::IDLE:
  default:
    return false;
  }
}

void resetPetWanderToHome()
{
  s_petScreenX = s_petHomeX;
  s_petScreenY = s_petHomeY;

  s_petWanderTargetX = s_petHomeX;
  s_petWanderSideAX = s_petHomeX;
  s_petWanderSideBX = s_petHomeX;

  s_petWanderState = PetWanderState::HOME_IDLE;
  s_petWanderLastStepMs = 0;

  s_petWalkFrame = 0;
  cancelAlienBabyBoredTeleport();
  scheduleNextPetWander();
}

// ============================================================================
// CLOCK MODE RESET
// ============================================================================

void resetClockModePetPresentation()
{
  s_petIntroWalkActive = false;
  s_petIntroArriveTurnActive = false;
  s_petIntroStandHoldActive = false;
  s_petIntroHandoffActive = false;

  getPetHomeScreenPosition(s_petHomeX, s_petHomeY);

  s_petScreenX = s_petHomeX;
  s_petScreenY = s_petHomeY;
  s_petScreenPosInitialized = true;

  s_petWanderTargetX = s_petHomeX;
  s_petWanderSideAX = s_petHomeX;
  s_petWanderSideBX = s_petHomeX;

  s_petWanderState = PetWanderState::HOME_IDLE;
  s_petWanderLastStepMs = 0;
  s_petWalkFrame = 0;

  cancelAlienBabyBoredTeleport();
  scheduleNextPetWander();
}

// ============================================================================
// INTRO WALK (ENTRY)
// ============================================================================

void startPetIntroWalkFromLeft()
{
  getPetHomeScreenPosition(s_petHomeX, s_petHomeY);

  s_petScreenX = -PET_SPR_W;
  s_petScreenY = s_petHomeY;

  s_petScreenPosInitialized = true;

  s_petWalkFrame = 0;

  s_petIntroWalkActive = true;
  s_petIntroWalkLastStepMs = millis();

  s_petWanderState = PetWanderState::HOME_IDLE;
}

void tickPetIntroWalk()
{
  if (!s_petIntroWalkActive)
  {
    if (s_petIntroStandHoldActive)
    {
      if ((millis() - s_petIntroStandHoldStartMs) >= kPetIntroStandHoldMs)
      {
        s_petIntroStandHoldActive = false;
        s_petIntroHandoffActive = true;
        requestUIRedraw();
      }
      return;
    }

    if (s_petIntroArriveTurnActive)
    {
      if ((millis() - s_petIntroArriveTurnStartMs) >= kPetIntroArriveTurnMs)
      {
        s_petIntroArriveTurnActive = false;
        s_petIntroStandHoldActive = true;
        s_petIntroStandHoldStartMs = millis();
        requestUIRedraw();
      }
      return;
    }

    return;
  }

  const bool freeRoamState = (g_app.uiState == UIState::PET_SCREEN || g_app.uiState == UIState::CLOCK_MODE);

  if (!freeRoamState)
    return;

  int homeX = 0;
  int homeY = 0;
  getPetHomeScreenPosition(homeX, homeY);

  s_petScreenY = homeY;

  const uint32_t now = millis();
  if ((now - s_petIntroWalkLastStepMs) < kPetIntroWalkStepMs)
    return;

  s_petIntroWalkLastStepMs = now;

  if (s_petScreenX < homeX)
  {
    s_petScreenX += kPetIntroWalkStepPx;
    if (s_petScreenX > homeX)
      s_petScreenX = homeX;

    s_petWalkFrame ^= 1;
    requestUIRedraw();
  }

  if (s_petScreenX >= homeX)
  {
    s_petScreenX = homeX;
    s_petScreenY = homeY;
    s_petIntroWalkActive = false;

    s_petIntroArriveTurnActive = true;
    s_petIntroArriveTurnStartMs = millis();

    requestUIRedraw();
  }
}

void tickPetWander()
{
  if (s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive)
    return;

  const bool wanderActive = (s_petWanderState != PetWanderState::HOME_IDLE);

  const bool freeRoamState = (g_app.uiState == UIState::PET_SCREEN || g_app.uiState == UIState::CLOCK_MODE);

  if (!freeRoamState)
  {
    // Keep wander state intact while browsing other tabs.
    // The pet should continue from the same path when returning to the pet tab,
    // not snap home or restart its route.
    return;
  }

  s_petScreenY = s_petHomeY;

  if (pet.isSleeping)
  {
    resetPetWanderToHome();
    return;
  }

  const uint32_t now = millis();
  const PetMood mood = petResolveMood(pet);

  if (tickAlienBabyBoredTeleport(now, mood))
    return;

  const bool wanderAllowed = (mood == MOOD_HAPPY || mood == MOOD_BORED);
  if (!wanderAllowed)
  {
    resetPetWanderToHome();
    return;
  }

  switch (s_petWanderState)
  {
  case PetWanderState::HOME_IDLE:
  {
    if (s_petWanderUntilMs == 0)
      scheduleNextPetWander();

    if ((int32_t)(now - s_petWanderUntilMs) < 0)
      return;

    int offsetA = 0;
    for (int tries = 0; tries < 8; ++tries)
    {
      offsetA = (int)random(-kPetWanderRangePx, kPetWanderRangePx + 1);
      if (abs(offsetA) >= kPetWanderMinMovePx)
        break;
    }

    if (abs(offsetA) < kPetWanderMinMovePx)
    {
      scheduleNextPetWander();
      return;
    }

    const bool inClockMode = (g_app.uiState == UIState::CLOCK_MODE);
    const int minAnchorX = PET_SPR_W / 2;
    const int rightClearancePx = 12;

    int maxAnchorX = 0;

    if (inClockMode)
    {
      maxAnchorX = SCREEN_W - (PET_SPR_W / 2) - rightClearancePx;
    }
    else
    {
      const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;
      maxAnchorX = petAreaW - (PET_SPR_W / 2) - rightClearancePx;
    }

    const int originX = s_petScreenX;

    s_petWanderSideAX = clampi(originX + offsetA, minAnchorX, maxAnchorX);

    int offsetB = -offsetA;
    if (abs(offsetB) < kPetWanderMinMovePx)
      offsetB = (offsetB < 0) ? -kPetWanderMinMovePx : kPetWanderMinMovePx;

    s_petWanderSideBX = clampi(originX + offsetB, minAnchorX, maxAnchorX);

    if (abs(s_petWanderSideAX - originX) < kPetWanderMinMovePx ||
        abs(s_petWanderSideBX - s_petWanderSideAX) < kPetWanderMinMovePx)
    {
      scheduleNextPetWander();
      return;
    }

    s_petWalkFrame = 0;
    s_petWanderTargetX = s_petWanderSideAX;
    s_petWanderState = PetWanderState::MOVING_TO_SIDE_A;
    s_petWanderLastStepMs = now;
    requestUIRedraw();
    return;
  }

  case PetWanderState::MOVING_TO_SIDE_A:
  {
    if ((int32_t)(now - s_petWanderLastStepMs) < (int32_t)kPetWanderStepMs)
      return;

    s_petWanderLastStepMs = now;

    if (abs(s_petScreenX - s_petWanderTargetX) <= kPetWanderStepPx)
    {
      s_petScreenX = s_petWanderTargetX;
      s_petWanderState = PetWanderState::PAUSE_AWAY_1;
      s_petWanderUntilMs = now + kPetWanderPauseAwayMs;
      return;
    }

    s_petScreenX += (s_petScreenX < s_petWanderTargetX) ? kPetWanderStepPx : -kPetWanderStepPx;
    s_petWalkFrame ^= 1;
    requestUIRedraw();
    return;
  }

  case PetWanderState::PAUSE_AWAY_1:
  {
    if ((int32_t)(now - s_petWanderUntilMs) < 0)
      return;

    s_petWanderTargetX = s_petWanderSideBX;
    s_petWanderState = PetWanderState::MOVING_TO_SIDE_B;
    s_petWanderLastStepMs = now;
    requestUIRedraw();
    return;
  }

  case PetWanderState::MOVING_TO_SIDE_B:
  {
    if ((int32_t)(now - s_petWanderLastStepMs) < (int32_t)kPetWanderStepMs)
      return;

    s_petWanderLastStepMs = now;

    if (abs(s_petScreenX - s_petWanderTargetX) <= kPetWanderStepPx)
    {
      s_petScreenX = s_petWanderTargetX;
      s_petWanderState = PetWanderState::PAUSE_AWAY_2;
      s_petWanderUntilMs = now + kPetWanderPauseAwayMs;
      return;
    }

    s_petScreenX += (s_petScreenX < s_petWanderTargetX) ? kPetWanderStepPx : -kPetWanderStepPx;
    s_petWalkFrame ^= 1;
    requestUIRedraw();
    return;
  }

  case PetWanderState::PAUSE_AWAY_2:
  {
    if ((int32_t)(now - s_petWanderUntilMs) < 0)
      return;

    s_petWanderState = PetWanderState::HOME_IDLE;
    scheduleNextPetWander();
    requestUIRedraw();
    return;
  }

  case PetWanderState::RETURNING_HOME:
  {
    if (now - s_petWanderLastStepMs < kPetWanderStepMs)
      return;

    s_petWanderLastStepMs = now;

    int dx = s_petHomeX - s_petScreenX;

    if (abs(dx) <= kPetWanderStepPx)
    {
      s_petScreenX = s_petHomeX;
      s_petScreenY = s_petHomeY;
      s_petWanderState = PetWanderState::HOME_IDLE;
      s_petWanderUntilMs = now + random(kPetWanderMinIdleMs, kPetWanderMaxIdleMs);
      return;
    }

    s_petScreenX += (dx > 0) ? kPetWanderStepPx : -kPetWanderStepPx;
    s_petWalkFrame ^= 1;
    requestUIRedraw();
    return;
  }
  }
}

static int getWalkBaselineAdjustForPet()
{
  switch (pet.type)
  {
  case PET_DEVIL:
    switch (pet.evoStage)
    {
    case 0:
      return -9;
    case 1:
      return -9;
    case 2:
      return -20;
    case 3:
      return -25;
    default:
      return -2;
    }

  case PET_ELDRITCH:
    switch (pet.evoStage)
    {
    case 0:
      return -1;
    case 1:
      return -9;
    case 2:
      return -18;
    case 3:
      return -19;
    default:
      return -6;
    }

  case PET_ALIEN:
    switch (pet.evoStage)
    {
    case 0:
      return -5;
    case 1:
      return -14;
    default:
      return -14;
    }
  }
}

bool drawIntroWalkingPetOverride()
{
  if (!g_sdReady)
    return false;

  const bool walking = s_petIntroWalkActive || s_petWanderState == PetWanderState::MOVING_TO_SIDE_A ||
                       s_petWanderState == PetWanderState::MOVING_TO_SIDE_B ||
                       s_petWanderState == PetWanderState::RETURNING_HOME;

  bool facingLeft = false;

  if (s_petIntroWalkActive)
  {
    facingLeft = false;
  }
  else if (s_petWanderState == PetWanderState::MOVING_TO_SIDE_A || s_petWanderState == PetWanderState::MOVING_TO_SIDE_B)
  {
    facingLeft = (s_petWanderTargetX < s_petScreenX);
  }
  else if (s_petWanderState == PetWanderState::RETURNING_HOME)
  {
    facingLeft = (s_petHomeX < s_petScreenX);
  }

  if (walking)
    s_petFacingLeft = facingLeft;

  const char *path = nullptr;

  if (walking)
  {
    const uint32_t frame = s_petWalkFrame & 1U;

    if (pet.type == PET_DEVIL)
    {
      if (pet.evoStage == 0)
      {
        if (facingLeft)
          path = frame ? PATH_DEV_BB_WALK2_L : PATH_DEV_BB_WALK1_L;
        else
          path = frame ? PATH_DEV_BB_WALK2 : PATH_DEV_BB_WALK1;
      }
      else if (pet.evoStage == 1)
      {
        if (facingLeft)
          path = frame ? PATH_DEV_TN_WALK2_L : PATH_DEV_TN_WALK1_L;
        else
          path = frame ? PATH_DEV_TN_WALK2 : PATH_DEV_TN_WALK1;
      }
      else if (pet.evoStage == 2)
      {
        if (facingLeft)
          path = frame ? PATH_DEV_AD_WALK2_L : PATH_DEV_AD_WALK1_L;
        else
          path = frame ? PATH_DEV_AD_WALK2 : PATH_DEV_AD_WALK1;
      }
      else
      {
        if (facingLeft)
          path = frame ? PATH_DEV_EL_WALK2_L : PATH_DEV_EL_WALK1_L;
        else
          path = frame ? PATH_DEV_EL_WALK2 : PATH_DEV_EL_WALK1;
      }
    }
    else if (pet.type == PET_ELDRITCH)
    {
      if (pet.evoStage == 0)
      {
        if (facingLeft)
          path = frame ? PATH_ELD_BB_WALK2_L : PATH_ELD_BB_WALK1_L;
        else
          path = frame ? PATH_ELD_BB_WALK2 : PATH_ELD_BB_WALK1;
      }
      else if (pet.evoStage == 1)
      {
        if (facingLeft)
          path = frame ? PATH_ELD_TN_WALK2_L : PATH_ELD_TN_WALK1_L;
        else
          path = frame ? PATH_ELD_TN_WALK2 : PATH_ELD_TN_WALK1;
      }
      else if (pet.evoStage == 2)
      {
        if (facingLeft)
          path = frame ? PATH_ELD_AD_WALK2_L : PATH_ELD_AD_WALK1_L;
        else
          path = frame ? PATH_ELD_AD_WALK2 : PATH_ELD_AD_WALK1;
      }
      else
      {
        if (facingLeft)
          path = frame ? PATH_ELD_EL_WALK2_L : PATH_ELD_EL_WALK1_L;
        else
          path = frame ? PATH_ELD_EL_WALK2 : PATH_ELD_EL_WALK1;
      }
    }
    else if (pet.type == PET_ALIEN)
    {
      if (pet.evoStage == 0)
      {
        if (facingLeft)
          path = frame ? PATH_AL_BB_WALK2_L : PATH_AL_BB_WALK1_L;
        else
          path = frame ? PATH_AL_BB_WALK2 : PATH_AL_BB_WALK1;
      }
      else if (pet.evoStage == 1)
      {
        if (facingLeft)
          path = frame ? PATH_AL_TN_WALK2_L : PATH_AL_TN_WALK1_L;
        else
          path = frame ? PATH_AL_TN_WALK2 : PATH_AL_TN_WALK1;
      }
      else
      {
        // Temporary fallback until Alien adult/elder walk frames exist.
        if (facingLeft)
          path = frame ? PATH_AL_TN_WALK2_L : PATH_AL_TN_WALK1_L;
        else
          path = frame ? PATH_AL_TN_WALK2 : PATH_AL_TN_WALK1;
      }
    }
  }

  if (!path || !path[0])
    return false;

  int w = PET_SPR_W;
  int h = PET_SPR_H;
  (void)getPngWH(path, w, h);

  const int drawX = s_petScreenX - (w / 2);

  const int yOffset =
      (s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive) ? kPetIntroYOffset : 0;

  const int anchorH = PET_SPR_H;
  const int walkBaselineAdjust = getWalkBaselineAdjustForPet();
  const int drawY = s_petScreenY - anchorH + yOffset + walkBaselineAdjust;

  const bool ok = sprDrawPngFromSD(path, drawX, drawY);

  if (!ok)
  {
#if defined(DEBUG_PET_DRAW)
    Serial.printf("[PET WALK] draw failed path='%s' x=%d y=%d w=%d h=%d\n", path, drawX, drawY, w, h);
#endif
  }
  return ok;
}

static bool drawAlienBabyBoredTeleportOverride()
{
  if (!alienBabyBoredTeleportActive())
    return false;

  if (s_alienTeleportState == AlienTeleportState::BLANK_HOLD)
    return true;

  const uint8_t idx = (s_alienTeleportFrame < kAlienTeleportFrameCount) ? s_alienTeleportFrame : 0;
  const char *path = PATH_AL_BB_BORED_TP[idx];

  if (!path || !path[0])
    return false;

  int w = PET_SPR_W;
  int h = PET_SPR_H;
  (void)getPngWH(path, w, h);

  const int drawX = s_petScreenX - (w / 2);
  static constexpr int kAlienTeleportYOffset = -15;
  const int drawY = s_petScreenY - PET_SPR_H + getWalkBaselineAdjustForPet() + kAlienTeleportYOffset;
  return sprDrawPngFromSD(path, drawX, drawY);
}

static void drawStepCounterBadge()
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
  const int x = 4;
  const int y = PET_AREA_Y + 2;

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

static void drawPetScreenImpl(bool redrawBg)
{
  if (!isScreenOn())
    return;

  static PetType s_lastBgPetType = (PetType)255;
  static uint8_t s_lastBgEvoStage = 255;

  const bool petChanged = (s_lastBgPetType != pet.type) || (s_lastBgEvoStage != pet.evoStage);

  const char *bgPath = bgPathForPetWithStage(pet.type, pet.evoStage);
  const bool cacheMissing = !petLayerReady || !s_petBgCachedPath || !bgPath || strcmp(s_petBgCachedPath, bgPath) != 0 ||
                            s_petBgCachedType != pet.type || s_petBgCachedStage != pet.evoStage;

  const bool needPetBg = cacheMissing || petChanged || g_forcePetBgCache;

  s_lastBgPetType = pet.type;
  s_lastBgEvoStage = pet.evoStage;

  bool animChanged = false;
  if (g_app.currentTab == Tab::TAB_PET)
    animChanged = animConsumeFrameChanged();
  else
    (void)animConsumeFrameChanged();

  const bool needRestore = redrawBg || animChanged || needPetBg;

  if (s_petIntroHandoffActive && animChanged)
  {
    s_petIntroHandoffActive = false;
    requestUIRedraw();
  }

  cachePetAreaBackgroundIfNeeded(needPetBg);
  g_forcePetBgCache = false;

  if (needPetBg || needRestore)
    restorePetAreaFromCache();

  drawTopBar();

  int homeCenterX = 0;
  int homeBottomY = 0;
  getPetHomeScreenPosition(homeCenterX, homeBottomY);

  s_petHomeX = homeCenterX;
  s_petHomeY = homeBottomY;

  if (!s_petScreenPosInitialized)
  {
    s_petScreenX = homeCenterX;
    s_petScreenY = homeBottomY;
    s_petScreenPosInitialized = true;
  }

  const bool animReady = animEnsurePetScreenReady();

  if (drawAlienBabyBoredTeleportOverride())
  {
    // Special Alien bored teleport drew a frame, or intentionally left the pet hidden.
  }
  else if (petWalkOverrideActive())
  {
    if (!drawIntroWalkingPetOverride() && animReady)
      animDrawPetFrameAnchoredBottom(s_petScreenX, s_petScreenY);
  }
  else if (animReady)
  {
    animDrawPetFrameAnchoredBottom(s_petScreenX, s_petScreenY);
  }

  drawMiniStatPreview();
  drawStepCounterBadge();
  drawTabBar();
  drawPetPerfHud();
}

void drawPetScreen(bool redrawBg) { drawPetScreenImpl(redrawBg); }