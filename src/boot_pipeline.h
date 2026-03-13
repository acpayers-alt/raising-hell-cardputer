#pragma once
#include <stdint.h>

// -----------------------------------------------------------------------------
// Boot pipeline public API + shared flags used by loop/UI
// -----------------------------------------------------------------------------

// Asset marker state
extern bool g_assetsChecked;
extern bool g_assetsMissing;
bool sdAssetsPresent();

// Time display gating (loop also checks these)
extern bool g_timeAnchorAttempted;
extern bool g_timeAnchorRestored;

// SD retry telemetry (optional UI/debug use)
extern uint32_t g_sdFirstTryMs;
extern bool     g_sdGaveUp;
extern uint8_t  g_sdTryCount;

// Main tick — moved out of .ino
void postBootInitTick();

void bootPipelineKick(uint32_t now, bool usbOpen);

void drawAssetsMissingScreen();

extern bool g_bootAssetProvisionMustComplete;

extern bool g_bootAssetProvisionActive;
extern bool g_bootAssetProvisionMustComplete;