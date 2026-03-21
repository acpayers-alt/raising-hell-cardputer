#pragma once

#include "ui_defs.h"

bool appLifecycleHasPendingOnboarding();
bool appLifecycleCanEnterNormalUi();
UIState appLifecycleResolveBootAfterOkState(bool saveFileExists);
bool appLifecycleLoadedSaveRequiresChoosePet(bool namePending, bool blankPetName);