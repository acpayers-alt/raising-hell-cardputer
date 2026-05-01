#pragma once
#include <stdint.h>

bool timeIsValid();
bool loadTimeAnchor();
bool restoreTimeFromAnchor();
void saveTimeAnchor();
void maybePeriodicTimeSave();

// NEW
bool timePersistIsRestored();
bool timePersistHasAnchor();

bool timeIsDirty();
void timeMarkClean();

// Factory reset / diagnostics helpers
// - clearTimeAnchor(): deletes the persisted anchor from NVS (Preferences)
// - invalidateTimeNow(): sets system epoch to 0 (forces timeIsValid() false)
void clearTimeAnchor();
void invalidateTimeNow();
