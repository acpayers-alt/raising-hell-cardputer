#pragma once
#include <Arduino.h>

static constexpr int WIFI_PROFILE_MAX = 3;

bool wifiStoreLoad(String &ssid, String &pass);
bool wifiStoreLoadProfile(int index, String &ssid, String &pass);
int wifiStoreCount();

void wifiStoreSave(const String &ssid, const String &pass);
void wifiStoreClear();
bool wifiStoreHasCreds();

bool wifiStoreDeleteProfile(int index);