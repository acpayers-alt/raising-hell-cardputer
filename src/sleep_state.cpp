// sleep_state.cpp
#include "sleep_state.h"
#include "app_state.h"
#include "pet.h"
#include "ui_runtime.h"
#include "auto_screen.h"
#include "brightness_state.h"

void sleepMiniStatsHeartbeat(uint32_t now) {
if (g_app.uiState != UIState::PET_SLEEPING) return;

  static bool s_inited = false;
  static int  s_hunger = 0;
  static int  s_mood   = 0;
  static int  s_energy = 0;
  static int  s_health = 0;

  static uint32_t s_nextPulseMs = 0;
  const uint32_t kPulseMs = 500;

  if (!s_inited) {
    s_inited = true;
    s_hunger = pet.hunger;
    s_mood   = pet.happiness;
    s_energy = pet.energy;
    s_health = pet.health;
    requestUIRedraw();
    s_nextPulseMs = now + kPulseMs;
    return;
  }

  const bool changed =
    (pet.hunger     != s_hunger) ||
    (pet.happiness  != s_mood)   ||
    (pet.energy     != s_energy) ||
    (pet.health     != s_health);

  if (changed) {
    s_hunger = pet.hunger;
    s_mood   = pet.happiness;
    s_energy = pet.energy;
    s_health = pet.health;
    requestUIRedraw();
    return;
  }

  if ((int32_t)(now - s_nextPulseMs) >= 0) {
    s_nextPulseMs = now + kPulseMs;
    requestUIRedraw();
  }
}
