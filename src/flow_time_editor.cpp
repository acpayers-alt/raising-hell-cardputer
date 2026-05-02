#include "flow_time_editor.h"

#include "timezone.h"
#include <Arduino.h>
#include <sys/time.h>
#include <time.h>

#include "app_state.h"
#include "boot_pipeline.h"
#include "input.h"
#include "time_editor_state.h"
#include "time_persist.h"
#include "ui_actions.h"
#include "ui_invalidate.h"
#include "wifi_setup_state.h"

// -----------------------------------------------------------------------------
// Set-Time flow
// -----------------------------------------------------------------------------

static constexpr uint8_t kFieldYear = 0;
static constexpr uint8_t kFieldMonth = 1;
static constexpr uint8_t kFieldDay = 2;
static constexpr uint8_t kFieldHour = 3;
static constexpr uint8_t kFieldMinute = 4;
static constexpr uint8_t kFieldTimezone = 5;
static constexpr uint8_t kFieldOk = 6;

static inline bool fieldIsAdjustable(uint8_t f) { return (f <= kFieldMinute); }

static void normalizeAndClampTm(struct tm &t)
{
  time_t epoch = mktime(&t);
  if (epoch < 0)
  {
    memset(&t, 0, sizeof(t));
    t.tm_year = 2026 - 1900;
    t.tm_mon = 0;
    t.tm_mday = 1;
    t.tm_hour = 12;
    t.tm_min = 0;
    t.tm_sec = 0;
    (void)mktime(&t);
    return;
  }

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
  // If we're in forced boot setup, ALWAYS start at 2026 baseline
  if (g_setTimeForceNoCancel)
  {
    memset(&g_setTimeTm, 0, sizeof(g_setTimeTm));
    g_setTimeTm.tm_year = 2026 - 1900;
    g_setTimeTm.tm_mon = 0;
    g_setTimeTm.tm_mday = 1;
    g_setTimeTm.tm_hour = 12;
    g_setTimeTm.tm_min = 0;
    g_setTimeTm.tm_sec = 0;
    g_setTimeTm.tm_isdst = -1;
    normalizeAndClampTm(g_setTimeTm);
    return;
  }

  // Otherwise (settings flow), use current time
  time_t now = time(nullptr);
  if (now <= 0)
  {
    memset(&g_setTimeTm, 0, sizeof(g_setTimeTm));
    g_setTimeTm.tm_year = 2026 - 1900;
    g_setTimeTm.tm_mon = 0;
    g_setTimeTm.tm_mday = 1;
    g_setTimeTm.tm_hour = 12;
    g_setTimeTm.tm_min = 0;
    g_setTimeTm.tm_sec = 0;
    g_setTimeTm.tm_isdst = -1;
    normalizeAndClampTm(g_setTimeTm);
    return;
  }

  struct tm *lt = localtime(&now);
  if (!lt)
    return;

  g_setTimeTm = *lt;
  g_setTimeTm.tm_isdst = -1;
  normalizeAndClampTm(g_setTimeTm);
}

static void adjustEditorField(uint8_t field, int delta)
{
  switch (field)
  {
  case kFieldYear:
    g_setTimeTm.tm_year += delta;
    break;
  case kFieldMonth:
    g_setTimeTm.tm_mon += delta;
    break;
  case kFieldDay:
    g_setTimeTm.tm_mday += delta;
    break;
  case kFieldHour:
    g_setTimeTm.tm_hour += delta;
    break;
  case kFieldMinute:
    g_setTimeTm.tm_min += delta;
    break;
  default:
    return;
  }

  normalizeAndClampTm(g_setTimeTm);
}

static void adjustTimezoneField(int delta)
{
  const int count = (int)tzCount();
  if (count <= 0)
    return;

  int next = (int)tzIndex + delta;

  if (next < 0)
    next = count - 1;
  else if (next >= count)
    next = 0;

  tzIndex = next;
  applyTimezoneIndex((uint8_t)tzIndex);
  saveTzIndexToNVS((uint8_t)tzIndex);
}

static void returnFromSetTime()
{
  g_setTimeActive = false;

  const UIReturnTarget ret = uiGetReturnTarget();
  uiPopReturnTarget();

  uiActionEnterState(ret.state, ret.tab, true);
}

static void commitSetTime()
{
  applyTimezoneIndex(tzIndex);
  saveTzIndexToNVS(tzIndex);

  struct tm tmp = g_setTimeTm;
  tmp.tm_isdst = -1;

  time_t epoch = mktime(&tmp);
  if (epoch > 0)
  {
    timeval tv;
    tv.tv_sec = epoch;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);
  }

  timeMarkClean();
  saveTimeAnchor();
}

static void finishForcedBootTime()
{
  Serial.println("[BOOT] manual time complete, finishing boot gate");

  g_setTimeActive = false;
  g_setTimeForceNoCancel = false;

  const UIReturnTarget ret = uiGetReturnTarget();
  uiPopReturnTarget();

  bootSetupClearPendingFlag();
  Serial.printf("[BOOT] manual time complete -> entering return state=%d tab=%d\n", (int)ret.state, (int)ret.tab);

  uiActionEnterState(ret.state, ret.tab, true);

  requestUIRedraw();
  inputForceClear();
}

// -----------------------------------------------------------------------------
// Public entry points
// -----------------------------------------------------------------------------

void beginForcedSetTimeBootGate(UIState returnState, Tab returnTab)
{
  uiPushReturnTarget(returnState, returnTab);

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
  uiPushReturnTarget(returnState, returnTab);

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

void uiSetTimeHandle(InputState &in)
{
  if (!g_setTimeActive)
    return;

  bool anyUiChange = false;

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

  if (in.upOnce && g_setTimeField == kFieldTimezone)
  {
    adjustTimezoneField(+1);
    anyUiChange = true;
  }

  if (in.downOnce && g_setTimeField == kFieldTimezone)
  {
    adjustTimezoneField(-1);
    anyUiChange = true;
  }

  if (in.selectOnce)
  {
    if (g_setTimeField == kFieldOk)
    {
      commitSetTime();

      if (g_setTimeForceNoCancel)
      {
        finishForcedBootTime();
        in.clearEdges();
        return;
      }

      returnFromSetTime();
      in.clearEdges();
      return;
    }

    g_setTimeField = kFieldOk;
    anyUiChange = true;
  }

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
      in.clearEdges();
      return;
    }
  }

  if (in.escOnce)
  {
    if (!g_setTimeForceNoCancel)
    {
      returnFromSetTime();
      in.clearEdges();
      return;
    }

    g_setTimeActive = false;
    g_setTimeForceNoCancel = false;

    g_wifiSetupFromBootWizard = true;
    g_wifi.setupStage = WIFI_SETUP_STAGE_SCAN;
    g_wifi.scanIndex = 0;
    g_wifi.buf[0] = '\0';

    g_wifi.returnState = UIState::BOOT_WIFI_PROMPT;
    g_wifi.returnTab = Tab::TAB_PET;
    g_wifi.aborted = false;

    uiActionEnterState(UIState::WIFI_SETUP, g_app.currentTab, true);
    requestUIRedraw();
    inputForceClear();
    in.clearEdges();
    return;
  }

  if (anyUiChange)
    requestUIRedraw();

  in.clearEdges();
}