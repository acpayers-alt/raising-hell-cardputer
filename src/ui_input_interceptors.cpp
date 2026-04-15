#include "ui_input_interceptors.h"

#include <Arduino.h> // millis()

#include "app_state.h"
#include "input.h"

#include "graphics.h"
#include "settings_flow_state.h"
#include "settings_nav_state.h"
#include "ui_defs.h"
#include "ui_runtime.h"
#include "ui_suppress.h"
#include "new_pet_flow_state.h"
#include "ui_input_common.h"
#include "flow_power_menu.h"
#include "factory_reset_state.h"
#include "flow_factory_reset.h"
#include "ui_actions.h"
#include "ui_state_settings.h"

// Keep the boot fix local to this module.
static bool s_bootNamePetFixApplied = false;

// Menu/ESC suppression is centralized in ui_suppress.*
static inline bool menuSuppressedNow() { return uiIsMenuSuppressed(); }

static inline bool canOpenSettingsFrom(UIState s)
{
  switch (s)
  {
    case UIState::PET_SCREEN:
    case UIState::INVENTORY:
    case UIState::SHOP:
    case UIState::SLEEP_MENU:
      return true;
    default:
      return false;
  }
}

// Forward declaration
static bool uiInterceptGlobalShortcuts(InputState& in);

// -----------------------------------------------------------------------------
// Global shortcut interception
// Returns true if a shortcut performed a transition / should consume input.
// -----------------------------------------------------------------------------
static bool uiInterceptGlobalShortcuts(InputState& in)
{
  // ESC key: open Settings menu from allowed states
  if (in.escOnce)
  {
    if (!menuSuppressedNow() && canOpenSettingsFrom(g_app.uiState))
    {
      g_settingsFlow.settingsReturnPage = SettingsPage::TOP;
      openSettingsWithReturn(g_app.uiState, g_app.currentTab, SettingsPage::TOP);

      // Swallow everything this tick so ESC doesn't also act as HOME/tab-jump/etc.
      in.clearEdges();
      return true;
    }
  }
  
  return false;
}

// -----------------------------------------------------------------------------
// Public entry point
// -----------------------------------------------------------------------------
bool uiHandleGlobalInterceptors(InputState &in)
{
  // If this tick is suppressed, swallow any menu-ish edges and stop.
  // (Suppression is used for modal flows where ESC/menu must not work.)
  if (menuSuppressedNow())
  {
    if (in.escOnce || in.menuOnce || in.homeOnce || in.consoleOnce)
    {
      uiActionSwallowEdges(in);
      return true;
    }
  }

  // Global shortcuts (ESC -> settings, etc.)
  if (uiInterceptGlobalShortcuts(in))
  {
    uiActionSwallowEdges(in);
    return true;
  }

  return false;
}