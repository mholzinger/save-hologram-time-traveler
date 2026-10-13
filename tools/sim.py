#!/usr/bin/env python3
"""Text-mode Time Traveler simulator driven only by data/game.json.

    python3 tools/sim.py --check                 # validate the scene graph, exit 1 on errors
    python3 tools/sim.py --auto 200 --policy perfect   # simulate games, print a summary
    python3 tools/sim.py --seed 7                # play interactively (type a move at each window)

Purpose: prove the graph extracted from the ROM is complete enough to drive a player, and be the
reference the 3DS engine (port/3ds/source/engine.c) is tested against game for game.
Rules follow docs/ROM_FINDINGS.md. Simplifications are marked SIM: below; they are
modelling choices for this tool, not claims about the ROM.
"""

import argparse
import collections
import json
import os
import random
import sys

DATA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "game.json")
DIRECTIONS = ["up", "down", "left", "right"]
BUTTONS = ["attack", "cube"]
MERCY_FRAMES = 300  # the mercy countdown [p*4+0x150E] (see Game.mercy_open)


class XorShift:
    """xorshift32, identical to tt_rng in port/3ds/source/engine.c (for parity tests)."""

    def __init__(self, seed):
        self.s = (seed * 2654435761 + 0x9E3779B9) & 0xFFFFFFFF or 1

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return x

    def randrange(self, n):
        return self.next() % n

    def randint(self, a, b):
        return a + self.randrange(b - a + 1)

    def choice(self, seq):
        return seq[self.randrange(len(seq))]

    def random(self):
        return self.next() / 4294967296.0


def load():
    return json.load(open(DATA))


# ---- The game ------------------------------------------------------------------------------------------------------
# Game mirrors port/3ds/source/engine.c: its methods carry the names of the engine.c functions, sit in the same
# order under the same section headers, and draw random numbers in the same order (make test: parity).
# "# engine.c: name" marks a method whose engine.c counterpart has another name.


class Game:
    # ---- Per-game selection (C359:0034 game_choose) --------------------------------------------------------------
    # Also C3CE:0055 chapter_order_init and the Hellgate slot (D668:012B).

    # engine.c: tt_new_game
    def __init__(self, g, seed, lives=3, max_cubes=6, devil=0, policy=None, out=print, rng="python"):
        self.g, self.policy, self.out = g, policy, out
        self.rng = XorShift(seed) if rng == "xorshift" else random.Random(seed)
        self.played = []
        self.lives, self.cubes, self.score = lives, g["cubes"]["start"], 0
        self.max_cubes, self.devil = max_cubes, devil
        self.learn = 1  # [p*2+0x74]
        self.players = 1  # g[0x72]; tools/sim.py plays one player (2P alternation is in the 3DS front end)
        self.cubes_per_coin = 2  # DSW2 cost 0x04 "1 Coin / 2 Cubes" (MAME default; harness: +2 per coin)
        self.eps = g["episodes"]
        self.find = {(e["chapter"], e["episode"]): e for e in self.eps}
        self.ordinal = {e["index"]: k for k, e in enumerate(self.eps)}
        self.status = {}  # index -> "done" | "failed"
        self.mercy_eps, self.mercy_ok = 0, True  # [p*2+0x1E], [p*2+0x1594] (D7C0:0616)
        self.mercy_frames = 0  # LD frames since chapter 0/1 entry ([p*4+0x150E] = 300 counts down)
        self.difficulty, self.good_eps, self.hard, self.death_t = 3, 0, False, 0  # DSW3 medium (C2BA:079D)
        self.prev_pts = self.cur_pts = 0  # f6 of the saved previous / current record (enter_rec)
        self.log = []
        self.reached = set()
        self.choose()

    def chance(self, p):
        return self.rng.randrange(100) < p

    def choose(self):
        lo, hi = self.g["selection"]["bonus_chapter_range"]
        self.bonus = self.rng.randint(lo, hi)
        chosen = set()
        i, eps = 0, self.eps
        while i < len(eps):
            e = eps[i]
            k = e["kind"]
            if k == "system":
                chosen.add(e["index"])
            elif k == "chance":
                if self.chance(e["weight"]):
                    chosen.add(e["index"])
            elif k == "chance_bonus":
                if e["chapter"] == self.bonus and self.chance(e["weight"]):
                    chosen.add(e["index"])
            elif k in ("pick_one", "pick_one_bonus"):
                run = [e]
                while (
                    i + 1 < len(eps)
                    and eps[i + 1]["kind"] in ("pick_one", "pick_one_bonus")
                    and not eps[i + 1]["run_start"]
                ):
                    i += 1
                    run.append(eps[i])
                chosen.add(self.pick_one(run)["index"])
            i += 1
        if self.chance(self.g["selection"]["rare_chance_percent"]):
            self.rare_substitution(chosen)
        self.chosen = chosen
        hs = self.g["hellgate_slot"]
        self.hellgate_slot = self.rng.choice(hs["slots"]) if self.chance(hs["chance_percent"]) else None

    def pick_one(self, run):
        """One scene of a pick-one run, drawn by weight; a bonus one only counts in the bonus chapter."""
        r, acc = self.rng.randrange(100), 0
        for x in run:
            acc += x["weight"]
            if r < acc and (x["kind"] == "pick_one" or x["chapter"] == self.bonus):
                return x
        plain = [x for x in run if x["kind"] == "pick_one"]  # SIM: fall back to the last plain pick_one
        return plain[-1] if plain else run[-1]

    def rare_substitution(self, chosen):
        """Each substitution group outside the bonus chapter gets a draw in turn until one hits; its scene is
        chosen and the scenes it replaces are dropped."""
        groups = [x for x in self.g["selection"]["substitutions"] if x["in"][0] != self.bonus]
        hit = None
        while groups and hit is None:
            for grp in groups:
                if self.chance(self.g["selection"]["rare_group_percent"]):
                    hit = grp
                    break
        if hit:
            chosen.add(self.find[tuple(hit["in"])]["index"])
            for o in hit["out"]:
                chosen.discard(self.find[tuple(o)]["index"])
            self.rare = hit

    # ---- Scoring and difficulty (D047) ---------------------------------------------------------------------------

    # Scoring, read from the instructions (D047:0761 / 0875 / 0996). Bands: score > 100000,
    # > 90000, else. Hit: x10 / x1 / x2. Scene bonus (D7C0:02D0 after a scene finished without a
    # death): x50 / x5 / x10. Chapter bonus (no next scene left, last scene good): +50000 / +5000 /
    # +10000 (the last only when the chapter is not the intro, chapter-list entry != 0).
    def band(self):
        return 2 if self.score > 100000 else 1 if self.score > 90000 else 0

    def add_points(self, pts):
        self.score += pts * (2, 1, 10)[self.band()]

    def add_scene_bonus(self, pts):
        self.score += pts * (10, 5, 50)[self.band()]

    def add_chapter_bonus(self, ch):
        b = self.band()
        if b == 2:
            self.score += 50000
        elif b == 1:
            self.score += 5000
        elif ch != 0:
            self.score += 10000

    # ---- Hellgate (D0F0:025E) ------------------------------------------------------------------------------------

    def hellgate(self):
        # D0F0:025E: the offer is declined unless the player accepts within ~301 ticks.
        if self.policy is None:
            if input("  HELLGATE offered. accept? [y/N]> ").strip().lower() != "y":
                return
        elif self.policy is perfect or self.rng.random() < 0.5:
            self.out("  HELLGATE declined")
            return
        r = self.rng.randrange(100)
        table = self.g["hellgate"]["thresholds_devil_takes_lives" if self.devil == 0 else "thresholds_devil_never"]
        result = next(name for lim, name in table if r < lim)
        if result == "life":
            self.lives += 1
        elif result == "cubes2":
            self.cubes += 2  # cubes_add (c000_fc7f) has no cap
        elif result == "lose_life":
            self.lives -= 1
        elif result == "lose_all":
            self.lives = 0
            self.mercy_ok = False
        self.score = max(
            0,
            self.score
            + (
                self.g["hellgate"]["win_score"]
                if result in ("jackpot", "life", "cubes2")
                else self.g["hellgate"]["lose_score"]
                if result.startswith("lose") and result != "lose_none"
                else 0
            ),
        )
        self.out(f"  HELLGATE roll {r}: {result}; lives {self.lives} cubes {self.cubes}")
        # After a jackpot with the devil allowed, a second offer plays (LD 23982-24152); accepting it
        # always ends in "devil takes all lives" (D0F0:025E).
        if result == "jackpot" and self.devil == 0:
            if self.policy is None:
                accept = input("  HELLGATE again? [y/N]> ").strip().lower() == "y"
            else:
                accept = not (self.policy is perfect or self.rng.random() < 0.5)
            if accept:
                self.lives, self.mercy_ok = 0, False
                self.score = max(0, self.score + self.g["hellgate"]["lose_score"])
                self.out("  HELLGATE second deal: the devil takes all lives")

    # ---- One record, frame by frame (CFD9: windows and rule actions) ---------------------------------------------

    # Input event at clip frame t: CFD9:0082 walks the key's rules in table order; for each rule whose
    # window is open (variant a/b by the hard flag, CFD9:0249) and not yet cleared: if the move matches
    # (CE55:03CC) run the action with phase 1, else with phase 4 (a no-op for every action here).
    # Action result 1 = clear that window and stop, 2 = stop, 0 = go on (stop anyway if the rule has
    # the stop-on-miss bit). Each frame CFD9:0316 lets one closing, uncleared rule with no move or a
    # nibble-6 action fire phase 2 (same 1/2/0 handling).
    # Return values per action target were read from each function (docs/ROM_FINDINGS.md).
    TABLE_RET = {
        "effect": 1,
        "noop": 2,
        "episode_end": 2,
        "cube_rewind": 0,
        "trap_c3e82": 2,
        "start_game": 0,
        "cubes_grant_start": 1,
        "next_episode": 1,
        "replay_from": 1,
        "attract_cycle": 2,
        "attract_next": 1,
        "chapter_next_quiet": 2,
        "choice_left": 0,
        "choice_right": 2,
        "choice_other": 0,
        "hellgate": 2,
        "ld_audio": 0,
        "phase_misc": 1,
    }
    BEEP_FNS = {"DA9D:0002": 2, "DA9D:0086": 2}  # beeps that return 2 (window stays open)

    # engine.c: WLO / WHI
    def window(self, r):
        return r["b"] if self.hard else r["a"]

    def act(self, ep, rec, r, phase):
        """Run rule r's action for phase 1 (input) or 2 (window closing). -> (ret, effect)"""
        a = r["action"]
        if "table" in a:
            for t in a["table"]:
                if t["phase"] != phase:
                    continue
                d = t["does"]
                ret = self.BEEP_FNS.get(t["fn"], self.TABLE_RET.get(d, 0))
                eff = None
                if d == "episode_end" and t["args"][0] == 1:
                    eff = "done"
                elif d == "phase_misc" and t["args"][0] == 1:
                    eff = "done"
                elif d in ("next_episode", "choice_other"):
                    eff = "done"
                elif d == "choice_left":  # D659:0041: rest of the clip, next scene, learn flag = 1
                    self.learn, eff = 1, "done"
                elif d == "chapter_next_quiet":  # D659:000C: chapter_next (skips the intro), learn flag = 0
                    self.learn, eff = 0, "skip"
                elif d == "choice_right":
                    eff = ("jump", 1, 30)  # D659:0086: beep, enter C1 E30
                elif d == "start_game":
                    eff = ("jump", 1, 10)  # DA9D:0EF8: beep, enter C1 E10
                elif d == "trap_c3e82":
                    eff = "fail"
                elif d == "cube_rewind":
                    eff = "rewind"
                elif d == "replay_from":
                    eff = ("replay", t["args"][0])
                elif d == "hellgate":
                    self.hellgate()
                elif d == "cubes_grant_start" and (t["args"][0] == 0 or self.cubes == 0):
                    self.cubes = 1
                if ret:
                    return ret, eff
                if eff:
                    return 0, eff
            return 0, None
        code = a["code"]
        n, hi, lo, low12 = code >> 12, (code >> 6) & 0x3F, code & 0x3F, code & 0xFFF
        if phase == 1:
            if (n == 2 and hi == 0) or (n == 4 and low12 == 0):
                self.add_points(rec["points"])
                return 1, None
            if n == 5:
                return 1, "done"
            if n == 6:
                return 1, ("death", str(hi))
            if n == 7:
                return 1, ("death", str(low12))
        else:
            if (n == 2 and lo == 0) or (n == 3 and low12 == 0):
                self.add_points(rec["points"])
                return 1, None
            if n == 6:
                return 1, ("death", str(lo))
            if n == 8:
                return 1, ("death", str(low12))
        return 0, None

    # engine.c: the front end's choose_input
    def ask(self, ep, rec, window, rules):
        options = ["none"]
        for r in rules:
            for group in r["move"]["inputs"]:
                for n in group:
                    if n not in options:
                        options.append(n)
        for n in DIRECTIONS + BUTTONS:
            if n not in options:
                options.append(n)
        if self.policy:
            return self.policy(self, ep, rec, window, rules, options)
        lo, hi = rules[0]["a"]
        self.out(
            f"    window {window} frames {lo}-{hi}: "
            + "; ".join(
                f"{'/'.join('+'.join(g_) for g_ in r['move']['inputs']) or 'NO-MOVE'}->{describe(r['action'])}"
                for r in rules
            )
        )
        while True:
            x = input(f"    move [{' '.join(options)}]> ").strip().lower() or "none"
            if x in options:
                return x

    @staticmethod
    def press_frame(rules):
        """The middle of where variants a and b of the window are both open, or its start if that is empty."""
        lo = max(min(r["a"][0] for r in rules), min(r["b"][0] for r in rules))
        hi = min(max(r["a"][1] for r in rules), max(r["b"][1] for r in rules))
        return max(lo, (lo + hi) // 2)

    def schedule(self, ep, rec):
        """The policy's presses for this record: {frame: input}. Each window, in opening order, is asked once
        with all its rules; a press moves on a frame while another press holds its frame."""
        groups = collections.OrderedDict()
        for r in sorted(rec["rules"], key=lambda r: (r["a"][0], r["window"])):
            groups.setdefault(r["window"], []).append(r)
        sched = {}
        for wid, rules in groups.items():
            x = self.ask(ep, rec, wid, rules)
            if x == "none":
                continue
            f = self.press_frame(rules)
            while f in sched:
                f += 1
            sched[f] = x
        return sched

    # engine.c: tt_ep.ld_frames (tools/pack_game.py)
    @staticmethod
    def ld_len(rec):
        m = [x for x in rec["media"] if "clip" in x]
        return m[0]["stop"] - m[0]["start"] if m else 0

    def record_length(self, rec):
        """A record's length in LD frames: its clip, or (no clip: an attract gap) the end of its last window."""
        return self.ld_len(rec) or (max((r["a"][1] for r in rec["rules"]), default=0) + 1)

    def play_record(self, ep, rec, start=0):
        """-> "done" | ("death", n) | ("jump", ch, ep) | "fail" | "rewind"."""
        rules = rec["rules"]
        length = self.record_length(rec)
        # tools/romdiff.py --random replays the frames the ROM saw (input_event clocks) via self.forced.
        forced = getattr(self, "forced", None)
        sched = dict(forced) if forced is not None else self.schedule(ep, rec)
        cleared = set()
        for t in range(start, length + 1):
            x = sched.get(t)
            if x:
                for r in rules:
                    lo, hi = self.window(r)
                    if r["window"] in cleared or not (lo <= t <= hi):
                        continue
                    hit = any(x in g_ for g_ in r["move"]["inputs"]) and x not in r["move"].get("dead_inputs", [])
                    ret, eff = self.act(ep, rec, r, 1) if hit else (0, None)
                    if isinstance(eff, tuple) and eff[0] == "replay":
                        return self.play_record(ep, rec, eff[1])
                    if eff:
                        self.death_t = t
                        return eff
                    if ret == 1:
                        cleared.add(r["window"])
                        break
                    if ret == 2:
                        break
                    if r["stop_on_miss"]:
                        break
            for r in rules:
                lo, hi = self.window(r)
                if r["window"] in cleared or hi > t + 1 or hi <= start:  # closed before a replay_from target
                    continue
                n = r["action"].get("code", 0) >> 12
                if not (r["move"]["mode"] == "none" or n == 6):
                    continue
                ret, eff = self.act(ep, rec, r, 2)
                if isinstance(eff, tuple) and eff[0] == "replay":
                    return self.play_record(ep, rec, eff[1])
                if eff:
                    self.death_t = t
                    return eff
                if ret == 1:
                    cleared.add(r["window"])
                    break
                if ret == 2:
                    cleared.add(r["window"])  # SIM: keeps a returning-2 expiry from firing every frame
                    break
        return "done"

    # ---- An episode with its deaths, cubes and lives (D047, D7C0) ------------------------------------------------

    def mercy_open(self):
        """The mercy life is still on offer: the countdown [p*4+0x150E], loaded with 300 at chapter 0/1 entry,
        has not run out. tools/romrun.py shows it falling ~30 per second of play, i.e. LD frames.
        SIM: counted as LD frames of clips played."""
        return self.mercy_ok and self.mercy_eps <= 6 and self.mercy_frames < MERCY_FRAMES

    def mercy_life(self):
        """After a life is lost: with none left, the mercy life if it is still on offer."""
        if self.lives < 1 and self.mercy_open():
            self.lives += 1

    def enter_rec(self, pts):
        """episode_enter (C40B:08A8) saves the previous record at [p*4+0x1582]; after_death reads its f6."""
        self.prev_pts, self.cur_pts = getattr(self, "cur_pts", 0), pts

    def hint(self, ep, death):
        """D63D:0002, from after_death (D7C0:06DE) once a death was not rewound. The death record's
        f6 (+0x0C) is a hint word: chance (bits 0-4 x4 when hardened, else bits 5-9 x4) of playing
        special object (bits 10+), which for 0x54xx (object 21) also needs the Trader unused. With no
        word: while lives remain in a scoring scene, object 0x15 (no cubes, Trader unused, 25%) or
        0x14 (25%) / 0x16 (25% of the rest)."""
        v = death["points"]
        trader_unused = getattr(self, "trader_flag", 0) != 2
        key = None
        if v == 0:
            if self.lives > 0 and ep["points"]:
                if self.cubes < 1:
                    if self.chance(25) and trader_unused:
                        key = 0x15
                elif self.chance(25):
                    key = 0x14
                elif self.chance(25):
                    key = 0x16
        else:
            p = (v & 0x1F) << 2 if self.hard else (v & 0x3E0) >> 3
            if self.chance(p) and (((v >> 8) & 0xFC) != 0x54 or trader_unused):
                key = v >> 10
        if key:
            self.hints = getattr(self, "hints", 0) + 1
            self.out(f"  HINT special object {key}")

    def trap(self, ep):
        """The C3 E82 trap (DA9D:0E6D, d000_b83d): the scene is marked cleared, lose_life, the death flag
        0x100A, then episode_done -> after_death (d000_82a5): lose_life again when the record saved as previous
        has a non-zero f6, the hint with this record, mercy, 2P switch (038F)."""
        self.status[ep["index"]] = "done"
        second = self.prev_pts != 0
        if ep["chapter"] != 0:
            self.lives -= 1 + (1 if second else 0)
        self.hint(ep, ep)
        self.mercy_life()
        self.out(f"  TRAP: lives {self.lives}")
        return "trap"

    def enter_death(self, ep, n):
        """episode_fail_to_death: enter death record n of the episode."""
        death = ep["deaths"][n]
        self.mercy_frames += self.ld_len(death)
        self.enter_rec(death["points"])
        self.out(f"  DEATH {n} (clip {death['media'][0].get('clip')})")
        return death

    def scene_failed(self, ep, death):
        """after_death (D7C0:06A5) once the death was not rewound: a scoring scene costs a life, then the hint
        and the mercy life."""
        self.status[ep["index"]] = "failed"
        if ep["points"] and ep["chapter"] != 0:
            self.lives -= 1
        self.hint(ep, death)
        self.mercy_life()
        self.out(f"  lives {self.lives}")
        return "failed"

    def play_episode(self, ep):
        self.reached.add((ep["chapter"], ep["episode"]))
        self.played.append(self.ordinal[ep["index"]])
        self.enter_rec(ep["points"])
        start = 0
        length = self.record_length(ep)
        while True:
            res = self.play_record(ep, ep, start)
            # The mercy countdown [p*4+0x150E] runs at 30 a second of play (romdiff --watch 0x150E: -1 every
            # 2 ticks), so the frames actually played count, not the whole clip up front.
            self.mercy_frames += max(0, (length if res == "done" else self.death_t) - start)
            if res == "fail":
                return self.trap(ep)
            if not (isinstance(res, tuple) and res[0] == "death"):
                if res == "done":
                    self.good_eps += 1
                    if self.good_eps == (3, 4, 7, 5)[self.difficulty]:
                        self.hard = True
                return res
            if self.good_eps < (5, 6, 11, 8)[self.difficulty]:
                self.hard = False
            death = self.enter_death(ep, res[1])
            use_cube = self.cubes > 0 and (
                self.policy is None
                and input("    use cube? [y/N]> ").lower() == "y"
                or self.policy is not None
                and self.rng.random() < 0.5
            )
            if use_cube:
                self.cubes -= 1
                self.out(f"  cube used, {self.cubes} left; rewinding")
                # D047:0679: re-enter 3 s (D245:07D8(3) = 3 x 30 frames) before the death.
                # SIM: the death time is taken as the window that killed (its close frame).
                start = max(0, self.death_t - 90)
                self.enter_rec(ep["points"])  # D047:0679 re-enters the saved scene
                continue
            return self.scene_failed(ep, death)

    # ---- A chapter: its scene queue (D70D) -----------------------------------------------------------------------

    def c6e50_allowed(self):
        # D668:05BB: offered when C6 E22 was chosen, C6 E30 or E40 was chosen, none of them failed, chance(80).
        idx = lambda ep: self.find[(6, ep)]["index"]
        if idx(22) not in self.chosen or not (idx(30) in self.chosen or idx(40) in self.chosen):
            return False
        if any(self.status.get(idx(x)) == "failed" for x in (22, 30, 40)):
            return False
        return self.chance(80)

    # engine.c: is_scoring
    def scoring(self, ch):
        return [
            e
            for e in self.eps
            if e["chapter"] == ch
            and e["index"] in self.chosen
            and e["episode"] != 10
            and e["points"]
            and e["kind"] != "system"
        ]

    def revisiting(self, ch):
        """D70D:0A84: some chosen scoring scene of ch has been played."""
        return any(self.status.get(e["index"]) for e in self.scoring(ch))

    def chapter_completed(self, ch):
        """D70D:0973: a chosen scoring scene was played and none is currently failed."""
        st = [self.status.get(e["index"]) for e in self.scoring(ch)]
        return any(st) and "failed" not in st

    def chapter_queue(self, ch):
        """The chapter's chosen scenes in table order: one pass per visit (chapter_start D70D:0661 -> D70D:06B7).
        On a revisit (D70D:0A84: a chosen scoring scene of this chapter was already played) only the failed
        scenes are queued."""
        eps = [e for e in self.eps if e["chapter"] == ch and e["index"] in self.chosen and e["kind"] != "system"]
        if self.revisiting(ch):
            eps = [e for e in eps if self.status.get(e["index"]) == "failed"]
        return eps

    def scene_done(self, ch, e):
        """episode_done for a scene finished without a death: marked done, the scene bonus, the mercy count."""
        self.status[e["index"]] = "done"
        # episode_done (D70D:0C7A-0D60): C1 E30 done also marks E20 and E21 done, E21 marks E20
        if ch == 1 and e["episode"] in (21, 30):
            for x in (20, 21) if e["episode"] == 30 else (20,):
                if (1, x) in self.find:
                    self.status[self.find[(1, x)]["index"]] = "done"
        self.mercy_eps += 1
        self.add_scene_bonus(e["points"])

    def chapter(self, ch):
        """One visit to chapter ch: one pass over its queue. Failed scenes wait for a later visit, except in
        chapter 1 after the opening duel, where a death replays the scene."""
        if ch in (0, 1):
            self.mercy_eps = 0
            self.mercy_frames = 0
        if ch == 0:
            self.learn = 1  # D70D:040E sets [p*2+0x74] on entering chapter 0
        if ch == 6 and self.c6e50_allowed():
            self.chosen.add(self.find[(6, 50)]["index"])
        queue = self.chapter_queue(ch)
        self.out(f"CHAPTER {ch}: {len(queue)} episodes")
        passes = 0
        # Chapter 1 flow (maybe_episode_next C40B:004F): after the opening duel (E20 succeeded, or
        # its 0-point twin E21 finished either way) play continues at the first chosen header from
        # E35 on (E22-E30 are the Learn path), and from then on a death replays the same scene
        # ([p*2+0x13C8] set: the previous episode is returned). Confirmed against the ROM with
        # tools/romdiff.py (E20 death -> E21; E21 death -> E35; E35 death -> E35).
        duel_done = False
        last_good, queue_started = False, bool(queue)
        e35 = self.find[(1, 35)]["index"] if (1, 35) in self.find else None
        retries = 0
        while queue and self.lives > 0 and passes < 1:  # one pass; failures return on a later visit
            nxt = []
            i = 0
            while i < len(queue):
                e = queue[i]
                if self.status.get(e["index"]) == "done":
                    i += 1
                    continue
                self.out(f" C{ch} E{e['episode']} ({e['kind']}, {e['points']} pts) score {self.score}")
                res = self.play_episode(e)
                failed = res == "failed" or res == "fail"
                last_good = False
                if res == "skip":  # chapter_next: the rest of this chapter is skipped
                    self.status[e["index"]] = "done"
                    return
                if res == "trap":
                    if self.lives <= 0:
                        return
                elif failed:
                    self.status[e["index"]] = "failed"
                    if self.lives <= 0:
                        return
                elif isinstance(res, tuple) and res[0] == "jump":
                    self.status[e["index"]] = "done"
                    target = self.find[(res[1], res[2])]
                    i = next((k for k, x in enumerate(queue) if x["index"] == target["index"]), i + 1)
                    continue
                else:
                    self.scene_done(ch, e)
                    last_good = True
                if ch == 1 and e35 is not None:
                    # Two players: E20 goes on to E35 either way (D70D:0917(1, 0x23) when g[0x72] >= 2).
                    # E21 dying goes on to E35 only with the learn flag set (C40B:0262); with the intro
                    # skipped (flag 0) play walks on into the Learn path E22-E30.
                    if not duel_done and (
                        (e["episode"] == 20 and (not failed or self.players >= 2))
                        or (e["episode"] == 21 and (not failed or self.learn))
                    ):
                        duel_done = True
                        i = next((k for k, x in enumerate(queue) if x["index"] >= e35), len(queue))
                        continue
                    if duel_done and failed and retries < 50:
                        retries += 1
                        continue  # replay the same scene
                if failed:
                    nxt.append(e)
                i += 1
            queue, passes = nxt, passes + 1
        if queue_started and last_good and self.lives > 0:
            self.add_chapter_bonus(ch)

    # ---- Trader, Time Map and the chapter list (CEDF, D843, C3CE) ------------------------------------------------

    # Trader (CEDF:0719). trader_flag [p*2+0x1B24]: 0 = on the map, 1 = not this visit, 2 = used for good.
    def trader(self, automatic):
        """Each coin buys cubes_per_coin cubes up to the maximum (CEDF:0D80). Policies insert none."""
        self.traders = getattr(self, "traders", 0) + 1
        coins = 0
        if self.policy is None:
            x = input(f"  TRADER{' (auto)' if automatic else ''}: cubes {self.cubes}/{self.max_cubes}; coins? ").strip()
            coins = int(x) if x.isdigit() else 0
        for _ in range(coins):
            self.cubes += self.cubes_per_coin  # cubes_add has no cap (trader c000_f509:144)

    def map_entry(self, cur):
        """D843:0127-0241, before the map's cursor loop; cur = [p*2+0x3A], the slot just played."""
        if (
            cur == 5
            and self.chapter_completed(self.slot_ch.get(5))
            and self.trader_flag != 2
            and self.cubes < self.max_cubes
        ):
            self.trader(True)  # D843:01B3 trader(1)
            self.trader_flag = 2
        if self.trader_flag != 2:
            self.trader_flag = 0 if self.cubes < self.max_cubes else 1  # D843:0223 / D843:023A

    def map_key(self):
        """The Time Map's key (D843:0A3D): the bonus chapter's bit (bonus + 8) | a bit per completed chapter
        1-19. Chapters past 7 are only played after the last map, so on the map only bits 1-7 can be set."""
        return sum(1 << c for c in range(1, 20) if self.chapter_completed(c)) | 1 << (self.bonus + 8)

    def map_options(self, remaining):
        # Time Map (D843:00B9): options for the key, table D843:0B33 (data/time_map.json); chapter -1 = Trader
        # entry, not a destination.
        key = "%04X" % self.map_key()
        # The Trader position (-1) is offered only while [p*2+0x1B24] == 0 (D843:098C).
        opts = [c for c in self.g.get("time_map", {}).get(key, []) if c >= 0 or (c == -1 and self.trader_flag == 0)]
        if not [c for c in opts if c >= 0]:
            self.map_fallbacks += 1
            opts = list(remaining) or [6]  # SIM: mask not in the table
        return opts

    # engine.c: the front end's map_choice
    def map_choice(self, remaining):
        if self.policy:
            return self.rng.choice(remaining)
        while True:
            x = input(f"TIME MAP: choose chapter {remaining}> ").strip()
            if x.isdigit() and int(x) in remaining:
                return int(x)

    # engine.c: tt_run
    def run(self):
        # Chapter list (C3CE:0055): slot 1 = chapter 0, 2 = 1, 3-7 = Time Map, 8 = 7, 9 = 8, 10 = 20.
        # chapter_advance (D70D:0021) moves to the next slot only when the chapter just played is
        # completed (D70D:0973); otherwise the slot is played again: for slots 2-7 the Time Map runs
        # again for it (D70D:0318-0369), for slots 8-9 the same chapter replays. Chapter 0 always moves
        # on (D70D:0118); after chapter 20 the game is over (D70D:0179). A replayed chapter queues only
        # its failed scenes (chapter(), revisiting).
        fixed = {int(k): v for k, v in self.g["chapter_slots"]["fixed"].items()}
        remaining = list(self.g["chapter_slots"]["map_chapters"])
        self.completed, self.map_fallbacks = set(), 0
        self.slot_ch, self.trader_flag = {}, 0  # [p*2+0x1B24] = 0 at game start (D668:01A7)
        slot, cur = 1, 0
        for _ in range(200):  # SIM: guard; every incomplete pass costs a life
            if self.lives <= 0 or slot > 10:
                break
            if slot in fixed:  # an incomplete chapter 1 replays at once (chapter_advance d000_70f1)
                ch = fixed[slot]
            else:  # Time Map (chapter_advance D70D:0021 -> D843:00B9)
                self.map_entry(cur)
                while True:
                    ch = self.map_choice(self.map_options(remaining))
                    if ch != -1:
                        break
                    self.trader(False)  # D843:05F1, then the map carries on
                    if self.trader_flag != 2:
                        self.trader_flag = 1
                if ch in remaining:
                    remaining.remove(ch)
            self.slot_ch[slot] = ch
            self.chapter(ch)
            # chapter_next (D70D:040E, d000_74de) clears the hard flag when the chapter just played is 0 or 1
            # (romdiff --watch 0x13C4: set at C1 E35, cleared at the end of C1, never set again).
            if ch in (0, 1):
                self.hard = False
            self.completed.add(ch)
            if slot == self.hellgate_slot and self.devil != 2 and self.lives > 0:
                self.hellgate_slot = None  # hellgate (d000_115e) zeroes the slot on entry: once a game
                self.hellgate()
            cur = slot
            if ch == 20:
                break
            if ch == 0 or self.chapter_completed(ch):
                slot += 1
        self.out(f"GAME OVER score {self.score} lives {self.lives}")
        return self


# ---- Command line: graph check, input policies, main --------------------------------------------------------------


def check(g):
    errs, warns = [], []
    eps = g["episodes"]
    by_ch = collections.defaultdict(list)
    for e in eps:
        by_ch[e["chapter"]].append(e)
    for slot, ch in g["chapter_slots"]["fixed"].items():
        if ch not in by_ch:
            errs.append(f"fixed slot {slot} -> chapter {ch} has no episodes")
    for ch in g["chapter_slots"]["map_chapters"] + [6]:
        if ch not in by_ch:
            errs.append(f"era chapter {ch} has no episodes")
    keyset = {(e["chapter"], e["episode"]) for e in eps}
    for grp in g["selection"]["substitutions"]:
        for ce in [grp["in"]] + grp["out"]:
            if tuple(ce) not in keyset:
                errs.append(f"substitution refers to missing C{ce[0]} E{ce[1]}")
    n_rules = 0
    for e in eps:
        for rec in [e] + list(e["deaths"].values()):
            name = f"C{e['chapter']} E{e['episode']}" + ("" if rec is e else f" death{rec['episode']}")
            clips = [m for m in rec["media"] if "clip" in m]
            if not rec["media"]:
                # asset_find() returns 0 for these; the episode is then only its timed windows
                # (attract gaps C10000 E30/E60/E210/E240). Fatal for anything else.
                (warns if e["kind"] == "system" else errs).append(f"{name}: asset {rec['asset']} has no media")
            length = (clips[0]["stop"] - clips[0]["start"]) if clips else None
            for r in rec["rules"]:
                n_rules += 1
                a = r["action"]
                for k in ("hit", "expire"):
                    v = a.get(k)
                    if v and v.startswith("death"):
                        n = v[5:]
                        if rec is not e:
                            errs.append(f"{name}: death inside a death record ({v})")
                        elif n not in e["deaths"]:
                            errs.append(f"{name}: {k} -> {v} but deaths are {sorted(e['deaths'])}")
                for t in a.get("table", []):
                    if t["does"] == "unknown":
                        errs.append(f"{name}: unknown table action {t['fn']}")
                if length is not None:
                    for v in ("a", "b"):
                        lo, hi = r[v]
                        if hi > length + 2:
                            warns.append(f"{name}: window {r['window']}{v} {lo}-{hi} past clip end {length}")
                if [x for x in r["move"].get("dead_inputs", []) if x != "cube"]:  # Cube: CE55:01D7, expected
                    warns.append(
                        f"{name}: window {r['window']} move {r['move']['id']} has dead inputs "
                        f"{r['move']['dead_inputs']} (never match in ROM)"
                    )
    for ch, lst in sorted(by_ch.items()):
        if ch in (10000,):
            continue
        playable = [e for e in lst if e["kind"] != "system"]
        if not playable:
            errs.append(f"chapter {ch}: no playable episodes")
    return errs, warns, n_rules


def describe(a):
    if "table" in a:
        return ",".join(f"p{t['phase']}:{t['does']}" for t in a["table"])
    return f"hit:{a['hit']} expire:{a['expire']}"


BIT_ORDER = ["up", "down", "left", "right", "attack", "cube", "start1", "start2", "coin", "bill"]


def perfect(game, ep, rec, window, rules, options):
    """Pick the lowest-bit input whose phase-1 outcome is not a death or failure; else none."""
    bad = ("trap_c3e82", "cube_rewind")
    for r in rules:
        a = r["action"]
        if "table" in a:
            calls = [t["does"] for t in a["table"] if t["phase"] == 1]
            good = bool(calls) and not any(c in bad for c in calls)
        else:
            good = a.get("hit") is not None and not a["hit"].startswith("death")
        if good and r["move"]["inputs"]:
            names = {n for grp in r["move"]["inputs"] for n in grp} - set(r["move"].get("dead_inputs", []))
            if names:
                return min(names, key=BIT_ORDER.index)
    return "none"


def nothing(game, ep, rec, window, rules, options):
    return "none"


def randomly(game, ep, rec, window, rules, options):
    """Uniform over no input and the six moves (fixed order, so port/3ds/host can draw the same)."""
    return game.rng.choice(["none"] + DIRECTIONS + BUTTONS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--auto", type=int, default=0)
    ap.add_argument("--policy", choices=["perfect", "random", "none"], default="perfect")
    ap.add_argument("--rng", choices=["python", "xorshift"], default="python")
    ap.add_argument("--trace", action="store_true", help="one line per game, for parity tests")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--lives", type=int, default=3)
    a = ap.parse_args()
    g = load()
    if a.check:
        errs, warns, n = check(g)
        for w in warns:
            print("warn:", w)
        for e in errs:
            print("ERROR:", e)
        print(
            f"{len(g['episodes'])} episodes, {sum(len(e['deaths']) for e in g['episodes'])} deaths, "
            f"{n} rules: {len(errs)} errors, {len(warns)} warnings"
        )
        sys.exit(1 if errs else 0)
    if a.auto:
        pol = {"perfect": perfect, "random": randomly, "none": nothing}[a.policy]
        if a.trace:
            for s in range(a.auto):
                gm = Game(g, a.seed + s, lives=a.lives, policy=pol, out=lambda *_: None, rng=a.rng).run()
                h = 0
                for i in gm.played:
                    h = (h * 31 + i + 1) & 0xFFFFFFFF
                print(a.seed + s, gm.score, gm.lives, gm.cubes, len(gm.played), h)
            return
        scores, finished, reached = [], 0, collections.Counter()
        for s in range(a.auto):
            gm = Game(g, a.seed + s, lives=a.lives, policy=pol, out=lambda *_: None).run()
            scores.append(gm.score)
            finished += gm.lives > 0
            for k in gm.reached:
                reached[k] += 1
        allk = {(e["chapter"], e["episode"]) for e in g["episodes"] if e["kind"] != "system"}
        never = sorted(allk - set(reached))
        print(
            f"{a.auto} games ({a.policy}): finished {finished}, score min/avg/max "
            f"{min(scores)}/{sum(scores) // len(scores)}/{max(scores)}"
        )
        print(
            f"episodes reached at least once: {len(reached)}/{len(allk)}; never: "
            + " ".join(f"C{c}E{e}" for c, e in never)
        )
        return
    Game(g, a.seed, lives=a.lives).run()


if __name__ == "__main__":
    main()
