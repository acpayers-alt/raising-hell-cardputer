#pragma once

#include <stddef.h>

void runtimeLogInit();
void runtimeLogClear();

void runtimeLogLine(const char *s);
void runtimeLogf(const char *fmt, ...);

int runtimeLogCount();
const char *runtimeLogGetLine(int idx);   // idx: 0..count-1, oldest first
void runtimeLogDumpToConsole();
void runtimeLogDumpTailToConsole(int n);