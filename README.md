# ESP32-S3 Unicorn

Firmware for a Makerfabs MaTouch ESP32-S3 4.3" touchscreen. It draws a shaded low-poly animal on a black background and turns it once every 18 seconds.

Tap **Animals** to open the menu and pick Unicorn, Fox, Penguin, Turtle, or Owl. The highlighted row is the one on screen.

The board plugged in here was identified as:

- ESP32-S3, 16MB flash, 8MB octal PSRAM
- CP2104 USB-UART (`/dev/cu.usbserial-011162BB` on this Mac)
- 800×480 RGB panel
- GT911 capacitive touch on GPIO 17/18

## Build and flash

Requires [arduino-cli](https://arduino.github.io/arduino-cli/), the ESP32 core, and LovyanGFX 1.2.x.

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install "LovyanGFX@1.2.29"
arduino-cli compile --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=default,FlashMode=qio,UploadSpeed=921600"
arduino-cli upload -p /dev/cu.usbserial-* --fqbn "esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,CDCOnBoot=default,FlashMode=qio,UploadSpeed=921600"
```

Serial logs run at 115200 on the CP2104 port and print the frame rate.

## Mesh

`tools/build_mesh.py` rebuilds `animals.h` and can write turntable previews. The on-device rasterizer uses the same camera, lights, and triangles as that script.
