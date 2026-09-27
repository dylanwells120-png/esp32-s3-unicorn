#pragma once
// microSD storage: 3D models, the web page, and firmware updates.
//
// Card layout (see sd/unicorn/ in the repo):
//   /unicorn/animals.bin   meshes written by tools/build_mesh.py
//   /unicorn/index.html    the web app served at "/"
//   /unicorn/firmware.bin  installed on the next boot, then renamed firmware.done

#include <SD.h>
#include <SPI.h>
#include <Update.h>
#include <esp_heap_caps.h>

#include "animals.h"

// MaTouch 4.3" microSD slot.
static constexpr int kSdCs = 10;
static constexpr int kSdMosi = 11;
static constexpr int kSdSck = 12;
static constexpr int kSdMiso = 13;

static constexpr const char *kSdDir = "/unicorn";
static constexpr const char *kModelsPath = "/unicorn/animals.bin";
static constexpr const char *kPagePath = "/unicorn/index.html";
static constexpr const char *kFirmwarePath = "/unicorn/firmware.bin";
static constexpr const char *kFirmwareDonePath = "/unicorn/firmware.done";
static constexpr const char *kFirmwareBadPath = "/unicorn/firmware.bad";
static constexpr uint16_t kModelVersion = 1;

static bool sdReady = false;
static AnimalMesh animals[kMaxAnimals];
static int animalCount = 0;
static int maxAnimalVertices = 0;
static String modelError = "no SD card";

static bool startSd() {
  SPI.begin(kSdSck, kSdMiso, kSdMosi, kSdCs);
  sdReady = SD.begin(kSdCs, SPI, 20000000);
  if (!sdReady) {
    Serial.println("sd missing");
    return false;
  }
  if (!SD.exists(kSdDir)) SD.mkdir(kSdDir);
  Serial.printf("sd ready %llu MB\n", SD.cardSize() / (1024 * 1024));
  return true;
}

static void freeAnimals() {
  for (int i = 0; i < animalCount; ++i) {
    heap_caps_free((void *)animals[i].vertices);
    heap_caps_free((void *)animals[i].triangles);
  }
  memset(animals, 0, sizeof(animals));
  animalCount = 0;
  maxAnimalVertices = 0;
}

static bool readExact(File &f, void *dst, size_t len) {
  return f.read((uint8_t *)dst, len) == len;
}

// Replaces the loaded meshes with the ones in /unicorn/animals.bin.
static bool loadAnimals() {
  freeAnimals();
  if (!sdReady) {
    modelError = "no SD card";
    return false;
  }
  File f = SD.open(kModelsPath, FILE_READ);
  if (!f) {
    modelError = "animals.bin missing";
    Serial.println("models: /unicorn/animals.bin missing");
    return false;
  }
  char magic[4];
  uint16_t version = 0;
  uint16_t count = 0;
  bool ok = readExact(f, magic, 4) && memcmp(magic, "ANIM", 4) == 0 && readExact(f, &version, 2) &&
            version == kModelVersion && readExact(f, &count, 2);
  if (!ok) {
    f.close();
    modelError = "animals.bin is not a model file";
    Serial.printf("models: %s\n", modelError.c_str());
    return false;
  }
  if (count > kMaxAnimals) count = kMaxAnimals;
  for (int i = 0; ok && i < count; ++i) {
    AnimalMesh &m = animals[i];
    uint32_t verts = 0;
    uint32_t tris = 0;
    float cam[7];
    ok = readExact(f, m.name, sizeof(m.name)) && readExact(f, cam, sizeof(cam)) && readExact(f, &verts, 4) &&
         readExact(f, &tris, 4) && verts > 0 && verts <= 65535 && tris > 0;
    if (!ok) break;
    m.name[sizeof(m.name) - 1] = 0;
    m.focal = cam[0];
    m.camX = cam[1];
    m.camY = cam[2];
    m.camZ = cam[3];
    m.targetX = cam[4];
    m.targetY = cam[5];
    m.targetZ = cam[6];
    AnimalVertex *v = (AnimalVertex *)heap_caps_malloc(verts * sizeof(AnimalVertex), MALLOC_CAP_SPIRAM);
    AnimalTriangle *t = (AnimalTriangle *)heap_caps_malloc(tris * sizeof(AnimalTriangle), MALLOC_CAP_SPIRAM);
    m.vertices = v;
    m.triangles = t;
    animalCount = i + 1;
    ok = v && t && readExact(f, v, verts * sizeof(AnimalVertex)) && readExact(f, t, tris * sizeof(AnimalTriangle));
    if (!ok) break;
    for (uint32_t k = 0; k < tris; ++k) {
      if (t[k].a >= verts || t[k].b >= verts || t[k].c >= verts) ok = false;
    }
    m.vertexCount = (int)verts;
    m.triangleCount = (int)tris;
    if ((int)verts > maxAnimalVertices) maxAnimalVertices = (int)verts;
    Serial.printf("models: %s verts=%u tris=%u\n", m.name, (unsigned)verts, (unsigned)tris);
  }
  f.close();
  if (!ok || animalCount == 0) {
    modelError = ok ? "animals.bin has no animals" : "animals.bin is damaged";
    Serial.printf("models: %s\n", modelError.c_str());
    freeAnimals();
    return false;
  }
  modelError = "";
  return true;
}

static void replaceFile(const char *from, const char *to) {
  if (SD.exists(to)) SD.remove(to);
  SD.rename(from, to);
}

// Installs /unicorn/firmware.bin if present and restarts. `progress` gets 0-100.
static void installFirmwareFromSd(void (*progress)(int percent, const char *status)) {
  if (!sdReady || !SD.exists(kFirmwarePath)) return;
  File f = SD.open(kFirmwarePath, FILE_READ);
  size_t size = f ? f.size() : 0;
  Serial.printf("firmware: installing %u bytes from SD\n", (unsigned)size);
  // Every ESP32 app image starts with 0xE9.
  if (size == 0 || f.peek() != 0xE9 || !Update.begin(size)) {
    Serial.printf("firmware: rejected (%s)\n", Update.errorString());
    if (f) f.close();
    replaceFile(kFirmwarePath, kFirmwareBadPath);
    progress(0, "Firmware file rejected");
    delay(2000);
    return;
  }
  static uint8_t buf[4096];
  size_t done = 0;
  int shown = -1;
  while (done < size) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0 || Update.write(buf, n) != (size_t)n) break;
    done += n;
    int pct = (int)(done * 100 / size);
    if (pct != shown) {
      shown = pct;
      progress(pct, "Installing firmware from SD card");
    }
  }
  f.close();
  bool ok = done == size && Update.end(true);
  if (!ok) {
    Serial.printf("firmware: failed (%s)\n", Update.errorString());
    Update.abort();
    replaceFile(kFirmwarePath, kFirmwareBadPath);
    progress(0, "Firmware install failed");
    delay(2000);
    return;
  }
  replaceFile(kFirmwarePath, kFirmwareDonePath);
  Serial.println("firmware: installed, restarting");
  progress(100, "Firmware installed. Restarting");
  delay(800);
  ESP.restart();
}

// ---------------------------------------------------------------------------
// Uploads. Only these names may be written, always inside /unicorn.

static bool sdPathFor(const String &name, String &path) {
  if (name != "animals.bin" && name != "index.html" && name != "firmware.bin") return false;
  path = String(kSdDir) + "/" + name;
  return true;
}

struct SdUpload {
  File file;
  String path;
  String tmpPath;
  size_t bytes = 0;
  bool ok = false;
};

static bool sdUploadBegin(SdUpload &up, const String &name) {
  up.ok = false;
  up.bytes = 0;
  if (!sdReady || !sdPathFor(name, up.path)) return false;
  up.tmpPath = up.path + ".part";
  if (SD.exists(up.tmpPath)) SD.remove(up.tmpPath);
  up.file = SD.open(up.tmpPath, FILE_WRITE);
  up.ok = (bool)up.file;
  return up.ok;
}

static bool sdUploadWrite(SdUpload &up, const uint8_t *data, size_t len) {
  if (!up.ok) return false;
  if (up.file.write(data, len) != len) {
    up.ok = false;
    return false;
  }
  up.bytes += len;
  return true;
}

static void sdUploadAbort(SdUpload &up) {
  if (up.file) up.file.close();
  if (up.tmpPath.length() && SD.exists(up.tmpPath)) SD.remove(up.tmpPath);
  up.ok = false;
}

// Moves the finished upload into place. Returns false if anything went wrong.
static bool sdUploadEnd(SdUpload &up) {
  if (!up.ok || up.bytes == 0) {
    sdUploadAbort(up);
    return false;
  }
  up.file.close();
  replaceFile(up.tmpPath.c_str(), up.path.c_str());
  Serial.printf("sd: wrote %s (%u bytes)\n", up.path.c_str(), (unsigned)up.bytes);
  return true;
}
