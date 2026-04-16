#include "graphics_shared_utils.h"

int clampi(int v, int lo, int hi)
{
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void listWindow(int total, int current, int maxVisible, int &start, int &count)
{
  count = (total < maxVisible) ? total : maxVisible;
  int half = count / 2;
  start = current - half;

  if (start < 0) start = 0;
  if (start > total - count) start = total - count;
}