#!/usr/bin/env python3
"""Pack web/ into the single page the board serves.

Source is split under web/ so each feature is its own file. The board only
serves /unicorn/index.html, and sd_store.h copies page_blob.h onto the card
when the sizes differ, so this writes both from the same pack.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"
PAGE = ROOT / "sd" / "unicorn" / "index.html"
BLOB = ROOT / "page_blob.h"

SCRIPTS = [
    "js/hologram.js",
    "js/viewer.js",
    "js/wildlife.js",
    "js/garden.js",
    "js/nes.js",
    "js/video.js",
]


def pack() -> str:
    html = (WEB / "index.html").read_text()
    css = (WEB / "css" / "app.css").read_text()
    if not css.endswith("\n"):
        css += "\n"
    html = html.replace(
        '<link rel="stylesheet" href="css/app.css">',
        "<style>\n" + css + "</style>",
    )
    jsnes = (WEB / "vendor" / "jsnes.min.js").read_text()
    if not jsnes.endswith("\n"):
        jsnes += "\n"
    html = html.replace(
        '<script src="vendor/jsnes.min.js"></script>\n',
        "<script>\n" + jsnes + "</script>\n",
    )
    chunks = []
    for name in SCRIPTS:
        src = f'<script src="{name}"></script>\n'
        if src not in html:
            raise SystemExit(f"web/index.html is missing {src.strip()}")
        html = html.replace(src, "")
        chunks.append((WEB / name).read_text())
    html = html.replace("</body>", "<script>\n" + "".join(chunks) + "</script>\n</body>")
    return html


def write_blob(data: bytes) -> None:
    lines = [
        "#pragma once",
        "#include <stdint.h>",
        f"static const unsigned kPageBlobLen = {len(data)};",
        "static const uint8_t kPageBlob[] = {",
    ]
    row = []
    for i, byte in enumerate(data):
        row.append(str(byte))
        if len(row) == 20:
            lines.append("  " + ",".join(row) + ",")
            row = []
    if row:
        lines.append("  " + ",".join(row) + ",")
    lines.append("};")
    lines.append("")
    BLOB.write_text("\n".join(lines))


def main() -> None:
    html = pack()
    PAGE.write_text(html)
    write_blob(html.encode())
    print(f"wrote {PAGE.relative_to(ROOT)} and page_blob.h ({len(html)} bytes)")


if __name__ == "__main__":
    main()
