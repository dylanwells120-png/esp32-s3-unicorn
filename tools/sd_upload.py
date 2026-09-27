#!/usr/bin/env python3
"""Copy the web app, 3D models and firmware onto the board's SD card.

Over Wi-Fi (the board's own network or yours):
    tools/sd_upload.py                        # models + web app to 192.168.4.1
    tools/sd_upload.py --host unicorn.local firmware
Over USB:
    tools/sd_upload.py --port /dev/cu.usbmodem1101 models page

Targets: models (sd/unicorn/animals.bin), page (sd/unicorn/index.html),
firmware (build/esp32-s3-unicorn.ino.bin, installed on the next boot).
Add --restart to restart the board afterwards, which installs uploaded firmware.
"""

import argparse
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    "models": ("animals.bin", ROOT / "sd" / "unicorn" / "animals.bin"),
    "page": ("index.html", ROOT / "sd" / "unicorn" / "index.html"),
    # arduino-cli --export-binaries writes build/<board>/esp32-s3-unicorn.ino.bin.
    "firmware": ("firmware.bin", next(iter(sorted((ROOT / "build").glob("*/esp32-s3-unicorn.ino.bin"))),
                                      ROOT / "build" / "esp32-s3-unicorn.ino.bin")),
}
CHUNK = 4096


def upload_http(host, name, data):
    req = urllib.request.Request(f"http://{host}/upload?name={name}", data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream"})
    with urllib.request.urlopen(req, timeout=120) as res:
        body = res.read().decode()
    if body != "ok":
        raise RuntimeError(body)


def restart_http(host):
    urllib.request.urlopen(urllib.request.Request(f"http://{host}/restart", method="POST"), timeout=10).read()


class SerialLink:
    def __init__(self, port):
        import serial  # pyserial

        # Leave DTR/RTS alone so opening the port doesn't reset the board.
        self.io = serial.Serial()
        self.io.port = port
        self.io.baudrate = 115200
        self.io.timeout = 0.2
        self.io.dtr = False
        self.io.rts = False
        self.io.open()

    def reply(self, timeout=10.0):
        """Next protocol line from the board, skipping its log output."""
        end = time.time() + timeout
        buf = b""
        while time.time() < end:
            buf += self.io.read(1)
            if buf.endswith(b"\n"):
                line = buf.decode(errors="replace").strip()
                buf = b""
                if line.startswith("SD"):
                    if line.startswith("SDERR"):
                        raise RuntimeError(line[6:])
                    return line
        raise RuntimeError("board did not answer")

    def upload(self, name, data):
        self.io.reset_input_buffer()
        self.io.write(f"SDPUT {name} {len(data)}\n".encode())
        self.reply()
        for off in range(0, len(data), CHUNK):
            self.io.write(data[off:off + CHUNK])
            self.reply()
            print(f"\r  {name}: {min(off + CHUNK, len(data)) * 100 // len(data)}%", end="", flush=True)
        print()
        self.reply(timeout=30)

    def listing(self):
        self.io.reset_input_buffer()
        self.io.write(b"SDLIST\n")
        lines = []
        while (line := self.reply()) != "SDDONE":
            lines.append(line)
        return lines

    def hologram(self, on, rotate):
        self.io.reset_input_buffer()
        self.io.write(f"SDHOLO {int(on)} {int(rotate)}\n".encode())
        self.reply()

    def restart(self):
        self.io.write(b"SDRESTART\n")
        self.reply()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("targets", nargs="*", metavar="target", help="models, page, firmware (default: models page)")
    ap.add_argument("--host", default="192.168.4.1", help="board address for Wi-Fi uploads")
    ap.add_argument("--port", help="upload over this USB serial port instead of Wi-Fi")
    ap.add_argument("--restart", action="store_true", help="restart the board afterwards")
    ap.add_argument("--hologram", choices=["on", "off", "rotate"],
                    help="hologram mode for the Pepper's ghost box; 'rotate' turns it on rotated 180 degrees (USB only)")
    ap.add_argument("--list", action="store_true", help="show the card's /unicorn folder and loaded models (USB only)")
    args = ap.parse_args()
    if args.hologram:
        if not args.port:
            ap.error("--hologram needs --port; over Wi-Fi, use the switch on the web app")
        SerialLink(args.port).hologram(args.hologram != "off", args.hologram == "rotate")
        print(f"hologram {args.hologram}")
        return
    if args.list:
        if not args.port:
            ap.error("--list needs --port; over Wi-Fi, open /setup instead")
        for line in SerialLink(args.port).listing():
            kind, _, rest = line.partition(" ")
            print(("  file  " if kind == "SDFILE" else "  ") + rest)
        return
    if not args.targets and not args.restart:
        args.targets = ["models", "page"]
    for target in args.targets:
        if target not in TARGETS:
            ap.error(f"unknown target {target!r}; choose from {', '.join(TARGETS)}")

    link = SerialLink(args.port) if args.port else None
    for target in args.targets:
        name, path = TARGETS[target]
        if not path.exists():
            hint = " (run arduino-cli compile --export-binaries)" if target == "firmware" else ""
            sys.exit(f"{path} not found{hint}")
        data = path.read_bytes()
        print(f"uploading {path.relative_to(ROOT)} -> /unicorn/{name} ({len(data)} bytes)")
        try:
            if link:
                link.upload(name, data)
            else:
                upload_http(args.host, name, data)
        except Exception as err:
            sys.exit(f"{name}: {err}")
    if args.restart:
        print("restarting board")
        link.restart() if link else restart_http(args.host)


if __name__ == "__main__":
    main()
