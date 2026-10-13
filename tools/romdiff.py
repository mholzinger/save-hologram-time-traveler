#!/usr/bin/env python3
"""Differential test: the real ROM (tools/romrun.py) against the decoded rules (data/game.json).

    .venv/bin/python -I tools/romdiff.py [--seconds 900] [--start 30] [--dips 06,04,7E]

The ROM is run with no player input after 1P Start (or driven: --perfect, --random, and the probes
below). Every laserdisc "<start>SE ... <stop>SM PL" pair is one clip play, identified by (start, stop)
against data/game.json records. Check, for every scene header the ROM plays: what the decoded rules
(tools/sim.py play_record) say must follow, a specific death record or "scene done", against what the
ROM actually played next, and the score the scene added. Exits 1 on any disagreement (make test).

Layout: RAM and input helpers (Rom), one function per probe run every tick, the perfect/random driver,
then the rule check and the report.
"""

import argparse
import collections
import os
import random
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT, load_json  # noqa: E402
import romrun  # noqa: E402
import sim  # noqa: E402


# DGROUP (segment 0x37) words the probes read; the player's current record is a far pointer at [p*4+0x0A]
# into the episode table at C52E:7E42 (14-byte records), set up from C40B:0058..0069.
DGROUP = 0x370
RAM_RECORD, RAM_LIVES, RAM_SCORE, RAM_CUBES, RAM_CLIP_CLOCK = 0x0A, 0x424, 0x101C, 0x1516, 0x192A
EP_TABLE, EP_SIZE = 0x7E42, 14

# Inputs as (register, bit), active low.
BITS = {
    "up": ("in0", 1),
    "down": ("in0", 2),
    "left": ("in0", 4),
    "right": ("in0", 8),
    "attack": ("in1", 0x10),
    "cube": ("in1", 0x20),
}
COIN = ("in0", 0x40)
START1 = 0x10  # IN0 bit 4


def plays(ld_log):
    """[(tick, start, stop, audio)] from the LD command log."""
    out, audio, start = [], None, None
    for t, line in ld_log:
        for num, op in re.findall(r"(\d*)([A-Z?][A-Z])", line):
            if op == "AD" and num:
                audio = int(num) or audio
            elif op == "SE" and num:
                start = int(num)
            elif op == "SM" and num and start is not None:
                out.append((t, start, int(num), audio))
                start = None
    return out


def parse_args():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=900)
    ap.add_argument("--start", type=float, default=30)
    ap.add_argument("--dips", default="06,04,7E")
    ap.add_argument("-v", action="store_true")
    ap.add_argument(
        "--perfect",
        action="store_true",
        help="drive the ROM with the correct input in every window (reads its current scene from RAM)",
    )
    ap.add_argument(
        "--random",
        type=int,
        metavar="SEED",
        help="like --perfect, but each window gets the right input 75%% of the time, else a random one "
        "(none/up/down/left/right/attack/cube; death records get none); the rule check replays "
        "the same choices",
    )
    ap.add_argument(
        "--die",
        help="C,E: with --perfect, press nothing in that scene once, then hold Cube "
        "through its death clip (checks the cube rewind)",
    )
    ap.add_argument(
        "--trader",
        help="input to tap every 20 ticks once the Trader footage starts (LD 27764), "
        "e.g. attack, cube, coin; logs cubes 0x37:0x1516",
    )
    ap.add_argument(
        "--map-keys",
        help="comma list of inputs tapped every 60 ticks once the first Time Map "
        "still after the Trader shows (with --trader)",
    )
    ap.add_argument(
        "--hellgate",
        action="store_true",
        help="tap Attack while the Hellgate offer (LD 23651) plays; log lives/cubes/score and the plays after",
    )
    ap.add_argument("--ld-log", action="store_true", help="print every laserdisc command with its tick")
    ap.add_argument(
        "--signpost",
        choices=["left", "right"],
        help="with --perfect: the input at the C0 E10 PLAY/LEARN signpost (right skips the intro)",
    )
    ap.add_argument(
        "--press",
        help="with --perfect: C,E,FRAME,INPUT: also press INPUT in that scene once its clip "
        "clock reaches FRAME (e.g. 3,82,190,right springs the C3 E82 trap)",
    )
    ap.add_argument(
        "--watch",
        help="comma list of 16-bit words in segment 0x37 (DGROUP) to log on every change, "
        "with the ROM's current scene, e.g. 0x13C4,0x1B2A,0x424",
    )
    ap.add_argument(
        "--trace-events",
        action="store_true",
        help="print every input_event (CE55:00C9) call: device, bits and the clip clock",
    )
    a = ap.parse_args()
    if a.random is not None:
        a.perfect = True
    return a


# ---- The ROM run: RAM reads, inputs, and what the probes remember between ticks ---------------------------------


class Rom:
    def __init__(self, a, g):
        self.a = a
        self.m = romrun.Machine(open(os.path.join(ROOT, "rom", "tt061891.bin"), "rb").read())
        self.m.dips = [int(x, 16) for x in a.dips.split(",")]
        self.m.reset()
        self.t0 = int(a.start * romrun.TICK_HZ)  # the tick 1P Start is pressed
        self.release = {}  # (register, bit) -> tick it is let go
        # data/game.json records by ROM record index
        self.by_index, self.death_ids = {}, set()
        for e in g["episodes"]:
            self.by_index[e["index"]] = e
            for d in e["deaths"].values():
                self.by_index[d["index"]] = d
                self.death_ids.add(d["index"])
        # what the ROM showed at every LD command: score and record
        self.score_at, self.rec_at, self.seen_ld = {}, {}, 0
        # perfect/random driver
        self.rnd = random.Random(a.random) if a.random is not None else None
        self.choices = {}  # (record index, occurrence, window) -> input chosen (--random)
        self.occ, self.cur_occ = collections.Counter(), None
        self.handled, self.last_rec, self.armed = set(), None, False
        self.seen_events = collections.defaultdict(dict)  # (record index, occurrence) -> {clip frame: input}
        # probes
        self.watched, self.last_score = {}, 0
        self.hg_from = self.hg_state = None
        self.trader_from = self.last_cubes = self.map_from = None
        self.die = tuple(int(x) for x in a.die.split(",")) if a.die else None
        self.died, self.die_tick, self.pressed_once = False, None, False

    @property
    def ticks(self):
        return self.m.ticks

    def word(self, off):
        return int.from_bytes(bytes(self.m.uc.mem_read(DGROUP + off, 2)), "little")

    def dword(self, off):
        return int.from_bytes(bytes(self.m.uc.mem_read(DGROUP + off, 4)), "little")

    def record_index(self):
        """Index of the record the ROM is playing (not checked for alignment)."""
        return (self.word(RAM_RECORD) - EP_TABLE) // EP_SIZE

    def record_name(self):
        r = self.by_index.get(self.record_index())
        return "C%dE%d" % (r["chapter"], r["episode"]) if r else hex(self.word(RAM_RECORD))

    def clip_clock(self):
        """The ROM's own clip clock (0x37:0x192A, 1/100 frame; set by episode_enter, read by input_event)."""
        return self.dword(RAM_CLIP_CLOCK) // 100

    def status(self):
        return self.word(RAM_LIVES), self.word(RAM_CUBES), self.dword(RAM_SCORE)

    def tap(self, reg, bit, hold):
        """Press (reg, bit) now, let go `hold` ticks later."""
        setattr(self.m, reg, getattr(self.m, reg) & ~bit)
        self.release[(reg, bit)] = self.ticks + hold

    def let_go(self):
        for (reg, bit), until in list(self.release.items()):
            if self.ticks >= until:
                setattr(self.m, reg, getattr(self.m, reg) | bit)
                del self.release[(reg, bit)]

    def note_ld_commands(self):
        """Score (32-bit) and record at every new LD command, for the rule check."""
        while self.seen_ld < len(self.m.ld_log):
            tk = self.m.ld_log[self.seen_ld][0]
            self.score_at.setdefault(tk, self.dword(RAM_SCORE))
            self.rec_at.setdefault(tk, self.record_index())
            self.seen_ld += 1

    def press_start(self):
        if self.ticks == self.t0:
            self.m.in0 &= ~START1
        elif self.ticks == self.t0 + 6:
            self.m.in0 |= START1

    def hook_input_events(self):
        """Hooks input_event (CE619): --trace-events prints each call; --random keeps the joystick and button
        events per record occurrence, so the rule check replays the frames the ROM saw."""
        from unicorn import UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_SP, UC_X86_REG_SS

        names = {
            (1, 1): "up",
            (1, 2): "down",
            (1, 4): "left",
            (1, 8): "right",
            (0x10, 0x100): "attack",
            (0x10, 0x200): "cube",
        }

        def on_event(uc, addr, size, _):
            ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
            arg = lambda o: int.from_bytes(bytes(uc.mem_read(ss * 16 + sp + o, 2)), "little")
            dev, bits = arg(4), arg(6)
            if dev == 8:
                return
            ck = self.clip_clock()
            if self.a.trace_events:
                print(f"  input_event dev {dev:#x} bits {bits:#x} clock {ck} tick {self.ticks}")
            name = names.get((dev, bits))
            if name and self.cur_occ is not None:
                self.seen_events[(self.record_index(), self.cur_occ)].setdefault(ck, name)

        self.m.uc.hook_add(UC_HOOK_CODE, on_event, None, 0xCE619, 0xCE619)


# ---- Probes, run every tick; one returning True skips the driver for that tick ------------------------------------


def probe_watch(rom, words):
    for wa in words:
        v = rom.word(wa)
        if rom.watched.get(wa) != v:
            print(f"  watch 0x{wa:04X}: {rom.watched.get(wa)} -> {v} at tick {rom.ticks} in {rom.record_name()}")
            rom.watched[wa] = v


def probe_score(rom):
    sc = rom.dword(RAM_SCORE)
    if sc != rom.last_score:
        print(f"  score +{sc - rom.last_score} at clock {rom.clip_clock()} in {rom.record_name()}")
        rom.last_score = sc


def probe_hellgate(rom):
    """Accept the Hellgate offer (Down at +300 ticks) and log the state and the plays after it."""
    m = rom.m
    if rom.hg_from is None and any("23651SE" in l for _, l in m.ld_log[-2:]):
        rom.hg_from, rom.hg_state = rom.ticks, rom.status()
        print(f"  hellgate offer at tick {rom.ticks}: lives/cubes/score {rom.hg_state}")
    if rom.hg_from is None:
        return False
    if rom.ticks in (rom.hg_from + 300, rom.hg_from + 312):
        m.in0 = m.in0 & ~0x02 if rom.ticks == rom.hg_from + 300 else m.in0 | 0x02
    if rom.ticks == rom.hg_from + 1500:
        print(f"  after: lives/cubes/score {rom.status()} (was {rom.hg_state})")
        for tk, l in m.ld_log:
            if rom.hg_from - 10 <= tk <= rom.hg_from + 1500 and l.strip() not in ("SR", "SF"):
                print(f"  LD t={tk} {l}")
    return rom.hg_from < rom.ticks < rom.hg_from + 1500


def probe_trader(rom, map_stills):
    """Log cubes; tap --trader every 20 ticks of the Trader visit; tap --map-keys once the map shows."""
    a, m = rom.a, rom.m
    cubes = rom.word(RAM_CUBES)
    if cubes != rom.last_cubes:
        print(f"  cubes {rom.last_cubes} -> {cubes} at tick {rom.ticks}")
        rom.last_cubes = cubes
    if a.trader and rom.trader_from is None and any("27764SE" in l for _, l in m.ld_log[-3:]):
        rom.trader_from = rom.ticks
        print(f"  trader footage at tick {rom.ticks}")
    if (
        rom.map_from is None
        and a.map_keys
        and any(int(mm.group(1)) in map_stills for _, l in m.ld_log[-2:] for mm in [re.match(r"(\d+)SE", l)] if mm)
    ):
        rom.map_from = rom.ticks
        print(f"  map still at tick {rom.ticks}: {m.ld_log[-1][1]}")
    if rom.map_from is not None and a.map_keys:
        keys = a.map_keys.split(",")
        k = (rom.ticks - rom.map_from) // 60
        if (rom.ticks - rom.map_from) % 60 == 30 and k < len(keys):
            rom.tap(*BITS[keys[k]], 8)
            print(f"  map tap {keys[k]} at tick {rom.ticks}")
    if a.trader and rom.trader_from is not None and rom.ticks < rom.trader_from + 2400:
        if a.trader != "none" and (rom.ticks - rom.trader_from) % 20 == 10:
            rom.tap(*{"coin": COIN, **BITS}[a.trader], 6)
            print(f"  tap {a.trader} at tick {rom.ticks}")
        return True
    return False


def probe_signpost(rom):
    if rom.ticks == rom.t0 + romrun.TICK_HZ * 3:  # mid C0 E10 window (frames 14-155)
        rom.tap(*BITS[rom.a.signpost], 8)
        print(f"  signpost: {rom.a.signpost} at tick {rom.ticks}")


# ---- The perfect / random driver ---------------------------------------------------------------------------------


def drive(rom):
    """Press each window's input once, mid-window by the ROM's own clip clock (--perfect: sim's perfect
    choice; --random: a mistake 25% of the time from chapter 2 on). Also runs --die and --press."""
    a = rom.a
    off = rom.word(RAM_RECORD)
    aligned = off >= EP_TABLE and (off - EP_TABLE) % EP_SIZE == 0
    rec = rom.by_index.get((off - EP_TABLE) // EP_SIZE) if aligned else None
    if rec is not rom.last_rec and rec is not None:
        rom.occ[rec["index"]] += 1
        rom.cur_occ = rom.occ[rec["index"]]
    if rec is not rom.last_rec:
        if rom.die and rom.last_rec is not None and rom.last_rec.get("index") in rom.death_ids and not rom.died:
            rom.died = True
            print(f"  cube test: left death record at tick {rom.ticks}")
        rom.handled, rom.last_rec, rom.armed = set(), rec, False
    t = rom.clip_clock()
    if rom.die and not rom.died and rec is not None:
        if rec["index"] in rom.death_ids:
            if rom.die_tick is None:
                rom.die_tick = rom.ticks
                print(f"  cube test: death record {rec['index']} at tick {rom.ticks}, clip clock {t}")
            if rom.ticks % 8 == 0:  # tap Cube through the death clip
                rom.tap(*BITS["cube"], 4)
            return
        if (rec.get("chapter"), rec.get("episode")) == rom.die:
            return  # no input: let the scene kill
    if not rom.armed:
        # The clip clock still shows the previous clip for a moment after a scene change; wait until it
        # restarts before trusting it.
        if t >= 20:
            return
        rom.armed = True
    if not rec or not rec["media"] or "clip" not in rec["media"][0]:
        return
    if a.press and not rom.pressed_once:
        pc, pe, pf, px = a.press.split(",")
        if (rec["chapter"], rec["episode"]) == (int(pc), int(pe)) and t >= int(pf):
            rom.tap(*BITS[px], 8)
            rom.pressed_once = True
            print(f"  press {px} at t={t} in C{pc}E{pe}, tick {rom.ticks}")
    groups = collections.OrderedDict()
    for r in rec["rules"]:
        groups.setdefault(r["window"], []).append(r)
    for wid, rules in groups.items():
        lo = max(min(r["a"][0] for r in rules), min(r["b"][0] for r in rules))
        hi = min(max(r["a"][1] for r in rules), max(r["b"][1] for r in rules))
        if wid in rom.handled or t < (lo + hi) // 2 or t > hi:  # press mid-window (clock offset slack)
            continue
        rom.handled.add(wid)
        x = choose_input(rom, rec, wid, rules)
        if a.v:
            print(f"  press t={t} C{rec['chapter']}E{rec['episode']} win{wid} -> {x}")
        if x in BITS:
            reg, bit = BITS[x]
            rom.tap(reg, bit, 12 if reg == "in0" else 4)  # the joystick is polled less often


def choose_input(rom, rec, wid, rules):
    if not rom.rnd:
        return sim.perfect(None, rec, rec, wid, rules, [])
    if rec["index"] in rom.death_ids:
        x = "none"
    elif rec["chapter"] in (0, 1) or rom.rnd.random() < 0.75:  # mistakes from chapter 2 on
        x = sim.perfect(None, rec, rec, wid, rules, [])
    else:
        x = rom.rnd.choice(["none"] + sim.DIRECTIONS + sim.BUTTONS)
    rom.choices[(rec["index"], rom.cur_occ, wid)] = x
    return x


def run_rom(rom):
    a = rom.a
    if a.trace_events or a.random is not None:
        rom.hook_input_events()
    map_stills = {
        o["still"][0] for v in load_json(os.path.join(ROOT, "data", "time_map.json"))["options"].values() for o in v
    }
    watch = [int(x, 16) for x in a.watch.split(",")] if a.watch else []
    for _ in range(int(a.seconds * romrun.TICK_HZ)):
        rom.m.run_ticks(1)
        rom.note_ld_commands()
        rom.press_start()
        rom.let_go()
        if watch:
            probe_watch(rom, watch)
        if a.v:
            probe_score(rom)
        if a.hellgate and probe_hellgate(rom):
            continue
        if (a.trader or a.map_keys) and probe_trader(rom, map_stills):
            continue
        if a.signpost:
            probe_signpost(rom)
        if a.perfect and rom.ticks >= rom.t0 + 60:
            drive(rom)


def print_probe_footage(rom):
    """The LD commands around the Trader / map and the cube test."""
    m = rom.m
    if rom.trader_from is not None or rom.map_from is not None:
        t_lo = rom.trader_from if rom.trader_from is not None else rom.map_from
        for tk, l in m.ld_log:
            if t_lo - 60 <= tk <= (rom.map_from or rom.trader_from) + 1800 and l.strip() not in ("SR", "SF"):
                print(f"  LD t={tk} {l}")
    if rom.die_tick is not None:
        for tk, line in m.ld_log:
            if rom.die_tick - 900 <= tk <= rom.die_tick + 900:
                print(f"  LD t={tk} ({(tk - rom.die_tick) / romrun.TICK_HZ:+.1f}s) {line}")


# ---- The rule check ----------------------------------------------------------------------------------------------


def check_rules(rom, g, seq):
    """Every scene header the ROM played: the rules' outcome (death n or scene done) against the ROM's next
    play, and the score the rules add against the ROM's. -> (checked, agree, mismatches, score_ok,
    score_checked, score_bad, unknown)"""
    a = rom.a
    by_media = collections.defaultdict(list)  # (start, stop) -> [(record, header)]
    for e in g["episodes"]:
        for rec in [e] + list(e["deaths"].values()):
            for md in rec["media"]:
                if "clip" in md:
                    by_media[(md["start"], md["stop"])].append((rec, e))
    check_occ = collections.Counter()

    def replay(game_, ep_, rec_, wid, rules_, opts_):
        return rom.choices.get((rec_["index"], check_occ[rec_["index"]], wid), "none")

    policy = replay if rom.rnd else sim.perfect if a.perfect else sim.nothing
    game = sim.Game(g, 1, policy=policy, out=lambda *_: None)
    checked = agree = score_checked = score_ok = 0
    score_bad, unknown, mismatches = [], [], []
    for i, (t, s, e, au) in enumerate(seq):
        cands = by_media.get((s, e), [])
        if not cands:
            unknown.append((t, s, e))
            continue
        headers = [(r, h) for r, h in cands if r is h and h["kind"] != "system"]
        if not headers or i + 1 >= len(seq):
            continue
        # Records can share footage (C2 E31 pick_one / pick_one_bonus): take the one the ROM points at.
        rec = next((r for r, _ in headers if r["index"] == rom.rec_at.get(t)), headers[0][0])
        check_occ[rec["index"]] += 1
        game.forced = rom.seen_events.get((rec["index"], check_occ[rec["index"]])) if rom.rnd else None
        game.score = s0 = rom.score_at.get(t, 0)
        # The scene's score is complete at the next play of a different record; replay_from
        # re-searches inside the same clip, which is not a new scene.
        j = next(
            (
                k
                for k in range(i + 1, len(seq))
                if not any(r is rec for r, _ in by_media.get((seq[k][1], seq[k][2]), []))
                and seq[k][1:3] != (s, e)
                and not (s <= seq[k][1] and seq[k][2] == e)
            ),
            None,
        )
        res = game.play_record(rec, rec)
        if not (isinstance(res, tuple) and res[0] == "death"):
            game.add_scene_bonus(rec["points"])
        want_delta = game.score - s0
        chapter_bonus = {2: 50000, 1: 5000, 0: 10000}[game.band()]
        got_delta = rom.score_at.get(seq[j][0], s0) - s0 if j is not None else None
        if got_delta is not None:
            score_checked += 1
            if got_delta in (want_delta, want_delta + chapter_bonus):
                score_ok += 1
            else:
                score_bad.append(
                    f"C{rec['chapter']} E{rec['episode']} at score {s0}: ROM +{got_delta}, rules +{want_delta}"
                )
        nxt = seq[j] if j is not None else seq[i + 1]  # the next play that is not a replay_from re-search
        nxt_c = by_media.get((nxt[1], nxt[2]), [])
        if isinstance(res, tuple) and res[0] == "death":
            want = rec["deaths"].get(res[1])
            ok = any(r is want for r, _ in nxt_c)
            expect = f"death{res[1]} {want['media'][0].get('start') if want else '?'}"
        else:
            ok = not any(r is not h for r, h in nxt_c if h is rec)  # next is not one of rec's deaths
            expect = "scene done"
        checked += 1
        agree += ok
        line = (
            f"t={t / romrun.TICK_HZ:7.1f}s C{rec['chapter']} E{rec['episode']} LD {s}-{e}: rules say "
            f"{expect}; ROM next LD {nxt[1]}-{nxt[2]} -> {'OK' if ok else 'MISMATCH'}"
        )
        if not ok:
            mismatches.append(line)
        if a.v:
            print(line)
    return checked, agree, mismatches, score_ok, score_checked, score_bad, unknown, by_media


def scene_order(g, seq, by_media):
    """The ROM's played headers, and tools/sim.py's for a no-input game (chapters 0-1 have no randomness
    except the E35/E36 pick, so with no input the orders must match)."""
    rom_order = []
    for t, s_, e_, au in seq:
        hs = [h for r, h in by_media.get((s_, e_), []) if r is h and h["kind"] != "system"]
        if hs:
            rom_order.append("C%dE%d" % (hs[0]["chapter"], hs[0]["episode"]))
    played = []
    sim_game = sim.Game(g, 7, policy=sim.nothing, out=lambda *_: None)
    orig = sim_game.play_episode

    def track(ep):
        played.append("C%dE%d" % (ep["chapter"], ep["episode"]))
        return orig(ep)

    sim_game.play_episode = track
    sim_game.run()
    norm = lambda xs: [x.replace("C1E21", "C1E20").replace("C1E6", "C1E5").replace("C1E36", "C1E35") for x in xs]
    n = min(len(rom_order), len(played))
    same = norm(rom_order[:n]) == norm(played[:n]) and len(rom_order) == len(played)
    return rom_order, played, same


def main():
    a = parse_args()
    g = sim.load()
    rom = Rom(a, g)
    run_rom(rom)
    print_probe_footage(rom)
    seq = [p for p in plays(rom.m.ld_log) if p[0] >= rom.t0]
    checked, agree, mismatches, score_ok, score_checked, score_bad, unknown, by_media = check_rules(rom, g, seq)
    rom_order, played, same = scene_order(g, seq, by_media)
    if a.ld_log:
        for tk, l in rom.m.ld_log:
            print(f"LD tick {tk} {l}")
    print("scene order ROM:", " ".join(rom_order))
    print("scene order sim:", " ".join(played))
    print(
        "scene order:",
        "IDENTICAL" if same else "DIFFERENT",
        f"(ROM {len(rom_order)}, sim {len(played)} scenes;"
        " E21/E20, E6/E5, E36/E35 share footage so are compared as equal)",
    )
    print(
        f"ROM played {len(seq)} clips in {a.seconds:.0f}s; {checked} scene transitions checked, "
        f"{agree} agree, {checked - agree} mismatch; {len(unknown)} plays not in game.json"
    )
    for l in mismatches:
        print("  " + l)
    print(f"score per scene: {score_ok}/{score_checked} agree")
    for l in score_bad[:15]:
        print("  score: " + l)
    for u in unknown[:10]:
        print(f"  unknown play t={u[0] / romrun.TICK_HZ:.1f}s LD {u[1]}-{u[2]}")
    return 1 if mismatches or score_bad else 0  # make test fails on any disagreement


if __name__ == "__main__":
    sys.exit(main())
