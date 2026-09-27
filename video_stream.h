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

static String videoAddress() {
  return (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP() : WiFi.localIP()).toString();
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
  float zoom = videoServer.hasArg("zoom") ? videoServer.arg("zoom").toFloat() : 1.0f;
  if (zoom < 1.0f || zoom > 4.0f) zoom = 1.0f;
  if (!videoActive) {
    videoActive = true;
    videoFrames = 0;
    videoWindowStart = millis();
    lcd.fillScreen(TFT_BLACK);
    Serial.println("video start");
  }
  bool ok = lcd.drawJpg(videoFrame, videoFrameLen, 0, 0, lcd.width(), lcd.height(), 0, 0, zoom, zoom);
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
    videoServer.send(200, "application/json", String("{\"hologram\":") + (holoMode ? "true" : "false") +
                                                  ",\"rotate\":" + (holoRotate ? "true" : "false") + "}");
  });
  videoServer.on("/settings", HTTP_POST, []() {
    bool on = videoServer.hasArg("hologram") ? videoServer.arg("hologram") == "1" : holoMode;
    bool rotate = videoServer.hasArg("rotate") ? videoServer.arg("rotate") == "1" : holoRotate;
    setHologram(on, rotate);
    videoServer.send(204);
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
