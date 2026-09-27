#pragma once
#include <stdint.h>

// Mesh layout shared with tools/build_mesh.py. The meshes themselves live on
// the SD card in /unicorn/animals.bin and are loaded by sd_store.h.

struct AnimalVertex {
  float x, y, z;
};

struct AnimalTriangle {
  uint16_t a, b, c;
  uint8_t red, green, blue;
  uint8_t flags;
};
static_assert(sizeof(AnimalTriangle) == 10, "AnimalTriangle must match the file layout");

// A moving part (a leg, the head, a tail): its vertices swing about `pivot` by
// amplitude * sin(2 pi (hz * t + phase)) radians around one axis.
struct AnimalPart {
  float pivot[3];
  uint8_t axis;  // 0 x, 1 y, 2 z
  float amplitude, hz, phase;
};
static constexpr int kMaxParts = 12;

struct AnimalMesh {
  char name[16];
  const AnimalVertex *vertices;
  int vertexCount;
  const AnimalTriangle *triangles;
  int triangleCount;
  float focal;
  float camX, camY, camZ;
  float targetX, targetY, targetZ;
  int partCount;
  AnimalPart parts[kMaxParts];
  const uint8_t *vertexParts;  // part of each vertex, 0 = still; null if none
};

static constexpr float kTurnSeconds = 18.0f;
// The menu has room for five rows.
static constexpr int kMaxAnimals = 5;
