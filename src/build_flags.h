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

// -----------------------------------------------------------------------------
// Manifest endpoints
//
// Legacy public firmware must keep using the old manifest-public.json endpoint.
// Newer firmware should use v2 manifest endpoints.
// These can be overridden from platformio.ini if needed.
// -----------------------------------------------------------------------------
#ifndef RH_PRIMARY_ASSET_BASE_URL
#define RH_PRIMARY_ASSET_BASE_URL "https://assets.raisinghellgame.com/assets/"
#endif

#ifndef RH_FALLBACK_ASSET_BASE_URL
#define RH_FALLBACK_ASSET_BASE_URL "https://backup-assets.raisinghellgame.com/assets/"
#endif

#ifndef RH_PUBLIC_MANIFEST_URL
#define RH_PUBLIC_MANIFEST_URL "https://assets.raisinghellgame.com/manifest-public-v2.json"
#endif

#ifndef RH_PUBLIC_MANIFEST_FALLBACK_URL
#define RH_PUBLIC_MANIFEST_FALLBACK_URL "https://backup-assets.raisinghellgame.com/manifest-public-v2.json"
#endif

#ifndef RH_DEV_MANIFEST_URL
#define RH_DEV_MANIFEST_URL "https://assets.raisinghellgame.com/manifest-dev-v2.json"
#endif

#ifndef RH_DEV_MANIFEST_FALLBACK_URL
#define RH_DEV_MANIFEST_FALLBACK_URL "https://backup-assets.raisinghellgame.com/manifest-dev-v2.json"
#endif