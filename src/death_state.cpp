#include "death_state.h"

#include "app_state.h"
#include "brightness_state.h"
#include "display.h"
#include "sound.h"
#include "ui_actions.h"
#include "ui_invalidate.h"
#include "pet.h"

// Default to first option (Resurrect).
int deathMenuIndex = 0;

namespace
{
static bool s_deathTransitionActive = false;
static bool s_deathTransitionSoundStarted = false;
static uint32_t s_deathTransitionStartMs = 0;

static constexpr uint32_t kDeathFadeMs = 2200;
static constexpr uint32_t kDeathBlackHoldMs = 600;
static bool s_restoreBrightnessOnDeathScreen = false;
static bool s_startDeathScreenFadeIn = false;
} // namespace

void resetDeathMenu() { deathMenuIndex = 0; }

bool deathTransitionActive() { return s_deathTransitionActive; }

void beginDeathTransition()
{
  s_deathTransitionActive = true;
  s_deathTransitionSoundStarted = false;
  s_deathTransitionStartMs = millis();

  Serial.printf("[PET] %s has died\n", pet.name[0] ? pet.name : "Your pet");

  resetDeathMenu();

  uiActionEnterState(UIState::DEATH_TRANSITION, Tab::TAB_PET, true);
  requestUIRedraw();
}

bool consumeDeathScreenFadeInStart()
{
  const bool wasSet = s_startDeathScreenFadeIn;
  s_startDeathScreenFadeIn = false;
  return wasSet;
}

bool consumeDeathScreenBrightnessRestore()
{
  const bool wasSet = s_restoreBrightnessOnDeathScreen;
  s_restoreBrightnessOnDeathScreen = false;
  return wasSet;
}

void tickDeathTransition(uint32_t now)
{
  if (!s_deathTransitionActive)
    return;

  if (g_app.uiState != UIState::DEATH_TRANSITION)
    return;

    if (!s_deathTransitionSoundStarted)
    {
      s_deathTransitionSoundStarted = true;
    
      soundDeathFlatline();
    }
  
    soundTickFlatlineFade();
    
  if ((int32_t)(now - s_deathTransitionStartMs) < 0)
    return;

  const uint32_t elapsed = now - s_deathTransitionStartMs;

  const uint32_t fadeElapsed = (elapsed > kDeathFadeMs) ? kDeathFadeMs : elapsed;

  const uint8_t targetBrightness = (uint8_t)brightnessValues[brightnessLevel];
  const uint8_t fadeBrightness =
      (uint8_t)(((uint32_t)targetBrightness * (kDeathFadeMs - fadeElapsed)) / kDeathFadeMs);

  setBacklight(fadeBrightness);

  requestUIRedraw();

  if (elapsed < (kDeathFadeMs + kDeathBlackHoldMs))
    return;

  s_deathTransitionActive = false;
  s_deathTransitionSoundStarted = false;
  s_startDeathScreenFadeIn = true;

  uiActionEnterState(UIState::DEATH, Tab::TAB_PET, true);
  requestUIRedraw();
}
