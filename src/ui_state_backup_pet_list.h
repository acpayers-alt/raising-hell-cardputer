#pragma once

#include "input.h"
#include "settings_flow_state.h"

struct PetExportEntry;

void uiBackupPetListOnEnter(InputState& in);
void uiBackupPetListHandle(InputState& in);
void openBackupPetListFromSettings(SettingsPage returnPage, InputState &in);

int uiBackupPetListCount();
int uiBackupPetListVisibleCount();
int uiBackupPetListWindowStart();
const PetExportEntry& uiBackupPetListGetVisible(int idx);
int uiBackupPetListSelected();
bool uiBackupPetListActionMenuActive();
int uiBackupPetListActionIndex();
bool uiBackupPetListConfirmDeleteActive();
int uiBackupPetListConfirmDeleteIndex();
bool uiBackupPetListConfirmRestoreActive();
int uiBackupPetListConfirmRestoreIndex();