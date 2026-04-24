#pragma once

void drawControlsHelpScreen();
void drawBootWifiPromptScreen();
void drawBootAssetWifiRequiredScreen();
void drawBootWifiImportedScreen();
void drawBootWifiWaitScreen(bool connected, int rssi);
void drawBootTimezonePickScreen();
void drawBootNtpWaitScreen(bool connected, bool synced);
void drawBootLowBatteryChargingScreen(int mv, int pct, bool usb, bool readyToBoot);
void drawBootSplash();