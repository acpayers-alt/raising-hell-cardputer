#include "pet_age.h"

#include <time.h>
#include <stdint.h>
#include <stdio.h>   // snprintf
#include <string.h>  // strlen, memcpy

AgeParts calcAgeParts(int64_t birthEpoch, int64_t nowEpoch) {
  AgeParts a{0, 0, 0, 0, 0};
  if (birthEpoch <= 0 || nowEpoch <= birthEpoch) return a;

  int64_t secs = nowEpoch - birthEpoch;

  a.minutes = (int)(secs / 60);
  secs %= 60;

  a.hours = (int)(a.minutes / 60);
  a.minutes %= 60;

  a.days = (int)(a.hours / 24);
  a.hours %= 24;

  a.months = (int)(a.days / 30);
  a.days %= 30;

  a.years = (int)(a.months / 12);
  a.months %= 12;

  return a;
}

void formatAgeString(char* out, size_t outSize, const AgeParts& a, bool includeDays) {
  if (!out || outSize == 0) return;
  out[0] = '\0';

  auto appendUnit = [&](int v, const char* singular, const char* plural) {
    if (v <= 0) return;
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%d %s", v, (v == 1) ? singular : plural);
    if (out[0] != '\0') strncat(out, " ", outSize - strlen(out) - 1);
    strncat(out, tmp, outSize - strlen(out) - 1);
  };

  // Priority order (largest → smallest)
  if (a.years > 0) {
    appendUnit(a.years, "year", "years");
    appendUnit(a.months, "mo", "mo");
  } else if (a.months > 0) {
    appendUnit(a.months, "mo", "mo");
    appendUnit(a.days, "day", "days");
  } else if (a.days > 0) {
    appendUnit(a.days, "day", "days");
    appendUnit(a.hours, "hr", "hr");
  } else if (a.hours > 0) {
    appendUnit(a.hours, "hr", "hr");
    appendUnit(a.minutes, "min", "min");
  } else {
    appendUnit(a.minutes, "min", "min");
  }

  if (out[0] == '\0') {
    snprintf(out, outSize, "0 minutes");
  }
}

void getPetAgeString(char* out, size_t outSize, uint32_t birthEpoch) {
  if (!out || outSize == 0) return;
  out[0] = '\0';

  int64_t now = (int64_t)time(nullptr);
  AgeParts a = calcAgeParts((int64_t)birthEpoch, now);
  formatAgeString(out, outSize, a, false);
}
