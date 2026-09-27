// Slowly rotating low-poly unicorn for the Makerfabs MaTouch ESP32-S3
// 4.3" 800x480 RGB touchscreen (GT911, CP2104 UART, 16MB flash, 8MB PSRAM).

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <Wire.h>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "animals.h"

static constexpr uint8_t kFlagDouble = 1;
static constexpr uint8_t kFlagSpecular = 2;

static constexpr int kBacklightPin = 44;
// Depth buffer holds 1/z scaled to uint16; larger is nearer. Must match tools/build_mesh.py.
static constexpr float kNearClip = 0.25f;
static constexpr float kDepthScale = 16000.0f;

class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Light_PWM _light_instance;

  LGFX() {
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width = 800;
      cfg.memory_height = 480;
      cfg.panel_width = 800;
      cfg.panel_height = 480;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;
      _panel_instance.config_detail(cfg);
    }
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;
      cfg.pin_d0 = GPIO_NUM_8;    // B0
      cfg.pin_d1 = GPIO_NUM_3;    // B1
      cfg.pin_d2 = GPIO_NUM_46;   // B2
      cfg.pin_d3 = GPIO_NUM_9;    // B3
      cfg.pin_d4 = GPIO_NUM_1;    // B4
      cfg.pin_d5 = GPIO_NUM_5;    // G0
      cfg.pin_d6 = GPIO_NUM_6;    // G1
      cfg.pin_d7 = GPIO_NUM_7;    // G2
      cfg.pin_d8 = GPIO_NUM_15;   // G3
      cfg.pin_d9 = GPIO_NUM_16;   // G4
      cfg.pin_d10 = GPIO_NUM_4;   // G5
      cfg.pin_d11 = GPIO_NUM_45;  // R0
      cfg.pin_d12 = GPIO_NUM_48;  // R1
      cfg.pin_d13 = GPIO_NUM_47;  // R2
      cfg.pin_d14 = GPIO_NUM_21;  // R3
      cfg.pin_d15 = GPIO_NUM_14;  // R4
      cfg.pin_henable = GPIO_NUM_40;
      cfg.pin_vsync = GPIO_NUM_41;
      cfg.pin_hsync = GPIO_NUM_39;
      cfg.pin_pclk = GPIO_NUM_42;
      cfg.freq_write = 16000000;
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 8;
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 8;
      cfg.pclk_idle_high = 1;
      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = kBacklightPin;
      cfg.invert = false;
      cfg.freq = 20000;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
    }
    _panel_instance.light(&_light_instance);
    setPanel(&_panel_instance);
  }
};

static LGFX lcd;
static lgfx::LGFX_Sprite sprite(&lcd);

static uint16_t *fb = nullptr;
static uint16_t *zb = nullptr;
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
static Preferences prefs;

static void setHologram(bool on, bool rotate) {
  holoMode = on;
  holoRotate = rotate;
  prefs.putBool("holo", on);
  prefs.putBool("holoRot", rotate);
  menuOpen = false;
  Serial.printf("hologram %s%s\n", on ? "on" : "off", rotate ? " rotated" : "");
}

// Called after new models are loaded from the SD card.
static void modelsChanged();
static void setupCamera();

#include "sd_store.h"
#include "video_stream.h"
#include "serial_upload.h"

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
  for (float **a : arrays) {
    free(*a);
    *a = (float *)malloc(count * sizeof(float));
  }
  free(svOk);
  svOk = (uint8_t *)malloc(count);
  bool ok = sx && sy && sz && rxv && ryv && rzv && svOk;
  vertexCapacity = ok ? count : 0;
  return ok;
}

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static void normalize3(float v[3]) {
  float n = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (n < 1e-6f) return;
  v[0] /= n;
  v[1] /= n;
  v[2] /= n;
}

static void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

static float dot3(const float a[3], const float b[3]) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static uint16_t packColor(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = sprite.color565(r, g, b);
  if (swapBytes) c = (uint16_t)((c >> 8) | (c << 8));
  return c;
}

static void setupCamera() {
  if (animalCount == 0) return;
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

static void raster(float x0, float y0, float z0, float x1, float y1, float z1, float x2, float y2, float z2, uint16_t color) {
  if (y0 > y1) { float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; t = z0; z0 = z1; z1 = t; }
  if (y1 > y2) { float t; t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; t = z1; z1 = z2; z2 = t; }
  if (y0 > y1) { float t; t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; t = z0; z0 = z1; z1 = t; }
  // A pixel is covered when its center (x + 0.5, y + 0.5) lies inside the triangle,
  // so triangles that share an edge leave no gaps and draw no pixel twice.
  int minY = (int)ceilf(y0 - 0.5f);
  int maxY = (int)ceilf(y2 - 0.5f) - 1;
  if (minY < 0) minY = 0;
  if (maxY >= viewH) maxY = viewH - 1;
  if (maxY < minY) return;

  auto edge = [](float y, float ya, float yb, float xa, float xb, float za, float zb, float &xo, float &zo) {
    float t = (yb == ya) ? 0.f : (y - ya) / (yb - ya);
    t = clampf(t, 0.f, 1.f);
    xo = xa + (xb - xa) * t;
    zo = za + (zb - za) * t;
  };

  for (int y = minY; y <= maxY; ++y) {
    float ys = y + 0.5f;
    float xa, za, xb, zRight;
    if (ys < y1) edge(ys, y0, y1, x0, x1, z0, z1, xa, za);
    else edge(ys, y1, y2, x1, x2, z1, z2, xa, za);
    edge(ys, y0, y2, x0, x2, z0, z2, xb, zRight);
    if (xa > xb) {
      float t;
      t = xa; xa = xb; xb = t;
      t = za; za = zRight; zRight = t;
    }
    int xStart = (int)ceilf(xa - 0.5f);
    int xEnd = (int)ceilf(xb - 0.5f) - 1;
    if (xStart < 0) xStart = 0;
    if (xEnd >= viewW) xEnd = viewW - 1;
    float span = xb - xa;
    if (xEnd < xStart || span < 1e-4f) continue;
    float dz = (zRight - za) / span;
    int zi = (int)((za + dz * ((xStart + 0.5f) - xa)) * kDepthScale);
    int dzi = (int)(dz * kDepthScale);
    uint16_t *rowC = fb + y * stride;
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
  sprite.pushSprite(0, 0);
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
  sprite.pushSprite(0, 0);
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

static void drawNoModels() {
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
  sprite.pushSprite(0, 0);
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
  lcd.fillRect(0, 170, screenW, 140, TFT_BLACK);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setTextSize(3);
  lcd.setCursor(60, 180);
  lcd.print(status);
  lcd.drawRect(60, 240, screenW - 120, 36, TFT_WHITE);
  lcd.fillRect(64, 244, (screenW - 128) * percent / 100, 28, lcd.color565(240, 108, 155));
}

static void renderFrame(float yaw) {
  if (animalCount == 0) {
    drawNoModels();
    return;
  }
  const AnimalMesh &animal = animals[animalIndex];
  memset(fb, 0, (size_t)stride * viewH * sizeof(uint16_t));
  memset(zb, 0, (size_t)stride * viewH * sizeof(uint16_t));

  float c = cosf(yaw);
  float s = sinf(yaw);
  for (int i = 0; i < animal.vertexCount; ++i) {
    const AnimalVertex &v = animal.vertices[i];
    float x = c * v.x + s * v.z;
    float y = v.y;
    float z = -s * v.x + c * v.z;
    rxv[i] = x;
    ryv[i] = y;
    rzv[i] = z;
    float dx = x - animal.camX;
    float dy = y - animal.camY;
    float dz = z - animal.camZ;
    float cx = dx * camRight[0] + dy * camRight[1] + dz * camRight[2];
    float cy = dx * camUp[0] + dy * camUp[1] + dz * camUp[2];
    float cz = dx * camForward[0] + dy * camForward[1] + dz * camForward[2];
    if (cz < kNearClip) {
      svOk[i] = 0;
      continue;
    }
    svOk[i] = 1;
    float px = (cx / cz) * viewFocal;
    float py = (cy / cz) * viewFocal;
    if (holoMode) {
      if (holoRotate) px = -px;
      else py = -py;
    }
    sx[i] = viewW * 0.5f + px;
    sy[i] = viewH * 0.5f - py;
    sz[i] = 1.f / cz;
  }

  for (int t = 0; t < animal.triangleCount; ++t) {
    const AnimalTriangle &tri = animal.triangles[t];
    if (!svOk[tri.a] || !svOk[tri.b] || !svOk[tri.c]) continue;

    float ax = rxv[tri.a], ay = ryv[tri.a], az = rzv[tri.a];
    float bx = rxv[tri.b], by = ryv[tri.b], bz = rzv[tri.b];
    float cxv = rxv[tri.c], cyv = ryv[tri.c], czv = rzv[tri.c];

    float e1[3] = {bx - ax, by - ay, bz - az};
    float e2[3] = {cxv - ax, cyv - ay, czv - az};
    float n[3];
    cross3(e1, e2, n);
    float mid[3] = {(ax + bx + cxv) / 3.f, (ay + by + cyv) / 3.f, (az + bz + czv) / 3.f};
    float view[3] = {animal.camX - mid[0], animal.camY - mid[1], animal.camZ - mid[2]};
    normalize3(view);
    if (!(tri.flags & kFlagDouble) && dot3(n, view) <= 0.f) continue;
    normalize3(n);

    uint16_t color = shade(tri.red, tri.green, tri.blue, n, tri.flags, view);
    raster(sx[tri.a], sy[tri.a], sz[tri.a], sx[tri.b], sy[tri.b], sz[tri.b], sx[tri.c], sy[tri.c], sz[tri.c], color);
  }

  drawAnimalsButton();
  sprite.pushSprite(0, 0);
}

void setup() {
  // Large enough for a whole SD upload chunk (see serial_upload.h).
  Serial.setRxBufferSize(8192);
  Serial.begin(115200);
  delay(200);
  Serial.println("unicorn boot");
  serialUploadSetup();

  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(255);
  lcd.fillScreen(TFT_BLACK);

  screenW = lcd.width();
  screenH = lcd.height();
  Serial.printf("panel %dx%d\n", screenW, screenH);

  sprite.setPsram(true);
  sprite.setColorDepth(16);
  if (!sprite.createSprite(screenW, screenH)) {
    Serial.println("sprite alloc failed");
    while (true) delay(1000);
  }
  fb = (uint16_t *)sprite.getBuffer();
  probeColorLayout();

  viewW = screenW;
  viewH = screenH;
  setupCamera();
  if (stride < screenW) stride = screenW;
  fb = (uint16_t *)sprite.getBuffer();
  zb = (uint16_t *)heap_caps_malloc((size_t)stride * screenH * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!fb || !zb) {
    Serial.println("framebuffer alloc failed");
    while (true) delay(1000);
  }
  Serial.printf("render %dx%d focal %.1f\n", viewW, viewH, viewFocal);
  prefs.begin("unicorn", false);
  holoMode = prefs.getBool("holo", false);
  holoRotate = prefs.getBool("holoRot", false);
  Serial.printf("hologram %s\n", holoMode ? "on" : "off");
  startTouch();
  if (startSd()) {
    installFirmwareFromSd(showInstallProgress);
    lcd.fillScreen(TFT_BLACK);
    loadAnimals();
  }
  modelsChanged();
  videoSetup();
  Serial.println("unicorn spinning");
}

void loop() {
  static uint32_t frames = 0;
  static uint32_t windowStart = millis();
  serialUploadPoll();
  if (videoPoll()) return;
  handleTouch();
  if (!started && !holoMode) {
    drawStartScreen();
    delay(16);
    return;
  }
  if (menuOpen && !holoMode) {
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
    Serial.printf("fps %.1f last %lu ms\n", frames / sec, (unsigned long)(now - t0));
    frames = 0;
    windowStart = now;
  }
}
