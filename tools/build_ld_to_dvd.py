#!/usr/bin/env python3
"""Chain ROM clips -> PC clips -> DVD positions into data/ld_to_dvd.json.

    python3 -I tools/build_ld_to_dvd.py data

Links: data/pc_to_ld.json (PC scene <-> ROM episode, structural match), data/pc_to_dvd.json
(PC AVI <-> DVD angle-1 MKV frame, fingerprint match). A ROM episode's main clip takes the PC
scene's main AVI; ROM death k takes PC death k. Only confident scene matches and DVD matches
with score >= 0.9 are used. DVD frame numbers index the angle-1 MKV (vtsNN_pgcMM_a1.mkv); the
same frame index in the _a2 file is the 3D version (same cell timing, video/README.md).
PC clips are re-cut, so the DVD range is the PC clip's extent (2 x PC frames), not the exact LD
range; `ld_frames` vs `dvd_frames` shows the difference.
"""

import bisect
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_json  # noqa: E402

FP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "fp", "dvd")


def programs(intake):
    """Angle-1 program extents per DVD file, start refined to the nearest shot cut (+-12 frames)."""

    def fr(t):
        h, mn, rest = t["hmsf"].split(":")
        s, f = rest.split(".")
        return (int(h) * 3600 + int(mn) * 60 + int(s)) * 30 + int(f)

    progs = {}
    for p in intake["produced"]:
        if p["angle"] == 2:
            continue
        name = p["file"][:-4]
        v = intake["vts"]["%02d" % p["vts"]]["pgcs"][p["pgc"] - 1]
        t, k, starts = 0, 0, []
        for i, c in enumerate(v["cells"], 1):
            cat = int(c["category"], 16)
            mode, bt = (cat >> 30) & 3, (cat >> 28) & 3
            sel = True
            if bt == 1:
                k = 1 if mode == 1 else k + 1
                sel = k == 1
            if i in v["program_map"]:
                starts.append(t)
            if sel:
                t += fr(c["time"])
        fpp = os.path.join(FP, name + ".npy")
        if os.path.exists(fpp):
            a = np.load(fpp).astype(np.float32)
            diff = np.abs(np.diff(a, axis=0)).mean(1)
            n = len(a)
            ref = []
            for s0 in starts:
                lo, hi = max(1, s0 - 12), min(n - 1, s0 + 12)
                if hi - lo < 2:
                    ref.append(min(s0, n))
                    continue
                j = int(np.argmax(diff[lo - 1 : hi - 1])) + lo
                ref.append(j if diff[j - 1] > 8 else s0)
            starts, t = ref, n
        progs[name] = [(s0, (starts[i + 1] if i + 1 < len(starts) else t) - s0) for i, s0 in enumerate(starts)]
    return progs


def main():
    d = sys.argv[1]
    g = load_json(os.path.join(d, "game.json"))
    sc = load_json(os.path.join(d, "pc_to_ld.json"))["matches"]
    dv = load_json(os.path.join(d, "pc_to_dvd.json"))["matches"]
    ep = {(e["chapter"], e["episode"]): e for e in g["episodes"]}
    out = {}

    def put(rec, pcname, scene):
        m = [x for x in rec["media"] if "clip" in x]
        v = dv.get(pcname)
        if not m or not v or v["score"] < 0.9:
            return
        c = m[0]
        prev = out.get(str(c["clip"]))
        cand = {
            "ld_start": c["start"],
            "ld_stop": c["stop"],
            "ld_frames": c["stop"] - c["start"],
            "audio": c["audio"],
            "dvd": v["dvd"],
            "dvd_start": v["frame"],
            "dvd_frames": v["frames"],
            "via": pcname,
            "pc_scene": scene,
            "score": v["score"],
        }
        cand["confident"] = bool(next(x for x in sc if x["scene"] == scene)["confident"])
        if prev is None or (cand["confident"], cand["score"]) > (prev.get("confident", True), prev["score"]):
            out[str(c["clip"])] = cand

    for r in sorted(sc, key=lambda r: not r["confident"]):
        # Confident scene matches first; then weak ones whose main-clip length still agrees (<= 3%),
        # which only fill clips nothing better has claimed.
        if not r["confident"] and r["length_error"] > 0.03:
            continue
        e = ep[(r["chapter"], r["episode"])]
        put(e, f"htt2{r['scene']:02d}", r["scene"])
        for k, de in e["deaths"].items():
            put(de, f"htt2{r['scene']:02d}d{k}", r["scene"])
    # Pass 2: every DVD program is one clip (138/152 pass-1 clips start within 10 frames of a
    # program start). Snap pass-1 starts to programs, then give each unmapped clip the unused
    # program in the titles of its LD neighbours whose length best fits ld_frames + 6.
    intake = load_json(os.path.join(d, "..", "video", "dvd", "intake.json"))
    progs = programs(intake)
    taken = set()
    for v in out.values():
        ps = progs.get(v["dvd"], [])
        best = min(ps, key=lambda p: abs(p[0] - v["dvd_start"]), default=None)
        if best and abs(best[0] - v["dvd_start"]) <= 12:
            v["dvd_program_start"], v["dvd_program_frames"] = best
            taken.add((v["dvd"], best[0]))
    allclips = {}
    for e in g["episodes"]:
        for rec in [e] + list(e["deaths"].values()):
            for m in rec["media"]:
                if "clip" in m:
                    allclips[m["clip"]] = m
    # Clips with an identical LD range share footage: copy the mapping instead of spending a program.
    by_range = {(v["ld_start"], v["ld_stop"]): v for v in out.values()}
    for cid, m in allclips.items():
        if str(cid) not in out and (m["start"], m["stop"]) in by_range:
            out[str(cid)] = dict(by_range[(m["start"], m["stop"])], audio=m["audio"], via_same_range=True)
    # Pass 2 (bracketed): the DVD keeps LD order inside a title for most clips (93/119 consecutive
    # pass-1 triples). An unmapped clip may only take an unused program lying between the DVD
    # positions of its nearest mapped LD neighbours in the same title; best length fit wins.
    # (A length-only version put 65 of 80 clips out of order, so it was dropped.)
    for _ in range(3):  # repeat: newly placed clips create tighter brackets
        anchors = sorted((v["ld_start"], v["dvd"], v["dvd_start"]) for v in out.values())
        for cid, m in sorted(allclips.items(), key=lambda kv: kv[1]["start"]):
            if str(cid) in out:
                continue
            if (m["start"], m["stop"]) in by_range:
                out[str(cid)] = dict(by_range[(m["start"], m["stop"])], audio=m["audio"], via_same_range=True)
                continue
            L = m["stop"] - m["start"]
            ks = [x[0] for x in anchors]
            i = bisect.bisect_left(ks, m["start"])
            prev = anchors[i - 1] if i > 0 else None
            nxt = anchors[i] if i < len(anchors) else None
            if not prev or not nxt or prev[1] != nxt[1] or prev[2] >= nxt[2]:
                continue
            t = prev[1]
            cands = [
                (abs(n - (L + 6)), s0, n)
                for s0, n in progs.get(t, [])
                if prev[2] < s0 < nxt[2] and (t, s0) not in taken
            ]
            if not cands:
                continue
            err, s0, n = min(cands)
            if err > max(10, L // 8):
                continue
            taken.add((t, s0))
            out[str(cid)] = by_range[(m["start"], m["stop"])] = {
                "ld_start": m["start"],
                "ld_stop": m["stop"],
                "ld_frames": L,
                "audio": m["audio"],
                "dvd": t,
                "dvd_start": s0,
                "dvd_frames": n,
                "dvd_program_start": s0,
                "dvd_program_frames": n,
                "via": "program-bracket",
                "length_error": err,
                "confident": False,
            }
    # Pass 3: unmapped main clips placed by DVD game-logic signature (tools/dvd_mains.py:
    # chain length + window count + timing + direction; unique match among unused programs;
    # 18/21 agree with PC-verified footage).
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import dvd_mains

    unmapped = {m["clip"] for m in allclips.values() if str(m["clip"]) not in out}
    prop, _ = dvd_mains.propose(d, exclude=frozenset(taken), only=unmapped)
    for clip, (t, si, ch, ep, n) in prop.items():
        if str(clip) in out:
            continue
        s0, nfr = progs[t][si]
        if (t, s0) in taken:
            continue
        taken.add((t, s0))
        m = allclips[clip]
        out[str(clip)] = {
            "ld_start": m["start"],
            "ld_stop": m["stop"],
            "ld_frames": m["stop"] - m["start"],
            "audio": m["audio"],
            "dvd": t,
            "dvd_start": s0,
            "dvd_frames": m["stop"] - m["start"] + 6,
            "dvd_program_start": s0,
            "dvd_program_frames": nfr,
            "via": "dvd-logic",
            "windows": n,
            "confident": False,
        }
    clips = load_json(os.path.join(d, "clips.json"))["records"]
    real = [c for c in clips if c["stop"] > c["start"]]
    used = set()
    for e in g["episodes"]:
        for rec in [e] + list(e["deaths"].values()):
            for m in rec["media"]:
                if "clip" in m:
                    used.add(m["clip"])
    mapped_frames = sum(v["ld_frames"] for v in out.values())
    total_frames = sum(c["stop"] - c["start"] for c in real if c["id"] in used)
    json.dump(
        {
            "source": "tools/build_ld_to_dvd.py (see docstring)",
            "coverage": {
                "clips_mapped": len(out),
                "clips_used_by_game": len(used),
                "ld_frames_mapped": mapped_frames,
                "ld_frames_used": total_frames,
            },
            "clips": dict(sorted(out.items(), key=lambda kv: int(kv[0]))),
        },
        open(os.path.join(d, "ld_to_dvd.json"), "w"),
        indent=1,
    )
    print(
        f"pass3 added {sum(1 for v in out.values() if v.get('via') == 'dvd-logic')}; pass2 added {sum(1 for v in out.values() if v.get('via') == 'program-bracket')}; "
        f"mapped {len(out)} of {len(used)} game clips; {mapped_frames}/{total_frames} LD frames "
        f"({100 * mapped_frames // max(total_frames, 1)}%)"
    )


if __name__ == "__main__":
    main()
