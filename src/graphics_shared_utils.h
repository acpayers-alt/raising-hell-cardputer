#pragma once

// shared helpers extracted from graphics.cpp

int clampi(int v, int lo, int hi);

void listWindow(int total, int current, int maxVisible, int &start, int &count);