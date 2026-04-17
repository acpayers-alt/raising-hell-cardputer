#include "ui_settings_pages.h"

#include "app_state.h"
#include "asset_ota.h"
#include "input.h"
#include "sound.h"
#include "ui_runtime.h"
#include "system_status_state.h"

namespace UiSettingsPages
{

void Handle_STATUS(InputState &input, int move)
{
  (void)input;

  if (move == 0)
    return;

  // Keep this in sync with drawSystemStatusMenu().
  const int totalLines = 34; // 17 key/value pairs
  const int pairCount = totalLines / 2;
  const int visibleLines = 6;
  const int maxIndex = (pairCount > visibleLines) ? (pairCount - visibleLines) : 0;

  g_systemStatus.scrollOffset += move;
  if (g_systemStatus.scrollOffset < 0)
    g_systemStatus.scrollOffset = 0;
  if (g_systemStatus.scrollOffset > maxIndex)
    g_systemStatus.scrollOffset = maxIndex;

  requestUIRedraw();
  playBeep();
}

} // namespace UiSettingsPages