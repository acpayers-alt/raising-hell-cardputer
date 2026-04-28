#pragma once

struct InputState;

namespace UiSettingsPages
{
void Handle_TOP(InputState &input, int move);
void Handle_SCREEN(InputState &input, int move);
void Handle_SYSTEM(InputState &input, int move);
void Handle_WIFI(InputState &input, int move);
bool HandleGameNewPetConfirm(InputState &input);
void Handle_AUTO_SCREEN(InputState &input, int move);
void Handle_DECAY_MODE(InputState &input, int move);
void Handle_STATUS(InputState &input, int move);
bool GameNewPetConfirmActive();
int GameNewPetConfirmIndex();
void ShowGameNewPetConfirm();
void HideGameNewPetConfirm();
void SetGameNewPetConfirmIndex(int idx);
} // namespace UiSettingsPages