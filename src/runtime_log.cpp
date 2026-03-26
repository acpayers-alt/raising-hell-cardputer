#include "runtime_log.h"

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Keep this modest for embedded use.
static constexpr int LOG_RING_LINES = 64;
static constexpr int LOG_LINE_MAX = 96;

static char g_logLines[LOG_RING_LINES][LOG_LINE_MAX];
static int g_logHead = 0;   // next write index
static int g_logCount = 0;  // valid lines in ring

static void runtimeLogPush(const char *s)
{
  if (!s)
    return;

  char tmp[LOG_LINE_MAX];
  strncpy(tmp, s, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  strncpy(g_logLines[g_logHead], tmp, LOG_LINE_MAX - 1);
  g_logLines[g_logHead][LOG_LINE_MAX - 1] = '\0';

  g_logHead = (g_logHead + 1) % LOG_RING_LINES;
  if (g_logCount < LOG_RING_LINES)
    g_logCount++;
}

void runtimeLogInit()
{
  runtimeLogClear();
}

void runtimeLogClear()
{
  g_logHead = 0;
  g_logCount = 0;
  for (int i = 0; i < LOG_RING_LINES; i++)
    g_logLines[i][0] = '\0';
}

void runtimeLogLine(const char *s)
{
  runtimeLogPush(s);

  if (!s)
    return;

  int need = (int)strlen(s) + 2; // \r\n
  if (Serial && Serial.availableForWrite() >= need)
    Serial.println(s);
}

void runtimeLogf(const char *fmt, ...)
{
  char buf[160];

  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  const char *p = buf;
  while (*p)
  {
    const char *nl = strchr(p, '\n');
    if (!nl)
    {
      runtimeLogLine(p);
      break;
    }

    char line[LOG_LINE_MAX];
    size_t n = (size_t)(nl - p);
    if (n >= sizeof(line))
      n = sizeof(line) - 1;
    memcpy(line, p, n);
    line[n] = '\0';
    runtimeLogLine(line);
    p = nl + 1;
  }
}

int runtimeLogCount()
{
  return g_logCount;
}

const char *runtimeLogGetLine(int idx)
{
  static const char *kEmpty = "";

  if (idx < 0 || idx >= g_logCount)
    return kEmpty;

  const int oldest = (g_logHead - g_logCount + LOG_RING_LINES) % LOG_RING_LINES;
  const int slot = (oldest + idx) % LOG_RING_LINES;
  return g_logLines[slot];
}

// These are intentionally weakly coupled to console.cpp.
// We'll call console logging functions from there instead of here.
void runtimeLogDumpToConsole()
{
  // no-op stub; implemented via console command loop
}

void runtimeLogDumpTailToConsole(int n)
{
  (void)n;
  // no-op stub; implemented via console command loop
}