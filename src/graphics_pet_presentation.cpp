#include "graphics_pet_presentation.h"
#include "display.h"
#include "graphics.h"
#include "graphics_render_utils.h"
#include "graphics_shared_utils.h"

#include <Arduino.h>
#include <stdlib.h>

#include "app_state.h"
#include "pet.h"

extern Pet pet;
extern AppState g_app;
extern bool g_sdReady;

extern bool g_forcePetBgCache;
extern const char *g_petBgCachedPath;
extern bool petLayerReady;

void requestUIRedraw();
bool isScreenOn();

void cachePetAreaBackgroundIfNeeded(bool forceRefresh);
void restorePetAreaFromCache();

void drawTopBar();
void drawMiniStatPreview();
void drawTabBar();
void drawPetPerfHud();

bool animConsumeFrameChanged();
void animDrawPetFrameAnchoredBottom(int anchorCenterX, int anchorBottomY);

const char *bgPathForPetWithStage(PetType t, uint8_t evoStage);

bool getPngWH(const char *path, int &w, int &h);

// ---------------------------------------------------------------------------
// Pet screen position (anchor-based, bottom-center)
// ---------------------------------------------------------------------------

static int s_petScreenX = 0;
static int s_petScreenY = 0;
static bool s_petScreenPosInitialized = false;

static bool s_petIntroWalkActive = false;
static uint32_t s_petIntroWalkLastStepMs = 0;
static constexpr int kPetIntroWalkStepPx = 3;
static constexpr uint32_t kPetIntroWalkStepMs = 40;

static bool s_petIntroArriveTurnActive = false;
static uint32_t s_petIntroArriveTurnStartMs = 0;
static constexpr uint32_t kPetIntroArriveTurnMs = 180;

static bool s_petIntroStandHoldActive = false;
static uint32_t s_petIntroStandHoldStartMs = 0;
static constexpr int kPetIntroYOffset = 0;
static constexpr uint32_t kPetIntroStandHoldMs = 300;
static bool s_petIntroHandoffActive = false;
static constexpr uint32_t kPetIntroWalkFrameMs = 45;

static int s_petHomeX = 0;
static int s_petHomeY = 0;

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

static constexpr int kPetWanderRangePx = 55;
static constexpr int kPetWanderMinMovePx = 28;
static constexpr int kPetWanderStepPx = 2;
static constexpr uint32_t kPetWanderStepMs = 30;
static constexpr uint32_t kPetWanderPauseAwayMs = 5000;
static constexpr uint32_t kPetWanderMinIdleMs = 5000;
static constexpr uint32_t kPetWanderMaxIdleMs = 7000;

int petPresentationX() { return s_petScreenX; }
int petPresentationY() { return s_petScreenY; }
bool petPresentationHasIntroHandoff() { return s_petIntroHandoffActive; }

void clearPetPresentationIntroHandoff() { s_petIntroHandoffActive = false; }

bool petPresentationScriptedIntroActive()
{
  return s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive || s_petIntroHandoffActive;
}

bool petPresentationAnimating()
{
  return petPresentationScriptedIntroActive() || s_petWanderState == PetWanderState::MOVING_TO_SIDE_A ||
         s_petWanderState == PetWanderState::MOVING_TO_SIDE_B || s_petWanderState == PetWanderState::RETURNING_HOME;
}

static const PetRenderProfile kPetProfile[] = {
    /* PET_DEVIL    */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
    /* PET_ELDRITCH */ {PET_SPR_W, PET_SPR_H, PET_X_OFFSET, PET_Y_OFFSET},
};

const PetRenderProfile &getPetProfile(PetType t)
{
  int idx = (int)t;
  const int count = (int)(sizeof(kPetProfile) / sizeof(kPetProfile[0]));
  if (idx < 0 || idx >= count)
    idx = 0;
  return kPetProfile[idx];
}

bool petWalkOverrideActive()
{
  return s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive || s_petIntroHandoffActive ||
         s_petWanderState == PetWanderState::MOVING_TO_SIDE_A || s_petWanderState == PetWanderState::MOVING_TO_SIDE_B ||
         s_petWanderState == PetWanderState::RETURNING_HOME;
}

// -- Pet Walking Paths

// -- Devil
// -- Devil Baby
static const char *PATH_DEV_BB_WALK1 = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walk1.png";
static const char *PATH_DEV_BB_WALK2 = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walk2.png";
static const char *PATH_DEV_BB_WALK1_L = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walkleft1.png";
static const char *PATH_DEV_BB_WALK2_L = "/raising_hell/graphics/pet/anim/dev/bb/wlk/dev_bb_walkleft2.png";

// -- Devil Teen
static const char *PATH_DEV_TN_WALK1 = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walk1.png";
static const char *PATH_DEV_TN_WALK2 = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walk2.png";
static const char *PATH_DEV_TN_WALK1_L = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walkleft1.png";
static const char *PATH_DEV_TN_WALK2_L = "/raising_hell/graphics/pet/anim/dev/tn/wlk/dev_tn_walkleft2.png";

// -- Devil Teen
static const char *PATH_DEV_AD_WALK1 = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walk1.png";
static const char *PATH_DEV_AD_WALK2 = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walk2.png";
static const char *PATH_DEV_AD_WALK1_L = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walkleft1.png";
static const char *PATH_DEV_AD_WALK2_L = "/raising_hell/graphics/pet/anim/dev/ad/wlk/dev_ad_walkleft2.png";

// -- Devil Elder
static const char *PATH_DEV_EL_WALK1 = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walk1.png";
static const char *PATH_DEV_EL_WALK2 = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walk2.png";
static const char *PATH_DEV_EL_WALK1_L = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walkleft1.png";
static const char *PATH_DEV_EL_WALK2_L = "/raising_hell/graphics/pet/anim/dev/ed/wlk/dev_edr_walkleft2.png";

// -- Eldritch
// -- Eldritch Baby
static const char *PATH_ELD_BB_WALK1 = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walk1.png";
static const char *PATH_ELD_BB_WALK2 = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walk2.png";
static const char *PATH_ELD_BB_WALK1_L = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walkleft1.png";
static const char *PATH_ELD_BB_WALK2_L = "/raising_hell/graphics/pet/anim/eld/bb/wlk/eld_bb_walkleft2.png";

// -- Eldritch Teen
static const char *PATH_ELD_TN_WALK1 = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walk1.png";
static const char *PATH_ELD_TN_WALK2 = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walk2.png";
static const char *PATH_ELD_TN_WALK1_L = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walkleft1.png";
static const char *PATH_ELD_TN_WALK2_L = "/raising_hell/graphics/pet/anim/eld/tn/wlk/eld_tn_walkleft2.png";

// -- Eldritch Adult
static const char *PATH_ELD_AD_WALK1 = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walk1.png";
static const char *PATH_ELD_AD_WALK2 = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walk2.png";
static const char *PATH_ELD_AD_WALK1_L = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walkleft1.png";
static const char *PATH_ELD_AD_WALK2_L = "/raising_hell/graphics/pet/anim/eld/ad/wlk/eld_ad_walkleft2.png";

// -- Eldritch Elder
static const char *PATH_ELD_EL_WALK1 = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walk1.png";
static const char *PATH_ELD_EL_WALK2 = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walk2.png";
static const char *PATH_ELD_EL_WALK1_L = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walkleft1.png";
static const char *PATH_ELD_EL_WALK2_L = "/raising_hell/graphics/pet/anim/eld/ed/wlk/eld_ed_walkleft2.png";

void getPetHomeScreenPosition(int &outX, int &outY)
{
  const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;
  const int petAreaX = 0;

  const PetRenderProfile &prof = getPetProfile(pet.type);

  outX = petAreaX + (petAreaW / 2) + prof.xOff;
  outY = (PET_AREA_Y + PET_AREA_H) + prof.yOff;
}

void resetPetScreenPositionToHome()
{
  int homeX = 0;
  int homeY = 0;
  getPetHomeScreenPosition(homeX, homeY);

  s_petHomeX = homeX;
  s_petHomeY = homeY;

  s_petScreenX = homeX;
  s_petScreenY = homeY;
  s_petScreenPosInitialized = true;

  s_petIntroWalkActive = false;
  s_petIntroWalkLastStepMs = 0;
  s_petIntroArriveTurnActive = false;
  s_petIntroArriveTurnStartMs = 0;
  s_petIntroStandHoldActive = false;
  s_petIntroStandHoldStartMs = 0;
  s_petIntroHandoffActive = false;

  resetPetWanderToHome();
}

static void scheduleNextPetWander()
{
  const uint32_t now = millis();
  const uint32_t span = (kPetWanderMaxIdleMs - kPetWanderMinIdleMs);
  s_petWanderUntilMs = now + kPetWanderMinIdleMs + (span ? (uint32_t)random((long)span) : 0);
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
  scheduleNextPetWander();
}

void resetClockModePetPresentation()
{
  // Clear any scripted intro ownership so Clock Mode can draw the pet normally.
  s_petIntroWalkActive = false;
  s_petIntroArriveTurnActive = false;
  s_petIntroStandHoldActive = false;
  s_petIntroHandoffActive = false;

  // Reset wander state so Clock Mode starts from a clean home position.
  s_petWanderState = PetWanderState::HOME_IDLE;
  s_petWanderTargetX = 0;
  s_petWanderSideAX = 0;
  s_petWanderSideBX = 0;
  s_petWanderUntilMs = 0;
  s_petWanderLastStepMs = 0;

  s_petScreenPosInitialized = false;
}

void startPetIntroWalkFromLeft()
{
  int homeX = 0;
  int homeY = 0;
  getPetHomeScreenPosition(homeX, homeY);
  s_petHomeX = homeX;
  s_petHomeY = homeY;

  // Start fully offscreen to the left using the pet sprite width as margin.
  s_petScreenX = -PET_SPR_W;
  s_petScreenY = homeY;
  s_petScreenPosInitialized = true;

  s_petIntroWalkActive = true;
  s_petIntroWalkLastStepMs = millis();
  s_petIntroArriveTurnActive = false;
  s_petIntroStandHoldActive = false;
  s_petIntroHandoffActive = false;
  s_petWanderState = PetWanderState::HOME_IDLE;
  s_petWanderTargetX = s_petHomeX;
  s_petWanderLastStepMs = 0;
  s_petWanderUntilMs = 0;
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

  const bool freeRoamState = ((g_app.uiState == UIState::PET_SCREEN && g_app.currentTab == Tab::TAB_PET) ||
                              (g_app.uiState == UIState::CLOCK_MODE));

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
  // Never wander while the scripted intro is still owning the pet.
  if (s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive)
    return;

  const bool wanderActive = (s_petWanderState != PetWanderState::HOME_IDLE);

  // Only wander on the main PET tab or in Clock Mode.
  // IMPORTANT: do not hard-reset an active wander.
  const bool freeRoamState = ((g_app.uiState == UIState::PET_SCREEN && g_app.currentTab == Tab::TAB_PET) ||
                              (g_app.uiState == UIState::CLOCK_MODE));

  if (!freeRoamState)
  {
    if (!wanderActive)
      resetPetWanderToHome();
    return;
  }

  // Keep the pet grounded at home Y.
  s_petScreenY = s_petHomeY;

  // Don't start a new wander while sleeping, but don't interrupt one already in progress.
  if (pet.isSleeping)
  {
    if (!wanderActive)
      resetPetWanderToHome();
    return;
  }

  // Only happy or bored pets should START wandering.
  // If a wander is already active, let it finish naturally.
  const PetMood mood = petResolveMood(pet);
  const bool wanderAllowed = (mood == MOOD_HAPPY || mood == MOOD_BORED);
  if (!wanderAllowed && !wanderActive)
  {
    s_petScreenX = s_petHomeX;
    s_petScreenY = s_petHomeY;
    return;
  }

  const uint32_t now = millis();

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

    const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;

    const bool inClockMode = (g_app.uiState == UIState::CLOCK_MODE);

    const int minAnchorX = PET_SPR_W / 2;
    const int rightClearancePx = 12;

    int maxAnchorX = 0;

    if (inClockMode)
    {
      // Clock Mode has no mini-stat cluster on the right, so let the pet use
      // the full screen width (minus sprite visibility padding).
      maxAnchorX = SCREEN_W - (PET_SPR_W / 2) - rightClearancePx;
    }
    else
    {
      const int petAreaW = SCREEN_W - MINI_STAT_W - MINI_STAT_PAD;

      // PET tab still reserves space for the mini-stat cluster.
      maxAnchorX = petAreaW - (PET_SPR_W / 2) - rightClearancePx;
    }

    const int originX = s_petScreenX;

    s_petWanderSideAX = clampi(originX + offsetA, minAnchorX, maxAnchorX);

    int offsetB = -offsetA;
    if (abs(offsetB) < kPetWanderMinMovePx)
      offsetB = (offsetB < 0) ? -kPetWanderMinMovePx : kPetWanderMinMovePx;

    s_petWanderSideBX = clampi(originX + offsetB, minAnchorX, maxAnchorX);

    // Reject tiny real moves after clamping.
    if (abs(s_petWanderSideAX - originX) < kPetWanderMinMovePx ||
        abs(s_petWanderSideBX - s_petWanderSideAX) < kPetWanderMinMovePx)
    {
      scheduleNextPetWander();
      return;
    }

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
    requestUIRedraw();
    return;
  }

  case PetWanderState::PAUSE_AWAY_2:
  {
    if ((int32_t)(now - s_petWanderUntilMs) < 0)
      return;

    // Intentionally end the wander wherever the pet currently is.
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

    // Close enough → snap ONLY position, do NOT reset state logic
    if (abs(dx) <= kPetWanderStepPx)
    {
      s_petScreenX = s_petHomeX;
      s_petScreenY = s_petHomeY;

      // Transition cleanly to idle WITHOUT teleport helper
      s_petWanderState = PetWanderState::HOME_IDLE;

      // Schedule next wander
      s_petWanderUntilMs = now + random(kPetWanderMinIdleMs, kPetWanderMaxIdleMs);

      return;
    }

    // Step toward home
    s_petScreenX += (dx > 0) ? kPetWanderStepPx : -kPetWanderStepPx;
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
      return -9; // baby
    case 1:
      return -9; // teen
    case 2:
      return -20; // adult
    case 3:
      return -25; // elder
    default:
      return -2;
    }

  case PET_ELDRITCH:
    switch (pet.evoStage)
    {
    case 0:
      return -1; // baby
    case 1:
      return -9; // teen
    case 2:
      return -18; // adult
    case 3:
      return -19; // elder
    default:
      return -6;
    }

  default:
    return -2;
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

  const char *path = nullptr;

  if (walking)
  {
    const uint32_t frame = (millis() / kPetIntroWalkFrameMs) & 1U;

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
  }

  if (!path || !path[0])
    return false;

  int w = PET_SPR_W;
  int h = PET_SPR_H;
  (void)getPngWH(path, w, h);

  const int drawX = s_petScreenX - (w / 2);

  const int yOffset =
      (s_petIntroWalkActive || s_petIntroArriveTurnActive || s_petIntroStandHoldActive) ? kPetIntroYOffset : 0;

  // Anchor walking frames to the same nominal sprite box as static frames.
  // If a walking PNG is shorter than PET_SPR_H, don't let that pull it lower.
  const int anchorH = PET_SPR_H;
  const int walkBaselineAdjust = getWalkBaselineAdjustForPet();

  const int drawY = s_petScreenY - anchorH + yOffset + walkBaselineAdjust;

  const bool ok = sprDrawPngFromSD(path, drawX, drawY);

  if (!ok)
  {
    Serial.printf("[PET WALK] draw failed path='%s' x=%d y=%d w=%d h=%d\n", path, drawX, drawY, w, h);
  }
  return ok;
}

static void drawPetScreenImpl(bool redrawBg)
{
  if (!isScreenOn())
    return;

  static PetType s_lastBgPetType = (PetType)255;
  static uint8_t s_lastBgEvoStage = 255;

  const bool petChanged = (s_lastBgPetType != pet.type) || (s_lastBgEvoStage != pet.evoStage);

  const bool cacheMissing = (g_petBgCachedPath == nullptr);

  const bool needPetBg = redrawBg || petChanged || cacheMissing || g_forcePetBgCache;

  s_lastBgPetType = pet.type;
  s_lastBgEvoStage = pet.evoStage;

  bool animChanged = false;
  if (g_app.currentTab == Tab::TAB_PET)
  {
    animChanged = animConsumeFrameChanged();
  }
  else
  {
    (void)animConsumeFrameChanged();
  }

  const bool needRestore = redrawBg || animChanged || needPetBg;

  if (s_petIntroHandoffActive && animChanged)
  {
    s_petIntroHandoffActive = false;
    requestUIRedraw();
  }

  cachePetAreaBackgroundIfNeeded(needPetBg);
  g_forcePetBgCache = false;

  if (needPetBg || needRestore)
  {
    restorePetAreaFromCache();
  }

  drawTopBar();

  int homeCenterX = 0;
  int homeBottomY = 0;
  getPetHomeScreenPosition(homeCenterX, homeBottomY);

  s_petHomeX = homeCenterX;
  s_petHomeY = homeBottomY;

  // Normal PET-screen entries should land at home unless a scripted intro
  // or wander movement is actively owning the position.
  if (!s_petScreenPosInitialized)
  {
    s_petScreenX = homeCenterX;
    s_petScreenY = homeBottomY;
    s_petScreenPosInitialized = true;
  }
  else if (!petWalkOverrideActive() && g_app.uiState == UIState::PET_SCREEN && g_app.currentTab == Tab::TAB_PET)
  {
    // Do not forcibly snap here.
    // Let the wander / intro state machine own the final position.
  }

  if (petWalkOverrideActive())
  {
    if (!drawIntroWalkingPetOverride())
    {
      animDrawPetFrameAnchoredBottom(s_petScreenX, s_petScreenY);
    }
  }
  else
  {
    animDrawPetFrameAnchoredBottom(s_petScreenX, s_petScreenY);
  }

  drawMiniStatPreview();
  drawTabBar();

  drawPetPerfHud();
}

void drawPetScreen(bool redrawBg)
{
  drawPetScreenImpl(redrawBg);
}