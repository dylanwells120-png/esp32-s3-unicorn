#pragma once
#include <stdint.h>

// Declared here so the Arduino builder's generated prototypes can see them.

struct DirtyBox {
  int x0, y0, x1, y1;  // inclusive; empty when x1 < x0
  bool empty() const { return x1 < x0 || y1 < y0; }
};
static constexpr DirtyBox kNoBox = {0, 0, -1, -1};

struct Band {
  int y0, y1;       // inclusive rows
  DirtyBox box;     // what this band drew
  uint32_t micros;  // how long it took
  uint32_t clearUs, trisUs;
  uint32_t fillCycles, trisSeen, trisDrawn, rows;  // profiling  // parts of it: clearing, then triangles (the rest is effects)
};
