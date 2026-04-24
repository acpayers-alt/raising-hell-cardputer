#pragma once

void drawCenteredLine(const char *s, int y, int font, int size);
void drawCenteredImageSpr(const char *path, int cx, int cy);
bool getPngWH(const char *path, int &outW, int &outH);

// Pet sprite sizing
static constexpr int PET_SPR_W = 84;
static constexpr int PET_SPR_H = 84;

// Pet anchor offsets
static constexpr int PET_X_OFFSET = 2;
static constexpr int PET_Y_OFFSET = 8;

// Mini stat panel
static constexpr int MINI_STAT_W = 56;
static constexpr int MINI_STAT_PAD = 4;