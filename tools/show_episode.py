#!/usr/bin/env python3
"""Print decoded episodes: header, clip, windows, input rules, deaths.

    python3 tools/show_episode.py [chapter [episode]]

Reads only data/*.json (derived from the ROM by tools/extract_tables.py).
Action codes with a high nibble are decoded per D047:00E6 (see docs/ROM_FINDINGS.md).
"""

import collections
import json
import os
import sys

D = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data")
load = lambda n: json.load(open(os.path.join(D, n + ".json")))["records"]
episodes, clips = load("episode_table"), {c["id"]: c for c in load("clips")}
windows = {w["id"]: w for w in load("windows")}
assets, rules, moves, actions = (collections.defaultdict(list) for _ in range(4))
for a in load("assets"):
    assets[a["asset_id"]].append(a)
for r in load("input_rules"):
    rules[r["key"]].append(r)
for m in load("moves"):
    moves[m["move_id"]].append(m)
for a in load("actions"):
    actions[a["code"]].append(a)

KIND = {10003: "death", 2000: "end", 1000: "ep", 1001: "ep1001", 1005: "ep1005", 1006: "ep1006", 10000: "sys"}


# Logical input bits -> names. Device/bit layout from D17E:0121, D17E:0002, D17E:02D5 (word g[0xC34], inverted
# IN0/IN1), names from reference/mame/timetrv.cpp:210-224.
INPUT_NAMES = {
    (1, 1): "Up",
    (1, 2): "Down",
    (1, 4): "Left",
    (1, 8): "Right",
    (0x10, 0x100): "Attack",
    (0x10, 0x200): "Cube",
    (0x20, 0x10): "Start1",
    (0x20, 0x20): "Start2",
    (4, 0x40): "Coin",
    (4, 0x80): "Bill",
}


def bits_str(device, bits):
    names = [n for (d, b), n in INPUT_NAMES.items() if d == device and bits & b]
    return "+".join(names) if names else "d%d:%x" % (device, bits)


def move_str(mid):
    if mid == 0:
        return "NO-MOVE"
    rows = moves.get(mid & 0x7FFF, [])
    if not rows:
        return "move%d?" % mid
    sep = {1: "", 2: "|", 3: "+"}.get(rows[0]["mode"], "?")
    return "m%d(%s)" % (mid & 0x7FFF, sep.join(bits_str(r["device"], r["bits"]) for r in rows))


def action_str(code):
    n, hi, lo = code >> 12, (code >> 6) & 0x3F, code & 0x3F
    low12 = code & 0xFFF
    if n == 0:
        return "table%d[%s]" % (
            code,
            ", ".join("p%d:%s(%d,%d)" % (a["phase"], a["fn"], a["arg1"], a["arg2"]) for a in actions.get(code, [])),
        )
    return {
        2: "hit:%s expire:%s" % ("ok+score" if hi == 0 else "-", "score" if lo == 0 else "-"),
        3: "expire:%s" % ("score" if low12 == 0 else "-"),
        4: "hit:ok+score" if low12 == 0 else "hit:-",
        5: "hit:beep+D7C0:0008",
        6: "hit:death%d expire:death%d" % (hi, lo),
        7: "hit:death%d" % low12,
        8: "expire:death%d" % low12,
    }.get(n, "?%04X" % code)


def clip_str(asset_id):
    out = []
    for a in assets.get(asset_id, []):
        if a["type"] == 1:
            c = clips[a["ref"]]
            out.append(
                "clip%d %d-%d (%d fr) audio%d"
                % (c["id"], c["start"], c["stop"], c["stop"] - c["start"], c["audio_mask"])
            )
        else:
            out.append("type%d ref%d" % (a["type"], a["ref"]))
    return "; ".join(out) or "no asset"


def show(rec, indent=""):
    print(
        "%s#%d C%d E%d %s weight=%d points=%d asset=%d: %s"
        % (
            indent,
            rec["index"],
            rec["f0"],
            rec["f3"],
            KIND.get(rec["f1"], rec["f1"]),
            rec["f2"],
            rec["f6"],
            rec["f5"],
            clip_str(rec["f5"]),
        )
    )
    for r in rules.get(rec["f4"], []):
        w = windows[r["window"]]
        print(
            "%s    win%d a=%d-%d b=%d-%d  %-22s -> %s%s"
            % (
                indent,
                w["id"],
                w["open_a"],
                w["close_a"],
                w["open_b"],
                w["close_b"],
                move_str(r["move"]),
                action_str(r["action"]),
                "  [stop]" if r["move"] & 0x8000 else "",
            )
        )


def main():
    ch = int(sys.argv[1]) if len(sys.argv) > 1 else None
    ep = int(sys.argv[2]) if len(sys.argv) > 2 else None
    cur = None
    for rec in episodes:
        if rec["f1"] not in (10003, 2000):
            cur = rec
        if ch is not None and cur["f0"] != ch:
            continue
        if ep is not None and cur["f3"] != ep:
            continue
        show(rec, "" if rec is cur else "  ")


if __name__ == "__main__":
    main()
