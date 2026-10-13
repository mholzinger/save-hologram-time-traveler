#!/usr/bin/env python3
"""Decode the DVD's interactive game logic into a segment graph -> data/dvd_logic.json.

    python3 -I tools/dvd_logic.py /Volumes/DVDVolume/VIDEO_TS data

For every PGC with an angle block (the game titles), per program (= decision segment):
  * window(s): button-highlight intervals from each VOBU's PCI (libdvdread nav_types.h:
    PCI data at sector+0x2D, hl_gi at PCI+96, btn_colit 24 bytes, btni[] at hl_gi+46, 18 bytes),
    in frames relative to the segment's first VOBU (PTM resets per cell);
  * buttons: button 1 is a hub; its up/down/left/right links name the direction buttons. A
    button whose command is SetGPRMMD (0x53) sets GPRM0 = code; `moves` lists the hub direction(s)
    leading to such buttons ("enter" if it is the hub itself);
  * links: cell command(s) of the segment's angle-1 cell, decoded for the forms seen on this disc:
    `20 c5 0000 vvvv 00pp` = if GPRM0 <op> vvvv LinkPTTN pp (op from bits 6..4 of byte 1),
    `30 05 00tt 00pp` = JumpVTS_PTT tt pp, `30 08 ...` = CallSS. Others are kept raw.
Command encodings follow the DVD-Video VM layout as implemented in libdvdnav (vmcmd.c); only
the forms listed above are interpreted. Output holds numbers only.
"""

import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dvd_angle  # noqa: E402
import dvd_ifo  # noqa: E402

PCI = 0x2D
FPS = 30000 / 1001
OPS = {1: "&", 2: "==", 3: "!=", 4: ">=", 5: ">", 6: "<=", 7: "<"}


def pgc_cmds(ifo_bytes, pgc_off):
    off = struct.unpack_from(">H", ifo_bytes, pgc_off + 0xE4)[0]
    if not off:
        return [], [], []
    c = pgc_off + off
    npre, npost, ncell = struct.unpack_from(">HHH", ifo_bytes, c)
    o = c + 8
    pre = [ifo_bytes[o + 8 * i : o + 8 * i + 8] for i in range(npre)]
    o += 8 * npre
    post = [ifo_bytes[o + 8 * i : o + 8 * i + 8] for i in range(npost)]
    o += 8 * npost
    cell = [ifo_bytes[o + 8 * i : o + 8 * i + 8] for i in range(ncell)]
    return pre, post, cell


def decode(cmd):
    b = cmd
    if b[0] == 0x20 and (b[1] & 0x0F) == 5:
        op = (b[1] >> 4) & 7
        return {
            "if": f"GPRM{b[3] & 0xF} {OPS.get(op, '?')} {struct.unpack('>H', b[4:6])[0]}" if op else None,
            "link_ptt": b[7] | ((b[6] & 0x3) << 8),
        }
    if b[0] == 0x30 and b[1] == 0x05:
        return {"jump_vts_ptt": [struct.unpack(">H", b[2:4])[0], struct.unpack(">H", b[4:6])[0]]}
    if b[0] == 0x30 and b[1] == 0x08:
        return {"call_ss": b[2:].hex()}
    return {"raw": b.hex()}


def segment_windows(vobs, cell, interleaved):
    first, wins = None, []
    for lbn, _ in dvd_angle.walk_cell(vobs, cell["first_sector"], cell["last_sector"], interleaved):
        sec = vobs.read(lbn)
        s_ptm = struct.unpack_from(">I", sec, PCI + 12)[0]
        if first is None:
            first = s_ptm
        h = PCI + 96
        hli_ss, hs, he, _bse, _md, _sn, nbtn = struct.unpack_from(">HIIIHBB", sec, h)
        if hli_ss & 3 != 1:
            continue
        btns = []
        for k in range(nbtn):
            o = h + 46 + 18 * k
            btns.append({"nav": [sec[o + 6 + d] & 0x3F for d in range(4)], "cmd": sec[o + 10 : o + 18]})
        hub = btns[0]["nav"] if btns else [0, 0, 0, 0]
        moves, codes = [], []
        for k, bt in enumerate(btns, 1):
            if bt["cmd"][0] == 0x53:
                codes.append(struct.unpack(">H", bt["cmd"][2:4])[0])
                dirs = [n for n, t in zip(("up", "down", "left", "right"), hub) if t == k and k != 1]
                moves += dirs or (["enter"] if k == 1 else [f"button{k}"])
        lo = round((hs - first) / 90000 * FPS)
        hi = round((he - first) / 90000 * FPS) if he != 0xFFFFFFFF else None
        wins.append({"lo": lo, "hi": hi, "buttons": nbtn, "moves": moves, "codes": codes})
    return wins


def main():
    vts_dir, out = sys.argv[1], sys.argv[2]
    vmg = dvd_ifo.read_vmg(os.path.join(vts_dir, "VIDEO_TS.IFO"))
    titles = {}
    for k in range(1, vmg["nr_vts"] + 1):
        path = os.path.join(vts_dir, "VTS_%02d_0.IFO" % k)
        raw = open(path, "rb").read()
        vts = dvd_ifo.read_vts(path)
        pgcit = struct.unpack_from(">I", raw, 0xCC)[0] * 2048
        vobs = dvd_angle.Vobs(vts_dir, k)
        for pi, pgc in enumerate(vts["pgcs"], 1):
            if not any((int(c["category"], 16) >> 28) & 3 == 1 for c in pgc["cells"]):
                continue
            pgc_off = pgcit + struct.unpack_from(">I", raw, pgcit + 8 + 8 * (pi - 1) + 4)[0]
            pre, post, cellcmds = pgc_cmds(raw, pgc_off)
            sel = dvd_angle.select_cells(pgc, 1)
            cell_index = {id(c): i + 1 for i, c in enumerate(pgc["cells"])}
            progs = []
            pm = pgc["program_map"] + [len(pgc["cells"]) + 1]
            for p in range(len(pgc["program_map"])):
                cells = [(c, inter) for c, inter in sel if pm[p] <= cell_index[id(c)] < pm[p + 1]]
                seg = {"program": p + 1, "windows": [], "links": []}
                for c, inter in cells:
                    seg["windows"] += segment_windows(vobs, c, inter)
                    n = int(c["category"], 16) & 0xFF
                    if n:
                        seg["links"].append(decode(cellcmds[n - 1]))
                    seg["frames"] = seg.get("frames", 0) + round(c["time"]["seconds"] * FPS)
                progs.append(seg)
            titles[f"vts{k:02d}_pgc{pi:02d}_a1"] = {
                "pre": [decode(c) for c in pre],
                "post": [decode(c) for c in post],
                "programs": progs,
            }
            print(
                f"VTS {k} PGC {pi}: {len(progs)} programs, {sum(len(s['windows']) for s in progs)} windows", flush=True
            )
    json.dump(
        {"source": "tools/dvd_logic.py (see docstring)", "titles": titles},
        open(os.path.join(out, "dvd_logic.json"), "w"),
        indent=1,
    )


if __name__ == "__main__":
    main()
