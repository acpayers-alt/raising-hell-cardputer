#pragma once

void drawCenteredLine(const char *s, int y, int font, int size);
void drawCenteredImageSpr(const char *path, int cx, int cy);
bool getPngWH(const char *path, int &outW, int &outH);