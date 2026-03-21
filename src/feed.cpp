#include <Arduino.h>

#include "feed.h"
#include "input.h"
#include "pet.h"
#include "sound.h"
#include "ui_defs.h"
#include "app_state.h"
#include "ui_invalidate.h"
#include "ui_actions.h"

static inline int clampi_local(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

// 0 = snack, 1 = until full
void feedPet(int mode) {
  // Safety clamp
  mode = (mode <= 0) ? 0 : 1;

  // Your UI uses 20 hunger per soul food in graphics.cpp for "needed" calc.
  const int kHungerGain = 20;

  if (mode == 0) {
    // Just a Snack
    pet.hunger += kHungerGain;
    if (pet.hunger > 100) pet.hunger = 100;
    soundConfirm();
  } else {
    // Until Full
    while (pet.hunger < 100) {
      pet.hunger += kHungerGain;
      if (pet.hunger > 100) pet.hunger = 100;
      if (pet.hunger >= 100) break;
    }
    soundConfirm();
  }

  // Return to pet screen
  uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
  requestUIRedraw();
}

void handleFeedInput(const InputState& in) {
  // Basic navigation (edge-based)
  int move = 0;
  if (in.upOnce)   move = -1;
  if (in.downOnce) move = +1;

  if (move != 0) {
    // Feed menu has exactly 2 items: 0 snack, 1 until full
    static const int kFeedOptionCount = 2;

    g_app.feedMenuIndex =
        (g_app.feedMenuIndex < 0) ? (kFeedOptionCount - 1)
      : (g_app.feedMenuIndex >= kFeedOptionCount) ? 0
      : g_app.feedMenuIndex;

    soundMenuTick();
    requestUIRedraw();
    return;
  }

  // Cancel / back
  if (in.menuOnce) {
    uiActionEnterState(UIState::PET_SCREEN, Tab::TAB_PET, true);
    soundCancel();
    requestUIRedraw();
    return;
  }
  
  // Select (ENTER)
  if (in.selectOnce) {
    feedPet(g_app.feedMenuIndex);
    return;
  }
}
