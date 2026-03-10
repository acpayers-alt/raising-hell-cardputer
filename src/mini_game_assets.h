#pragma once

#include "display.h" // M5Canvas
#include "mini_games.h"
#include <Arduino.h>

// -----------------------------------------------------------------------------
// Low-level mini-game asset helpers
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

bool mgAssetsLoadSprite(M5Canvas &dst,
                        const char *path,
                        int colorDepth = 8,
                        uint16_t fillColor = TFT_BLACK,
                        const char *tag = nullptr);

void mgAssetsReleaseSprite(M5Canvas &dst, const char *tag = nullptr);

bool mgAssetsReadPngDims(const char *path, int *outW, int *outH, const char **outUsePath = nullptr);

bool mgAssetsLoadCachedSprite(M5Canvas &dst,
                              bool &ready,
                              char *cachedPath,
                              size_t cachedPathSize,
                              const char *path,
                              uint8_t depth,
                              uint16_t transparentKey,
                              const char *loadTag,
                              const char *releaseTag);

// -----------------------------------------------------------------------------
// Higher-level mini-game memory/session wrapper
// -----------------------------------------------------------------------------
// First pass:
// - explicit current session tracking
// - one shared fullscreen bg slot
// - no sprite registry yet
// - games can migrate gradually
// -----------------------------------------------------------------------------

namespace mgmem
{
  void beginSession(MiniGame game, int petType);
  void endSession();

  bool ensureSharedBg(const char *path, M5Canvas *&out, int &outW, int &outH);

  bool ensureSprite(MiniGame owner,
                    const char *assetId,
                    const char *path,
                    uint8_t colorDepth,
                    uint16_t transparentKey,
                    M5Canvas *&out);

  void releaseSprite(MiniGame owner, const char *assetId);
  void releaseAllForCurrentGame();

  size_t freeBytes();
  size_t largestBlock();
  void logUsage(const char *tag);

  MiniGame currentGame();
  int currentPetType();
}