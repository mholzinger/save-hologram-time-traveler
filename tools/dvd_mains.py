#!/usr/bin/env python3
"""Place ROM episode main clips on the DVD by window signature (data/dvd_logic.json).

    .venv/bin/python -I tools/dvd_mains.py data

ROM signature: per window group with a successful move, (start frame, set of directions,
attack -> "enter"). DVD chain: walk the segment chain from a start program (success links) until
it covers the ROM clip's length, collecting (accumulated offset + window start, moves). A start
program matches when the chain length is within 15 frames of the ROM clip, the window counts are
equal, and every ROM window has a DVD window within TOL frames sharing a move.
Measured on PC-verified mains: 21 unique matches, 18 show the same footage as the PC placement
(fingerprint correlation >= 0.85; the DVD often holds a clip twice, game title + VTS12 "Watch"
reel, so footage equality is the right test, not location). Directions are compared
unmirrored: on 168 PC-verified pairs, DVD and ROM directions agree in the same sense
(down/down 27, left/left 19, right/right 19, up/up 11). Accepted only if exactly one DVD start
matches. Prints an evaluation against PC-verified placements and returns proposals.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_json  # noqa: E402
import build_ld_to_dvd as b  # noqa: E402

TOL = 30


def rom_sig(e):
    groups = {}
    for r in e["rules"]:
        a = r["action"]
        good = a.get("hit") in ("ok", "ok_episode_done") or any(
            t["phase"] == 1 and t["does"] in ("effect", "episode_end") for t in a.get("table", [])
        )
        if not good:
            continue
        names = {n for g_ in r["move"]["inputs"] for n in g_} - set(r["move"].get("dead_inputs", []))
        mv = {n for n in names if n in ("up", "down", "left", "right")}
        if names & {"attack"}:
            mv.add("enter")
        if mv:
            lo, s = groups.get(r["window"], (r["a"][0], set()))
            groups[r["window"]] = (lo, s | mv)
    return sorted(groups.values(), key=lambda x: x[0])


def dvd_sig(t, ps, start, limit):
    sig, off, cur = [], 0, start
    for _ in range(30):
        if cur >= len(t["programs"]) or off >= limit - 20:
            break
        seg = t["programs"][cur]
        for w in seg["windows"]:
            sig.append((off + w["lo"], set(w["moves"])))
        link = seg["links"][0] if seg["links"] else {}
        off += ps[cur][1]
        cur = link["link_ptt"] - 1 if ">= 1" in (link.get("if") or "") else cur + 1
    return sig, off


def matches(rs, chain, L):
    ds, off = chain
    if abs(off - L) > 15 or len(ds) != len(rs):
        return False
    for lo, mv in rs:
        if not any(abs(dl - lo) <= TOL and (dm & mv) for dl, dm in ds):
            return False
    return True


def propose(data, exclude=frozenset(), only=None):
    g = load_json(os.path.join(data, "game.json"))
    logic = load_json(os.path.join(data, "dvd_logic.json"))["titles"]
    progs = b.programs(load_json(os.path.join(data, "..", "video", "dvd", "intake.json")))
    out = {}
    for e in g["episodes"]:
        main = next((m for m in e["media"] if "clip" in m), None)
        rs = rom_sig(e)
        if not main or len(rs) < 1 or (only is not None and main["clip"] not in only):
            continue
        L = main["stop"] - main["start"]
        hits = []
        for name, t in logic.items():
            ps = progs.get(name)
            if not ps or len(ps) != len(t["programs"]):
                continue
            for s in range(len(ps)):
                if (name, ps[s][0]) not in exclude and matches(rs, dvd_sig(t, ps, s, L), L):
                    hits.append((name, s))
        if len(hits) == 1:
            out[main["clip"]] = (hits[0][0], hits[0][1], e["chapter"], e["episode"], len(rs))
    return out, progs


def main():
    data = sys.argv[1]
    l2d = load_json(os.path.join(data, "ld_to_dvd.json"))["clips"]
    prop, progs = propose(data)
    ok = bad = new = 0
    for clip, (t, s, ch, ep, n) in prop.items():
        v = l2d.get(str(clip))
        if v and str(v.get("via", "")).startswith("htt"):
            ps = v.get("dvd_program_start", v["dvd_start"])
            ok += v["dvd"] == t and abs(progs[t][s][0] - ps) <= 12
            bad += not (v["dvd"] == t and abs(progs[t][s][0] - ps) <= 12)
        elif not v:
            new += 1
    print(f"unique signature matches {len(prop)}: agree with PC-verified {ok}, disagree {bad}, new {new}")
    return prop


if __name__ == "__main__":
    main()
