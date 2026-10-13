#!/usr/bin/env python3
"""Convert the cut art in build/art/ (docs/reference/art/cut_art.py) to the raw images the 3DS port loads.

    .venv/bin/python -I tools/art_convert.py

Writes build/art/sdmc/timetrv/art/:
  splash.bgr         top screen at boot, 400x240 (splash_top_400x240.png)
  splash_bottom.bgr  bottom screen at boot, 320x240 (menu_bottom_320x240.png)
  menu.bgr           bottom-screen menu: menu_bottom with logo_lockup scaled to 320 wide as a header,
                     the area under it darkened so the menu text reads

All are BGR8 in the framebuffer's memory order (column-major, 240-px columns, origin bottom-left), as
the port's raw screens use, so the port can copy them straight to the screen.
User-local art (docs/ART.md): everything stays in build/ (ignored) and is never committed or shipped.
"""

import os
import sys

from PIL import Image, ImageEnhance

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT  # noqa: E402

SRC = os.path.join(ROOT, "build", "art")
OUT = os.path.join(SRC, "sdmc", "timetrv", "art")
MENU_DIM = 0.55  # brightness of the menu background under the header (text legibility)


def fb_bytes(im):
    """RGB image -> BGR8 bytes in 3DS framebuffer order."""
    w, h = im.size
    px = im.convert("RGB").load()
    out = bytearray(w * h * 3)
    i = 0
    for x in range(w):
        for y in range(h - 1, -1, -1):  # each column bottom to top
            r, g, b = px[x, y]
            out[i : i + 3] = bytes((b, g, r))
            i += 3
    return bytes(out)


def load(name, size):
    path = os.path.join(SRC, name)
    if not os.path.exists(path):
        sys.exit(f"missing {path}: run docs/reference/art/cut_art.py (see docs/ART.md)")
    im = Image.open(path).convert("RGB")
    if im.size != size:
        sys.exit(f"{path} is {im.size}, expected {size}")
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    splash = load("splash_top_400x240.png", (400, 240))
    bottom = load("menu_bottom_320x240.png", (320, 240))
    logo = Image.open(os.path.join(SRC, "logo_lockup.png")).convert("RGB")
    head_h = round(logo.height * 320 / logo.width)
    menu = ImageEnhance.Brightness(bottom).enhance(MENU_DIM)
    menu.paste(logo.resize((320, head_h), Image.LANCZOS), (0, 0))
    for name, im in (("splash", splash), ("splash_bottom", bottom), ("menu", menu)):
        path = os.path.join(OUT, name + ".bgr")
        open(path, "wb").write(fb_bytes(im))
        print(f"{path}  {im.size[0]}x{im.size[1]}")
    print(f"menu header: 320x{head_h}")


if __name__ == "__main__":
    main()
