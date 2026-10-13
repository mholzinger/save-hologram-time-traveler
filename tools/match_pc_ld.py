#!/usr/bin/env python3
"""Match PC scenes to ROM episodes using structure, not just length.

    python3 -I tools/match_pc_ld.py data

For each PC scene (main clip + deaths, data/pc_clips.csv; rows, data/pc_scenes.json) and each
ROM episode (data/game.json) the score combines:
  * outcome agreement: each PC row is a window ending at PC frame `frame` (LD ~ 2*frame) whose
    columns give the outcome per key (c1 none, c2 up, c3 down, c4 left, c5 right, c6 fire;
    from htt.exe, see video/README.md). The ROM's outcome for the same key in a window that
    overlaps the row is compared (ok vs death n vs none),
  * length agreement of the main clip and of each death clip (PC frames x2 vs LD frames).
A global one-to-one assignment is made greedily from the best pairs. Output: data/pc_to_ld.json.
"""

import csv
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_json  # noqa: E402

COLS = ["none", "up", "down", "left", "right", "attack"]


def rom_outcome(rec, key, lo, hi):
    """Outcome of pressing `key` (or none) in ROM windows overlapping LD frames lo..hi."""
    res = None
    for r in rec["rules"]:
        wlo, whi = r["a"]
        if whi < lo or wlo > hi:
            continue
        a = r["action"]
        if key == "none":
            if r["move"]["mode"] == "none" or (a.get("expire") or "").startswith("death"):
                v = a.get("expire")
                if v and v.startswith("death"):
                    return "d" + v[5:]
                if v in ("score",) or any(t["phase"] == 2 for t in a.get("table", [])):
                    res = res or "ok"
            continue
        if any(key in grp for grp in r["move"]["inputs"]):
            v = a.get("hit")
            if v and v.startswith("death"):
                return "d" + v[5:]
            return "ok"
    if key != "none":
        # No rule for this key: the input is ignored and the window then expires (CFD9:0316).
        return rom_outcome(rec, "none", lo, hi)
    return res


def main():
    d = sys.argv[1]
    g = load_json(os.path.join(d, "game.json"))
    pcs = load_json(os.path.join(d, "pc_scenes.json"))["scenes"]
    clips = list(csv.DictReader(open(os.path.join(d, "pc_clips.csv"))))
    pc = {}
    for c in clips:
        if c["scene"] == "":
            continue
        s = pc.setdefault(int(c["scene"]), {"main": None, "deaths": {}})
        if c["kind"] == "main":
            s["main"] = int(c["frames"])
        elif c["kind"] == "death":
            s["deaths"][int(c["k"])] = int(c["frames"])

    def ld_len(rec):
        m = [x for x in rec["media"] if "clip" in x]
        return m[0]["stop"] - m[0]["start"] if m else None

    eps = [e for e in g["episodes"] if e["kind"] != "system" and ld_len(e)]
    pairs = []
    for sc, s in pc.items():
        if s["main"] is None:
            continue
        rows = pcs.get(str(sc), {}).get("rows", [])
        for e in eps:
            L = ld_len(e)
            len_cost = abs(2 * s["main"] - L) / max(L, 1)
            dcost, dn = 0.0, 0
            for k, f in s["deaths"].items():
                de = e["deaths"].get(str(k))
                if de is None or not ld_len(de):
                    dcost += 1
                else:
                    dcost += abs(2 * f - ld_len(de)) / ld_len(de)
                dn += 1
            dcost += 0.5 * abs(len(e["deaths"]) - len(s["deaths"]))
            agree = total = 0
            prev = 0
            for row in rows:
                # Rows are sequential checkpoints: row i covers (previous frame, frame] unless `a` is set.
                start = row["a"] if row["a"] else prev
                lo, hi = 2 * start - 6, 2 * row["frame"] + 6
                prev = row["frame"]
                for col, o in zip(COLS, row["c"]):
                    pco = "ok" if o.get("ok") else ("death" if "death" in o else None)
                    if pco is None or (col == "none" and pco == "ok"):
                        continue
                    ro = rom_outcome(e, col, lo, hi)
                    ro = "death" if ro and ro.startswith("d") else ro
                    total += 1
                    agree += ro == pco
            frac = agree / total if total else 0.5
            score = 2.0 * frac - len_cost - dcost / max(dn, 1)
            pairs.append((score, sc, e["chapter"], e["episode"], frac, total, len_cost))
    pairs.sort(reverse=True)
    used_pc, used_rom, out = set(), set(), []
    for score, sc, ch, ep, frac, total, lc in pairs:
        if sc in used_pc or (ch, ep) in used_rom:
            continue
        used_pc.add(sc)
        used_rom.add((ch, ep))
        out.append(
            {
                "scene": sc,
                "chapter": ch,
                "episode": ep,
                "score": round(score, 3),
                "outcome_agreement": f"{round(frac * total)}/{total}",
                "length_error": round(lc, 3),
                "confident": total >= 3 and frac >= 0.6 and lc <= 0.05,
            }
        )
    out.sort(key=lambda r: r["scene"])
    unmatched_rom = sorted(
        f"C{e['chapter']}E{e['episode']}" for e in eps if (e["chapter"], e["episode"]) not in used_rom
    )
    json.dump(
        {
            "source": "tools/match_pc_ld.py (structure + length; see docstring)",
            "matches": out,
            "rom_episodes_unmatched": unmatched_rom,
        },
        open(os.path.join(d, "pc_to_ld.json"), "w"),
        indent=1,
    )
    conf = [r for r in out if r["confident"]]
    print(f"{len(out)} PC scenes matched, {len(conf)} confident; {len(unmatched_rom)} ROM episodes unmatched")
    for r in out:
        print(r)


if __name__ == "__main__":
    main()
