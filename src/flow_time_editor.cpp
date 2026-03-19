#include "flow_time_editor.h"

#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include "app_state.h"
#include "input.h"
#include "time_editor_state.h"
#include "time_persist.h"
#include "ui_actions.h"
#include "ui_invalidate.h"
#include "wifi_setup_state.h"

// -----------------------------------------------------------------------------
// Set-Time flow
//
// This module owns the SET_TIME UI behavior.
//
// Expected UX (matches graphics.cpp drawSetDateTimePanel()):
//   - g_setTimeField selects: 0=year,1=month,2=day,3=hour,4=min,5=OK pill
//   - Left/Right: move selection (wrap)
//   - Up/Down: adjust selected field (only when field 0-4)
//   - Enter/Select:
//        - If on OK: commit and return
//        - Otherwise: jump selection to OK (no commit)
//   - ESC:
//        - If cancel allowed: return without commit
//        - If cancel blocked (boot gate): ignored
//   - Menu/Backspace:
//        - If not on OK: jump to OK
//        - If on OK: behaves like cancel (if allowed)
// -----------------------------------------------------------------------------

static constexpr uint8_t kFieldYear   = 0;
static constexpr uint8_t kFieldMonth  = 1;
static constexpr uint8_t kFieldDay    = 2;
static constexpr uint8_t kFieldHour   = 3;
static constexpr uint8_t kFieldMinute = 4;
static constexpr uint8_t kFieldOk     = 5;

static inline bool fieldIsAdjustable(uint8_t f) { return (f <= kFieldMinute); }

static void normalizeAndClampTm(struct tm& t)
{
  // Normalize with mktime() (handles overflow like month 13, day 0, etc.)
  time_t epoch = mktime(&t);
  if (epoch < 0)
  {
    // Fallback to a sane default if normalization fails.
    memset(&t, 0, sizeof(t));
    t.tm_year = 2026 - 1900;
    t.tm_mon  = 0;
    t.tm_mday = 1;
    t.tm_hour = 12;
    t.tm_min  = 0;
    t.tm_sec  = 0;
    (void)mktime(&t);
    return;
  }

  // Clamp year range (helps avoid weird UI states if the user holds keys).
  const int year = t.tm_year + 1900;
  if (year < 2000)
  {
    t.tm_year = 2000 - 1900;
    (void)mktime(&t);
  }
  else if (year > 2099)
  {
    t.tm_year = 2099 - 1900;
    (void)mktime(&t);
  }
}

static void initEditorFromNow()
{
  // Seed editor from current system time.
  time_t now = time(nullptr);
  if (now <= 0)
  {
    memset(&g_setTimeTm, 0, sizeof(g_setTimeTm));
    g_setTimeTm.tm_year = 2026 - 1900;
    g_setTimeTm.tm_mon  = 0;
    g_setTimeTm.tm_mday = 1;
    g_setTimeTm.tm_hour = 12;
    g_setTimeTm.tm_min  = 0;
    g_setTimeTm.tm_sec  = 0;
    normalizeAndClampTm(g_setTimeTm);
    return;
  }

  struct tm* lt = localtime(&now);
  if (!lt)
  {
    memset(&g_setTimeTm, 0, sizeof(g_setTimeTm));
    g_setTimeTm.tm_year = 2026 - 1900;
    g_setTimeTm.tm_mon  = 0;
    g_setTimeTm.tm_mday = 1;
    g_setTimeTm.tm_hour = 12;
    g_setTimeTm.tm_min  = 0;
    g_setTimeTm.tm_sec  = 0;
    normalizeAndClampTm(g_setTimeTm);
    return;
  }

  g_setTimeTm = *lt;
  g_setTimeTm.tm_isdst = -1; // let mktime decide
  normalizeAndClampTm(g_setTimeTm);
}

static void adjustEditorField(uint8_t field, int delta)
{
  switch (field)
  {
    case kFieldYear:   g_setTimeTm.tm_year += delta; break;
    case kFieldMonth:  g_setTimeTm.tm_mon  += delta; break;
    case kFieldDay:    g_setTimeTm.tm_mday += delta; break;
    case kFieldHour:   g_setTimeTm.tm_hour += delta; break;
    case kFieldMinute: g_setTimeTm.tm_min  += delta; break;
    default: return;
  }

  normalizeAndClampTm(g_setTimeTm);
}

static void returnFromSetTime()
{
  g_setTimeActive = false;

  // Return to stored state/tab if valid; otherwise, fall back to PET_SCREEN.
  if (g_setTimeReturnValid)
    uiActionEnterState((UIState)g_setTimeReturnState, (Tab)g_setTimeReturnTab, true);
  else
    uiActionEnterState(UIState::PET_SCREEN, g_app.currentTab, true);
}

static void commitSetTime()
{
  // Commit edited tm -> system time.
  struct tm tmp = g_setTimeTm;
  tmp.tm_isdst = -1;
  time_t epoch = mktime(&tmp);
  if (epoch > 0)
  {
    timeval tv;
    tv.tv_sec  = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
  }

  // Persist anchor used by your time system.
  saveTimeAnchor();
}

// -----------------------------------------------------------------------------
// Public entry points
// -----------------------------------------------------------------------------

void beginForcedSetTimeBootGate(UIState returnState, Tab returnTab)
{
  g_setTimeReturnValid = true;
  g_setTimeReturnState = (uint8_t)returnState;
  g_setTimeReturnTab   = (uint8_t)returnTab;
  g_setTimeForceNoCancel = true;
  g_setTimeActive = true;
  g_setTimeField = kFieldYear;
  initEditorFromNow();

  uiActionEnterState(UIState::SET_TIME, g_app.currentTab, true);
  requestUIRedraw();
  inputForceClear();
}

void beginSetTimeEditorFromSettings(SettingsPage /*page*/, UIState returnState, Tab returnTab)
{
  g_setTimeReturnValid = true;
  g_setTimeReturnState = (uint8_t)returnState;
  g_setTimeReturnTab   = (uint8_t)returnTab;
  g_setTimeForceNoCancel = false;
  g_setTimeActive = true;
  g_setTimeField = kFieldYear;
  initEditorFromNow();

  uiActionEnterState(UIState::SET_TIME, g_app.currentTab, true);
  requestUIRedraw();
  inputForceClear();
}

// -----------------------------------------------------------------------------
// UI handler
// -----------------------------------------------------------------------------

void uiSetTimeHandle(InputState& in)
{
  if (!g_setTimeActive)
    return;

  bool anyUiChange = false;

  // Left/Right change selection (wrap)
  if (in.leftOnce)
  {
    g_setTimeField = (g_setTimeField == 0) ? kFieldOk : (uint8_t)(g_setTimeField - 1);
    anyUiChange = true;
  }
  if (in.rightOnce)
  {
    g_setTimeField = (g_setTimeField >= kFieldOk) ? 0 : (uint8_t)(g_setTimeField + 1);
    anyUiChange = true;
  }

  // Up/Down adjust current field
  if (in.upOnce && fieldIsAdjustable(g_setTimeField))
  {
    adjustEditorField(g_setTimeField, +1);
    anyUiChange = true;
  }
  if (in.downOnce && fieldIsAdjustable(g_setTimeField))
  {
    adjustEditorField(g_setTimeField, -1);
    anyUiChange = true;
  }

  // Enter/Select behavior: commit only when OK selected.
  if (in.selectOnce)
  {
    if (g_setTimeField == kFieldOk)
    {
      commitSetTime();
      returnFromSetTime();
      return;
    }
    else
    {
      // Enter shouldn't do anything except move focus to OK.
      g_setTimeField = kFieldOk;
      anyUiChange = true;
    }
  }

  // Menu/backspace acts as "go to OK" first; cancel only when already on OK.
  if (in.menuOnce)
  {
    if (g_setTimeField != kFieldOk)
    {
      g_setTimeField = kFieldOk;
      anyUiChange = true;
    }
    else if (!g_setTimeForceNoCancel)
    {
      returnFromSetTime();
      return;
    }
  }

  // ESC cancels normally; during forced boot time entry, return to Wi-Fi setup.
  if (in.escOnce)
  {
    if (!g_setTimeForceNoCancel)
    {
      returnFromSetTime();
      return;
    }

    g_setTimeActive = false;
    g_wifiSetupFromBootWizard = true;
    g_wifi.setupStage = WIFI_SETUP_STAGE_SCAN;
    g_wifi.scanIndex = 0;
    g_wifi.buf[0] = '\0';

    uiActionEnterState(UIState::WIFI_SETUP, g_app.currentTab, true);
    requestUIRedraw();
    inputForceClear();
    in.clearEdges();
    return;
  }
  
  if (anyUiChange)
    requestUIRedraw();

  // Do not allow other systems to interpret these edges.
  in.clearEdges();
}