# ESP32-S3 Unicorn

Firmware for a Makerfabs MaTouch ESP32-S3 4.3" touchscreen. It opens on a Holobox-mini title with a Start button. After Start, it draws a shaded low-poly animal on a black background and turns it once every 18 seconds.

Tap **Animals** in the bottom-right corner to open the menu and pick Unicorn, Fox, Penguin, Turtle, or Owl. The highlighted row is the one on screen.

The board plugged in here was identified as:

- ESP32-S3, 16MB flash, 8MB octal PSRAM
- CP2104 USB-UART (`/dev/cu.usbserial-011162BB` on this Mac)
- 800×480 RGB panel
- GT911 capacitive touch on GPIO 17/18

## Stream a video

The board also hosts a web page that sends any video to the screen over Wi-Fi.

1. Connect to the board's Wi-Fi network **Unicorn-Display** (password `unicorn123`), then open <http://192.168.4.1/>.
   To use your own network instead, create `wifi_secrets.h` next to the sketch (it is gitignored):

   ```c
   #define WIFI_SSID "your network"
   #define WIFI_PASSWORD "your password"
   ```

   Then open <http://unicorn.local/>. The serial log prints the address either way.
2. Pick or drop a video, then press **Send to display**. The video plays in the browser, and each frame is sent to the board as a JPEG.
3. Press **Stop**, or close the page. The animals come back after 2.5 seconds without frames.

**400 × 240** mode sends smaller frames that the board scales up, which gives a higher frame rate.

## SD card

The 3D models and the web app load from a microSD card, not from flash. The card holds:

- `/unicorn/animals.bin`: the models, built by `tools/build_mesh.py` into `sd/unicorn/`.
- `/unicorn/index.html`: the web app (source in `sd/unicorn/`).
- `/unicorn/firmware.bin`: optional. If it's there, the board installs it on the next boot, then renames it `firmware.done`.

Copy files over Wi-Fi from the board's **SD card** page (`/setup`, built into the firmware so it works with an empty card), or use the script:

```bash
python3 tools/sd_upload.py                                   # models + web app over Wi-Fi
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 models page
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 firmware --restart
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 --list
```

Build the firmware file first with `arduino-cli compile --export-binaries`. Flash over USB once, to set up the 3 MB program partitions. After that, firmware can go through the card.

## Hologram box

`enclosure/` holds a 3D-printable Pepper's ghost box. The screen faces up in the base, and a 4 × 2¾ in polycarbonate sheet sits at 45° above it. Print `base.stl`, `hood.stl` and `lid.stl` in black; none of them need supports. To change sizes, edit the numbers at the top of `enclosure/hologram_box_blender.py`, then run:

```bash
/Applications/Blender.app/Contents/MacOS/Blender -b -P enclosure/hologram_box_blender.py
```

`enclosure/hologram_box.blend` has every cutout as a live Boolean modifier, so you can also adjust it by hand.

The sheet leans toward you: its bottom edge is at the back, and its top edge is up at the front. The screen's light bounces off its underside toward you, and the animal appears standing at the back of the box.

When the board is in the box, turn on **Hologram mode**, either on the web app or with `python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 --hologram on`. It mirrors the picture to undo the reflection, hides the on-screen buttons, and skips the start screen. The setting survives restarts. If the animal appears upside down, press **Rotate 180°** (or use `--hologram rotate`). Videos streamed from the web app are mirrored the same way. Pick the animal on the web app, since the screen can't be tapped inside the box.

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
