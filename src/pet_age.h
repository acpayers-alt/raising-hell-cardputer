#pragma once
#include <stdint.h>
#include <stddef.h>  
#include <stdint.h>   

struct AgeParts {
  int years;
  int months;
  int days;
  int hours;
  int minutes;
};


AgeParts calcAgeParts(int64_t birthEpoch, int64_t nowEpoch);
AgeParts calcAgePartsFromSeconds(int64_t totalSeconds);

void formatAgeString(char* out, size_t outSize, const AgeParts& a, bool includeDays);

void getPetAgeString(char* out, size_t outSize, uint32_t birthEpoch);
