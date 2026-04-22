#pragma once

#include <stdint.h>

enum class HelpLineType : uint8_t
{
  TITLE,
  SECTION,
  BODY,
  GAP
};

struct HelpLine
{
  HelpLineType type;
  const char *text;
};

extern const HelpLine kControlsManual[];
extern const int kControlsManualCount;