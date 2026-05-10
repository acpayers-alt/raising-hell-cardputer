#pragma once

#include <stdint.h>
#include "ui_defs.h"

// Dedicated WiFi UI/Wizard state.
// This replaces the old g_ui.wifi* bucket fields.

enum WifiSetupStage : uint8_t
{
  WIFI_SETUP_STAGE_SCAN = 0,
  WIFI_SETUP_STAGE_SSID = 1,
  WIFI_SETUP_STAGE_PASS = 2
  
};

struct WifiSetupState
{
  // WiFi settings page cursor (Settings -> WiFi)
  int wifiSettingsIndex = 0;
  int storedNetworkIndex = 0;
  int storedNetworkActionIndex = 0; // 0 Connect, 1 Delete
  bool storedNetworkActionActive = false;
  
  // Setup stage
  uint8_t setupStage = WIFI_SETUP_STAGE_SCAN;

  // Buffers
  char ssid[33] = {0};
  char pass[65] = {0};
  char buf[65] = {0};

  // Scan UI state
  bool scanStarted = false;
  bool scanInProgress = false;
  int16_t scanCount = 0;
  int scanIndex = 0;

  static constexpr int kMaxScanResults = 8;
  char scanSsids[kMaxScanResults][33] = {};
  int16_t scanRssi[kMaxScanResults] = {};
  bool scanOpen[kMaxScanResults] = {};

  uint8_t connectFailCount = 0;
  bool connectResultPending = false;
  bool connectResultSuccess = false;
  uint32_t connectResultShownAtMs = 0;

  UIState returnState = UIState::SETTINGS;
  Tab returnTab = Tab::TAB_PET;

  bool aborted = false;
};

extern WifiSetupState g_wifi;
extern bool g_wifiSetupFromBootWizard;

// Legacy reference aliases
extern int &wifiSettingsIndex;
extern uint8_t &wifiSetupStage;
extern char (&wifiSetupSsid)[33];
extern char (&wifiSetupPass)[65];
extern char (&wifiSetupBuf)[65];

