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

struct AnimalMesh {
  char name[16];
  const AnimalVertex *vertices;
  int vertexCount;
  const AnimalTriangle *triangles;
  int triangleCount;
  float focal;
  float camX, camY, camZ;
  float targetX, targetY, targetZ;
};

static constexpr float kTurnSeconds = 18.0f;
// The menu has room for five rows.
static constexpr int kMaxAnimals = 5;
