#pragma once

#include <Arduino.h>

bool bootMarkFirmwareSeenAndRequestProvisionIfChanged();

bool bootFirmwareMarkerRead(String &outBuildId);
bool bootFirmwareMarkerWrite(const char *buildId);
bool bootFirmwareMarkerClear();
const char *bootCurrentBuildId();