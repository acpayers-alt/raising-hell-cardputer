#include "timezone.h"
#include <time.h>
#include <stdlib.h>

static const char* kTzNames[] = {
  "UTC",

  // United States
  "US/Eastern",
  "US/Central",
  "US/Mountain",
  "US/Pacific",
  "US/Alaska",
  "US/Hawaii",

  // Europe
  "Europe/UK",
  "Europe/Central",
  "Europe/Eastern",

  // Asia
  "Asia/Japan",
  "Asia/Korea",
  "Asia/China",
  "Asia/India",
  "Asia/Singapore",

  // Oceania
  "Australia/Eastern",
  "Australia/Central",
  "New Zealand",

  // Americas
  "Canada/Atlantic",
  "Brazil",
  "Argentina",

  // Africa
  "South Africa"
};

static const char* kTzPosix[] = {
  "UTC0",

  // United States
  "EST5EDT,M3.2.0/2,M11.1.0/2",
  "CST6CDT,M3.2.0/2,M11.1.0/2",
  "MST7MDT,M3.2.0/2,M11.1.0/2",
  "PST8PDT,M3.2.0/2,M11.1.0/2",
  "AKST9AKDT,M3.2.0/2,M11.1.0/2",
  "HST10",

  // Europe
  "GMT0BST,M3.5.0/1,M10.5.0/2",      // UK
  "CET-1CEST,M3.5.0/2,M10.5.0/3",    // Central Europe
  "EET-2EEST,M3.5.0/3,M10.5.0/4",    // Eastern Europe

  // Asia
  "JST-9",
  "KST-9",
  "CST-8",
  "IST-5:30",
  "SGT-8",

  // Oceania
  "AEST-10AEDT,M10.1.0/2,M4.1.0/3",
  "ACST-9:30ACDT,M10.1.0/2,M4.1.0/3",
  "NZST-12NZDT,M9.5.0/2,M4.1.0/3",

  // Americas
  "AST4ADT,M3.2.0/2,M11.1.0/2",
  "BRT3BRST,M11.1.0/0,M2.3.0/0",
  "ART3",

  // Africa
  "SAST-2"
};

static const uint8_t kTzCount =
  sizeof(kTzNames) / sizeof(kTzNames[0]);

uint8_t tzCount() {
  return kTzCount;
}

const char* tzName(uint8_t idx) {
  if (idx >= kTzCount) idx = 0;
  return kTzNames[idx];
}

void applyTimezoneIndex(uint8_t idx) {
  if (idx >= kTzCount) idx = 0;
  setenv("TZ", kTzPosix[idx], 1);
  tzset();
}

#include <Preferences.h>

static constexpr const char* NVS_NS  = "rh_sys";
static constexpr const char* NVS_KEY = "tzIndex";

bool loadTzIndexFromNVS(uint8_t* outIdx) {
  if (!outIdx) return false;

  Preferences p;
  if (!p.begin(NVS_NS, true)) return false;

  bool has = p.isKey(NVS_KEY);
  uint8_t v = p.getUChar(NVS_KEY, 0);
  p.end();

  if (!has) return false;
  if (v >= tzCount()) return false;

  *outIdx = v;
  return true;
}

void saveTzIndexToNVS(uint8_t idx) {
  if (idx >= tzCount()) idx = 0;

  Preferences p;
  if (!p.begin(NVS_NS, false)) return;
  p.putUChar(NVS_KEY, idx);
  p.end();
}
