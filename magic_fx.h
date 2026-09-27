#pragma once
// Magic effects for hologram mode: glowing comets that fly around the animal
// trailing light, sparkles shed from the trails, and a ring of light spinning
// on the floor. Each animal has its own theme.
//
// Black is see-through in the hologram box, so everything here is added on top
// of the frame as light. Effects are depth tested against the animal, so comets
// disappear behind it. Every path is a function of time, so trails stay smooth
// at any frame rate and nothing is kept between frames.

enum MagicPath : uint8_t {
  kPathOrbit,     // tilted orbits, like electrons
  kPathFlame,     // spirals rising from the floor and fading out at the top
  kPathFirefly,   // wandering loops through and around the body
  kPathFigure8,   // low figure-eights around the body
  kPathDrift,     // wide slow circles that rise and fall
};

enum MagicSparkle : uint8_t {
  kSparkleGlitter,  // four-point stars that drift down
  kSparkleEmber,    // sparks that float up and flicker
  kSparkleSnow,     // flakes that fall and sway
  kSparkleBubble,   // rings that rise and wobble
  kSparkleStar,     // stars that twinkle in place
};

struct MagicTheme {
  const char *animal;
  MagicPath path;
  MagicSparkle sparkle;
  uint8_t tracers;
  float speed;
  bool rainbow;  // cycle through the rainbow instead of using colors
  uint8_t colors[3][3];
};

// Matched by animal name; anything else gets the first theme.
static const MagicTheme kMagicThemes[] = {
    {"Unicorn", kPathOrbit, kSparkleGlitter, 6, 1.0f, true, {{255, 120, 210}, {120, 200, 255}, {255, 230, 120}}},
    {"Fox", kPathFlame, kSparkleEmber, 6, 1.0f, false, {{255, 80, 20}, {255, 160, 30}, {255, 225, 110}}},
    {"Penguin", kPathDrift, kSparkleSnow, 5, 0.8f, false, {{110, 220, 255}, {200, 245, 255}, {90, 140, 255}}},
    {"Turtle", kPathFigure8, kSparkleBubble, 4, 0.9f, false, {{50, 255, 190}, {70, 190, 255}, {170, 255, 110}}},
    {"Owl", kPathFirefly, kSparkleStar, 7, 0.9f, false, {{255, 205, 70}, {185, 110, 255}, {110, 140, 255}}},
};

static constexpr int kTrailSamples = 16;      // points along each trail
static constexpr float kTrailStep = 0.04f;    // seconds between them
static constexpr int kSparklesPerTracer = 8;  // alive at once per comet
static constexpr float kSparkleEvery = 0.11f;  // seconds between new sparkles
static constexpr int kRingDots = 36;
// Glow sizes are in pixels at full resolution; halved when drawing at 400x240.
static float fxScale = 1.f;

struct MagicColor {
  float r, g, b;
};

static const MagicTheme *magicTheme = &kMagicThemes[0];
// Where the effects fly, in world units, from the current animal's shape.
static float magicCx = 0.f, magicCz = 0.f, magicFloor = 0.f, magicTop = 1.f, magicRadius = 1.f;
// Radius of a sphere around the animal and its effects, for the 8-bit depth range.
static float magicBound = 2.f;

// Picks the theme for `animal` and sizes the paths to it. Call after the camera is set.
static void magicPrepare(const AnimalMesh &animal, float cameraDistance) {
  magicTheme = &kMagicThemes[0];
  for (const MagicTheme &theme : kMagicThemes) {
    if (strcasecmp(theme.animal, animal.name) == 0) magicTheme = &theme;
  }
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < animal.vertexCount; ++i) {
    lo = fminf(lo, animal.vertices[i].y);
    hi = fmaxf(hi, animal.vertices[i].y);
  }
  // Measure the body, not the floor shadow, which sits just above the lowest point.
  float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
  for (int i = 0; i < animal.vertexCount; ++i) {
    const AnimalVertex &v = animal.vertices[i];
    if (v.y < lo + 0.03f) continue;
    x0 = fminf(x0, v.x);
    x1 = fmaxf(x1, v.x);
    z0 = fminf(z0, v.z);
    z1 = fmaxf(z1, v.z);
  }
  magicCx = (x0 + x1) * 0.5f;
  magicCz = (z0 + z1) * 0.5f;
  magicFloor = lo;
  magicTop = hi;
  float height = hi - lo;
  float body = 0.f;
  for (int i = 0; i < animal.vertexCount; ++i) {
    const AnimalVertex &v = animal.vertices[i];
    if (v.y < lo + 0.03f) continue;
    body = fmaxf(body, hypotf(v.x - magicCx, v.z - magicCz));
  }
  // Wide enough to circle the body, narrow enough to stay on screen.
  float room = 0.44f * viewW / viewFocal * cameraDistance * 0.85f;
  magicRadius = fminf(fmaxf(body * 0.95f, height * 0.42f), room);
  magicBound = sqrtf(fmaxf(body, magicRadius) * fmaxf(body, magicRadius) + height * height * 0.36f) * 1.1f + 0.1f;
}

static inline float frac(float v) { return v - floorf(v); }

static inline uint32_t magicHash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static MagicColor rainbow(float h) {
  h = frac(h) * 6.f;
  return {255.f * clampf(fabsf(h - 3.f) - 1.f, 0.f, 1.f), 255.f * clampf(2.f - fabsf(h - 2.f), 0.f, 1.f),
          255.f * clampf(2.f - fabsf(h - 4.f), 0.f, 1.f)};
}

static MagicColor themeColor(int index) {
  const uint8_t *c = magicTheme->colors[index % 3];
  return {(float)c[0], (float)c[1], (float)c[2]};
}

static MagicColor towardWhite(MagicColor c, float amount) {
  return {c.r + (255.f - c.r) * amount, c.g + (255.f - c.g) * amount, c.b + (255.f - c.b) * amount};
}

// Position of comet `i` at time `t`. `life` runs 0 to 1 for paths that restart
// (flames), and is 0.5 for the rest.
static void magicPath(int i, float t, float &x, float &y, float &z, float &life) {
  const MagicTheme &th = *magicTheme;
  float n = th.tracers;
  float phase = i * 6.2831853f / n;
  float dir = (i & 1) ? 1.f : -1.f;
  float h = magicTop - magicFloor;
  float mid = magicFloor + h * 0.5f;
  float r = magicRadius;
  float s = th.speed;
  life = 0.5f;
  switch (th.path) {
    case kPathOrbit: {
      float a = dir * s * (1.5f + 0.25f * (i % 3)) * t + phase;
      x = magicCx + r * cosf(a);
      z = magicCz + r * 0.85f * sinf(a);
      y = mid + h * 0.38f * sinf(a + i * 1.3f);  // height follows the angle, so the orbit is tilted
      break;
    }
    case kPathFlame: {
      float u = frac(t * s / 2.4f + i / n);
      float rr = r * (0.95f - 0.7f * u);
      float a = s * 1.5f * t + phase + u * 4.f;
      x = magicCx + rr * cosf(a);
      z = magicCz + rr * sinf(a);
      y = magicFloor + u * h * 1.05f;
      life = u;
      break;
    }
    case kPathFirefly:
      x = magicCx + r * 0.95f * sinf(s * 0.9f * t + phase) * cosf(0.37f * t + 2.f * phase);
      y = mid + h * 0.45f * sinf(s * 1.3f * t + 1.7f * phase);
      z = magicCz + r * 0.85f * cosf(s * 0.7f * t + 1.3f * phase);
      break;
    case kPathFigure8: {
      float a = s * 1.1f * t + phase;
      x = magicCx + r * sinf(a);
      z = magicCz + r * 0.75f * sinf(2.f * a);
      y = magicFloor + h * (0.45f + 0.35f * sinf(a * 0.5f + i));
      break;
    }
    case kPathDrift:
    default: {
      float a = dir * s * t + phase;
      x = magicCx + r * cosf(a);
      z = magicCz + r * 0.9f * sinf(a);
      y = magicFloor + h * (0.5f + 0.42f * sinf(0.45f * t + i * 1.7f));
      break;
    }
  }
}

// Paths that restart fade in at the bottom and out at the top.
static inline float lifeFade(float life) { return magicTheme->path == kPathFlame ? sinf(3.1415927f * life) : 1.f; }

// Adds light to one pixel in the band unless the animal is in front of it.
static inline void addLight(const Band &band, int x, int y, uint16_t depth, MagicColor c, float amount) {
  if ((unsigned)x >= (unsigned)viewW || y < band.y0 || y > band.y1) return;
  int i = y * stride + x;
  if (depth8 ? zb8[i] > depth : zb[i] > depth) return;
  uint16_t px = fb[i];
  if (swapBytes) px = (uint16_t)((px >> 8) | (px << 8));
  int r = (px >> 11) + (int)(c.r * amount * (31.f / 255.f) + 0.5f);
  int g = ((px >> 5) & 63) + (int)(c.g * amount * (63.f / 255.f) + 0.5f);
  int b = (px & 31) + (int)(c.b * amount * (31.f / 255.f) + 0.5f);
  px = (uint16_t)((r > 31 ? 31 : r) << 11 | (g > 63 ? 63 : g) << 5 | (b > 31 ? 31 : b));
  if (swapBytes) px = (uint16_t)((px >> 8) | (px << 8));
  fb[i] = px;
}

// Soft round glow, brightest in the middle.
static void glowDot(Band &band, float cx, float cy, uint16_t depth, float radius, MagicColor c, float amount) {
  if (amount <= 0.01f) return;
  radius = fmaxf(radius * fxScale, 0.8f);
  int x0 = (int)floorf(cx - radius), x1 = (int)ceilf(cx + radius);
  int y0 = max((int)floorf(cy - radius), band.y0), y1 = min((int)ceilf(cy + radius), band.y1);
  if (y1 < y0) return;
  growBox(band.box, x0, y0, x1, y1);
  float inv = 1.f / (radius * radius);
  for (int y = y0; y <= y1; ++y) {
    float dy = y + 0.5f - cy;
    for (int x = x0; x <= x1; ++x) {
      float dx = x + 0.5f - cx;
      float f = 1.f - (dx * dx + dy * dy) * inv;
      if (f > 0.f) addLight(band, x, y, depth, c, amount * f * f);
    }
  }
}

static void sparkleStar(Band &band, float cx, float cy, uint16_t depth, int arm, MagicColor c, float amount) {
  glowDot(band, cx, cy, depth, 1.8f, c, amount);
  arm = (int)(arm * fxScale + 0.5f);
  int x = (int)cx, y = (int)cy;
  growBox(band.box, x - arm, y - arm, x + arm, y + arm);
  for (int d = 1; d <= arm; ++d) {
    float f = amount * (1.f - d / (arm + 1.f));
    addLight(band, x + d, y, depth, c, f);
    addLight(band, x - d, y, depth, c, f);
    addLight(band, x, y + d, depth, c, f);
    addLight(band, x, y - d, depth, c, f);
  }
}

// Line whose brightness falls off across its width. Much cheaper than a row of glowDots.
static void glowLine(Band &band, float x0, float y0, uint16_t d0, float x1, float y1, uint16_t d1, int halfWidth, MagicColor c,
                     float amount) {
  if (amount <= 0.01f) return;
  halfWidth = (int)(halfWidth * fxScale + 0.5f);
  if (fmaxf(y0, y1) + halfWidth < band.y0 || fminf(y0, y1) - halfWidth > band.y1) return;
  float dx = x1 - x0, dy = y1 - y0;
  bool steep = fabsf(dy) > fabsf(dx);
  int steps = (int)ceilf(fmaxf(fabsf(dx), fabsf(dy)));
  if (steps < 1) steps = 1;
  growBox(band.box, (int)fminf(x0, x1) - halfWidth - 1, (int)fminf(y0, y1) - halfWidth - 1, (int)fmaxf(x0, x1) + halfWidth + 1,
          (int)fmaxf(y0, y1) + halfWidth + 1);
  for (int s = 0; s < steps; ++s) {
    float u = (float)s / steps;
    int x = (int)(x0 + dx * u), y = (int)(y0 + dy * u);
    uint16_t d = (uint16_t)(d0 + (int)((d1 - d0) * u));
    addLight(band, x, y, d, c, amount);
    for (int o = 1; o <= halfWidth; ++o) {
      float f = amount * (1.f - o / (halfWidth + 1.f));
      f *= f;
      if (steep) {
        addLight(band, x + o, y, d, c, f);
        addLight(band, x - o, y, d, c, f);
      } else {
        addLight(band, x, y + o, d, c, f);
        addLight(band, x, y - o, d, c, f);
      }
    }
  }
}

static void bubbleRing(Band &band, float cx, float cy, uint16_t depth, float radius, MagicColor c, float amount) {
  radius *= fxScale;
  growBox(band.box, (int)(cx - radius) - 1, (int)(cy - radius) - 1, (int)(cx + radius) + 1, (int)(cy + radius) + 1);
  for (int k = 0; k < 20; ++k) {
    float a = k * 6.2831853f / 20.f;
    // Brighter on the upper left, like light catching a bubble.
    float shine = 0.55f + 0.45f * cosf(a - 2.4f);
    addLight(band, (int)(cx + radius * cosf(a)), (int)(cy + radius * sinf(a)), depth, c, amount * shine);
  }
}

static inline uint16_t depthValue(float invz) {
  if (depth8) {
    float v = (invz - depth8Min) * depth8Scale + 1.f;
    return (uint16_t)(v < 1.f ? 1.f : (v > 255.f ? 255.f : v));
  }
  float d = invz * kDepthScale;
  return (uint16_t)(d > 65535.f ? 65535.f : d);
}

static void drawTracer(Band &band, const AnimalMesh &animal, int i, float t) {
  float px[kTrailSamples], py[kTrailSamples], iz[kTrailSamples], life[kTrailSamples];
  bool ok[kTrailSamples];
  for (int k = 0; k < kTrailSamples; ++k) {
    float x, y, z;
    magicPath(i, t - k * kTrailStep, x, y, z, life[k]);
    ok[k] = projectPoint(animal, x, y, z, px[k], py[k], iz[k]);
  }
  bool rainbowTheme = magicTheme->rainbow;
  // Tail first, so the bright head is drawn last.
  for (int k = kTrailSamples - 2; k >= 0; --k) {
    if (!ok[k] || !ok[k + 1]) continue;
    if (life[k] < life[k + 1]) continue;  // the path restarted between these samples
    float near = 1.f - (float)k / kTrailSamples;
    float amount = 0.9f * near * near * lifeFade(life[k]);
    MagicColor c = rainbowTheme ? rainbow(t * 0.12f + (float)i / magicTheme->tracers + k * 0.012f) : themeColor(i);
    c = towardWhite(c, 0.45f * near * near * near);
    uint16_t d0 = depthValue(iz[k + 1]), d1 = depthValue(iz[k]);
    glowLine(band, px[k + 1], py[k + 1], d0, px[k], py[k], d1, (int)(near * 3.2f), c, amount);
    // Soft halo around the brightest part of the trail.
    if (k < 4) glowLine(band, px[k + 1], py[k + 1], d0, px[k], py[k], d1, 6, c, 0.3f * near);
  }
  if (!ok[0]) return;
  float fade = lifeFade(life[0]);
  MagicColor c = rainbowTheme ? rainbow(t * 0.12f + (float)i / magicTheme->tracers) : themeColor(i);
  uint16_t depth = depthValue(iz[0]);
  glowDot(band, px[0], py[0], depth, 13.f, c, 0.6f * fade);
  sparkleStar(band, px[0], py[0], depth, 9, towardWhite(c, 0.5f), 0.9f * fade);
  glowDot(band, px[0], py[0], depth, 3.5f, towardWhite(c, 0.75f), 1.f * fade);
}

static void drawSparkles(Band &band, const AnimalMesh &animal, int i, float t) {
  const MagicTheme &th = *magicTheme;
  float h = magicTop - magicFloor;
  float lifetime = kSparklesPerTracer * kSparkleEvery;
  int newest = (int)floorf(t / kSparkleEvery);
  for (int j = 0; j < kSparklesPerTracer; ++j) {
    int born = newest - j;
    float bornAt = born * kSparkleEvery;
    float age = t - bornAt;
    float lf = age / lifetime;
    uint32_t seed = magicHash((uint32_t)(i * 1013 + born * 7919));
    float r1 = (seed & 0xFFFF) / 65535.f;
    float r2 = ((seed >> 16) & 0xFFFF) / 65535.f;
    float x, y, z, life;
    magicPath(i, bornAt, x, y, z, life);
    float fade = lifeFade(life);
    if (fade < 0.1f) continue;
    x += (r1 - 0.5f) * magicRadius * 0.15f;
    z += (r2 - 0.5f) * magicRadius * 0.15f;
    float amount = (1.f - lf) * fade;
    switch (th.sparkle) {
      case kSparkleGlitter:
        y -= 0.25f * h * age;
        amount *= 0.55f + 0.45f * sinf(18.f * age + r1 * 6.28f);
        break;
      case kSparkleEmber:
        y += 0.35f * h * age;
        x += 0.05f * magicRadius * sinf(6.f * age + r2 * 6.28f);
        amount *= 0.6f + 0.4f * sinf(25.f * age + r1 * 6.28f);
        break;
      case kSparkleSnow:
        y -= 0.3f * h * age;
        x += 0.08f * magicRadius * sinf(3.f * age + r1 * 6.28f);
        break;
      case kSparkleBubble:
        y += 0.3f * h * age;
        x += 0.04f * magicRadius * sinf(8.f * age + r2 * 6.28f);
        break;
      case kSparkleStar:
        amount = sinf(3.1415927f * lf) * (0.5f + 0.5f * sinf(14.f * age + r1 * 6.28f)) * fade;
        break;
    }
    float sx, sy, iz;
    if (!projectPoint(animal, x, y, z, sx, sy, iz)) continue;
    MagicColor c;
    if (th.sparkle == kSparkleSnow) c = {215.f, 240.f, 255.f};
    else if (th.rainbow) c = rainbow(r1);
    else c = towardWhite(themeColor(seed >> 8), 0.35f);
    uint16_t depth = depthValue(iz);
    if (th.sparkle == kSparkleBubble) bubbleRing(band, sx, sy, depth, 2.5f + 2.f * r2, c, amount * 0.9f);
    else sparkleStar(band, sx, sy, depth, 3 + (int)(r2 * 4.f), c, amount);
  }
}

static void drawFloorRing(Band &band, const AnimalMesh &animal, float t) {
  float ringR = magicRadius * 0.9f;
  for (int k = 0; k < kRingDots; ++k) {
    float a = k * 6.2831853f / kRingDots + 0.35f * t;
    float wave = fmaxf(0.f, sinf(3.f * a - 2.5f * t));
    float amount = 0.2f + 0.8f * wave * wave;
    float sx, sy, iz;
    if (!projectPoint(animal, magicCx + ringR * cosf(a), magicFloor + 0.01f, magicCz + ringR * sinf(a), sx, sy, iz))
      continue;
    MagicColor c = magicTheme->rainbow ? rainbow((float)k / kRingDots + 0.1f * t) : themeColor(k);
    glowDot(band, sx, sy, depthValue(iz), 3.f, c, amount * 0.9f);
  }
}

static void drawMagic(Band &band, const AnimalMesh &animal) {
  fxScale = viewW < kPanelW ? 0.5f : 1.f;
  // Wrap hourly so float time keeps millisecond precision.
  float t = (millis() % 3600000UL) / 1000.f;
  drawFloorRing(band, animal, t);
  for (int i = 0; i < magicTheme->tracers; ++i) {
    drawSparkles(band, animal, i, t);
    drawTracer(band, animal, i, t);
  }
}
