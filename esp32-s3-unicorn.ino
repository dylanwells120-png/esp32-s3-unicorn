// Slowly rotating low-poly unicorn for the Makerfabs MaTouch ESP32-S3
// 4.3" 800x480 RGB touchscreen (GT911, CP2104 UART, 16MB flash, 8MB PSRAM).

// Speed over size: the renderer is the hot path.
#pragma GCC optimize("O2,fast-math")

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <Wire.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "animals.h"
#include "render_types.h"

static constexpr uint8_t kFlagDouble = 1;
static constexpr uint8_t kFlagSpecular = 2;

// Depth buffer holds 1/z scaled to uint16; larger is nearer. Must match tools/build_mesh.py.
static constexpr float kNearClip = 0.25f;
static constexpr float kDepthScale = 16000.0f;

#include "display.h"

// Draws text and JPEGs into the frame buffer being prepared (see beginDraw).
static lgfx::LGFX_Sprite sprite;

static uint16_t *fb = nullptr;
static uint16_t *zb = nullptr;
static uint16_t *fullZb = nullptr;
static int stride = 400;
static int screenW = 800;
static int screenH = 480;
static int viewW = 400;
static int viewH = 240;
static float viewFocal = 470.0f;
static int animalIndex = 0;
static bool menuOpen = false;
static bool started = false;
static bool swapBytes = false;

// Hologram mode, for the Pepper's ghost box in enclosure/. The sheet reflects
// the screen once, so the picture must be mirrored. Which axis depends on
// which screen edge faces the back of the box; holoRotate picks the other one.
// UI overlays are hidden because they would show up in the reflection.
static bool holoMode = false;
static bool holoRotate = false;
// Comets, sparkles and a ring of light around the animal in hologram mode (magic_fx.h).
static bool magicOn = true;
// Speed options: draw at 400x240 and scale up 2x, and/or use the lite models.
// Half resolution only applies in hologram mode, where no menus are drawn.
static bool halfRes = false;
static uint16_t *lowZb = nullptr;       // depth buffer for half resolution
// At half resolution the depth buffer is 8-bit, small enough for fast internal
// RAM. Depth is mapped over just the space around the animal: depth8Min is the
// 1/z of the far edge, and depth8Scale spreads the range over 1-255.
static uint8_t *zb8 = nullptr;
static bool depth8 = false;
static float depth8Min = 0.f;
static float depth8Scale = 1.f;
static uint32_t fullPclkHz = kDefaultPclkHz;
static Preferences prefs;
static float lastFps = 0.f;

// Part of the screen drawn so far. Clearing only what was drawn before saves
// most of the PSRAM traffic of clearing whole buffers.

static inline void growBox(DirtyBox &box, int x0, int y0, int x1, int y1) {
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= viewW) x1 = viewW - 1;
  if (y1 >= viewH) y1 = viewH - 1;
  if (x1 < x0 || y1 < y0) return;
  if (box.empty()) {
    box = {x0, y0, x1, y1};
    return;
  }
  if (x0 < box.x0) box.x0 = x0;
  if (y0 < box.y0) box.y0 = y0;
  if (x1 > box.x1) box.x1 = x1;
  if (y1 > box.y1) box.y1 = y1;
}

// Each frame is split into two bands of rows, one per CPU core. A band only
// clears, fills and lights pixels in its own rows.
static Band bands[2];
static int bandSplit = kPanelH / 2;  // first row of the second band

// What each frame buffer and the depth buffer held the last time they were
// drawn. Stale means something other than the animal was drawn there, so the
// whole buffer is cleared next time.
static DirtyBox bufferBox[kFrameBuffers] = {kNoBox, kNoBox, kNoBox};
static bool bufferStale[kFrameBuffers] = {true, true, true};
static DirtyBox depthBox = kNoBox;
static bool depthStale = true;

static void invalidateFrames() {
  for (int i = 0; i < kFrameBuffers; ++i) bufferStale[i] = true;
  depthStale = true;
}

// What renderBand clears in the colour buffer it draws into.
static const DirtyBox *clearBox = nullptr;
static bool clearStale = true;

// Points `fb` and `sprite` at the next free frame buffer. Pair with presentFrame().
static void beginDraw() {
  fb = beginFrame();
  sprite.setBuffer(fb, displayW, displayH);
}

static void setHologram(bool on, bool rotate) {
  holoMode = on;
  holoRotate = rotate;
  prefs.putBool("holo", on);
  prefs.putBool("holoRot", rotate);
  menuOpen = false;
  invalidateFrames();
  applyDisplayMode();
  Serial.printf("hologram %s%s\n", on ? "on" : "off", rotate ? " rotated" : "");
}

// Restarts the panel if hologram mode or half resolution changed which mode it needs.
static void applyDisplayMode() {
  bool half = holoMode && halfRes && lowZb;
  if (panel && half == displayHalf) return;
  if (!startDisplay(half, half ? kHalfPclkHz : fullPclkHz)) Serial.println("display restart failed");
  invalidateFrames();
}

static void setHalfRes(bool on) {
  halfRes = on;
  prefs.putBool("half", on);
  applyDisplayMode();
}

static void setLiteModels(bool on);

static void setMagic(bool on) {
  magicOn = on;
  prefs.putBool("magic", on);
  Serial.printf("magic %s\n", on ? "on" : "off");
}

// Called after new models are loaded from the SD card.
static void modelsChanged();
static void setupCamera();

#include "sd_store.h"
#include "video_stream.h"
#include "serial_upload.h"

static void setLiteModels(bool on) {
  useLiteModels = on;
  prefs.putBool("lite", on);
  loadAnimals();
  modelsChanged();
}


static float camRight[3];
static float camUp[3];
static float camForward[3];
static float keyLight[3];
static float fillLight[3];

// Per-vertex scratch, sized for the largest mesh on the SD card.
static int vertexCapacity = 0;
static float *sx = nullptr;
static float *sy = nullptr;
static float *sz = nullptr;
static float *rxv = nullptr;
static float *ryv = nullptr;
static float *rzv = nullptr;
static uint8_t *svOk = nullptr;

static bool ensureVertexBuffers(int count) {
  if (count <= vertexCapacity) return true;
  float **arrays[] = {&sx, &sy, &sz, &rxv, &ryv, &rzv};
  // Internal RAM: these are read at random for every triangle, and in PSRAM
  // the cache misses cost more than all the maths.
  constexpr uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  for (float **a : arrays) {
    heap_caps_free(*a);
    *a = (float *)heap_caps_malloc(count * sizeof(float), caps);
  }
  heap_caps_free(svOk);
  svOk = (uint8_t *)heap_caps_malloc(count, caps);
  bool ok = sx && sy && sz && rxv && ryv && rzv && svOk;
  vertexCapacity = ok ? count : 0;
  return ok;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static inline void normalize3(float v[3]) {
  float n2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
  if (n2 < 1e-12f) return;
  float inv = 1.f / sqrtf(n2);
  v[0] *= inv;
  v[1] *= inv;
  v[2] *= inv;
}

static void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static float dot3(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline uint16_t packColor(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
  if (swapBytes) c = (uint16_t)((c >> 8) | (c << 8));
  return c;
}

static inline bool projectPoint(const AnimalMesh &animal, float x, float y, float z, float &px, float &py,
                                float &invz);
#include "magic_fx.h"

// Unit normal of each triangle in model space, as signed bytes (x, y, z, pad),
// so each frame only has to turn it by the spin instead of rebuilding it.
static int8_t *triNormals = nullptr;
static int triNormalCount = 0;
static float spinCos = 1.f, spinSin = 0.f;
// This frame's swing of each moving part (index 0 is the still body).
static float partCos[kMaxParts + 1], partSin[kMaxParts + 1];
static uint8_t partAxis[kMaxParts + 1];

// Rotates (x, y, z) about part p's axis through its pivot. Pivot is null for normals.
static inline void swing(int p, const float *pivot, float &x, float &y, float &z) {
  float c = partCos[p], s = partSin[p];
  float px = pivot ? pivot[0] : 0.f, py = pivot ? pivot[1] : 0.f, pz = pivot ? pivot[2] : 0.f;
  float dx = x - px, dy = y - py, dz = z - pz;
  switch (partAxis[p]) {
    case 0: y = py + c * dy - s * dz; z = pz + s * dy + c * dz; break;
    case 1: x = px + c * dx + s * dz; z = pz - s * dx + c * dz; break;
    default: x = px + c * dx - s * dy; y = py + s * dx + c * dy; break;
  }
}

static void forgetFastAnimal() {
  heap_caps_free(triNormals);
  triNormals = nullptr;
  triNormalCount = 0;
}

static const AnimalMesh &currentAnimal() { return animals[animalIndex]; }

static void cacheNormals(const AnimalMesh &m) {
  heap_caps_free(triNormals);
  triNormalCount = 0;
  triNormals = (int8_t *)heap_caps_malloc(m.triangleCount * 4, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!triNormals) return;
  for (int t = 0; t < m.triangleCount; ++t) {
    const AnimalTriangle &tri = m.triangles[t];
    const AnimalVertex &a = m.vertices[tri.a], &b = m.vertices[tri.b], &c = m.vertices[tri.c];
    float e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
    float e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
    float n[3];
    cross3(e1, e2, n);
    normalize3(n);
    for (int k = 0; k < 3; ++k) triNormals[t * 4 + k] = (int8_t)lroundf(n[k] * 127.f);
    triNormals[t * 4 + 3] = 0;
  }
  triNormalCount = m.triangleCount;
}

// Only the normals are cached: copying the whole mesh to internal RAM didn't
// make rendering faster, and Wi-Fi needs that RAM.
static void cacheAnimal() {
  forgetFastAnimal();
  cacheNormals(animals[animalIndex]);
}

static void setupCamera() {
  invalidateFrames();
  if (animalCount == 0) return;
  cacheAnimal();
  const AnimalMesh &animal = animals[animalIndex];
  viewFocal = animal.focal;
  float forward[3] = {
      animal.targetX - animal.camX,
      animal.targetY - animal.camY,
      animal.targetZ - animal.camZ,
  };
  normalize3(forward);
  float worldUp[3] = {0.f, 1.f, 0.f};
  cross3(forward, worldUp, camRight);
  normalize3(camRight);
  cross3(camRight, forward, camUp);
  normalize3(camUp);
  memcpy(camForward, forward, sizeof(forward));

  keyLight[0] = -0.35f;
  keyLight[1] = 1.0f;
  keyLight[2] = 0.45f;
  normalize3(keyLight);
  fillLight[0] = 0.85f;
  fillLight[1] = 0.25f;
  fillLight[2] = 0.35f;
  normalize3(fillLight);
  float distance = sqrtf((animal.targetX - animal.camX) * (animal.targetX - animal.camX) +
                         (animal.targetY - animal.camY) * (animal.targetY - animal.camY) +
                         (animal.targetZ - animal.camZ) * (animal.targetZ - animal.camZ));
  magicPrepare(animal, distance);
}

static void probeColorLayout() {
  sprite.fillScreen(TFT_BLACK);
  uint16_t logical = sprite.color565(255, 0, 0);
  sprite.drawPixel(0, 0, logical);
  uint16_t stored = fb[0];
  uint16_t swapped = (uint16_t)((logical >> 8) | (logical << 8));
  swapBytes = (stored != logical && stored == swapped);
  Serial.printf("color logical=%04X stored=%04X swap=%d\n", logical, stored, swapBytes ? 1 : 0);

  sprite.drawPixel(0, 1, logical);
  stride = screenW;
  int limit = screenW * screenH;
  for (int i = 1; i < limit; ++i) {
    if (fb[i] == stored || fb[i] == logical || fb[i] == swapped) {
      stride = i;
      break;
    }
  }
  Serial.printf("sprite %dx%d stride %d\n", screenW, screenH, stride);
}

static uint16_t shade(uint8_t r, uint8_t g, uint8_t b, const float n[3], uint8_t flags, const float view[3]) {
  float normal[3] = {n[0], n[1], n[2]};
  if ((flags & kFlagDouble) && dot3(normal, view) < 0.f) {
    normal[0] = -normal[0];
    normal[1] = -normal[1];
    normal[2] = -normal[2];
  }
  float lambert = 0.30f + 0.72f * fmaxf(dot3(normal, keyLight), 0.f);
  lambert += 0.22f * fmaxf(dot3(normal, fillLight), 0.f);
  if (flags & kFlagSpecular) {
    float halfv[3] = {keyLight[0] + view[0], keyLight[1] + view[1], keyLight[2] + view[2]};
    normalize3(halfv);
    float spec = fmaxf(dot3(normal, halfv), 0.f);
    float shaped = spec * spec;
    shaped = shaped * shaped;
    shaped = shaped * shaped;
    lambert += 0.55f * shaped;
  }
  lambert = clampf(lambert, 0.f, 1.35f);
  return packColor(
      (uint8_t)clampf(r * lambert, 0.f, 255.f),
      (uint8_t)clampf(g * lambert, 0.f, 255.f),
      (uint8_t)clampf(b * lambert, 0.f, 255.f));
}

// ceilf is a library call on this chip; this is a few instructions.
static inline int fastCeil(float v) {
  int i = (int)v;  // truncates toward zero
  return i + (v > (float)i);
}

static void raster(Band &band, float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2,
                   uint16_t color) {
  if (y0 > y1) { float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; t = z0; z0 = z1; z1 = t; }
  if (y1 > y2) { float t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; t = z1; z1 = z2; z2 = t; }
  if (y0 > y1) { float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; t = z0; z0 = z1; z1 = t; }
  // A pixel is covered when its center (x + 0.5, y + 0.5) lies inside the triangle,
  // so triangles that share an edge leave no gaps and draw no pixel twice.
  int minY = fastCeil(y0 - 0.5f);
  int maxY = fastCeil(y2 - 0.5f) - 1;
  if (minY < band.y0) minY = band.y0;
  if (maxY > band.y1) maxY = band.y1;
  if (maxY < minY) return;

  // Depth varies linearly across the triangle, so its change per pixel in x is constant.
  float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
  if (fabsf(area) < 1e-6f) return;
  float dzdx = ((z1 - z0) * (y2 - y0) - (z2 - z0) * (y1 - y0)) / area;
  int dzi = (int)(dzdx * kDepthScale);
  // Edge slopes: long edge 0-2, short edges 0-1 and 1-2.
  float inv02 = 1.f / (y2 - y0);
  float s02 = (x2 - x0) * inv02;
  float z02 = (z2 - z0) * inv02;
  float s01 = y1 > y0 ? (x1 - x0) / (y1 - y0) : 0.f;
  float s12 = y2 > y1 ? (x2 - x1) / (y2 - y1) : 0.f;

  float lo = fminf(x0, fminf(x1, x2));
  float hi = fmaxf(x0, fmaxf(x1, x2));
  growBox(band.box, (int)floorf(lo), minY, (int)ceilf(hi), maxY);

  band.rows += maxY - minY + 1;
  for (int y = minY; y <= maxY; ++y) {
    float ys = y + 0.5f;
    float xLong = x0 + (ys - y0) * s02;
    float zLong = z0 + (ys - y0) * z02;
    float xShort = ys < y1 ? x0 + (ys - y0) * s01 : x1 + (ys - y1) * s12;
    float xa = fminf(xLong, xShort);
    float xb = fmaxf(xLong, xShort);
    int xStart = fastCeil(xa - 0.5f);
    int xEnd = fastCeil(xb - 0.5f) - 1;
    if (xStart < 0) xStart = 0;
    if (xEnd >= viewW) xEnd = viewW - 1;
    if (xEnd < xStart) continue;
    float zStart = zLong + dzdx * ((xStart + 0.5f) - xLong);
    uint16_t *rowC = fb + y * stride;
    if (depth8) {
      // 8.8 fixed point, clamped to 1-255 when stored.
      int zf = (int)((zStart - depth8Min) * depth8Scale * 256.f) + 256;
      int dzf = (int)(dzdx * depth8Scale * 256.f);
      uint8_t *row8 = zb8 + y * stride;
      for (int x = xStart; x <= xEnd; ++x) {
        int v = zf >> 8;
        v = v < 1 ? 1 : (v > 255 ? 255 : v);
        if (v > row8[x]) {
          row8[x] = (uint8_t)v;
          rowC[x] = color;
        }
        zf += dzf;
      }
      continue;
    }
    int zi = (int)(zStart * kDepthScale);
    uint16_t *rowZ = zb + y * stride;
    for (int x = xStart; x <= xEnd; ++x) {
      if ((uint16_t)zi > rowZ[x]) {
        rowZ[x] = (uint16_t)zi;
        rowC[x] = color;
      }
      zi += dzi;
    }
  }
}

static constexpr int kButtonW = 176;
static constexpr int kButtonH = 54;
static constexpr int kStartButtonW = 240;
static constexpr int kStartButtonH = 64;

static void startButtonRect(int &x, int &y) {
  x = (screenW - kStartButtonW) / 2;
  y = screenH / 2 + 24;
}

static void drawStartScreen() {
  beginDraw();
  sprite.fillScreen(TFT_BLACK);
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(5);
  const char *title = "Holobox-mini";
  int titleW = (int)strlen(title) * 6 * 5;
  sprite.setCursor((screenW - titleW) / 2, screenH / 2 - 120);
  sprite.print(title);

  int x = 0;
  int y = 0;
  startButtonRect(x, y);
  sprite.fillRoundRect(x, y, kStartButtonW, kStartButtonH, 16, sprite.color565(18, 18, 22));
  sprite.drawRoundRect(x, y, kStartButtonW, kStartButtonH, 16, TFT_WHITE);
  sprite.setTextSize(3);
  const char *label = "Start";
  int labelW = (int)strlen(label) * 6 * 3;
  int labelH = 8 * 3;
  sprite.setCursor(x + (kStartButtonW - labelW) / 2, y + (kStartButtonH - labelH) / 2);
  sprite.print(label);
  presentFrame();
}

static void buttonRect(int &x, int &y) {
  x = screenW - 16 - kButtonW;
  y = screenH - 16 - kButtonH;
}

static void drawAnimalsButton() {
  if (holoMode) return;
  int x = 0;
  int y = 0;
  buttonRect(x, y);
  sprite.fillRoundRect(x, y, kButtonW, kButtonH, 14, sprite.color565(18, 18, 22));
  sprite.drawRoundRect(x, y, kButtonW, kButtonH, 14, TFT_WHITE);
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_WHITE);
  sprite.setCursor(x + 28, y + 18);
  sprite.print("Animals");
  if (animalCount > 0) {
    sprite.setCursor(screenW - 16 - (int)strlen(animals[animalIndex].name) * 12, 34);
    sprite.print(animals[animalIndex].name);
  }
}

static void drawMenu() {
  beginDraw();
  sprite.fillScreen(TFT_BLACK);
  sprite.setTextSize(3);
  sprite.setTextColor(TFT_WHITE);
  sprite.setCursor(36, 24);
  sprite.print("Choose an animal");

  const int bx = 36;
  const int bw = 728;
  const int bh = 66;
  const int gap = 10;
  int top = 84;
  for (int i = 0; i < animalCount; ++i) {
    bool selected = i == animalIndex;
    uint16_t fill = selected ? sprite.color565(46, 46, 54) : sprite.color565(20, 20, 24);
    uint16_t edge = selected ? TFT_WHITE : sprite.color565(78, 78, 86);
    sprite.fillRoundRect(bx, top, bw, bh, 14, fill);
    sprite.drawRoundRect(bx, top, bw, bh, 14, edge);
    sprite.setTextSize(3);
    sprite.setTextColor(TFT_WHITE);
    sprite.setCursor(bx + 28, top + 20);
    sprite.print(animals[i].name);
    top += bh + gap;
  }
  presentFrame();
}

static bool hit(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

// The animal frame takes longer than LovyanGFX's touch timeout, so a tap
// that lands during a frame is thrown away. Poll the GT911 directly and
// keep the press until the UI reads it.
static constexpr int kTouchSda = 17;
static constexpr int kTouchScl = 18;
static constexpr int kTouchRst = 38;
static constexpr uint8_t kGt911AddrA = 0x5D;
static constexpr uint8_t kGt911AddrB = 0x14;

static uint8_t gtAddr = kGt911AddrA;
static portMUX_TYPE touchMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t touchGeneration = 0;
static volatile int touchPressX = 0;
static volatile int touchPressY = 0;
static uint32_t touchSeen = 0;
static uint32_t touchIgnoreUntil = 0;

static bool gtRead(uint16_t reg, uint8_t *dst, size_t len) {
  Wire.beginTransmission(gtAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)gtAddr, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; ++i) dst[i] = Wire.read();
  return true;
}

static bool gtWrite8(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(gtAddr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static bool gtAlive(uint8_t addr) {
  gtAddr = addr;
  uint8_t id[4] = {};
  if (!gtRead(0x8140, id, 4)) return false;
  return id[0] == '9' && id[1] == '1' && id[2] == '1';
}

static void touchTask(void *) {
  bool wasDown = false;
  for (;;) {
    uint8_t status = 0;
    if (gtRead(0x814E, &status, 1) && (status & 0x80)) {
      uint8_t points = status & 0x0F;
      int rawX = 0;
      int rawY = 0;
      bool down = false;
      if (points > 0 && points <= 5) {
        uint8_t pt[7] = {};
        if (gtRead(0x814F, pt, 7)) {
          rawX = pt[1] | (pt[2] << 8);
          rawY = pt[3] | (pt[4] << 8);
          down = true;
        }
      }
      gtWrite8(0x814E, 0);
      if (down && !wasDown) {
        // Raw GT911 coordinates already match this panel. Mirroring them
        // puts the tap in the opposite corner from the finger.
        if (rawX < 0) rawX = 0;
        if (rawY < 0) rawY = 0;
        if (rawX > 799) rawX = 799;
        if (rawY > 479) rawY = 479;
        portENTER_CRITICAL(&touchMux);
        touchPressX = rawX;
        touchPressY = rawY;
        touchGeneration++;
        portEXIT_CRITICAL(&touchMux);
      }
      wasDown = down;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void startTouch() {
  pinMode(kTouchRst, OUTPUT);
  digitalWrite(kTouchRst, LOW);
  delay(10);
  digitalWrite(kTouchRst, HIGH);
  delay(50);
  Wire.begin(kTouchSda, kTouchScl, 400000);
  bool found = gtAlive(kGt911AddrA) || gtAlive(kGt911AddrB);
  Serial.printf("touch %s addr 0x%02X\n", found ? "ready" : "missing", gtAddr);
  if (!found) return;
  xTaskCreatePinnedToCore(touchTask, "gt911", 4096, nullptr, 2, nullptr, 0);
}

static void handleTouch() {
  uint32_t generation = 0;
  int tx = 0;
  int ty = 0;
  portENTER_CRITICAL(&touchMux);
  generation = touchGeneration;
  tx = touchPressX;
  ty = touchPressY;
  portEXIT_CRITICAL(&touchMux);
  if (generation == touchSeen) return;
  touchSeen = generation;
  if ((int32_t)(touchIgnoreUntil - millis()) > 0) return;
  touchIgnoreUntil = millis() + 280;
  Serial.printf("touch %d %d menu %d\n", tx, ty, menuOpen ? 1 : 0);
  // Nothing on screen is tappable inside the hologram box.
  if (holoMode) return;

  if (!started) {
    int x = 0;
    int y = 0;
    startButtonRect(x, y);
    if (hit(tx, ty, x - 12, y - 12, kStartButtonW + 24, kStartButtonH + 24)) {
      started = true;
      Serial.println("start");
    }
    return;
  }

  if (!menuOpen) {
    int x = 0;
    int y = 0;
    buttonRect(x, y);
    if (hit(tx, ty, x - 8, y - 8, kButtonW + 16, kButtonH + 16)) menuOpen = true;
    return;
  }

  const int bx = 36;
  const int bw = 728;
  const int bh = 66;
  const int gap = 10;
  int top = 84;
  for (int i = 0; i < animalCount; ++i) {
    if (hit(tx, ty, bx, top, bw, bh)) {
      animalIndex = i;
      setupCamera();
      menuOpen = false;
      Serial.printf("animal %s\n", animals[i].name);
      return;
    }
    top += bh + gap;
  }
}

// Projects a world point (already turned by the frame's yaw) to the screen.
// Returns false for points behind the near clip. invz is 1/depth.
static inline bool projectPoint(const AnimalMesh &animal, float x, float y, float z, float &px, float &py,
                                float &invz) {
  float dx = x - animal.camX;
  float dy = y - animal.camY;
  float dz = z - animal.camZ;
  float cz = dx * camForward[0] + dy * camForward[1] + dz * camForward[2];
  if (cz < kNearClip) return false;
  invz = 1.f / cz;
  float u = (dx * camRight[0] + dy * camRight[1] + dz * camRight[2]) * invz * viewFocal;
  float v = (dx * camUp[0] + dy * camUp[1] + dz * camUp[2]) * invz * viewFocal;
  if (holoMode) {
    if (holoRotate) u = -u;
    else v = -v;
  }
  px = viewW * 0.5f + u;
  py = viewH * 0.5f - v;
  return true;
}

static void drawNoModels() {
  invalidateFrames();
  beginDraw();
  sprite.fillScreen(TFT_BLACK);
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(3);
  sprite.setCursor(36, 150);
  sprite.print("No 3D models found");
  sprite.setTextSize(2);
  sprite.setTextColor(sprite.color565(170, 170, 184));
  sprite.setCursor(36, 200);
  sprite.printf("SD card: %s", modelError.c_str());
  sprite.setCursor(36, 236);
  sprite.printf("Upload animals.bin at http://%s/setup", videoAddress().c_str());
  drawAnimalsButton();
  presentFrame();
}

static void modelsChanged() {
  if (animalCount > 0 && !ensureVertexBuffers(maxAnimalVertices)) {
    Serial.println("models: out of memory");
    freeAnimals();
    modelError = "not enough memory for the models";
  }
  if (animalIndex >= animalCount) animalIndex = 0;
  setupCamera();
}

static void showInstallProgress(int percent, const char *status) {
  beginDraw();
  sprite.fillScreen(TFT_BLACK);
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(3);
  sprite.setCursor(60, 180);
  sprite.print(status);
  sprite.drawRect(60, 240, screenW - 120, 36, TFT_WHITE);
  sprite.fillRect(64, 244, (screenW - 128) * percent / 100, 28, sprite.color565(240, 108, 155));
  presentFrame();
}

// Second core's share of each frame (see renderFrame).
static const AnimalMesh *frameAnimal = nullptr;
static bool frameMagic = false;
static TaskHandle_t renderWorker = nullptr;
static SemaphoreHandle_t workStart = nullptr;
static SemaphoreHandle_t workDone = nullptr;

static void clearRows(uint16_t *buf, const DirtyBox &box, bool whole, const Band &band) {
  DirtyBox area = whole ? DirtyBox{0, 0, viewW - 1, viewH - 1} : box;
  if (area.empty()) return;
  int y0 = max(area.y0, band.y0), y1 = min(area.y1, band.y1);
  size_t bytes = (size_t)(area.x1 - area.x0 + 1) * sizeof(uint16_t);
  for (int y = y0; y <= y1; ++y) memset(buf + y * stride + area.x0, 0, bytes);
}

static void renderBand(Band &band) {
  uint32_t t0 = micros();
  clearRows(fb, *clearBox, clearStale, band);
  if (depth8) {
    DirtyBox area = depthStale ? DirtyBox{0, 0, viewW - 1, viewH - 1} : depthBox;
    if (!area.empty()) {
      for (int y = max(area.y0, band.y0); y <= min(area.y1, band.y1); ++y)
        memset(zb8 + y * stride + area.x0, 0, area.x1 - area.x0 + 1);
    }
  } else {
    clearRows(zb, depthBox, depthStale, band);
  }
  band.box = kNoBox;
  uint32_t t1 = micros();
  band.clearUs = t1 - t0;
  band.fillCycles = band.trisSeen = band.trisDrawn = band.rows = 0;
  const AnimalMesh &animal = *frameAnimal;
  float top = band.y0 - 1.f, bottom = band.y1 + 1.f;
  for (int t = 0; t < animal.triangleCount; ++t) {
    const AnimalTriangle &tri = animal.triangles[t];
    if (!svOk[tri.a] || !svOk[tri.b] || !svOk[tri.c]) continue;
    // Skip triangles outside this band before any shading work.
    float ya = sy[tri.a], yb = sy[tri.b], yc = sy[tri.c];
    float yMin = fminf(ya, fminf(yb, yc)), yMax = fmaxf(ya, fmaxf(yb, yc));
    if (yMax < top || yMin > bottom) continue;
    band.trisSeen++;
    // Skip triangles that cover no pixel centre (common for tiny faces at half
    // resolution) before paying for lighting and setup.
    if (fastCeil(yMin - 0.5f) > fastCeil(yMax - 0.5f) - 1) continue;
    float xa0 = sx[tri.a], xb0 = sx[tri.b], xc0 = sx[tri.c];
    if (fastCeil(fminf(xa0, fminf(xb0, xc0)) - 0.5f) > fastCeil(fmaxf(xa0, fmaxf(xb0, xc0)) - 0.5f) - 1) continue;

    float ax = rxv[tri.a], ay = ryv[tri.a], az = rzv[tri.a];
    float bx = rxv[tri.b], by = ryv[tri.b], bz = rzv[tri.b];
    float cxv = rxv[tri.c], cyv = ryv[tri.c], czv = rzv[tri.c];

    float n[3];
    if (t < triNormalCount) {
      // Turn the stored model-space normal by this frame's spin.
      const int8_t *m = triNormals + t * 4;
      float mx = m[0] * (1.f / 127.f), my = m[1] * (1.f / 127.f), mz = m[2] * (1.f / 127.f);
      int part = animal.vertexParts ? animal.vertexParts[tri.a] : 0;
      if (part) swing(part, nullptr, mx, my, mz);
      n[0] = spinCos * mx + spinSin * mz;
      n[1] = my;
      n[2] = -spinSin * mx + spinCos * mz;
    } else {
      float e1[3] = {bx - ax, by - ay, bz - az};
      float e2[3] = {cxv - ax, cyv - ay, czv - az};
      cross3(e1, e2, n);
    }
    float mid[3] = {(ax + bx + cxv) / 3.f, (ay + by + cyv) / 3.f, (az + bz + czv) / 3.f};
    float view[3] = {animal.camX - mid[0], animal.camY - mid[1], animal.camZ - mid[2]};
    // Cull before normalizing: the sign doesn't depend on length.
    if (!(tri.flags & kFlagDouble) && dot3(n, view) <= 0.f) continue;
    // Only shiny and two-sided faces use the view direction.
    if (tri.flags) normalize3(view);
    if (t >= triNormalCount) normalize3(n);

    uint16_t color = shade(tri.red, tri.green, tri.blue, n, tri.flags, view);
    band.trisDrawn++;
    uint32_t c0 = esp_cpu_get_cycle_count();
    raster(band, sx[tri.a], ya, sz[tri.a], sx[tri.b], yb, sz[tri.b], sx[tri.c], yc, sz[tri.c], color);
    band.fillCycles += esp_cpu_get_cycle_count() - c0;
  }
  band.trisUs = micros() - t1;
  if (frameMagic) drawMagic(band, animal);
  band.micros = micros() - t0;
}

static void renderWorkerTask(void *) {
  for (;;) {
    xSemaphoreTake(workStart, portMAX_DELAY);
    renderBand(bands[1]);
    xSemaphoreGive(workDone);
  }
}

static void startRenderWorker() {
  workStart = xSemaphoreCreateBinary();
  workDone = xSemaphoreCreateBinary();
  // The sketch's loop runs on core 1; the other band runs on core 0 alongside Wi-Fi.
  if (xTaskCreatePinnedToCore(renderWorkerTask, "render", 8192, nullptr, 2, &renderWorker, 0) != pdPASS) {
    renderWorker = nullptr;
    Serial.println("render worker failed; drawing on one core");
  }
}

static void renderFrame(float yaw) {
  if (animalCount == 0) {
    drawNoModels();
    return;
  }
  const AnimalMesh &animal = currentAnimal();
  beginDraw();
  viewW = displayW;
  viewH = displayH;
  stride = displayW;
  zb = displayHalf ? lowZb : fullZb;
  viewFocal = animal.focal * (displayHalf ? 0.5f : 1.f);
  bool was8 = depth8;
  depth8 = displayHalf && zb8;
  if (depth8 != was8) depthStale = true;
  if (depth8) {
    float dist = sqrtf((animal.targetX - animal.camX) * (animal.targetX - animal.camX) +
                       (animal.targetY - animal.camY) * (animal.targetY - animal.camY) +
                       (animal.targetZ - animal.camZ) * (animal.targetZ - animal.camZ));
    float nearZ = fmaxf(dist - magicBound, kNearClip);
    depth8Min = 1.f / (dist + magicBound);
    depth8Scale = 254.f / (1.f / nearZ - depth8Min);
  }
  clearBox = &bufferBox[backIndex];
  clearStale = bufferStale[backIndex];

  float c = cosf(yaw);
  float s = sinf(yaw);
  spinCos = c;
  spinSin = s;
  float now = (millis() % 3600000UL) / 1000.f;
  for (int p = 0; p < animal.partCount; ++p) {
    const AnimalPart &part = animal.parts[p];
    float angle = part.amplitude * sinf(6.2831853f * (part.hz * now + part.phase));
    partCos[p + 1] = cosf(angle);
    partSin[p + 1] = sinf(angle);
    partAxis[p + 1] = part.axis;
  }
  for (int i = 0; i < animal.vertexCount; ++i) {
    const AnimalVertex &v = animal.vertices[i];
    float vx = v.x, vy = v.y, vz = v.z;
    int part = animal.vertexParts ? animal.vertexParts[i] : 0;
    if (part) swing(part, animal.parts[part - 1].pivot, vx, vy, vz);
    float x = c * vx + s * vz;
    float y = vy;
    float z = -s * vx + c * vz;
    rxv[i] = x;
    ryv[i] = y;
    rzv[i] = z;
    svOk[i] = projectPoint(animal, x, y, z, sx[i], sy[i], sz[i]);
  }

  frameAnimal = &animal;
  frameMagic = holoMode && magicOn;
  int split = bandSplit * viewH / kPanelH;
  bands[0].y0 = 0;
  bands[0].y1 = split - 1;
  bands[1].y0 = split;
  bands[1].y1 = viewH - 1;
  if (renderWorker) {
    xSemaphoreGive(workStart);
    renderBand(bands[0]);
    xSemaphoreTake(workDone, portMAX_DELAY);
  } else {
    renderBand(bands[0]);
    renderBand(bands[1]);
  }

  // Move the split toward the faster core so both finish together next frame.
  float ta = bands[0].micros, tb = bands[1].micros;
  if (ta + tb > 0.f) {
    bandSplit -= (int)((ta - tb) / (ta + tb) * kPanelH * 0.2f);
    bandSplit = constrain(bandSplit, kPanelH / 8, kPanelH - kPanelH / 8);
  }

  DirtyBox box = bands[0].box;
  if (!bands[1].box.empty()) {
    growBox(box, bands[1].box.x0, bands[1].box.y0, bands[1].box.x1, bands[1].box.y1);
  }
  bufferBox[backIndex] = box;
  depthBox = box;
  depthStale = false;
  bufferStale[backIndex] = false;

  drawAnimalsButton();
  presentFrame();
}

void setup() {
  // Large enough for a whole SD upload chunk (see serial_upload.h).
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  delay(200);
  Serial.println("unicorn boot");
  serialUploadSetup();

  prefs.begin("unicorn", false);
  fullPclkHz = prefs.getUInt("pclk", kDefaultPclkHz);
  if (fullPclkHz < kMinPclkHz || fullPclkHz > kMaxPclkHz) fullPclkHz = kDefaultPclkHz;
  holoMode = prefs.getBool("holo", false);
  halfRes = prefs.getBool("half", false);
  lowZb = (uint16_t *)heap_caps_malloc(kPanelW / 2 * kPanelH / 2 * 2, MALLOC_CAP_SPIRAM);
  zb8 = (uint8_t *)heap_caps_malloc(kPanelW / 2 * kPanelH / 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  Serial.printf("8-bit depth buffer %s\n", zb8 ? "in internal RAM" : "unavailable");
  bool half = holoMode && halfRes && lowZb;
  if (!startDisplay(half, half ? kHalfPclkHz : fullPclkHz)) {
    Serial.println("display start failed");
    while (true) delay(1000);
  }
  screenW = kPanelW;
  screenH = kPanelH;
  Serial.printf("panel %dx%d, %.1f MHz pixel clock, %.1f Hz refresh\n", screenW, screenH, pclkHz / 1e6f,
                refreshRate());

  // The panel wants RGB565 in native byte order.
  sprite.setColorDepth(lgfx::color_depth_t::rgb565_nonswapped);
  beginDraw();
  probeColorLayout();

  viewW = screenW;
  viewH = screenH;
  setupCamera();
  if (stride < screenW) stride = screenW;
  zb = (uint16_t *)heap_caps_malloc((size_t)stride * screenH * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  fullZb = zb;
  if (!fb || !zb) {
    Serial.println("framebuffer alloc failed");
    while (true) delay(1000);
  }
  Serial.printf("render %dx%d focal %.1f\n", viewW, viewH, viewFocal);
  holoMode = prefs.getBool("holo", false);
  holoRotate = prefs.getBool("holoRot", false);
  magicOn = prefs.getBool("magic", true);
  useLiteModels = prefs.getBool("lite", false);
  Serial.printf("hologram %s\n", holoMode ? "on" : "off");
  startTouch();
  if (startSd()) {
    installFirmwareFromSd(showInstallProgress);
    loadAnimals();
  }
  modelsChanged();
  startRenderWorker();
  videoSetup();
  Serial.println("unicorn spinning");
}

void loop() {
  static uint32_t frames = 0;
  static uint32_t windowStart = millis();
  serialUploadPoll();
  if (videoPoll()) {
    invalidateFrames();
    return;
  }
  if (nesPoll()) {
    invalidateFrames();
    delay(16);
    return;
  }
  handleTouch();
  if (!started && !holoMode) {
    invalidateFrames();
    drawStartScreen();
    delay(16);
    return;
  }
  if (menuOpen && !holoMode) {
    invalidateFrames();
    drawMenu();
    delay(16);
    return;
  }
  float yaw = (millis() / 1000.0f) * (2.f * PI / kTurnSeconds);
  uint32_t t0 = millis();
  renderFrame(yaw);
  frames++;
  uint32_t now = millis();
  if (now - windowStart >= 2000) {
    float sec = (now - windowStart) / 1000.0f;
    lastFps = frames / sec;
    Serial.printf("fps %.1f last %lu ms\n", lastFps, (unsigned long)(now - t0));
    frames = 0;
    windowStart = now;
  }
}
