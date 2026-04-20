#include "boot_state.h"

bool g_bootSplashActive = true;
uint32_t g_bootCount = 0;

void BootState::enter()
{
  g_bootSplashActive = true;
  g_bootCount = 0;
}

void BootState::exit()
{
  g_bootSplashActive = false;
}

void BootState::update()
{
  // Legacy BootState is no longer part of the active runtime state flow.
  // Keep this stub only because boot-splash globals are still referenced elsewhere.
}