#pragma once

#include <stdint.h>
#include "ui_defs.h"

struct InputState;

namespace UiSettingsMenu
{

bool Handle(InputState &input, int move);
int WifiItemCount();
const char *WifiItemLabel(int index);

} // namespace UiSettingsMenu