#!/usr/bin/env python3
"""The 3DS icon file (SMDH): titles, settings and the 48x48 / 24x24 icon, written in Python.

    .venv/bin/python -I tools/smdh.py ICON48.png OUT.smdh

Layout as bannertool's makesmdh writes it (github.com/diasurgical/bannertool, source/3ds/smdh.h, MIT):
  "SMDH", u16 version 0, u16 0
  16 x {u16 short title[0x40], u16 long title[0x80], u16 publisher[0x40]} (UTF-16LE)
  settings {u8 ratings[16], u32 region lock, u32 match-maker id, u64 match-maker bit id, u32 flags,
            u16 EULA version, u16 0, u32 optimal banner frame, u32 StreetPass id}, u64 0
  small icon 24x24, large icon 48x48: RGB565 in 8x8 Morton-order tiles, alpha premultiplied
The same file goes in the .3dsx (after the code) and in the .cia (makerom -icon).
"""

import struct
import sys

from PIL import Image

SHORT, LONG, PUBLISHER = "Time Traveler", "Time Traveler 3D", "save-hologram-time-traveler"
REGION_FREE = 0x7FFFFFFF
FLAGS = 0x0001 | 0x0004 | 0x0100  # visible, allow 3D, record usage (bannertool's default)


def tiles(im):
    """RGBA image -> RGB565 words in the 3DS tile order (bannertool image_data_to_tiles)."""
    w, h = im.size
    px = im.load()
    out = [0] * (w * h)
    for y in range(h):
        for x in range(w):
            i = (((y >> 3) * (w >> 3) + (x >> 3)) << 6) + (
                (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)
            )
            r, g, b, a = px[x, y]
            k = a / 255.0
            r, g, b = int(r * k) & 0xFF, int(g * k) & 0xFF, int(b * k) & 0xFF
            out[i] = ((r & ~0x7) << 8) | ((g & ~0x3) << 3) | (b >> 3)
    return struct.pack(f"<{w * h}H", *out)


def half(im):
    """48x48 -> 24x24 by averaging each 2x2 block, as bannertool does."""
    src, out = im.load(), Image.new("RGBA", (24, 24))
    dst = out.load()
    for y in range(24):
        for x in range(24):
            p = [src[2 * x + ox, 2 * y + oy] for oy in (0, 1) for ox in (0, 1)]
            dst[x, y] = tuple(sum(c[k] for c in p) // 4 for k in range(4))
    return out


def text(s, units):
    b = s.encode("utf-16-le")[: units * 2]
    return b.ljust(units * 2, b"\0")


def build(icon48, short=SHORT, long=LONG, publisher=PUBLISHER):
    im = icon48.convert("RGBA")
    assert im.size == (48, 48), "the icon is 48x48"
    title = text(short, 0x40) + text(long, 0x80) + text(publisher, 0x40)
    settings = bytes(16) + struct.pack("<IIQIHHII", REGION_FREE, 0, 0, FLAGS, 0, 0, 0, 0)
    return b"SMDH" + struct.pack("<HH", 0, 0) + title * 16 + settings + bytes(8) + tiles(half(im)) + tiles(im)


def main():
    with open(sys.argv[2], "wb") as f:
        f.write(build(Image.open(sys.argv[1])))


if __name__ == "__main__":
    main()
