#pragma once

#include "input.h"
#include "save_manager.h"

// ─────────────────────────────────────
// State lifecycle
// ─────────────────────────────────────
void uiImportPetListOnEnter(InputState &in);
void uiImportPetListHandle(InputState &in);

// ─────────────────────────────────────
// List data / view state
// ─────────────────────────────────────
int uiImportPetListCount();
int uiImportPetListVisibleCount();
int uiImportPetListWindowStart();
int uiImportPetListSelected();

const PetExportEntry &uiImportPetListGetVisible(int idx);

// ─────────────────────────────────────
// Action menu state
// ─────────────────────────────────────
bool uiImportPetListActionMenuActive();
int  uiImportPetListActionIndex();

// ─────────────────────────────────────
// Delete confirmation state
// ─────────────────────────────────────
bool uiImportPetListConfirmDeleteActive();
int  uiImportPetListConfirmDeleteIndex();