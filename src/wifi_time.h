#pragma once

// -----------------------------------------------------------------------------
// WiFi + Time bridge
//
// This module owns:
//  - the global WiFi enable gate (soft switch)
//  - connection status caching (for UI)
//  - starting SNTP once per successful connect
//  - small console helpers to avoid including <WiFi.h> everywhere
// -----------------------------------------------------------------------------

#include <stdint.h>

// Boot / loop hooks
void wifiTimeInit();
void wifiTimeTick();

// Enable gate (soft switch)
bool wifiIsEnabled();
void wifiSetEnabled(bool en);

// Status (cached for UI; updated from events/tick)
bool wifiIsConnected();
int  wifiRssi();
bool timeIsSynced();
bool timeIsNtpSyncedStrict();

// Instant status check (polls WiFi.status())
// Prefer wifiIsConnected() for UI; prefer this for one-shot guards.
bool wifiIsConnectedNow();

// Console helpers (avoid including <WiFi.h> in console.cpp)
void        wifiConsoleBeginConnect(const char* ssid, const char* pass);
void        wifiConsoleDisconnect(bool eraseCreds);
const char* wifiConsoleSsid();
int         wifiConsoleRssi();
const char* wifiConsoleIpString();

uint32_t    wifiConsoleConnectAgeMs();
int         wifiConsoleStatus();
const char* wifiConsoleStatusString();

void wifiForceSntpPending();
void wifiStartSntpNow();