#!/usr/bin/env python3
"""Dump DVD-Video IFO structure (titles, PGCs, cells, stream attributes) as JSON.

    python3 tools/dvd_ifo.py /Volumes/DVDVolume/VIDEO_TS > build/dvd/ifo.json

Reads only IFO files (no media). Field offsets follow the public DVD-Video IFO
layout as documented by libdvdread (ifo_types.h) and dvd.sourceforge.net
"DVD-Video Information" (IFO, PGC, cell playback tables).
"""

import json
import os
import struct
import sys

SECTOR = 2048


def u8(b, o):
    return b[o]


def u16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def bcd(x):
    return (x >> 4) * 10 + (x & 0xF)


def dvd_time(b, o):
    """4-byte BCD playback time hh:mm:ss:ff; top 2 bits of ff = frame rate (3=30, 1=25)."""
    h, m, s, f = b[o], b[o + 1], b[o + 2], b[o + 3]
    rate = {3: 30000 / 1001, 1: 25.0}.get(f >> 6)
    secs = bcd(h) * 3600 + bcd(m) * 60 + bcd(s)
    frames = bcd(f & 0x3F)
    total = secs + (frames / rate if rate else 0)
    return {
        "hmsf": "%02d:%02d:%02d.%02d" % (bcd(h), bcd(m), bcd(s), frames),
        "seconds": round(total, 3),
        "fps_code": f >> 6,
    }


def video_attr(v):
    return {
        "coding": ["mpeg1", "mpeg2"][(v >> 14) & 3] if (v >> 14) & 3 < 2 else "?",
        "standard": ["ntsc", "pal"][(v >> 12) & 3] if (v >> 12) & 3 < 2 else "?",
        "aspect": {0: "4:3", 3: "16:9"}.get((v >> 10) & 3, "?"),
        "resolution_code": (v >> 3) & 7,  # 0=720x480, 1=704x480, 2=352x480, 3=352x240
        "letterboxed": (v >> 2) & 1,
        "film_mode": v & 1,  # 0=camera, 1=film
        "raw": "0x%04x" % v,
    }


def audio_attrs(b, o, n):
    out = []
    for i in range(n):
        a = b[o + 8 * i : o + 8 * i + 8]
        lang = a[2:4].decode("latin-1") if a[2] else ""
        out.append(
            {
                "coding": {0: "ac3", 2: "mpeg1", 3: "mpeg2ext", 4: "lpcm", 6: "dts"}.get(a[0] >> 5, "?"),
                "channels": (a[1] & 7) + 1,
                "sample_rate_code": (a[1] >> 4) & 3,
                "lang": lang,
                "code_ext": a[5],
                "raw": a.hex(),
            }
        )
    return out


def pgc(b, off):
    nprog, ncell = b[off + 2], b[off + 3]
    audio_ctl = [u16(b, off + 0x0C + 2 * i) for i in range(8)]
    sub_ctl = [u32(b, off + 0x1C + 4 * i) for i in range(32)]
    next_pgc, prev_pgc, goup_pgc = u16(b, off + 0x9C), u16(b, off + 0x9E), u16(b, off + 0xA0)
    prog_map_off, cell_play_off = u16(b, off + 0xE6), u16(b, off + 0xE8)
    prog_map = [b[off + prog_map_off + i] for i in range(nprog)] if prog_map_off else []
    cells = []
    for i in range(ncell if cell_play_off else 0):
        c = off + cell_play_off + 24 * i
        cells.append(
            {
                "time": dvd_time(b, c + 4),
                "first_sector": u32(b, c + 8),
                "last_sector": u32(b, c + 20),
                "category": "0x%08x" % u32(b, c),
            }
        )
    cmd_off = u16(b, off + 0xE4)
    ncmds = {}
    if cmd_off:
        ncmds = {"pre": u16(b, off + cmd_off), "post": u16(b, off + cmd_off + 2), "cell": u16(b, off + cmd_off + 4)}
    return {
        "programs": nprog,
        "cells_n": ncell,
        "time": dvd_time(b, off + 4),
        "audio_enabled": [i for i, a in enumerate(audio_ctl) if a & 0x8000],
        "subpic_enabled": [i for i, s in enumerate(sub_ctl) if s & 0x80000000],
        "next_pgc": next_pgc,
        "prev_pgc": prev_pgc,
        "goup_pgc": goup_pgc,
        "commands": ncmds,
        "program_map": prog_map,
        "cells": cells,
    }


def read_vts(path):
    b = open(path, "rb").read()
    assert b[:12] == b"DVDVIDEO-VTS", path
    pgcit = u32(b, 0xCC) * SECTOR
    n = u16(b, pgcit)
    pgcs = []
    for i in range(n):
        cat = u32(b, pgcit + 8 + 8 * i)
        off = u32(b, pgcit + 8 + 8 * i + 4)
        p = pgc(b, pgcit + off)
        p["entry"] = bool(cat & 0x80000000)
        p["title_no"] = (cat >> 24) & 0x7F
        pgcs.append(p)
    return {
        "video": video_attr(u16(b, 0x200)),
        "audio": audio_attrs(b, 0x204, u16(b, 0x202)),
        "subpic_streams": u16(b, 0x254),
        "pgcs": pgcs,
    }


def read_vmg(path):
    b = open(path, "rb").read()
    assert b[:12] == b"DVDVIDEO-VMG", path
    tt = u32(b, 0xC4) * SECTOR
    n = u16(b, tt)
    titles = []
    for i in range(n):
        e = tt + 8 + 12 * i
        titles.append(
            {
                "title": i + 1,
                "type": "0x%02x" % b[e],
                "angles": b[e + 1],
                "chapters": u16(b, e + 2),
                "vts": b[e + 6],
                "vts_ttn": b[e + 7],
            }
        )
    return {"nr_vts": u16(b, 0x3E), "titles": titles}


def main():
    d = sys.argv[1]
    out = {"vmg": read_vmg(os.path.join(d, "VIDEO_TS.IFO")), "vts": {}}
    for k in range(1, out["vmg"]["nr_vts"] + 1):
        out["vts"]["%02d" % k] = read_vts(os.path.join(d, "VTS_%02d_0.IFO" % k))
    json.dump(out, sys.stdout, indent=1)


if __name__ == "__main__":
    main()
