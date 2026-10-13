#!/usr/bin/env python3
"""The real ROM's LED line over time (tools/romrun.py), compared with the port's LED module
(port/3ds/host/test_ledmsg) for one message.

    .venv/bin/python -I tools/romled.py attract     # message 0x0B in attract mode (after boot)
    .venv/bin/python -I tools/romled.py status      # message 0x0E, the in-game status loop

The ROM run uses free-play DIPs 06,04,7E. The ROM timeline starts at the first LED change after the
message is shown; the module's at its first change after show(id). Both are lists of (tick, text) at
60 Hz; they must match change for change (text and tick offsets).
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT  # noqa: E402
import romrun  # noqa: E402


def rom_timeline(start_s, end_s, press_start_at=None, until_text=None):
    m = romrun.Machine(open(os.path.join(ROOT, "rom", "tt061891.bin"), "rb").read())
    m.dips = [0x06, 0x04, 0x7E]
    m.reset()
    out, last = [], None
    for _ in range(int(end_s * 60)):
        m.run_ticks(1)
        if press_start_at is not None and m.ticks == int(press_start_at * 60):
            m.in0 &= ~0x10
        if press_start_at is not None and m.ticks == int(press_start_at * 60) + 6:
            m.in0 |= 0x10
        t = m.led_text()
        if t != last:
            if m.ticks >= start_s * 60:
                out.append((m.ticks, t))
            last = t
    return out


def module_timeline(msg, seconds, initial):
    exe = os.path.join(ROOT, "port", "3ds", "host", "test_ledmsg")
    r = subprocess.run(
        [exe, os.path.join(ROOT, "build", "timetrv.ttg"), hex(msg), str(seconds), initial],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    return [(int(l.split()[0]), l[l.index("|") + 1 : -1]) for l in r.splitlines()]


def compare(rom, mod, n):
    r0, m0 = rom[0][0], mod[0][0]
    ok = 0
    for i, ((rt, rx), (mt, mx)) in enumerate(zip(rom[:n], mod[:n])):
        same = rx == mx and (rt - r0) == (mt - m0)
        ok += same
        if not same:
            print(f"  #{i}: ROM +{rt - r0} |{rx}|  module +{mt - m0} |{mx}|")
            if i > ok + 5:
                break
    print(f"{ok}/{min(n, len(rom), len(mod))} changes identical")


if __name__ == "__main__":
    which = sys.argv[1] if len(sys.argv) > 1 else "attract"
    if which == "attract":
        rom = rom_timeline(20.6, 60)  # 0x0B starts with attract E20 (asset type 3, ref 11)
        rom = [x for x in rom if x[0] >= 20.6 * 60]
        mod = module_timeline(0x0B, 40, "DISC ON")
        compare(rom[1:] if rom and rom[0][1].startswith("DISC") else rom, mod[1:], 400)
    else:
        rom = rom_timeline(60.0, 100, press_start_at=60.0)
        rom = [x for x in rom if x[1].strip() and not x[1].startswith("1ST") and not x[1].startswith("HALL")]
        k = next(i for i, x in enumerate(rom) if "CUBE" in x[1])
        mod = [x for x in module_timeline(0x0E, 30, "") if x[1].strip()]
        # From the second line on (the first line's offset depends on the tick phase show() lands in),
        # up to the ROM's next re-show of 0x0E (a new scene shows it again).
        compare(rom[k + 1 : k + 6], mod[1:6], 5)
