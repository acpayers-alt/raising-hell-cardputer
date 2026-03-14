// flow_boot_wifi.h
#pragma once

#include "ui_defs.h"

struct InputState;

// Boot wizard entry point (called by boot_pipeline)
void bootWizardBegin(UIState afterState, Tab afterTab);

// Boot wizard UIState handlers (routed by ui_input_router)
void uiBootWifiPromptHandle(InputState &in);
void uiBootWifiWaitHandle(InputState &in);
void uiBootWifiImportedHandle(InputState &in);
void uiBootTzPickHandle(InputState &in);
void uiBootNtpWaitHandle(InputState &in);
void uiBootAssetWifiRequiredHandle(InputState &in);


//Launcher Import Handlers
void bootWifiSetImportedInfo(const char *ssid);
void bootWifiClearImportedInfo();

const char *bootWifiImportedSsid();

extern bool g_bootProvisionWifiOnboardingStarted;