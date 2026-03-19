#pragma once

#include <stdint.h>

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

  uint8_t connectFailCount = 0;
};

extern WifiSetupState g_wifi;
extern bool g_wifiSetupFromBootWizard;

// Legacy reference aliases
extern int &wifiSettingsIndex;
extern uint8_t &wifiSetupStage;
extern char (&wifiSetupSsid)[33];
extern char (&wifiSetupPass)[65];
extern char (&wifiSetupBuf)[65];

extern WifiSetupState g_wifi;
extern bool g_wifiSetupFromBootWizard;

// -----------------------------------------------------------------------------
// Legacy reference aliases
// -----------------------------------------------------------------------------
// Keep these for older code that still uses bare names like wifiSetupBuf.
// IMPORTANT: do NOT use macros for these names.

extern int& wifiSettingsIndex;
extern uint8_t& wifiSetupStage;
extern char (&wifiSetupSsid)[33];
extern char (&wifiSetupPass)[65];
extern char (&wifiSetupBuf)[65];
