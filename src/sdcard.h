#pragma once
#include <Arduino.h>
#include <stdint.h>

// Global SD readiness flag
extern bool g_sdReady;

// Init / diagnostics
bool initSD();
void listDir(const char* path);
bool remountSD();
bool remountSDWithRetry(uint8_t attempts = 2);

// Save / load
bool sdLoadSave(struct SavePayload &out);
bool sdWriteSave(const struct SavePayload &in);
bool loadSettingsFromSD();
bool sdReady();

// Autosave helpers
void sdMarkDirty();     // call whenever data changes
void sdAutosaveTick();  // call in loop()
bool sdForceSaveNow();  // call on power/menu exit
