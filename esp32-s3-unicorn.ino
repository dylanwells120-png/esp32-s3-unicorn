// Slowly rotating low-poly unicorn for the Makerfabs MaTouch ESP32-S3
// 4.3" 800x480 RGB touchscreen (GT911, CP2104 UART, 16MB flash, 8MB PSRAM).

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "animals.h"

static constexpr uint8_t kFlagDouble = 1;
static constexpr uint8_t kFlagSpecular = 2;

static constexpr int kBacklightPin = 44;

class LGFX : public lgfx::LGFX_Device {
 public:
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_GT911 _touch_instance;

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

    {
      auto cfg = _touch_instance.config();
      // MaTouch 4.3 reports touch with X and Y mirrored against the panel.
      cfg.x_min = 800;
      cfg.x_max = 0;
      cfg.y_min = 480;
      cfg.y_max = 0;
      cfg.pin_int = -1;
      cfg.pin_rst = 38;
      cfg.pin_sda = 17;
      cfg.pin_scl = 18;
      cfg.i2c_port = 1;
      cfg.i2c_addr = 0x5D;
      cfg.freq = 400000;
      cfg.bus_shared = false;
      cfg.offset_rotation = 0;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }
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
static bool touchWasDown = false;
static bool swapBytes = false;

static float camRight[3];
static float camUp[3];
static float camForward[3];
static float keyLight[3];
static float fillLight[3];

static float sx[kMaxAnimalVertices];
static float sy[kMaxAnimalVertices];
static float sz[kMaxAnimalVertices];
static float rxv[kMaxAnimalVertices];
static float ryv[kMaxAnimalVertices];
static float rzv[kMaxAnimalVertices];
static uint8_t svOk[kMaxAnimalVertices];

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
  const AnimalMesh &animal = kAnimals[animalIndex];
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
  int minY = (int)ceilf(y0);
  int maxY = (int)floorf(y2);
  if (minY < 0) minY = 0;
  if (maxY >= viewH) maxY = viewH - 1;
  if (maxY < minY || (y2 - y0) < 0.5f) return;

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
    int xStart = (int)ceilf(xa);
    int xEnd = (int)floorf(xb);
    if (xStart < 0) xStart = 0;
    if (xEnd >= viewW) xEnd = viewW - 1;
    float span = xb - xa;
    if (xEnd < xStart || span < 1e-4f) continue;
    float dz = (zRight - za) / span;
    int zi = (int)((za + dz * ((xStart + 0.5f) - xa)) * 8000.f);
    int dzi = (int)(dz * 8000.f);
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

static void drawAnimalsButton() {
  sprite.fillRoundRect(16, 16, 176, 54, 14, sprite.color565(18, 18, 22));
  sprite.drawRoundRect(16, 16, 176, 54, 14, TFT_WHITE);
  sprite.setTextSize(2);
  sprite.setTextColor(TFT_WHITE);
  sprite.setCursor(44, 34);
  sprite.print("Animals");
  sprite.setCursor(screenW - 16 - (int)strlen(kAnimals[animalIndex].name) * 12, 34);
  sprite.print(kAnimals[animalIndex].name);
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
  for (int i = 0; i < kAnimalCount; ++i) {
    bool selected = i == animalIndex;
    uint16_t fill = selected ? sprite.color565(46, 46, 54) : sprite.color565(20, 20, 24);
    uint16_t edge = selected ? TFT_WHITE : sprite.color565(78, 78, 86);
    sprite.fillRoundRect(bx, top, bw, bh, 14, fill);
    sprite.drawRoundRect(bx, top, bw, bh, 14, edge);
    sprite.setTextSize(3);
    sprite.setTextColor(TFT_WHITE);
    sprite.setCursor(bx + 28, top + 20);
    sprite.print(kAnimals[i].name);
    top += bh + gap;
  }
  sprite.pushSprite(0, 0);
}

static bool hit(int tx, int ty, int x, int y, int w, int h) {
  return tx >= x && tx < x + w && ty >= y && ty < y + h;
}

static void handleTouch() {
  uint16_t tx = 0;
  uint16_t ty = 0;
  bool down = lcd.getTouch(&tx, &ty) > 0;
  bool pressed = down && !touchWasDown;
  touchWasDown = down;
  if (!pressed) return;
  Serial.printf("touch %u %u menu %d\n", tx, ty, menuOpen ? 1 : 0);

  if (!menuOpen) {
    if (hit(tx, ty, 16, 16, 176, 54)) menuOpen = true;
    return;
  }

  const int bx = 36;
  const int bw = 728;
  const int bh = 66;
  const int gap = 10;
  int top = 84;
  for (int i = 0; i < kAnimalCount; ++i) {
    if (hit(tx, ty, bx, top, bw, bh)) {
      animalIndex = i;
      setupCamera();
      menuOpen = false;
      Serial.printf("animal %s\n", kAnimals[i].name);
      return;
    }
    top += bh + gap;
  }
}

static void renderFrame(float yaw) {
  const AnimalMesh &animal = kAnimals[animalIndex];
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
    if (cz < 0.08f) {
      svOk[i] = 0;
      continue;
    }
    svOk[i] = 1;
    sx[i] = viewW * 0.5f + (cx / cz) * viewFocal;
    sy[i] = viewH * 0.5f - (cy / cz) * viewFocal;
    sz[i] = cz;
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
  Serial.begin(115200);
  delay(200);
  Serial.println("unicorn boot");

  setupCamera();
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
  Serial.println("unicorn spinning");
}

void loop() {
  static uint32_t frames = 0;
  static uint32_t windowStart = millis();
  handleTouch();
  if (menuOpen) {
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
