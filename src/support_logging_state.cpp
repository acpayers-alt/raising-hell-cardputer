#include "support_logging_state.h"

#include <Arduino.h>
#include <Preferences.h>

#include "build_flags.h"

static bool s_supportLoggingEnabled = false;

static constexpr const char *kNs = "rh_support";
static constexpr const char *kKey = "log";

void supportLoggingBegin()
{
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  Preferences p;
  if (p.begin(kNs, true))
  {
    s_supportLoggingEnabled = p.getBool(kKey, false);
    p.end();
  }
  else
  {
    s_supportLoggingEnabled = false;
  }
#else
  // Dev builds stay chatty by default.
  s_supportLoggingEnabled = true;
#endif
}

bool supportLoggingEnabled()
{
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  return s_supportLoggingEnabled;
#else
  return true;
#endif
}

void setSupportLoggingEnabled(bool enabled)
{
#if defined(PUBLIC_BUILD) && PUBLIC_BUILD
  s_supportLoggingEnabled = enabled;

  Preferences p;
  if (p.begin(kNs, false))
  {
    p.putBool(kKey, enabled);
    p.end();
  }
#else
  (void)enabled;
  s_supportLoggingEnabled = true;
#endif
}