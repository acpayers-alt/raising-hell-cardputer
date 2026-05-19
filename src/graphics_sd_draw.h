#pragma once

#include <M5GFX.h>

bool sprDrawJpgFromSD(const char *path, int x, int y);
bool sprDrawPngFromSD(const char *path, int x, int y);
bool sprDrawPngFromSDMirroredX(const char *path, int x, int y, int w, int h);

bool canvasDrawPngFromSD(M5Canvas &canvas, const char *path, int x, int y);
bool canvasDrawJpgFromSD(M5Canvas &canvas, const char *path, int x, int y);

bool canvasDrawPngFromSD(LGFX_Sprite &canvas, const char *path, int x, int y);
bool canvasDrawJpgFromSD(LGFX_Sprite &canvas, const char *path, int x, int y);