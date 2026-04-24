#pragma once
#include <stdint.h>

extern int tzIndex;

uint8_t tzCount();
uint8_t tzDefaultIndex();

bool tzIndexIsValid(int idx);

const char *tzName(uint8_t idx);      // short UI label
const char *tzIanaName(uint8_t idx);  // canonical IANA zone
const char *tzPosixRule(uint8_t idx); // POSIX TZ rule

int tzFindIndexByIana(const char *ianaName);

void applyTimezoneIndex(uint8_t idx);

// Persist tzIndex to NVS
bool loadTzIndexFromNVS(uint8_t *outIdx); // returns true if present+valid
void saveTzIndexToNVS(uint8_t idx);