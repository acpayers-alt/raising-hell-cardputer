#include "time_persist.h"
#include <Preferences.h>
#include <sys/time.h>
#include <time.h>

static Preferences prefs;

static uint32_t lastSaveMs = 0;
static constexpr uint32_t SAVE_INTERVAL_MS = 5UL * 60UL * 1000UL; // every 5 minutes

// ------------- module state -------------
static bool g_timeRestored = false;
static bool g_timeDirty = false;

// Jan 1, 2021 (UTC). Anything below this is treated as invalid.
static constexpr uint64_t MIN_VALID_EPOCH = 1609459200ULL;

// Optional upper bound sanity check (Jan 1, 2100)
static constexpr uint64_t MAX_VALID_EPOCH = 4102444800ULL;

bool timeIsValid()
{
  time_t now = time(nullptr);
  return (uint64_t)now >= MIN_VALID_EPOCH && (uint64_t)now <= MAX_VALID_EPOCH;
}

static void applyEpoch(time_t epoch)
{
  timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

void invalidateTimeNow()
{
  applyEpoch((time_t)0);
  g_timeRestored = false;
  g_timeDirty = false;
  lastSaveMs = millis();
}

void clearTimeAnchor()
{
  // Wipe the entire namespace so no stale keys survive
  prefs.begin("rh_time", false);
  prefs.clear();
  prefs.end();
  g_timeRestored = false;
  g_timeDirty = false;
}

// NEW: lets UI/boot code know if we’ve restored time this session
bool timePersistIsRestored() { return g_timeRestored; }

bool timeIsDirty() { return timeIsValid() && g_timeDirty; }

void timeMarkClean()
{
  g_timeDirty = false;
  g_timeRestored = false;
}

// NEW: check if an anchor exists (without modifying time)
bool timePersistHasAnchor()
{
  prefs.begin("rh_time", true);
  uint64_t savedEpoch = prefs.getULong64("epoch", 0);
  prefs.end();
  return (savedEpoch >= MIN_VALID_EPOCH && savedEpoch <= MAX_VALID_EPOCH);
}

bool loadTimeAnchor()
{
  prefs.begin("rh_time", true);
  uint64_t savedEpoch = prefs.getULong64("epoch", 0);
  prefs.end();

  // Reject missing/implausible epochs
  if (savedEpoch < MIN_VALID_EPOCH || savedEpoch > MAX_VALID_EPOCH)
  {
    g_timeRestored = false;
    g_timeDirty = false;
    return false;
  }
  
applyEpoch((time_t)savedEpoch);

g_timeRestored = true;
g_timeDirty = true;
lastSaveMs = millis();   // prevent immediate resave
return true;
}

// Backward-compatible alias (matches your .ino call)
bool restoreTimeFromAnchor() { return loadTimeAnchor(); }

void saveTimeAnchor()
{
  if (!timeIsValid())
  {
    return;
  }

  time_t now = time(nullptr);

  time_t nowEpoch = time(nullptr);
  uint32_t nowMs = millis();

  prefs.begin("rh_time", false);
  prefs.putULong64("epoch", (uint64_t)nowEpoch);
  prefs.putUInt("ms", nowMs);
  prefs.end();

  lastSaveMs = nowMs;
}

void maybePeriodicTimeSave()
{
  uint32_t nowMs = millis();
  if (nowMs - lastSaveMs >= SAVE_INTERVAL_MS)
  {
    saveTimeAnchor();
  }
}
