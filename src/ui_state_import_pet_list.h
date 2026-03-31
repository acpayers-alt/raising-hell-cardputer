#pragma once
#include "input.h"


struct PetExportEntry;

int uiImportPetListCount();
const PetExportEntry& uiImportPetListGet(int idx);
int uiImportPetListSelected();

void uiImportPetListOnEnter(InputState& in);
void uiImportPetListHandle(InputState& in);