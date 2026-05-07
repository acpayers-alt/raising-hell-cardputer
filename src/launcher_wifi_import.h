#pragma once

#include <Arduino.h>

bool launcherImportWifiCreds(String &outSsid, String &outPwd);

bool launcherWifiSsidVisible(const char *ssid);