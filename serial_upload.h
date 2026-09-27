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
    io.printf("SDINFO hologram %s%s\n", holoMode ? "on" : "off", holoMode && holoRotate ? ", rotated" : "");
    if (animalCount == 0) io.printf("SDINFO no models: %s\n", modelError.c_str());
    for (int i = 0; i < animalCount; ++i) io.printf("SDINFO model %s\n", animals[i].name);
    io.println("SDDONE");
  } else if (line.startsWith("SDHOLO ")) {
    setHologram(line.charAt(7) == '1', line.charAt(9) == '1');
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
