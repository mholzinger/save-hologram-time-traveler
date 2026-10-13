#!/usr/bin/env python3
"""Extract known data tables from the Time Traveler program ROM.

Usage: extract_tables.py <tt061891.bin> <outdir>

Every table here is located by address found in pass-1 analysis
(see docs/ROM_FINDINGS.md). The ROM hash is checked first so the
addresses can't silently apply to the wrong set (e.g. timetrv2).
"""

import csv
import hashlib
import json
import os
import struct
import sys

ROM_BASE = 0xC0000
DGROUP_ROM = 0xDBE10  # DGROUP initial image in ROM (DBE1:0000, copied to RAM 01EB:0000 at boot)
EXPECTED_SHA1 = "7c5003b6d3df1e472db45abd725e7d3d43f0dfb4"  # timetrv / tt061891.bin

# Clip table, C52E:6EC2. Layout from the lookup C4B6:00CB (key at +0, stride 8, key 0 ends,
# scan starts at id 1 = C52E:6ECA) and its consumer C4B6:020B (+2 start, +4 stop,
# byte +6 compared with 1/2/3, byte +7 passed to ld_audio D37E:0F2B).
CLIP_TABLE = 0xCC1A2  # 348 x {u16 id, u16 start, u16 stop, u8 kind, u8 audio_mask}
CLIP_COUNT = 348
# Asset table, C52E:5DF6. Lookup C4B6:0035 (key +0, stride 12, key 0 ends); count C4B6:0081 (< 0x1C2).
# Consumer C4B6:03FC: picks the record with lo(+6) <= t < hi(+8) (hi 0 = open), then
# dispatch C4B6:0158(type +2, ref +4, t - lo). type 1 -> clip id; 3 -> D47D:00F3(ref); 4 -> DA9D:0002.
ASSET_TABLE = 0xCB0D6  # {u16 asset_id, u16 type, u16 ref, u16 lo, u16 hi, u16 flags}
# Episode table, C52E:7E42. Count loop C40B:0004 (stride 14, ends when +6 == 0xFFFF, < 0x1C2).
# Field meanings not yet decoded; emitted raw as f0..f6.
EPISODE_TABLE = 0xCD122  # 7 x u16
# Input windows, C52E:07C4. window_find CFD9:0002 (fast path id*10 + 0x7BA, else scan; key 0 ends).
# Timing check CFD9:0249 uses +6 as open and +8 (or +4, per-player flag g[0x13C4+2p]) as close.
WINDOW_TABLE = 0xC5AA4  # {u16 id, u16 open_a, u16 close_a, u16 open_b, u16 close_b}; frames within the clip
# Action table, C52E:2D3A. action_find D047:000C (stride 12, key 0 ends); action_do D047:0058.
ACTION_TABLE = 0xC801A  # {u16 code, u8 phase, u8 argc, u16 fn_off, u16 fn_seg, u16 arg1, u16 arg2}
# Input rules, C52E:2FB6. rule_find CE55:02F5 (stride 8, key 0 ends, count < 0x5DC); matcher CFD9:0082.
RULE_TABLE = 0xC8296  # {u16 key, u16 move (bit15 = stop on miss), u16 window_id, u16 action}
# Move definitions, C52E:79AA. move_find CE55:059F; matcher CE55:03CC.
MOVE_TABLE = 0xCCC8A  # {u16 move_id, u16 mode (1 single, 2 any, 3 sequence), u16 device, u16 bits}
# Rare-episode substitution groups, DGROUP 0x00CA, read by C359:0620 (50% of games, 3% per group per pass):
# pairs {u16 chapter, u16 episode}; first pair = episode switched in, following pairs = switched out;
# (0, x) ends a group, (0, 0) ends the list.
SUBST_TABLE = DGROUP_ROM + 0x00CA
# Chapter slot weights, D372:0060 (segment word g[0x196C]), read by C3CE:0055 chapter_order_init:
# word at 0x60 + 2*(slot*5 + chapter) = % weight of `chapter` in `slot`, slots 3..7, chapters 2..6.
SLOT_WEIGHTS = 0xD3720 + 0x60
FIXED_SLOTS = {0: 10000, 1: 0, 2: 1, 8: 7, 9: 8, 10: 20}  # C3CE:0055 stores these literally
# Time Map options, D843:0B33 (in the map module's code segment), read by D843:0889 / D843:00B9:
# 8-byte {u16 key, u16 still_start, u16 still_stop, i16 chapter}; key = completed-chapter bits |
# 1 << (bonus + 8); groups separated by {0, 1, 1, x}; {0, 0, 0, x} ends. chapter -1 = Trader entry
# (skipped by D843:093B when already used or cubes are full).
TIME_MAP = 0xD8430 + 0x0B33
LED_MESSAGES = 0xC52E0 + 0x0008  # C52E:0008, read by D47D:009A
# Sounds: tone(freq, len) is D2DC:0150 (timer 0 CMPA = CMPB = 2,000,000 / freq, CON 0xC003; len on the
# 30 Hz timer list, D245:04B1 slot 5); tunes are {freq, len} lists played by D2DC:0179 until {0, 0}.
TONE_CALLS = {
    "beep": 0xDA9D0 + 0x0B,
    "beep_long": 0xDA9D0 + 0x30,
    "beep_short": 0xDA9D0 + 0x55,
    "low": 0xDA9D0 + 0x8F,
    "low_long": 0xDA9D0 + 0xB4,
}  # "mov ax, len; push; mov ax, freq" in DA9D:0002/0027/004C/0086/00AB
TUNES = {
    "warble": (0xD2DC, 0x0110, "D2DC:021C, with LD effects 1-3 (DA9D:0C5E)"),
    "death": (0xD2DC, 0x0024, "D2DC:0238, from death(n) D047:053A"),
    "alarm": (0xD372, 0x0036, "D2DC:01FD, Hellgate results D0F0:03A0-0817, high score DA9D:07EA"),
    "alarm_short": (0xD372, 0x000E, "D2DC:0254, LED token <SSOUND> (D47D:0EAA)"),
    "warp": (0xD2DC, 0x0040, "D2DC:02CA, LED token <WARP> (D47D:0ECD), D945:0E51"),
    "yeardone": (0xD2DC, 0x00F8, "D2DC:02E6, LED token <YEARDONE> (D47D:0EF0), chapter_advance D70D:0021"),
}
SPECOBJ_TABLE = DGROUP_ROM + 0x117A  # {u16 key, u16 audio_mask, u16 start, u16 stop}, key==0 ends


def lin(addr):
    return addr - ROM_BASE


def write_json(out, name, obj):
    with open(os.path.join(out, name), "w") as f:
        json.dump(obj, f, indent=1)


def read_keyed(rom, addr, fmt, names, stride, end=lambda r: r[0] == 0):
    """Records of struct `fmt` from addr, `stride` apart, until end(record)."""
    recs, o = [], lin(addr)
    while True:
        r = struct.unpack_from(fmt, rom, o)
        if end(r):
            return recs
        recs.append(dict(zip(names, r)))
        o += stride


# ---- One reader per ROM table ------------------------------------------------------------------------------------


def read_clips(rom):
    clips = []
    for i in range(CLIP_COUNT):
        cid, s, e, kind, audio = struct.unpack_from("<3H2B", rom, lin(CLIP_TABLE) + i * 8)
        assert cid == i, f"clip table desync at index {i} (id {cid})"
        clips.append({"id": cid, "start": s, "stop": e, "kind": kind, "audio_mask": audio})
    assert struct.unpack_from("<H", rom, lin(CLIP_TABLE) + CLIP_COUNT * 8)[0] == 0, "clip table terminator missing"
    return clips


def read_assets(rom):
    assets = read_keyed(rom, ASSET_TABLE, "<6H", ("asset_id", "type", "ref", "lo", "hi", "flags"), 12)
    assert len(assets) < 0x1C2
    return assets


def read_episodes(rom):
    episodes, o = [], lin(EPISODE_TABLE)
    while True:
        r = struct.unpack_from("<7H", rom, o)
        if r[3] == 0xFFFF:
            break
        episodes.append({"index": len(episodes), **{f"f{i}": v for i, v in enumerate(r)}})
        o += 14
    assert len(episodes) < 0x1C2
    return episodes


def read_input_tables(rom):
    """Windows, actions, rules and moves: the tables the input matcher reads."""
    windows = read_keyed(rom, WINDOW_TABLE, "<5H", ("id", "open_a", "close_a", "open_b", "close_b"), 10)
    assert all(w["id"] == i + 1 for i, w in enumerate(windows)), "window ids must equal index + 1 (fast path)"
    actions = read_keyed(rom, ACTION_TABLE, "<HBB4H", ("code", "phase", "argc", "fn_off", "fn_seg", "arg1", "arg2"), 12)
    for a in actions:
        a["fn"] = f"{a['fn_seg']:04X}:{a['fn_off']:04X}"
    rules = read_keyed(rom, RULE_TABLE, "<4H", ("key", "move", "window", "action"), 8)
    assert len(rules) < 0x5DC
    moves = read_keyed(rom, MOVE_TABLE, "<4H", ("move_id", "mode", "device", "bits"), 8)
    return windows, actions, rules, moves


def read_substitutions(rom):
    subst, cur, o = [], None, lin(SUBST_TABLE)
    while True:
        ch, ep = struct.unpack_from("<2H", rom, o)
        o += 4
        if ch == 0 and ep == 0:
            break
        if ch == 0:
            subst.append(cur)
            cur = None
        elif cur is None:
            cur = {"in": [ch, ep], "out": []}
        else:
            cur["out"].append([ch, ep])
    assert cur is None, "substitution list ended mid-group"
    return subst


def read_specobj(rom):
    return read_keyed(rom, SPECOBJ_TABLE, "<4H", ("key", "audio_mask", "start", "stop"), 8)


def read_time_map(rom):
    time_map, o = {}, lin(TIME_MAP)
    while True:
        key, a, b_, ch = struct.unpack_from("<3Hh", rom, o)
        o += 8
        if key == 0 and a == 0 and b_ == 0:
            break
        if key == 0:
            continue
        time_map.setdefault(f"{key:04X}", []).append({"chapter": ch, "still": [a, b_]})
    return time_map


# DGROUP offsets of the strings the LED placeholder expander D47D:0294 / D47D:0F7C writes, and the blank
# line D47D:1237 shows (0x10F8).
LED_STRINGS = {
    "blank16": 0x10F8,
    "h": 0xE74,
    "score_pad": 0xE8B,
    "player1": 0xEA3,
    "player2": 0xF5A,
    "first": 0xEED,
    "second": 0xF14,
    "third": 0xF3B,
    "hs_pad": 0xEF2,
    "orb": 0xFB5,
    "cube_obj": 0xFC4,
    "life": 0xFE4,
    "lives": 0xFEB,
    "credit": 0xFFA,
    "credits": 0x1003,
    "coin": 0x1019,
    "coins": 0x1020,
    "cube": 0x106A,
    "cubes": 0x1071,
    "lives_left": 0x10D7,
    "extra_life": 0x10E3,
    "last_life": 0x10EE,
}


def read_led(rom):
    """The LED panel's messages, token strings, era names and default hall of fame.

    Messages: D47D:00F3 show(id) -> D47D:009A finds the first record with that id; the following records with
    the same id are its further lines, D47D:015D/01A5 show each. Records are 10 bytes {u16 id, u16 mode, u16
    text_off, u16 text_seg, u16 time}; text_seg 0x01EB = DGROUP, whose image is at ROM DBE1:0000. id 0 records
    are pauses between lines. Ends at {0, 0, ...}. The era names are what <YEAR> reads (D70D:0000: far
    pointers at DGROUP 0x1346). Hall of fame: initials at DGROUP 0x0B45 region (ROM DC955), scores are
    "mov ax/dx" constants at D2229 / D224E / D2273 in the account init."""

    def dstr(off):
        t = DGROUP_ROM - 0xC0000 + off
        return rom[t : rom.index(b"\0", t)].decode("latin-1")

    strings = {k: dstr(v) for k, v in LED_STRINGS.items()}
    eras = []
    for ch in range(7):
        toff, tseg = struct.unpack_from("<2H", rom, DGROUP_ROM - 0xC0000 + 0x1346 + 4 * ch)
        assert tseg == 0x01EB
        eras.append(dstr(toff))
    hof_init = rom[0xDC955 - 0xC0000 : 0xDC955 - 0xC0000 + 12].split(b"\0")[:3]
    hof_scores = []
    for at in (0xD2229, 0xD224E, 0xD2273):
        o = at - 0xC0000
        assert rom[o] == 0xB8 and rom[o + 3] == 0xBA, hex(at)  # mov ax, lo ; mov dx, hi
        hof_scores.append(struct.unpack_from("<H", rom, o + 1)[0] | struct.unpack_from("<H", rom, o + 4)[0] << 16)
    hof = [{"initials": i.decode(), "score": sc} for i, sc in zip(hof_init, hof_scores)]
    msgs, o = [], lin(LED_MESSAGES)
    while True:
        mid, mode, toff, tseg, time = struct.unpack_from("<5H", rom, o)
        if mid == 0 and mode == 0:
            break
        rec = {"id": mid, "mode": mode, "time": time}
        if tseg == 0x01EB:
            rec["text"] = dstr(toff)
        msgs.append(rec)
        o += 10
    assert len(msgs) == 197, len(msgs)
    return msgs, strings, eras, hof


def read_sounds(rom):
    sounds = {"tones": {}, "tunes": {}}
    for name, at in TONE_CALLS.items():
        o = at - 0xC0000
        assert rom[o] == 0xB8 and rom[o + 3] == 0x50 and rom[o + 4] == 0xB8, name  # mov ax,len; push ax; mov ax,freq
        length, freq = struct.unpack_from("<H", rom, o + 1)[0], struct.unpack_from("<H", rom, o + 5)[0]
        sounds["tones"][name] = {"freq": freq, "len": length}
    for name, (seg, off, why) in TUNES.items():
        o, seq = seg * 16 + off - 0xC0000, []
        while True:
            f, l = struct.unpack_from("<2H", rom, o + 4 * len(seq))
            if f == 0 and l == 0:
                break
            seq.append([f, l])
            assert len(seq) < 64, name
        sounds["tunes"][name] = {"at": f"{seg:04X}:{off:04X}", "used_by": why, "steps": seq}
    return sounds


def read_slot_weights(rom):
    slot_weights = {
        slot: {ch: struct.unpack_from("<h", rom, lin(SLOT_WEIGHTS) + 2 * (slot * 5 + ch))[0] for ch in range(2, 7)}
        for slot in range(3, 8)
    }
    for slot, w in slot_weights.items():
        assert sum(w.values()) == 100, f"slot {slot} weights do not sum to 100: {w}"
    return slot_weights


def led_json(rom):
    """build/led_messages.json: the LED text tools/pack_game.py packs (also written by tools/patcher.py)."""
    msgs, strings, eras, hof = read_led(rom)
    return {
        "source": f"ROM {LED_MESSAGES:05X} (C52E:0008), 10-byte records, read by D47D:009A; "
        "text from the DGROUP image at DBE1:0000",
        "records": msgs,
        "strings": strings,
        "eras": eras,
        "hall_of_fame": hof,
    }


# ---- main: read every table, write data/*.json (and the LED text to build/) ---------------------------------------


def main():
    rom_path, out = sys.argv[1], sys.argv[2]
    rom = open(rom_path, "rb").read()
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != EXPECTED_SHA1:
        sys.exit(f"ROM SHA1 {sha1} != expected {EXPECTED_SHA1} (tt061891.bin). Addresses are set-specific.")
    os.makedirs(out, exist_ok=True)

    clips = read_clips(rom)
    write_json(
        out,
        "clips.json",
        {
            "source": f"ROM {CLIP_TABLE:05X} (C52E:6EC2), {CLIP_COUNT} x 8 bytes {{u16 id,u16 start,u16 stop,u8 kind,u8 audio_mask}}, lookup C4B6:00CB",
            "records": clips,
        },
    )
    with open(os.path.join(out, "clips.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["clip_id", "kind", "audio_mask", "start_frame", "stop_frame", "frames", "seconds_29_97"])
        for c in clips:
            n = c["stop"] - c["start"]
            w.writerow([c["id"], c["kind"], c["audio_mask"], c["start"], c["stop"], n, round(n / 29.97, 2)])

    assets = read_assets(rom)
    write_json(
        out,
        "assets.json",
        {"source": f"ROM {ASSET_TABLE:05X} (C52E:5DF6), 12-byte records, lookup C4B6:0035", "records": assets},
    )

    episodes = read_episodes(rom)
    write_json(
        out,
        "episode_table.json",
        {
            "source": f"ROM {EPISODE_TABLE:05X} (C52E:7E42), 14-byte records, count C40B:0004. "
            "Fields undecoded: f4 == f5 == asset id in every record seen.",
            "records": episodes,
        },
    )

    windows, actions, rules, moves = read_input_tables(rom)
    for name, addr, recs, what in (
        ("windows", WINDOW_TABLE, windows, "10-byte input windows, window_find CFD9:0002"),
        ("actions", ACTION_TABLE, actions, "12-byte actions {code,phase,argc,far fn,arg1,arg2}, action_do D047:0058"),
        ("input_rules", RULE_TABLE, rules, "8-byte rules {key, move|0x8000 stop, window, action}, matcher CFD9:0082"),
        ("moves", MOVE_TABLE, moves, "8-byte move rows {move_id, mode, device, bits}, matcher CE55:03CC"),
    ):
        write_json(
            out, f"{name}.json", {"source": f"ROM {addr:05X} (C52E:{addr - 0xC52E0:04X}), {what}", "records": recs}
        )

    subst = read_substitutions(rom)
    write_json(
        out,
        "substitutions.json",
        {
            "source": f"ROM {SUBST_TABLE:05X} (DGROUP 0x00CA), read by C359:0620; [chapter, episode] pairs",
            "records": subst,
        },
    )

    spec = read_specobj(rom)
    write_json(
        out,
        "specobj_clips.json",
        {"source": f"ROM {SPECOBJ_TABLE:05X} (DGROUP 01EB:117A), read by D47D:1D6C", "records": spec},
    )

    time_map = read_time_map(rom)
    write_json(
        out,
        "time_map.json",
        {
            "source": f"ROM {TIME_MAP:05X} (D843:0B33), read by D843:0889; key = completed bits | 1 << (bonus+8)",
            "options": time_map,
        },
    )

    # The LED text is the ROM's own wording, so it goes to build/ (ignored), not data/.
    build = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build")
    os.makedirs(build, exist_ok=True)
    write_json(build, "led_messages.json", led_json(rom))

    sounds = read_sounds(rom)
    write_json(
        out,
        "sounds.json",
        {
            "source": "ROM tone/tune tables (see TONE_CALLS / TUNES in tools/extract_tables.py); "
            "freq = value whose square wave is value/2 Hz (timer 0 alternate compare at 2 MHz, "
            "D2DC:0273; the /2 is the 80188 dual-compare mode, UNVERIFIED), len in 1/30 s",
            **sounds,
        },
    )

    slot_weights = read_slot_weights(rom)
    game = build_game(clips, assets, episodes, windows, rules, moves, actions, subst, slot_weights, sha1)
    game["time_map"] = {k: [x["chapter"] for x in v] for k, v in time_map.items()}
    write_json(out, "game.json", game)

    print(
        f"clips={len(clips)} assets={len(assets)} episodes={len(episodes)} windows={len(windows)} "
        f"rules={len(rules)} moves={len(moves)} actions={len(actions)} subst={len(subst)} time_map={len(time_map)} specobj={len(spec)} -> {out}"
    )


# ---------------------------------------------------------------------------
# game.json: one self-contained scene graph built from the tables above.
# Semantics and their sources are in docs/ROM_FINDINGS.md ("Input model", "Built-in actions",
# "Per-game episode selection", "Lives, cubes, Trader, Hellgate", "Chapter order and Time Map").

KIND_NAMES = {
    10000: "system",
    1000: "pick_one",
    1001: "pick_one_bonus",
    1005: "chance",
    1006: "chance_bonus",
    10003: "death",
    2000: "group_end",
}

# Input names: device/bit layout from D17E:0121, D17E:0002, D17E:02D5; names from
# reference/mame/timetrv.cpp:210-224.
INPUTS = {
    (1, 1): "up",
    (1, 2): "down",
    (1, 4): "left",
    (1, 8): "right",
    (0x10, 0x100): "attack",
    (0x10, 0x200): "cube",
    (0x20, 0x10): "start1",
    (0x20, 0x20): "start2",
    (4, 0x40): "coin",
    (4, 0x80): "bill",
}

# Table-action targets, by far address. Names from re/ghidra/scripts/TimeTravelerLabels.java
# or from reading the function (cited in ROM_FINDINGS); "effect" = presentation only (beep, LD speed).
ACTION_FNS = {
    "D047:0343": "phase_misc",  # (0 beep, 1 episode_done, 2 D668:0081, 3 beep + ...)
    "D047:03E5": "episode_end",  # action_episode_end
    "D047:0679": "cube_rewind",
    "DA9D:0002": "effect",
    "DA9D:0027": "effect",
    "DA9D:0086": "effect",
    "DA9D:00AB": "effect",
    "DA9D:0C5E": "effect",  # LD speed/still effects by arg (DB62E)
    "DA9D:0071": "noop",
    "DA9D:0E6D": "trap_c3e82",  # fail C3 E82, lose_life, episode_done (DB83D)
    "DA9D:0EF8": "start_game",  # beep, episode_enter(C1 E10) (DB8C8)
    "DA9D:0F3A": "cubes_grant_start",
    "D0F0:023A": "hellgate",
    "D37E:0F2B": "ld_audio",
    "C40B:07C6": "next_episode",  # maybe_episode_next + episode_enter (C4876)
    "C40B:0889": "replay_from",  # beep + episode_enter(current, t = arg1) (C4939 -> C4904)
    "C2BA:039F": "attract_cycle",  # g[0x1A]-- ; alternates C10000 E20 / E200 (C2F3F)
    "C2BA:037F": "attract_next",  # D3FA7(0) + next_episode (C2F1F)
    "D659:000C": "chapter_next_quiet",  # beep, D70D:040E chapter_next, [p*2+0x74] = 0 (D659C)
    "D659:0041": "choice_left",  # beep, play LD 129-245, next_episode, [p*2+0x74] = 1 (D65D1)
    "D659:0086": "choice_right",  # beep, episode_enter(C1 E30) (D6616)
    "D659:00C8": "choice_other",  # beep, play LD 14064-14181, next_episode (D6658)
}


# Game rules read from code rather than from a table (ROM addresses in docs/ROM_FINDINGS.md); game.json carries
# them so tools/sim.py and tools/pack_game.py need nothing else.
GAME_CONSTANTS = {
    "scoring": {"multiplier": [[90000, 2], [100000, 1], [None, 10]]},
    "lives": {"dip_values": [2, 3, 4, 5], "default": 3, "free_in_chapters": [0], "lost_only_if_points": True},
    # 0 at game start: the first cube comes from C1 E5's cubes_grant_start (DA9D:0F3A). ROM run:
    # 0x37:0x1516 stays 0 from 1P Start until tick 5967 (C1 E5), and the LED status reads "0 CUBES".
    "cubes": {"start": 0, "max_dip_values": [6, 40]},
    "hellgate_slot": {"chance_percent": 85, "slots": [4, 5, 6, 7], "source": "D668:012B"},
    "hellgate": {
        "thresholds_devil_takes_lives": [
            [5, "jackpot"],
            [45, "life"],
            [55, "cubes2"],
            [60, "lose_life"],
            [90, "lose_life"],
            [100, "lose_all"],
        ],
        "thresholds_devil_never": [
            [5, "jackpot"],
            [30, "life"],
            [35, "cubes2"],
            [60, "lose_none"],
            [90, "lose_none"],
            [100, "lose_none"],
        ],
        "win_score": 25000,
        "lose_score": -25000,
    },
}


def build_game(clips, assets, episodes, windows, rules, moves, actions, subst, slot_weights, sha1):
    """game.json: every episode header with its rules (window, move, action) and media, its death records
    under it, plus the chapter slots, the selection tables and GAME_CONSTANTS."""
    clip_by_id = {c["id"]: c for c in clips}
    win_by_id = {w["id"]: w for w in windows}
    assets_by_id, rules_by_key, moves_by_id, actions_by_code = {}, {}, {}, {}
    for a in assets:
        assets_by_id.setdefault(a["asset_id"], []).append(a)
    for r in rules:
        rules_by_key.setdefault(r["key"], []).append(r)
    for m in moves:
        moves_by_id.setdefault(m["move_id"], []).append(m)
    for a in actions:
        actions_by_code.setdefault(a["code"], []).append(a)

    def move_desc(mid):
        if mid & 0x7FFF == 0:
            return {"mode": "none", "inputs": []}
        rows = moves_by_id[mid & 0x7FFF]
        mode = {1: "single", 2: "any", 3: "sequence"}[rows[0]["mode"]]
        ins, dead = [], []
        for r in rows:
            names = [n for (d, b), n in INPUTS.items() if d == r["device"] and r["bits"] & b]
            if not names:
                # Row whose bits belong to another device: move_match (CE55:03CC) compares the event's
                # device with the row's, so it can never match (moves 8 and 35 list Attack/Cube under
                # device 1). Kept for fidelity, flagged dead.
                names = [n for (d, b), n in INPUTS.items() if r["bits"] & b]
                assert names, f"unknown bits {r['bits']:#x} in move {mid}"
                dead.extend(names)
            # A button event with the Cube bit (0x200) never reaches rule matching: input_event
            # (CE55:01D7) uses it only to rewind while a death plays, else drops it (-> CE55:0247).
            # Rules listing Cube can't match it (seen on the ROM: tools/romdiff.py --random 4, C1 E35).
            dead.extend(n for n in names if n == "cube" and n not in dead)
            ins.append(names)
        d = {"id": mid & 0x7FFF, "mode": mode, "inputs": ins}
        if dead:
            d["dead_inputs"] = dead
        return d

    def action_desc(code):
        n, hi, lo, low12 = code >> 12, (code >> 6) & 0x3F, code & 0x3F, code & 0xFFF
        if n == 0:
            calls = []
            for a in actions_by_code[code]:
                calls.append(
                    {
                        "phase": a["phase"],
                        "fn": a["fn"],
                        "does": ACTION_FNS.get(a["fn"], "unknown"),
                        "args": [a["arg1"], a["arg2"]][: max(a["argc"], 0) or 2],
                    }
                )
            return {"code": code, "table": calls}
        # Built-in, D047:00E6. Each entry: what happens on phase 1 (input) and phase 2 (window closed).
        hit = expire = None
        if n == 2:
            hit = "ok" if hi == 0 else None
            expire = "score" if lo == 0 else None
        elif n == 3:
            expire = "score" if low12 == 0 else None
        elif n == 4:
            hit = "ok" if low12 == 0 else None
        elif n == 5:
            hit = "ok_episode_done"
        elif n == 6:
            hit, expire = f"death{hi}", f"death{lo}"
        elif n == 7:
            hit = f"death{low12}"
        elif n == 8:
            expire = f"death{low12}"
        else:
            raise ValueError(f"unknown built-in action {code:#06x}")
        return {"code": code, "hit": hit, "expire": expire}

    def clip_of(asset_id):
        out = []
        for a in assets_by_id.get(asset_id, []):
            if a["type"] == 1:
                c = clip_by_id[a["ref"]]
                out.append({"clip": c["id"], "start": c["start"], "stop": c["stop"], "audio": c["audio_mask"]})
            else:
                out.append({"type": a["type"], "ref": a["ref"]})
        return out

    def rec_desc(r):
        rl = []
        for x in rules_by_key.get(r["f4"], []):
            w = win_by_id[x["window"]]
            rl.append(
                {
                    "window": w["id"],
                    "a": [w["open_a"], w["close_a"]],
                    "b": [w["open_b"], w["close_b"]],
                    "move": move_desc(x["move"]),
                    "stop_on_miss": bool(x["move"] & 0x8000),
                    "action": action_desc(x["action"]),
                }
            )
        return {
            "index": r["index"],
            "chapter": r["f0"],
            "episode": r["f3"],
            "kind": KIND_NAMES.get(r["f1"], r["f1"]),
            "weight": r["f2"],
            "points": r["f6"],
            "asset": r["f5"],
            "media": clip_of(r["f5"]),
            "rules": rl,
        }

    # A kind-2000 record ends a weighted pick-one run in choose_episodes (C359:00B6 looks at the next
    # record's kind); keep that boundary as run_start on the following header.
    eps, cur, brk = [], None, True
    for r in episodes:
        if r["f1"] == 2000:
            cur, brk = None, True
            continue
        if r["f1"] == 10003:
            assert cur is not None, f"death record {r['index']} without a header"
            d = rec_desc(r)
            cur["deaths"][str(r["f3"])] = d
            continue
        cur = rec_desc(r)
        cur["deaths"] = {}
        cur["run_start"] = brk or not eps or eps[-1]["chapter"] != cur["chapter"]
        brk = False
        eps.append(cur)

    return {
        "source": {
            "rom_sha1": sha1,
            "built_by": "tools/extract_tables.py",
            "notes": "Derived scene graph. Semantics: docs/ROM_FINDINGS.md. Frames are LD frames (29.97 fps).",
        },
        "chapter_slots": {
            "fixed": {str(k): v for k, v in FIXED_SLOTS.items()},
            "weights": {str(s): {str(c): w for c, w in ws.items()} for s, ws in slot_weights.items()},
            "era_slots": [3, 4, 5, 6],
            "map_chapters": [2, 3, 4, 5],
        },
        "selection": {
            "bonus_chapter_range": [2, 5],
            "rare_chance_percent": 50,
            "rare_group_percent": 3,
            "substitutions": subst,
        },
        **GAME_CONSTANTS,
        "episodes": eps,
    }


if __name__ == "__main__":
    main()
