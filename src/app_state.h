#pragma once

#include "inventory.h"
#include "pet_flow_state.h"
#include "ui_defs.h"
#include "ui_invalidate.h"
#include <Arduino.h>

// Central app-wide runtime state.
// Keep this as the home for "global but not gameplay" UI/runtime flags.
struct AppState
{
  // Display / rendering
  bool screenOn = true;
  uint32_t lastRenderTimeMs = 0;
  bool uiNeedsRedraw = true;
  PetFlowState flow;
  Inventory inventory;

  // Primary UI routing
  UIState uiState = UIState::BOOT;
  Tab currentTab = Tab::TAB_PET;

  // Legacy UI flags (still used by a few call sites)
  bool inShopMenu = false;
  bool inMiniGame = false;

  // Common menu cursors
  int settingsIndex = 0;
  int powerMenuIndex = 0;

  // Legacy menu cursors (migrated from ui_menu_state.cpp)
  int sleepMenuIndex = 0;
  int playMenuIndex = 0;
  int selectedMenu = 0;

  // Settings sub-menu indices (migrated from g_ui / UIRuntimeState)
  int screenSettingsIndex = 0;
  int systemSettingsIndex = 0;
  int petSettingsIndex = 0;
  int gameOptionsIndex = 0;
  int autoScreenIndex = 0;
  int decayModeIndex = 0;

  // Runtime flags
  bool gameOver = false;
  bool newPetFlowActive = false;
  bool petScreenIntroFadePending = false;

  // Sleep state
  bool isSleeping = false;
  bool sleepingByTimer = false;
  bool sleepUntilRested = false;
  bool sleepUntilAwakened = false;
  uint8_t sleepTargetEnergy = 0;
  uint32_t sleepStartTime = 0;
  uint32_t sleepDurationMs = 0;

  // Settings toggles
  bool wifiEnabled = false;
  bool autoScreenOffEnabled = true;
};

extern AppState g_app;
