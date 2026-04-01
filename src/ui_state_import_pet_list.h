#pragma once

#include "input.h"
#include "save_manager.h"

void uiImportPetListOnEnter(InputState &in);
void uiImportPetListHandle(InputState &in);

int uiImportPetListCount();
int uiImportPetListVisibleCount();
int uiImportPetListWindowStart();
const PetExportEntry &uiImportPetListGetVisible(int idx);
int uiImportPetListSelected();
bool uiImportPetListConfirming();
int uiImportPetListConfirmIndex();

bool uiImportPetListActionMenuActive();
int uiImportPetListActionIndex();

bool uiImportPetListConfirmDeleteActive();
int uiImportPetListConfirmDeleteIndex();