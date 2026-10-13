#!/usr/bin/env python3
"""Pack data/game.json (+ data/clips.json) into the binary scene graph read by
port/3ds/source/engine.c.

    python3 -I tools/pack_game.py data build/timetrv.ttg

Layout (all little-endian; struct definitions mirrored in port/3ds/source/engine.h):
  header   "TTG7", u16 n_eps, u16 n_rules, u16 n_subst, u16 n_clips, u8 weights[5][5] (slot 3..7 x ch 2..6)
  eps[]    20 bytes: u16 chapter, u16 episode, u8 kind, u8 weight, u8 flags(bit0 run_start, bit1 death),
           u8 n_deaths, u16 points, u16 clip, u16 first_rule, u16 n_rules, u16 ld_frames, u16 msg
           (LED message shown when the record starts: its type-3 asset, C4B6:0158; 0 = none)
           (deaths follow their header)
  rules[]  22 bytes: u16 window, u16 a_lo, u16 a_hi, u16 b_lo, u16 b_hi, u16 inputs, u8 mode,
           u8 flags (bit0 stop_on_miss, bit1 may fire on window close, bit2 good input for the perfect policy,
           bit3 the matching-input action beeps first),
           u8 hit_op, u8 hit_ret, u8 expire_op, u8 expire_ret, u16 hit_arg, u16 expire_arg
  subst[]  14 bytes: u16 in_ch, u16 in_ep, u8 n_out, u8 pad, u16 out[2][2] (chapter, episode)
  clips[]  12 bytes: u16 clip, u16 source (0), u32 ld_start, u16 ld_frames, u8 audio_mask, u8 1
           (every LD clip record, data/clips.json; was the DVD placement table, which nothing read)
  titles   NUL-separated source names, indexed by source (one: "LD")
  tmap     u16 n, then n x 20 bytes: u16 key, u8 n_opts, u8 pad, 4 x {i16 chapter (-1 = Trader), u16 LD still}
           (Time Map, D843:0B33 / data/time_map.json; each option's still is 5 LD frames from that frame)
  msgs     u16 n, then n x {u16 id, u8 mode, u8 pad, u16 time, NUL-terminated text}: every record of the
           LED message table C52E:0008 in order, control records included (build/led_messages.json)
  sounds   u16 n, then n x {u8 id, u8 steps, steps x {u16 freq, u16 len}} (data/sounds.json; ids TT_SND_*)
  strings  u16 n, then n x {key\0 value\0}: LED token strings, era<i> names, hof<i> "INI score" (defaults)
Each rule phase (1 = matching input, 2 = window closing) is one op with the action's return code
(1 clear window, 2 stop, 0 go on), the same decisions tools/sim.py makes in Game.act.
"""

import json
import os
import struct
import sys

INPUT_BITS = {
    "up": 1,
    "down": 2,
    "left": 4,
    "right": 8,
    "attack": 16,
    "cube": 32,
    "start1": 64,
    "start2": 128,
    "coin": 256,
    "bill": 512,
}
MODES = {"none": 0, "single": 1, "any": 2, "sequence": 3}
KINDS = {"system": 0, "pick_one": 1, "pick_one_bonus": 2, "chance": 3, "chance_bonus": 4, "death": 5}
# ops (engine.h TT_OP_*)
OP = {
    "none": 0,
    "score": 1,
    "done": 2,
    "death": 3,
    "jump": 4,
    "fail": 5,
    "rewind": 6,
    "replay": 7,
    "hellgate": 8,
    "cubes": 9,
    "skip": 10,
    "learn": 11,
    "speed": 12,
    "sound": 13,
}
# Sound ids (engine.h TT_SND_*), data/sounds.json: tones, then tunes.
SOUNDS = [
    "beep",
    "beep_long",
    "beep_short",
    "low",
    "low_long",
    "warble",
    "death",
    "alarm",
    "alarm_short",
    "warp",
    "yeardone",
]
TONE_FN = {"DA9D:0002": 0, "DA9D:0027": 1, "DA9D:004C": 2, "DA9D:0086": 3, "DA9D:00AB": 4}
# Table actions whose function starts with a beep (DA9D:0002): D659:000C/0041/0086/00C8, start_game
# DA9D:0EF8, replay_from C40B:0889 (docs/ROM_FINDINGS.md "Remaining table-action targets").
BEEP_FIRST = {"D659:000C", "D659:0041", "D659:0086", "D659:00C8", "DA9D:0EF8", "C40B:0889", "DA9D:0E6D"}
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import load_json  # noqa: E402
import sim  # noqa: E402


def table_op(calls, phase):
    """-> (op, ret, arg), mirroring sim.Game.act for a table action."""
    for t in calls:
        if t["phase"] != phase:
            continue
        d, arg = t["does"], t["args"][0]
        ret = sim.Game.BEEP_FNS.get(t["fn"], sim.Game.TABLE_RET.get(d, 0))
        op = {
            "next_episode": "done",
            "chapter_next_quiet": "skip",
            "choice_left": "learn",
            "choice_other": "done",
            "choice_right": "jump",
            "trap_c3e82": "fail",
            "cube_rewind": "rewind",
            "replay_from": "replay",
            "hellgate": "hellgate",
            "cubes_grant_start": "cubes",
        }.get(d, "none")
        if d in ("episode_end", "phase_misc") and arg == 1:
            op = "done"
        if d == "start_game":
            op, arg = "jump", 10  # DA9D:0EF8: beep, enter C1 E10
        if op == "jump" and d == "choice_right":
            arg = 30  # C1 E30 (D659:0086)
        if t["fn"] == "DA9D:0C5E":  # LD speed effect n (300/50/100, 1-6 with a sound first)
            op = "speed"
        elif t["fn"] in TONE_FN:
            op, arg = "sound", TONE_FN[t["fn"]]
        return OP[op], ret, arg if op in ("replay", "cubes", "jump", "speed", "sound") else 0
    return OP["none"], 0, 0


def builtin_op(code, phase):
    """-> (op, ret, arg), mirroring sim.Game.act for a built-in action code."""
    n, hi, lo, low12 = code >> 12, (code >> 6) & 0x3F, code & 0x3F, code & 0xFFF
    if phase == 1:
        if (n == 2 and hi == 0) or (n == 4 and low12 == 0):
            return OP["score"], 1, 0
        if n == 5:
            return OP["done"], 1, 0
        if n == 6:
            return OP["death"], 1, hi
        if n == 7:
            return OP["death"], 1, low12
    else:
        if (n == 2 and lo == 0) or (n == 3 and low12 == 0):
            return OP["score"], 1, 0
        if n == 6:
            return OP["death"], 1, lo
        if n == 8:
            return OP["death"], 1, low12
    return OP["none"], 0, 0


def main():
    d, out = sys.argv[1], sys.argv[2]
    g = load_json(os.path.join(d, "game.json"))
    eps, rules = [], []

    def add(rec, kind, flags, n_deaths):
        first = len(rules)
        for r in rec["rules"]:
            mask = 0
            for grp in r["move"]["inputs"]:
                for n in grp:
                    if n not in r["move"].get("dead_inputs", []):
                        mask |= INPUT_BITS[n]
            a = r["action"]
            if "table" in a:
                h, e = table_op(a["table"], 1), table_op(a["table"], 2)
            else:
                h, e = builtin_op(a["code"], 1), builtin_op(a["code"], 2)
            closes = r["move"]["mode"] == "none" or a.get("code", 0) >> 12 == 6
            if "table" in a:
                beep = any(t["phase"] == 1 and t["fn"] in BEEP_FIRST for t in a["table"])
            else:  # built-in actions that beep on a matching input (D047:00E6 table)
                c = a["code"]
                n_, hi_, low12_ = c >> 12, (c >> 6) & 0x3F, c & 0xFFF
                beep = (n_ == 2 and hi_ == 0) or (n_ == 4 and low12_ == 0) or n_ in (5, 6, 7)
            good = sim.perfect(None, rec, rec, r["window"], [r], []) != "none"
            rflags = int(r["stop_on_miss"]) | closes << 1 | good << 2 | beep << 3
            rules.append(
                struct.pack(
                    "<6H6B2H",
                    r["window"],
                    r["a"][0],
                    r["a"][1],
                    r["b"][0],
                    r["b"][1],
                    mask,
                    MODES[r["move"]["mode"]],
                    rflags,
                    h[0],
                    h[1],
                    e[0],
                    e[1],
                    h[2],
                    e[2],
                )
            )
        med = next((m for m in rec["media"] if "clip" in m), None)
        clip = med["clip"] if med else 0xFFFF
        ld = med["stop"] - med["start"] if med else 0
        msg = next((m["ref"] for m in rec["media"] if m.get("type") == 3), 0)
        eps.append(
            struct.pack(
                "<2H4B6H",
                rec["chapter"] & 0xFFFF,
                rec["episode"],
                kind,
                rec["weight"],
                flags,
                n_deaths,
                rec["points"],
                clip,
                first,
                len(rec["rules"]),
                ld,
                msg,
            )
        )

    for e in g["episodes"]:
        add(e, KINDS[e["kind"]], 1 if e["run_start"] else 0, len(e["deaths"]))
        for k in sorted(e["deaths"], key=int):
            de = e["deaths"][k]
            de = dict(de, chapter=e["chapter"])
            add(de, KINDS["death"], 2, 0)
    subst = []
    for s in g["selection"]["substitutions"]:
        outs = s["out"] + [[0, 0]] * (2 - len(s["out"]))
        assert len(s["out"]) <= 2
        subst.append(
            struct.pack(
                "<2H2B4H", s["in"][0], s["in"][1], len(s["out"]), 0, outs[0][0], outs[0][1], outs[1][0], outs[1][1]
            )
        )
    # The LD clip table: where each clip lies on the disc, so the front end can tell how far a search
    # goes (seek blanking, port/3ds/source/fe_ld.c).
    titles = ["LD"]
    clips = [
        struct.pack("<2HI H2B", c["id"], 0, c["start"], c["stop"] - c["start"], c["audio_mask"], 1)
        for c in load_json(os.path.join(d, "clips.json"))["records"]
        if c["id"]
    ]
    w = g["chapter_slots"]["weights"]
    weights = bytes(w[str(s)][str(c)] for s in range(3, 8) for c in range(2, 7))
    tmap = []
    tm = load_json(os.path.join(d, "time_map.json"))["options"]
    for k, v in sorted(tm.items()):
        assert len(v) <= 4 and [o["chapter"] for o in v] == g["time_map"][k]
        opts = [(o["chapter"], o["still"][0]) for o in v] + [(0, 0)] * (4 - len(v))
        tmap.append(struct.pack("<HBB" + "hH" * 4, int(k, 16), len(v), 0, *[x for o in opts for x in o]))
    blob = (
        b"TTG7"
        + struct.pack("<4H", len(eps), len(rules), len(subst), len(clips))
        + weights
        + b"".join(eps)
        + b"".join(rules)
        + b"".join(subst)
        + b"".join(clips)
        + b"\0".join(t.encode() for t in titles)
        + b"\0"
        + struct.pack("<H", len(tmap))
        + b"".join(tmap)
    )
    led = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "led_messages.json")
    if not os.path.exists(led):
        sys.exit(f"missing {led}: run make tables first (tools/extract_tables.py writes the LED text)")
    with open(led) as f:
        ledj = json.load(f)
    msgs = ledj["records"]  # all of them, control records (id 0) included: the display module walks them
    snd = load_json(os.path.join(d, "sounds.json"))
    steps = [
        [[snd["tones"][n]["freq"], snd["tones"][n]["len"]]] if n in snd["tones"] else snd["tunes"][n]["steps"]
        for n in SOUNDS
    ]
    sound_blob = struct.pack("<H", len(steps)) + b"".join(
        struct.pack("<BB", i, len(st)) + b"".join(struct.pack("<2H", f, l) for f, l in st) for i, st in enumerate(steps)
    )
    blob += struct.pack("<H", len(msgs)) + b"".join(
        struct.pack("<HBBH", m["id"], m["mode"], 0, m["time"]) + m.get("text", "").encode("latin-1") + b"\0"
        for m in msgs
    )
    strs = (
        list(ledj["strings"].items())
        + [("era%d" % i, e) for i, e in enumerate(ledj["eras"])]
        + [("hof%d" % i, "%s %d" % (h["initials"], h["score"])) for i, h in enumerate(ledj["hall_of_fame"])]
    )
    blob += sound_blob
    blob += struct.pack("<H", len(strs)) + b"".join(k.encode() + b"\0" + v.encode("latin-1") + b"\0" for k, v in strs)
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    open(out, "wb").write(blob)
    print(
        f"{out}: {len(blob)} bytes, {len(eps)} records, {len(rules)} rules, {len(subst)} subst, "
        f"{len(clips)} LD clips, {len(msgs)} LED lines, {len(steps)} sounds"
    )


if __name__ == "__main__":
    main()
