#!/usr/bin/env python3
"""Laserdisc picture number -> CHD field, from the CHD's VBI metadata (tag AVLD).

    chdman dumpmeta -i rom/timetrv.chd -t AVLD -o build/chd/avld.bin -f
    python3 -I tools/chd_frames.py build/chd/avld.bin [--frame N]

AVLD is 16 bytes per field (MAME vbiparse.cpp vbi_metadata_pack): u24 field number (BE), u8 white
flag, then the 24-bit codes of lines 16, 17, 18 and the 17/18 consensus. A CAV picture number is
coded 0xFxxxxx: ((code >> 16) & 7) * 10000 + four BCD digits (vbiparse.h VBI_CAV_PICTURE).
The CHD itself (rom/, ignored) is never copied; only this derived map is written under build/.
"""

import argparse
import json
import os


def bcd(v, digits):
    out = 0
    for i in reversed(range(digits)):
        d = (v >> (4 * i)) & 0xF
        if d > 9:
            return None
        out = out * 10 + d
    return out


def picture(code):
    """CAV picture number from a 24-bit VBI code, or None."""
    if code & 0xF00000 != 0xF00000:
        return None
    lo = bcd(code & 0xFFFF, 4)
    return None if lo is None else ((code >> 16) & 7) * 10000 + lo


def read(path):
    """{picture: first field carrying it}"""
    data = open(path, "rb").read()
    pic_field = {}
    for off in range(0, len(data) - 15, 16):
        rec = data[off : off + 16]
        field = int.from_bytes(rec[0:3], "big")
        l1718 = int.from_bytes(rec[13:16], "big")
        p = picture(l1718)
        if p is None:
            p = picture(int.from_bytes(rec[7:10], "big")) or picture(int.from_bytes(rec[10:13], "big"))
        if p is not None and p not in pic_field:
            pic_field[p] = field
    return pic_field


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("avld")
    ap.add_argument("--frame", type=int, action="append")
    ap.add_argument("-o", default=None, help="write {picture: field} JSON here")
    a = ap.parse_args()
    m = read(a.avld)
    pics = sorted(m)
    print(f"{len(pics)} picture numbers, {pics[0]}..{pics[-1]}; fields {m[pics[0]]}..{m[pics[-1]]}")
    gaps = [(p, q) for p, q in zip(pics, pics[1:]) if q != p + 1]
    print(f"gaps in numbering: {len(gaps)}", gaps[:10])
    odd = [p for p in pics if m[p] != 2 * (p - pics[0]) + m[pics[0]]]
    print(f"pictures off the field = 2*(pic-{pics[0]})+{m[pics[0]]} line: {len(odd)}", odd[:10])
    for f in a.frame or []:
        print(f"picture {f} -> field {m.get(f)}")
    if a.o:
        os.makedirs(os.path.dirname(a.o) or ".", exist_ok=True)
        json.dump({str(k): v for k, v in sorted(m.items())}, open(a.o, "w"))


if __name__ == "__main__":
    main()
