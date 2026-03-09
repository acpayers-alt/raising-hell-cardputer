#pragma once

#include "display.h" // M5Canvas
#include "mini_games.h"
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Mini-game asset manager (first pass)
// -----------------------------------------------------------------------------
// First pass scope:
// - one shared fullscreen background sprite slot
// - heap logging helpers
// - explicit owner tracking for the shared bg slot
//
// Later passes can add:
// - keyed sprite registry
// - per-game asset packs
// - budget enforcement
// - shared transient slots
// -----------------------------------------------------------------------------

void mgAssetsLogHeap(const char *tag);

// Session helpers (lightweight for now; mostly logging / lifecycle markers)
void mgAssetsBeginSession(MiniGame game, const char *tag);
void mgAssetsEndSession(MiniGame game, const char *tag);

// Shared fullscreen background slot
bool mgAssetsEnsureSharedBg(MiniGame owner, const char *path);
void mgAssetsReleaseSharedBgIfOwner(MiniGame owner);
void mgAssetsReleaseSharedBg();

bool mgAssetsHasSharedBg();
MiniGame mgAssetsSharedBgOwner();
const char *mgAssetsSharedBgPath();

M5Canvas *mgAssetsSharedBg();
int mgAssetsSharedBgW();
int mgAssetsSharedBgH();

bool mgAssetsLoadSprite(M5Canvas &dst, const char *path, int colorDepth = 8, uint16_t fillColor = TFT_BLACK,
                        const char *tag = nullptr);

void mgAssetsReleaseSprite(M5Canvas &dst, const char *tag = nullptr);

bool mgAssetsReadPngDims(const char *path, int *outW, int *outH, const char **outUsePath = nullptr);

bool mgAssetsLoadSpriteFromPath(M5Canvas &dst, const char *path, uint8_t depth, uint16_t transparentKey,
                                const char *tag);
bool mgAssetsLoadCachedSprite(M5Canvas &dst, bool &ready, char *cachedPath, size_t cachedPathSize, const char *path,
                              uint8_t depth, uint16_t transparentKey, const char *loadTag, const char *releaseTag);

bool mgAssetsLoadSpriteFromPath(M5Canvas &dst, const char *path, uint8_t depth, uint16_t transparentKey,
                                const char *tag);

bool mgAssetsLoadCachedSprite(M5Canvas &dst, bool &ready, char *cachedPath, size_t cachedPathSize, const char *path,
                              uint8_t depth, uint16_t transparentKey, const char *loadTag, const char *releaseTag);

                              