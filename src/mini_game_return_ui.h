#pragma once

#include "ui_defs.h"

void miniGameSetReturnUi(UIState state, Tab tab);
void miniGameClearReturnUi();

UIState miniGameGetReturnUiOrDefault(UIState fallbackState);
Tab miniGameGetReturnTabOrDefault(Tab fallbackTab);