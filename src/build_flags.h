#pragma once
#include "asset_ota_types.h"

#ifndef PROJECT_PAGE_URL
#define PROJECT_PAGE_URL "https://github.com/acpayers-alt/raising-hell-cardputer"
#endif

// -----------------------------------------------------------------------------
// Build Flags
// -----------------------------------------------------------------------------
#ifndef RH_MINIGAMES_IMPL_IN_PAUSE_MENU
#define RH_MINIGAMES_IMPL_IN_PAUSE_MENU 0
#endif

// Set to 1 for public test builds.
// Set to 0 for normal development builds.
#ifndef PUBLIC_BUILD
#define PUBLIC_BUILD 0
#endif

#ifndef RH_BUILD_DEFAULT_OTA_CHANNEL
  #if PUBLIC_BUILD
    #define RH_BUILD_DEFAULT_OTA_CHANNEL ((uint8_t)AssetOtaChannel::PUBLIC)
  #else
    #define RH_BUILD_DEFAULT_OTA_CHANNEL ((uint8_t)AssetOtaChannel::DEV)
  #endif
#endif