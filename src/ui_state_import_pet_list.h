#pragma once

#include "input.h"
#include "save_manager.h"

void uiImportPetListOnEnter(InputState& in);
void uiImportPetListHandle(InputState& in);

int uiImportPetListCount();
int uiImportPetListVisibleCount();
const PetExportEntry& uiImportPetListGet(int idx);
int uiImportPetListSelected();
bool uiImportPetListConfirming();