#!/usr/bin/env python3
"""Extract one angle of one PGC from a DVD-Video title set as a plain MPEG-PS.

    python3 tools/dvd_angle.py VIDEO_TS_DIR VTS PGC ANGLE OUT.vob [--json cells.json]

Cells outside an angle block are copied for every angle. Cells inside an
interleaved angle block are walked VOBU by VOBU using the DSI packet in each
NAV pack: within an ILVU the next VOBU follows the current one; at the last
VOBU of an ILVU (sml_pbi.category bit 12) we jump by sml_pbi.ilvu_sa.

DSI field offsets follow libdvdread nav_types.h (dsi_gi_t 32 bytes, sml_pbi_t
at +0x20). Sector numbers in the cell table are relative to VTS_xx_1.VOB.
The output keeps the original packs unchanged (lossless); remux it with
mkvmerge or ffmpeg -c copy.
"""

import argparse
import glob
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_json  # noqa: E402

SECTOR = 2048
DSI = 0x407  # first DSI data byte inside a NAV pack
ILVU_BLOCK = 1 << 14
ILVU_LAST = 1 << 12


class Vobs:
    """Random sector access across VTS_xx_1..9.VOB."""

    def __init__(self, d, vts):
        self.files = []
        start = 0
        for p in sorted(glob.glob(os.path.join(d, "VTS_%02d_[1-9].VOB" % vts))):
            n = os.path.getsize(p) // SECTOR
            self.files.append((start, n, open(p, "rb")))
            start += n

    def read(self, lbn, count=1):
        out = b""
        while count:
            for start, n, f in self.files:
                if start <= lbn < start + n:
                    k = min(count, start + n - lbn)
                    f.seek((lbn - start) * SECTOR)
                    out += f.read(k * SECTOR)
                    lbn += k
                    count -= k
                    break
            else:
                raise IndexError("sector %d out of range" % lbn)
        return out


def nav(sec):
    if sec[:4] != b"\x00\x00\x01\xba" or sec[0x400:0x404] != b"\x00\x00\x01\xbf" or sec[0x406] != 1:
        return None
    vobu_ea = struct.unpack_from(">I", sec, DSI + 8)[0]
    cat = struct.unpack_from(">H", sec, DSI + 0x20)[0]
    ilvu_sa = struct.unpack_from(">I", sec, DSI + 0x20 + 6)[0]
    return vobu_ea, cat, ilvu_sa


def walk_cell(vobs, first, last, interleaved):
    """Yield (lbn, count) runs for one cell."""
    if not interleaved:
        yield first, last - first + 1
        return
    lbn = first
    while lbn <= last:
        sec = vobs.read(lbn)
        n = nav(sec)
        if n is None:
            raise ValueError("no NAV pack at sector %d" % lbn)
        vobu_ea, cat, ilvu_sa = n
        yield lbn, vobu_ea + 1
        if (cat & ILVU_BLOCK) and (cat & ILVU_LAST):
            if ilvu_sa in (0, 0xFFFFFFFF, 0x7FFFFFFF):
                break
            lbn += ilvu_sa
        else:
            lbn += vobu_ea + 1


def select_cells(pgc, angle):
    """Cells to play for an angle: non-block cells, plus the angle-th cell of each angle block."""
    out, k = [], 0
    for c in pgc["cells"]:
        cat = int(c["category"], 16)
        mode, btype = (cat >> 30) & 3, (cat >> 28) & 3
        if btype != 1:  # not an angle block
            out.append((c, False))
            continue
        k = 1 if mode == 1 else k + 1  # mode 1 = first cell in block
        if k == angle:
            out.append((c, bool(cat & (1 << 26))))  # bit 26 = interleaved
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video_ts")
    ap.add_argument("vts", type=int)
    ap.add_argument("pgc", type=int)
    ap.add_argument("angle", type=int)
    ap.add_argument("out")
    ap.add_argument("--ifo-json", default=None, help="output of tools/dvd_ifo.py (else run it)")
    a = ap.parse_args()

    if a.ifo_json:
        info = load_json(a.ifo_json)
    else:
        here = os.path.dirname(os.path.abspath(__file__))
        info = json.loads(subprocess.check_output([sys.executable, "-I", os.path.join(here, "dvd_ifo.py"), a.video_ts]))
    pgc = info["vts"]["%02d" % a.vts]["pgcs"][a.pgc - 1]
    vobs = Vobs(a.video_ts, a.vts)
    total = 0
    with open(a.out, "wb") as f:
        for c, inter in select_cells(pgc, a.angle):
            for lbn, n in walk_cell(vobs, c["first_sector"], c["last_sector"], inter):
                f.write(vobs.read(lbn, n))
                total += n
    print(
        "VTS %d PGC %d angle %d: %d sectors (%.1f MB) -> %s"
        % (a.vts, a.pgc, a.angle, total, total * SECTOR / 1e6, a.out)
    )


if __name__ == "__main__":
    main()
