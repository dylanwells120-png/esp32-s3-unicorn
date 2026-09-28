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

## iPhone app

`ios/UnicornDisplay.xcodeproj` is the phone app. It runs the same animals, wildlife, NES, and video screens on the phone and sends each frame to the board. Open the project in Xcode, pick your phone, and run it. The phone has to be on the same Wi-Fi as the board.

The first screen asks for the board address. On the board's own network **Unicorn-Display** that is `192.168.4.1`. On your network it is `unicorn.local`, or the address printed in the serial log. Allow local network access when iOS asks.

The screens are the files in `web/`. The app serves them on the phone and forwards `/frame`, `/settings`, and the other board routes to that address. The board still runs the firmware. The page on the SD card is the same screens, for a browser.

## SD card

The 3D models and the web app load from a microSD card, not from flash. The card holds:

- `/unicorn/animals.bin` and `/unicorn/animals_lite.bin`: the full and lite models, built by `tools/build_mesh.py` into `sd/unicorn/`.
- `/unicorn/index.html`: the web app. Edit the files in `web/`, then run `python3 tools/build_page.py` to pack `sd/unicorn/index.html` and `page_blob.h`.
- `/unicorn/firmware.bin`: optional. If it's there, the board installs it on the next boot, then renames it `firmware.done`.

Copy files over Wi-Fi from the board's **SD card** page (`/setup`, built into the firmware so it works with an empty card), or use the script:

```bash
python3 tools/sd_upload.py                                   # models, lite models + web app over Wi-Fi
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 models page
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 firmware --restart
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 --list
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 --animal Fox
python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 --screenshot screen.png
```

`--list` also shows the frame rate and settings. `--screenshot` saves exactly what the screen shows.

Build the firmware file first with `arduino-cli compile --export-binaries`. Flash over USB once, to set up the 3 MB program partitions. After that, firmware can go through the card.

## Hologram box

`enclosure/` holds a two-piece Pepper's ghost and a lightweight table it sits on. The screen sits in the lid, glass down, and a 4 × 2¾ in polycarbonate sheet lies at 45° on rails inside the hood.

- **Hologram:** `hood.stl` and `lid.stl` from `enclosure/hologram_box_blender.py`. Lay the sheet on the two rails, low edge toward the front. Drop the board into the lid, glass down, USB edge toward the back, and set the lid on the pins.
- **Table:** `stand.stl` from `enclosure/hologram_stand_blender.py`. Four legs and an open top. Set the hood in the recess.

Print in black. The hood prints upright, the lid prints with the window on the bed, and the stand prints with the tabletop on the bed and the legs up. None of them need supports. To change sizes, edit the numbers at the top of the script, then run:

```bash
/Applications/Blender.app/Contents/MacOS/Blender -b -P enclosure/hologram_box_blender.py
/Applications/Blender.app/Contents/MacOS/Blender -b -P enclosure/hologram_stand_blender.py
```

`enclosure/hologram_box.blend` and `enclosure/hologram_stand.blend` have every cutout as a live Boolean modifier, so you can also adjust them by hand.

The sheet's low edge is at the front and its high edge leans back. Light from the screen hits the top of the sheet and bounces out the open front.

When the board is in the box, turn on **Hologram mode**, either on the web app or with `python3 tools/sd_upload.py --port /dev/cu.usbmodem1101 --hologram on`. It mirrors the picture to undo the reflection, hides the on-screen buttons, and skips the start screen. The setting survives restarts. If the animal appears upside down, press **Rotate 180°** (or use `--hologram rotate`). Videos streamed from the web app are mirrored the same way. In hologram mode, **Magic effects** (on by default; switch on the web app, or `--magic on|off`) add glowing comets with trails, sparkles and a spinning ring of light on the floor, themed per animal: rainbow orbits for the unicorn, fox fire, falling snow for the penguin, bubbles for the turtle, fireflies and stars for the owl. Themes are in `magic_fx.h`.

The animals are animated. Legs walk in a diagonal gait, heads nod or turn, tails swish, the penguin waddles and flaps its flippers, the turtle paddles, and the owl looks around. `tools/build_mesh.py` tags each moving part with a pivot and a swing (`Mesh.animate`). The firmware rotates those parts every frame, and the web app's 3D viewer plays the same motion.

### Frame rate

In hologram mode, turn on **Half resolution** (web app, or `--half on`) and **Lite models** (`--lite on`) to get 60 fps. All five animals then hold 61 fps, the panel's refresh rate, with magic effects on. Here's what makes that possible:

- **Display driver:** the panel runs on ESP-IDF's `esp_lcd` driver (`display.h`) with bounce buffers, which keeps the picture from glitching while the chip renders.
- **Half resolution:** there's no full-size frame buffer. The chip draws a 400×240 image, and the display interrupt scales it up 2× into each strip of lines just before the panel sends it, so the panel can refresh at 61 Hz.
- **Both CPU cores:** each core draws a band of rows, and the split is rebalanced every frame.
- **Fast maths:** the sketch is compiled with `-O2` and fast-math, which turns float helper functions into inline instructions and more than halves triangle time.
- **Fast depth buffer:** in half-resolution mode it's 8-bit and lives in internal RAM.
- **Lite models:** `sd/unicorn/animals_lite.bin` has about 60% of the detail. `tools/build_mesh.py` writes it next to `animals.bin`.

Full resolution, used outside hologram mode, runs at about 19 fps. `python3 tools/sd_upload.py --port ... --list` shows the frame rate, how the frame time splits between the two cores, and the interrupt's CPU share. `--pclk` sets the panel's pixel clock in full-resolution mode (16 MHz gives 39 Hz). Pick the animal on the web app, since the screen can't be tapped inside the box.

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
