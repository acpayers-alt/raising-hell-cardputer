#include "ui_settings_pages.h"

#include "app_state.h"
#include "asset_ota.h"
#include "ui_runtime.h"
#include "sound.h"
#include "input.h"

namespace UiSettingsPages {

void Handle_STATUS(InputState& input, int move)
{
  (void)input;

  if (move == 0)
    return;

  // Keep this in sync with drawSystemStatusMenu().
  const int totalLines = 26;   // 13 key/value pairs
  const int pairCount = totalLines / 2;
  const int visibleLines = 6;
  const int maxIndex = (pairCount > visibleLines) ? (pairCount - visibleLines) : 0;

  g_app.statusScreenIndex += move;

  if (g_app.statusScreenIndex < 0)
    g_app.statusScreenIndex = 0;
  if (g_app.statusScreenIndex > maxIndex)
    g_app.statusScreenIndex = maxIndex;

  requestUIRedraw();
  playBeep();
}

} // namespace UiSettingsPages