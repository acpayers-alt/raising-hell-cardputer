#include "pet_state.h"

#include <Arduino.h>
#include "pet.h"
#include "save_manager.h"
#include "app_state.h"

Pet pet;

static uint32_t g_resGraceUntilMs = 0;

void petResurrectFull() {
  pet.health     = 100;
  pet.hunger     = 100;
  pet.energy     = 100;
  pet.happiness  = 100;
  pet.isSleeping = false;

  g_app.isSleeping = false;
  g_app.sleepingByTimer = false;
  g_app.sleepUntilRested = false;
  g_app.sleepUntilAwakened = false;
  g_app.sleepTargetEnergy = 0;
  g_app.sleepStartTime = 0;
  g_app.sleepDurationMs = 0;

  saveManagerClearSleepPendingFlag();

  g_resGraceUntilMs = millis() + 2000;

  petResetUpdateTimers();

  saveManagerMarkDirty();
}

bool petResurrectGraceActive() {
  return (int32_t)(g_resGraceUntilMs - millis()) > 0;
}
