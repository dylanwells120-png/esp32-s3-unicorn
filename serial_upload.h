#pragma once
// USB file transfer to the SD card, used by `tools/sd_upload.py --port`.
// Listens on the CP2104 port and, when it is not already Serial, the ESP32-S3's
// native USB port.
//
//   host: SDPUT <name> <size>     board: SDREADY
//   host: up to 4096 bytes        board: SDOK <total so far>    (repeat)
//                                 board: SDDONE  (or SDERR <reason> at any point)
//   host: SDLIST                  board: SDFILE <name> <size> (each), SDINFO <models>, SDDONE
//   host: SDHOLO <on> <rotate>    board: SDDONE      (hologram mode, 0 or 1 each)
//   host: SDMAGIC <on>            board: SDDONE      (hologram effects, 0 or 1)
//   host: SDANIMAL <index>        board: SDDONE      (which animal is shown)
//   host: SDPCLK <hz>             board: SDDONE      (panel pixel clock; sets the refresh rate)
//   host: SDSHOT                  board: SDSHOT <w> <h> <swapped>, w*h RGB565 pixels, SDDONE
//   host: SDRESTART               board: SDDONE, then restarts

#include "sd_store.h"

#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#define UNICORN_NATIVE_USB 1
static HWCDC nativeUsb;
#endif

static void serialUploadSetup() {
#ifdef UNICORN_NATIVE_USB
  // Must hold a whole 4 KB chunk; the default 256 bytes drops data.
  nativeUsb.setRxBufferSize(8192);
  nativeUsb.begin();
#endif
}

static void serialUploadPut(Stream &io, const String &args) {
  int space = args.indexOf(' ');
  String name = args.substring(0, space);
  long size = space > 0 ? args.substring(space + 1).toInt() : 0;
  SdUpload up;
  if (size <= 0 || !sdUploadBegin(up, name)) {
    io.println(sdReady ? "SDERR cannot write that file" : "SDERR no SD card");
    return;
  }
  io.println("SDREADY");
  static uint8_t buf[4096];
  size_t got = 0;
  io.setTimeout(5000);
  while (got < (size_t)size) {
    size_t want = min(sizeof(buf), (size_t)size - got);
    size_t n = io.readBytes(buf, want);
    if (n != want || !sdUploadWrite(up, buf, n)) {
      sdUploadAbort(up);
      io.println(n != want ? "SDERR timed out" : "SDERR SD write failed");
      io.setTimeout(1000);
      return;
    }
    got += n;
    io.printf("SDOK %u\n", (unsigned)got);
  }
  io.setTimeout(1000);
  if (!sdUploadEnd(up)) {
    io.println("SDERR SD write failed");
    return;
  }
  if (name == "animals.bin") {
    loadAnimals();
    modelsChanged();
    if (animalCount == 0) {
      io.printf("SDERR %s\n", modelError.c_str());
      return;
    }
  }
  io.println("SDDONE");
}

static void serialUploadPollPort(Stream &io) {
  if (!io.available()) return;
  if (io.peek() != 'S') {
    io.read();
    return;
  }
  String line = io.readStringUntil('\n');
  line.trim();
  if (line.startsWith("SDPUT ")) {
    serialUploadPut(io, line.substring(6));
  } else if (line == "SDLIST") {
    if (!sdReady) {
      io.println("SDERR no SD card");
      return;
    }
    File dir = SD.open(kSdDir);
    for (File f = dir ? dir.openNextFile() : File(); f; f = dir.openNextFile()) {
      if (!f.isDirectory()) io.printf("SDFILE %s %u\n", f.name(), (unsigned)f.size());
    }
    io.printf("SDINFO fps %.1f\n", lastFps);
    {
      static uint32_t lastCycles = 0, lastAt = 0;
      uint32_t now = millis(), cyc = bounceCycles;
      if (lastAt && now > lastAt)
        io.printf("SDINFO scale-up interrupt %.0f%% of a core\n", (cyc - lastCycles) / 240000.f / (now - lastAt) * 100.f);
      lastCycles = cyc;
      lastAt = now;
    }
    io.printf("SDINFO internal RAM free %u KB, largest block %u KB\n",
              (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
              (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024));
    io.printf("SDINFO refresh %.1f Hz (%.1f MHz pixel clock)\n", refreshRate(), pclkHz / 1e6f);
    io.printf("SDINFO cores %.1f ms + %.1f ms, split at row %d\n", bands[0].micros / 1000.f, bands[1].micros / 1000.f,
              bandSplit);
    for (int b = 0; b < 2; ++b)
      io.printf("SDINFO core %d: clear %.1f ms, triangles %.1f ms, effects %.1f ms\n", b, bands[b].clearUs / 1000.f,
                bands[b].trisUs / 1000.f, (bands[b].micros - bands[b].clearUs - bands[b].trisUs) / 1000.f);
    for (int b = 0; b < 2; ++b)
      io.printf("SDINFO core %d: %u triangles in band, %u drawn, %u rows, raster %.1f ms\n", b,
                (unsigned)bands[b].trisSeen, (unsigned)bands[b].trisDrawn, (unsigned)bands[b].rows,
                bands[b].fillCycles / 240000.f);
    io.printf("SDINFO hologram %s%s\n", holoMode ? "on" : "off", holoMode && holoRotate ? ", rotated" : "");
    io.printf("SDINFO magic %s\n", magicOn ? "on" : "off");
    io.printf("SDINFO half %s, lite %s\n", halfRes ? "on" : "off", useLiteModels ? "on" : "off");
    if (animalCount == 0) io.printf("SDINFO no models: %s\n", modelError.c_str());
    for (int i = 0; i < animalCount; ++i) io.printf("SDINFO model %s\n", animals[i].name);
    io.println("SDDONE");
  } else if (line.startsWith("SDHOLO ")) {
    setHologram(line.charAt(7) == '1', line.charAt(9) == '1');
    io.println("SDDONE");
  } else if (line.startsWith("SDHALF ")) {
    setHalfRes(line.charAt(7) == '1');
    io.println("SDDONE");
  } else if (line.startsWith("SDLITE ")) {
    setLiteModels(line.charAt(7) == '1');
    io.println("SDDONE");
  } else if (line.startsWith("SDMAGIC ")) {
    setMagic(line.charAt(8) == '1');
    io.println("SDDONE");
  } else if (line.startsWith("SDANIMAL ")) {
    int i = line.substring(9).toInt();
    if (i < 0 || i >= animalCount) {
      io.println("SDERR no such animal");
      return;
    }
    animalIndex = i;
    setupCamera();
    io.println("SDDONE");
  } else if (line.startsWith("SDPCLK ")) {
    uint32_t hz = (uint32_t)line.substring(7).toInt();
    if (!setPixelClock(hz)) {
      io.println("SDERR pixel clock must be 8-30 MHz");
      return;
    }
    if (!displayHalf) {
      fullPclkHz = hz;
      prefs.putUInt("pclk", hz);
    }
    io.println("SDDONE");
  } else if (line == "SDSHOT") {
    // The last frame drawn is still in the sprite buffer.
    io.printf("SDSHOT %d %d %d\n", viewW, viewH, swapBytes ? 1 : 0);
    for (int y = 0; y < viewH; ++y) {
      const uint8_t *row = (const uint8_t *)(fb + y * stride);
      size_t sent = 0;
      uint32_t deadline = millis() + 2000;
      while (sent < (size_t)viewW * 2 && (int32_t)(deadline - millis()) > 0) {
        size_t n = io.write(row + sent, (size_t)viewW * 2 - sent);
        if (n == 0) delay(1);
        sent += n;
      }
    }
    io.println();
    io.println("SDDONE");
  } else if (line == "SDRESTART") {
    io.println("SDDONE");
    io.flush();
    delay(200);
    ESP.restart();
  }
}

static void serialUploadPoll() {
  serialUploadPollPort(Serial);
#ifdef UNICORN_NATIVE_USB
  serialUploadPollPort(nativeUsb);
#endif
}
