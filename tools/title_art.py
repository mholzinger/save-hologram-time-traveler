#!/usr/bin/env python3
"""The splash and menu screens, made from the player's own laserdisc footage and our title lettering.

    .venv/bin/python -I tools/title_art.py ENC_DIR OUT_DIR [--preview PNG]

ENC_DIR holds the encodes (tools/encode_ld.py writes a middle frame of each clip, clipNNN_mid.png).
Writes OUT_DIR/splash.bgr, splash_bottom.bgr and menu.bgr, the raw screens port/3ds/source/art.c loads.
Nothing here is Sega's artwork: the pictures come from the user's disc on their machine, and the title is
set in Mr Dafoe and the tagline in Michroma (both SIL Open Font License, tools/fonts/OFL-*.txt), with our
own swoosh.
"""

import os
import sys

from PIL import Image, ImageDraw, ImageEnhance, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from art_convert import fb_bytes  # noqa: E402

FONTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts")
FONT = os.path.join(FONTS, "MrDafoe-Regular.ttf")  # the title
TAG_FONT = os.path.join(FONTS, "Michroma-Regular.ttf")  # the tagline
TAG = "GENUINE ARCADE LASERDISC"
SPLASH_CLIP = 34  # Marshal Gram and Kyi-La together (C1)
MENU_CLIP = 2  # the Time Map's galaxy (attract)
PIC_X0, PIC_W = 43, 320  # the picture inside an encode's left eye (encode_ld.py, port fe.h PIC_X0)
MAGENTA, WHITE, SHADOW = (214, 38, 150), (236, 236, 244), (24, 6, 30)
MENU_TOP = 106  # first menu text row (port art.h ART_MENU_TOP): the header stays above it


def picture(enc, clip):
    """The clip's middle frame: the 320x240 picture of the left eye."""
    im = Image.open(os.path.join(enc, f"clip{clip:03d}_mid.png")).convert("RGB")
    return im.crop((PIC_X0, 0, PIC_X0 + PIC_W, 240))


def title(im, cx, cy, width):
    """'Time Traveler' in Mr Dafoe over a magenta swoosh, centred on (cx, cy), about `width` wide."""
    d = ImageDraw.Draw(im)
    size = 80
    while size > 12:
        f = ImageFont.truetype(FONT, size)
        gap = size * 0.28  # the face's own space is narrow: the words would run together
        words = [d.textlength(w, font=f) for w in ("Time", "Traveler")]
        if sum(words) + gap <= width:
            break
        size -= 2
    total = sum(words) + gap
    x0 = cx - total / 2
    h = size * 0.5
    # the swoosh: a long thin wedge rising to the right, under the words
    d.polygon(
        [
            (x0 - h * 0.6, cy + h * 0.55),
            (x0 + total + h * 0.8, cy - h * 0.15),
            (x0 + total + h, cy - h * 0.05),
            (x0 - h * 0.3, cy + h * 0.75),
        ],
        fill=MAGENTA,
    )
    x = x0
    for w, wl in zip(("Time", "Traveler"), words):
        d.text((x + 2, cy + 2), w, font=f, fill=SHADOW, anchor="lm")
        d.text((x, cy), w, font=f, fill=WHITE, anchor="lm")
        x += wl + gap


def fade(w, h, to):
    """White at the top fading to black by row `to`."""
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(min(to, h)):
        v = round(255 * (1 - y / to) ** 1.6)
        d.line([(0, y), (w, y)], fill=(v, v, v))
    return im


def superimpose(bg, pic, x, y):
    """The laserdisc picture over bg, its black see-through (the footage is keyed to black)."""
    lum = pic.convert("L").point(lambda v: min(255, max(0, (v - 12) * 6)))
    bg.paste(pic, (x, y), lum)


def screens(enc):
    top = fade(400, 240, 120)
    superimpose(top, picture(enc, SPLASH_CLIP), 40, 0)
    d = ImageDraw.Draw(top)
    size = 26
    while size > 8 and d.textlength(TAG, font=ImageFont.truetype(TAG_FONT, size)) > 360:
        size -= 1
    d.text((200, 18), TAG, font=ImageFont.truetype(TAG_FONT, size), fill=MAGENTA, anchor="mm")
    title(top, 200, 206, 330)
    galaxy = picture(enc, MENU_CLIP)
    bottom = ImageEnhance.Brightness(galaxy).enhance(0.8)
    title(bottom, 160, 120, 290)
    menu = ImageEnhance.Brightness(galaxy).enhance(0.4)
    title(menu, 160, MENU_TOP // 2, 280)
    return {"splash": top, "splash_bottom": bottom, "menu": menu}


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    enc, out = argv[0], argv[1]
    os.makedirs(out, exist_ok=True)
    ims = screens(enc)
    for name, im in ims.items():
        with open(os.path.join(out, name + ".bgr"), "wb") as f:
            f.write(fb_bytes(im))
    if "--preview" in argv:
        sheet = Image.new("RGB", (400, 488), (30, 30, 30))
        sheet.paste(ims["splash"], (0, 0))
        sheet.paste(ims["menu"], (40, 248))
        sheet.resize((800, 976), Image.NEAREST).save(argv[argv.index("--preview") + 1])


if __name__ == "__main__":
    main()
