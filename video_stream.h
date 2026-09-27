#pragma once
// Wi-Fi web app. The page at "/" comes from the SD card (/unicorn/index.html).
// It streams video to the screen and shows the 3D models. "/setup" is built
// into the firmware so an empty card can still be filled over Wi-Fi.
//
// Video frames arrive as JPEGs posted to /frame. The reply is sent after the
// frame is on screen, so the browser never queues frames.

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "sd_store.h"
#include "setup_page.h"

// Optional: create wifi_secrets.h (gitignored) with
//   #define WIFI_SSID "your network"
//   #define WIFI_PASSWORD "your password"
// to join your network. Without it the board starts its own network.
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif

static constexpr const char *kHostName = "unicorn";
static constexpr const char *kApName = "Unicorn-Display";
static constexpr const char *kApPassword = "unicorn123";
static constexpr size_t kMaxFrameBytes = 400 * 1024;
static constexpr uint32_t kVideoTimeoutMs = 2500;

static WebServer videoServer(80);
static uint8_t *videoFrame = nullptr;
static size_t videoFrameLen = 0;
static bool videoFrameOk = false;
static bool videoActive = false;
static uint32_t videoLastFrameMs = 0;
static uint32_t videoFrames = 0;
static uint32_t videoWindowStart = 0;
static SdUpload webUpload;
static bool webUploadStarted = false;

// NES pad from the web app. Bits match a standard controller byte:
// A, B, Select, Start, Up, Down, Left, Right.
static uint8_t nesPad = 0;
static bool nesPlaying = false;
static String nesRom;
static constexpr int kMaxNesRoms = 24;
static String nesRoms[kMaxNesRoms];
static int nesRomCount = 0;

static bool nesNameOk(const String &name) {
  if (name.length() < 5 || name.length() > 48) return false;
  if (!name.endsWith(".nes")) return false;
  for (unsigned i = 0; i < name.length(); ++i) {
    char c = name[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
              c == '-' || c == ' ';
    if (!ok) return false;
  }
  return true;
}

static void nesScanDir(const char *dir) {
  if (!sdReady || nesRomCount >= kMaxNesRoms) return;
  File d = SD.open(dir);
  if (!d) return;
  for (File f = d.openNextFile(); f && nesRomCount < kMaxNesRoms; f = d.openNextFile()) {
    if (f.isDirectory()) continue;
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (!nesNameOk(name)) continue;
    bool seen = false;
    for (int i = 0; i < nesRomCount; ++i)
      if (nesRoms[i] == name) seen = true;
    if (!seen) nesRoms[nesRomCount++] = name;
  }
}

static void nesScan() {
  nesRomCount = 0;
  nesScanDir("/");
  nesScanDir("/nes");
  nesScanDir("/unicorn");
}

static void nesDraw() {
  beginDraw();
  sprite.fillScreen(TFT_BLACK);
  sprite.setTextColor(TFT_WHITE);
  sprite.setTextSize(3);
  sprite.setCursor(24, 40);
  sprite.print("NES");
  sprite.setTextSize(2);
  sprite.setCursor(24, 100);
  sprite.print(nesRom);
  sprite.setTextSize(2);
  sprite.setCursor(24, 180);
  sprite.printf("A:%d B:%d Sel:%d Start:%d", (nesPad & 1) != 0, (nesPad & 2) != 0, (nesPad & 4) != 0,
                (nesPad & 8) != 0);
  sprite.setCursor(24, 220);
  sprite.printf("U:%d D:%d L:%d R:%d", (nesPad & 16) != 0, (nesPad & 32) != 0, (nesPad & 64) != 0,
                (nesPad & 128) != 0);
  sprite.setTextSize(1);
  sprite.setCursor(24, 280);
  sprite.print("Controller is the web page. This panel is not an Anemoia display,");
  sprite.setCursor(24, 300);
  sprite.print("so the ROM stays on the SD card until a matching emulator is running.");
  presentFrame();
}

static String videoAddress() {
  return (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP() : WiFi.localIP()).toString();
}

// Stretch a little-endian RGB565 frame to the whole panel. Nearest-neighbor,
// so a 256x240 game becomes full screen without a JPEG decode.
static void blitRgb565(const uint16_t *src, int sw, int sh) {
  const int dw = displayW, dh = displayH;
  const uint32_t xStep = ((uint32_t)sw << 16) / dw;
  const uint32_t yStep = ((uint32_t)sh << 16) / dh;
  const bool flipX = false;
  const bool flipY = false;
  uint16_t *dst = fb;
  uint32_t yAcc = 0;
  for (int y = 0; y < dh; ++y, yAcc += yStep) {
    int sy = yAcc >> 16;
    if (flipY) sy = sh - 1 - sy;
    const uint16_t *row = src + sy * sw;
    uint16_t *out = dst + y * dw;
    uint32_t xAcc = 0;
    if (!flipX) {
      for (int x = 0; x < dw; ++x, xAcc += xStep) out[x] = row[xAcc >> 16];
    } else {
      for (int x = 0; x < dw; ++x, xAcc += xStep) out[x] = row[sw - 1 - (xAcc >> 16)];
    }
  }
}

static bool frameMirror = false;
static bool frameUpside = false;

static void orientFrame() {
  const int dw = displayW, dh = displayH;
  if (!fb || (!frameMirror && !frameUpside)) return;
  if (frameUpside) {
    for (int y = 0; y < dh / 2; ++y) {
      uint16_t *a = fb + y * dw;
      uint16_t *b = fb + (dh - 1 - y) * dw;
      for (int x = 0; x < dw; ++x) {
        uint16_t t = a[x];
        a[x] = b[x];
        b[x] = t;
      }
    }
  }
  if (frameMirror) {
    for (int y = 0; y < dh; ++y) {
      uint16_t *row = fb + y * dw;
      for (int x = 0; x < dw / 2; ++x) {
        uint16_t t = row[x];
        row[x] = row[dw - 1 - x];
        row[dw - 1 - x] = t;
      }
    }
  }
}

static bool jpegSize(const uint8_t *p, size_t len, int *w, int *h) {
  for (size_t i = 0; i + 9 < len; ++i) {
    if (p[i] != 0xFF) continue;
    uint8_t marker = p[i + 1];
    if (marker != 0xC0 && marker != 0xC1 && marker != 0xC2) continue;
    *h = (p[i + 5] << 8) | p[i + 6];
    *w = (p[i + 7] << 8) | p[i + 8];
    return *w > 0 && *h > 0;
  }
  return false;
}

static void videoHandleFrameBody() {
  HTTPRaw &raw = videoServer.raw();
  if (raw.status == RAW_START) {
    videoFrameLen = 0;
    videoFrameOk = videoFrame != nullptr;
  } else if (raw.status == RAW_WRITE) {
    if (!videoFrameOk || videoFrameLen + raw.currentSize > kMaxFrameBytes) {
      videoFrameOk = false;
      return;
    }
    memcpy(videoFrame + videoFrameLen, raw.buf, raw.currentSize);
    videoFrameLen += raw.currentSize;
  } else if (raw.status == RAW_ABORTED) {
    videoFrameOk = false;
  }
}

static void videoHandleFrame() {
  if (!videoFrameOk || videoFrameLen == 0) {
    videoServer.send(413, "text/plain", "frame too large");
    return;
  }
  if (!videoActive) {
    videoActive = true;
    videoFrames = 0;
    videoWindowStart = millis();
    Serial.println("video start");
  }
  beginDraw();
  bool ok = true;
  bool looksJpeg = videoFrameLen >= 2 && videoFrame[0] == 0xFF && videoFrame[1] == 0xD8;
  int rw = videoServer.hasArg("w") ? videoServer.arg("w").toInt() : 0;
  int rh = videoServer.hasArg("h") ? videoServer.arg("h").toInt() : 0;
  if ((rw <= 0 || rh <= 0 || videoFrameLen < (size_t)rw * rh * 2) && !looksJpeg) {
    if (videoFrameLen == 128 * 120 * 2) { rw = 128; rh = 120; }
    else if (videoFrameLen == 256 * 240 * 2) { rw = 256; rh = 240; }
  }
  if (!looksJpeg && rw > 0 && rh > 0 && rw <= 800 && rh <= 480 && videoFrameLen >= (size_t)rw * rh * 2) {
    blitRgb565((const uint16_t *)videoFrame, rw, rh);
  } else if (looksJpeg) {
    sprite.fillScreen(TFT_BLACK);
    int jw = 0, jh = 0;
    float zx = 1, zy = 1;
    int dx = 0, dy = 0;
    if (jpegSize(videoFrame, videoFrameLen, &jw, &jh)) {
      float cover = max((float)displayW / jw, (float)displayH / jh);
      zx = zy = cover;
      dx = (int)((displayW - jw * cover) / 2);
      dy = (int)((displayH - jh * cover) / 2);
    }
    ok = sprite.drawJpg(videoFrame, videoFrameLen, dx, dy, displayW, displayH, 0, 0, zx, zy);
  }
  orientFrame();
  presentFrame();
  videoLastFrameMs = millis();
  videoFrames++;
  if (videoLastFrameMs - videoWindowStart >= 2000) {
    Serial.printf("video fps %.1f frame %u bytes\n", videoFrames * 1000.0f / (videoLastFrameMs - videoWindowStart),
                  (unsigned)videoFrameLen);
    videoFrames = 0;
    videoWindowStart = videoLastFrameMs;
  }
  videoServer.send(ok ? 204 : 400, "text/plain", ok ? "" : "bad jpeg");
}

static void handleUploadBody() {
  HTTPRaw &raw = videoServer.raw();
  if (raw.status == RAW_START) {
    webUploadStarted = sdUploadBegin(webUpload, videoServer.arg("name"));
  } else if (raw.status == RAW_WRITE) {
    if (webUploadStarted) sdUploadWrite(webUpload, raw.buf, raw.currentSize);
  } else if (raw.status == RAW_ABORTED) {
    sdUploadAbort(webUpload);
    webUploadStarted = false;
  }
}

static void handleUpload() {
  String name = videoServer.arg("name");
  String path;
  if (!sdReady) {
    videoServer.send(503, "text/plain", "No SD card");
  } else if (!sdPathFor(name, path)) {
    videoServer.send(400, "text/plain", "Only animals.bin, index.html and firmware.bin can be uploaded");
  } else if (!webUploadStarted || !sdUploadEnd(webUpload)) {
    videoServer.send(500, "text/plain", "Writing to the SD card failed");
  } else {
    if (name == "animals.bin") {
      loadAnimals();
      modelsChanged();
    }
    videoServer.send(200, "text/plain", name == "animals.bin" && animalCount == 0 ? modelError : "ok");
  }
  webUploadStarted = false;
}

static void handleFiles() {
  String json = "{\"sd\":";
  json += sdReady ? "true" : "false";
  json += ",\"files\":[";
  bool first = true;
  if (sdReady) {
    File dir = SD.open(kSdDir);
    for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
      if (f.isDirectory()) continue;
      json += first ? "" : ",";
      json += "{\"name\":\"" + String(f.name()) + "\",\"size\":" + String((unsigned)f.size()) + "}";
      first = false;
    }
  }
  json += "],\"animals\":[";
  for (int i = 0; i < animalCount; ++i) {
    json += (i ? ",\"" : "\"") + String(animals[i].name) + "\"";
  }
  json += "],\"current\":" + String(animalIndex) + ",\"modelError\":\"" + modelError + "\"}";
  videoServer.send(200, "application/json", json);
}

static bool streamFromSd(const char *path, const char *type) {
  if (!sdReady) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  videoServer.streamFile(f, type);
  f.close();
  return true;
}

static void videoSetup() {
  videoFrame = (uint8_t *)heap_caps_malloc(kMaxFrameBytes, MALLOC_CAP_SPIRAM);
  frameMirror = prefs.getBool("mirror", false);
  frameUpside = prefs.getBool("upside", false);

  WiFi.persistent(false);
  bool joined = false;
#if defined(WIFI_SSID)
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(kHostName);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("wifi joining %s", WIFI_SSID);
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  joined = WiFi.status() == WL_CONNECTED;
#endif
  if (joined) {
    Serial.printf("web app: http://%s.local/  (or http://%s/)\n", kHostName, videoAddress().c_str());
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(kApName, kApPassword);
    Serial.printf("wifi network %s password %s\n", kApName, kApPassword);
    Serial.printf("web app: http://%s/\n", videoAddress().c_str());
  }
  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t) {
    if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) Serial.println("wifi: a device joined");
    if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) Serial.println("wifi: a device left");
  });
  // Power saving adds tens of milliseconds of latency to every frame.
  WiFi.setSleep(false);
  if (MDNS.begin(kHostName)) MDNS.addService("http", "tcp", 80);

  videoServer.on("/", HTTP_GET, []() {
    if (streamFromSd(kPagePath, "text/html")) return;
    videoServer.sendHeader("Location", "/setup");
    videoServer.send(302);
  });
  videoServer.on("/setup", HTTP_GET, []() { videoServer.send_P(200, "text/html", kSetupPage); });
  videoServer.on("/animals.bin", HTTP_GET, []() {
    if (!streamFromSd(kModelsPath, "application/octet-stream")) videoServer.send(404, "text/plain", "no models");
  });
  videoServer.on("/files", HTTP_GET, handleFiles);
  videoServer.on("/upload", HTTP_POST, handleUpload, handleUploadBody);
  videoServer.on("/animal", HTTP_POST, []() {
    int i = videoServer.arg("i").toInt();
    if (i < 0 || i >= animalCount) {
      videoServer.send(400, "text/plain", "no such animal");
      return;
    }
    animalIndex = i;
    setupCamera();
    videoServer.send(204);
  });
  videoServer.on("/settings", HTTP_GET, []() {
    Serial.println("web: GET /settings");
    videoServer.send(200, "application/json", String("{\"hologram\":") + (holoMode ? "true" : "false") +
                                                  ",\"rotate\":" + (holoRotate ? "true" : "false") +
                                                  ",\"magic\":" + (magicOn ? "true" : "false") +
                                                  ",\"half\":" + (halfRes ? "true" : "false") +
                                                  ",\"lite\":" + (useLiteModels ? "true" : "false") +
                                                  ",\"mirror\":" + (frameMirror ? "true" : "false") +
                                                  ",\"upside\":" + (frameUpside ? "true" : "false") + "}");
  });
  videoServer.on("/settings", HTTP_POST, []() {
    uint32_t t0 = millis();
    Serial.printf("web: POST /settings %s, internal RAM %u KB (largest %u KB)\n", videoServer.uri().c_str(),
                  (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                  (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    for (int i = 0; i < videoServer.args(); ++i)
      Serial.printf("web:   %s=%s\n", videoServer.argName(i).c_str(), videoServer.arg(i).c_str());
    bool on = videoServer.hasArg("hologram") ? videoServer.arg("hologram") == "1" : holoMode;
    bool rotate = videoServer.hasArg("rotate") ? videoServer.arg("rotate") == "1" : holoRotate;
    setHologram(on, rotate);
    if (videoServer.hasArg("magic")) setMagic(videoServer.arg("magic") == "1");
    if (videoServer.hasArg("half")) setHalfRes(videoServer.arg("half") == "1");
    if (videoServer.hasArg("lite") && (videoServer.arg("lite") == "1") != useLiteModels)
      setLiteModels(videoServer.arg("lite") == "1");
    if (videoServer.hasArg("mirror")) {
      frameMirror = videoServer.arg("mirror") == "1";
      prefs.putBool("mirror", frameMirror);
    }
    if (videoServer.hasArg("upside")) {
      frameUpside = videoServer.arg("upside") == "1";
      prefs.putBool("upside", frameUpside);
    }
    videoServer.send(204);
    Serial.printf("web: settings applied in %lu ms\n", (unsigned long)(millis() - t0));
  });
  videoServer.on("/restart", HTTP_POST, []() {
    videoServer.send(204);
    delay(200);
    ESP.restart();
  });
  videoServer.on("/frame", HTTP_POST, videoHandleFrame, videoHandleFrameBody);
  videoServer.on("/stop", HTTP_POST, []() {
    videoLastFrameMs = millis() - kVideoTimeoutMs;
    videoServer.send(204);
  });
  videoServer.on("/nes", HTTP_GET, []() {
    nesScan();
    String json = "{\"playing\":";
    json += nesPlaying ? "true" : "false";
    json += ",\"rom\":\"" + nesRom + "\",\"pad\":" + String(nesPad) + ",\"roms\":[";
    for (int i = 0; i < nesRomCount; ++i) {
      if (i) json += ",";
      json += "\"" + nesRoms[i] + "\"";
    }
    json += "]}";
    videoServer.send(200, "application/json", json);
  });
  videoServer.on("/nes/pad", HTTP_POST, []() {
    if (videoServer.hasArg("m")) nesPad = (uint8_t)videoServer.arg("m").toInt();
    videoServer.send(204);
  });
  videoServer.on("/nes/play", HTTP_POST, []() {
    String name = videoServer.arg("name");
    if (!nesNameOk(name)) {
      videoServer.send(400, "text/plain", "bad name");
      return;
    }
    bool found = SD.exists(String("/nes/") + name) || SD.exists(String("/") + name) ||
                 SD.exists(String("/unicorn/") + name);
    if (!found) {
      videoServer.send(404, "text/plain", "rom not on card");
      return;
    }
    nesRom = name;
    nesPlaying = true;
    nesPad = 0;
    Serial.printf("nes: %s\n", name.c_str());
    videoServer.send(204);
  });
  videoServer.on("/nes/stop", HTTP_POST, []() {
    nesPlaying = false;
    nesPad = 0;
    videoServer.send(204);
  });
  videoServer.begin();
}

// Serves web requests. Returns true while a video owns the screen.
static bool videoPoll() {
  videoServer.handleClient();
  if (videoActive && millis() - videoLastFrameMs >= kVideoTimeoutMs) {
    videoActive = false;
    Serial.println("video stop");
  }
  return videoActive;
}

// Owns the screen while a ROM is selected from the web controller.
static bool nesPoll() {
  if (!nesPlaying) return false;
  nesDraw();
  return true;
}
