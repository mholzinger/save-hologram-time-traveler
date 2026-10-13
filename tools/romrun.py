#!/usr/bin/env python3
"""Run the real Time Traveler program ROM headless on an emulated 80188 (Unicorn, x86 16-bit)
and log what it asks the laserdisc player to do.

    .venv/bin/python -I tools/romrun.py [--seconds N] [--trace-io]

Hardware faked (sources in docs/ROM_FINDINGS.md "Hardware", reference/mame/timetrv.cpp):
  0x1000-0x1003 PPI1 (IN0 joystick/start/coin, IN1 buttons), 0x1080-0x1082 DIPs,
  0x1100-0x1107 16450 UART -> Pioneer LD-V4200 (reference/mame/ldv4200hle.cpp: commands end in CR,
  each command line answered "R\\r", search/stop-marker answered when done, "?F" -> frame number),
  0x1180/0x1200 LED, 0xFF00-0xFFFF 80188 peripheral control block (timers + interrupt controller).
This is a test harness for comparing the ROM with tools/sim.py; it stays in tools/, runs only
against the user's own ROM, and stores nothing from it.
"""

import argparse
import collections
import os
import re
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UcError
from unicorn.x86_const import (
    UC_X86_INS_IN,
    UC_X86_INS_OUT,
    UC_X86_REG_CS,
    UC_X86_REG_IP,
    UC_X86_REG_SP,
    UC_X86_REG_SS,
    UC_X86_REG_EFLAGS,
)

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT  # noqa: E402

# Timing. The ROM programs Timer 1 (PCB relocated to I/O 0x100 by writing 0x0001 to 0xFFFE at
# C000:0025) with CMPA 0x8235 and CON 0xE001 (DB9B:026D) and points INT 0x12 at D245:0127.
# With MAME's 16 MHz XTAL (reference/mame/timetrv.cpp) and the 80188's /2 CPU and /4 timer
# clocks (Intel 80C188 data sheet, UNVERIFIED here: not in the repo), that is 60 Hz.
TICK_HZ = 60
INSNS_PER_TICK = 8_000_000 // 4 // TICK_HZ  # rough: ~4 clocks per instruction at 8 MHz
TIMER1_VECTOR = 0x12


class Machine:
    def __init__(self, rom, trace_io=False):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x100000)
        self.uc.mem_write(0xC0000, rom)
        self.trace_io = trace_io
        self.ports = collections.Counter()
        self.pcb = {}
        self.dips = [0xFF, 0xFF, 0xFF]  # PPI2 PA/PB/PC (active-low DIPs?) - set by caller
        self.in0, self.in1 = 0xFF, 0xFF  # active low (MAME IP_ACTIVE_LOW)
        self.tx = bytearray()  # bytes sent by the game to the player
        self.rx = collections.deque()  # reply bytes waiting for the game
        self.ld_log = []
        self.later = []
        self.led = bytearray(b" " * 16)  # the cabinet's 16-character LED line (MAME timetrv.cpp:203-204)
        self.led_log = []  # [(tick, text)] each time the line changes
        self.uc.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, UC_X86_INS_OUT)

    # --- port I/O -----------------------------------------------------------
    pcb_base = 0xFF00  # 80188 PCB I/O window; the ROM moves it to 0x0100 (C000:0025)

    def is_pcb(self, port):
        return self.pcb_base <= port < self.pcb_base + 0x100

    def pcb_off(self, port):
        return (port - self.pcb_base) & 0xFE

    def on_in(self, uc, port, size, _):
        self.ports[("in", port)] += 1
        v = 0xFF
        if port == 0x1000:
            v = self.in0
        elif port == 0x1001:
            v = self.in1
        elif 0x1080 <= port <= 0x1082:
            v = self.dips[port - 0x1080]
        elif port == 0x1100:  # RBR
            v = self.rx.popleft() if self.rx else 0
        elif port == 0x1105:  # LSR: bit0 data ready, bit5 THRE, bit6 TEMT
            v = 0x60 | (1 if self.rx else 0)
        elif self.is_pcb(port):
            v = self.pcb.get(self.pcb_off(port), 0)
            if port & 1:
                v >>= 8
        if self.trace_io:
            print(f"IN  {port:04X} -> {v:02X}")
        return v & (0xFFFF if size == 2 else 0xFF)

    def on_out(self, uc, port, size, value, _):
        self.ports[("out", port)] += 1
        if self.trace_io:
            print(f"OUT {port:04X} <- {value:0{size * 2}X}")
        if port == 0x1103:  # 16450 LCR; bit 7 = DLAB (divisor latch, D30C:0002)
            self.lcr = value & 0xFF
        elif port == 0x1100 and not (getattr(self, "lcr", 0) & 0x80):
            self.ld_byte(value & 0xFF)
        elif 0x1180 <= port <= 0x1187 or 0x1200 <= port <= 0x1207:
            i = (port - 0x1180) if port < 0x1200 else 8 + port - 0x1200
            self.led[i] = value & 0xFF
            text = self.led_text()
            if not self.led_log or self.led_log[-1][1] != text:
                self.led_log.append((self.ticks, text))
        elif self.is_pcb(port):
            off = self.pcb_off(port)
            self.pcb[off] = value
            if off == 0xFE:  # relocation register: new I/O base
                self.pcb_base = (value & 0x0FFF) << 8

    # --- Pioneer LD-V4200 (reply model from ldv4200hle.cpp) -------------------
    # reference/mame/ldv4200hle.cpp process_command (349-495), stop-mark check (797-830), multispeed
    # (907-925): SM sets the mark to n + 1; PL/MF/MR with n set the mark to n + 1 and answer "R" when
    # it is crossed (in either direction), otherwise they answer at once; MF/MR to the current mark
    # go straight to still; SE answers when done; SP sets the multispeed rate (speed/60 per field).
    frame, mode, mark, running, speed, accum = 1, "still", None, False, 60, 0

    def ld_byte(self, b):
        self.tx.append(b)
        if b != 0x0D:
            return
        line = self.tx.decode("latin-1").strip("\r")
        self.tx.clear()
        self.ld_log.append((self.ticks, line))
        defer = False
        for num, op in re.findall(r"(\d*)([A-Z?][A-Z])", line):
            n = int(num) if num else None
            if op == "SE" and n is not None:
                self.frame, self.mode = n, "still"
            elif op == "SM" and n is not None:
                self.mark = n + 1
            elif op == "PL":
                self.mode, self.accum = "play", 0
                if n is not None:
                    self.mark, defer = n + 1, True
            elif op in ("MF", "MR"):
                if n is not None and n + 1 == self.frame:
                    self.mode = "still"
                else:
                    self.mode = "msf" if op == "MF" else "msr"
                    if n is not None:
                        self.mark, defer = n + 1, True
            elif op == "SP" and n is not None:
                self.speed = max(1, min(255, n))
            elif op in ("ST", "PA"):
                self.mode = "still"
            elif op == "?F":
                self.reply(b"%05d\r" % self.frame)
                return
        if defer:
            self.running = True  # "R" when the mark is reached
        else:
            self.reply(b"R\r")

    def led_text(self):
        """The LED line as text: MAME's glyph index is the byte & 0x3F (timetrv.cpp led_w)."""
        return "".join(chr(0x40 + (b & 0x3F)) if (b & 0x3F) < 0x20 else chr(b & 0x3F) for b in self.led)

    def ld_tick(self):
        """60 Hz field tick: PLAY advances one frame per two fields, MF/MR speed/60 per field."""
        old = self.frame
        if self.mode == "play" and self.ticks % 2 == 0:
            self.frame += 1
        elif self.mode in ("msf", "msr"):
            self.accum += self.speed
            step = self.accum // 60
            self.accum %= 60
            if self.mode == "msr":
                step = -step
            if self.mark is not None and (self.mark - old < 0) != (self.mark - old - step < 0):
                step = self.mark - old
            self.frame += step
        if self.mode != "still" and self.mark is not None and self.frame != old:
            d0, d1 = self.mark - old, self.mark - self.frame
            if d1 == 0 or (d0 < 0) != (d1 < 0):
                self.mark, self.mode = None, "still"
                if self.running:
                    self.running = False
                    self.reply(b"R\r")

    def reply(self, data, delay_ticks=1):
        # A real player answers after the command has been sent and processed. ld_send (D30C:0071)
        # flushes anything received while it is still transmitting, so an instant reply is lost.
        self.later.append((self.ticks + delay_ticks, bytes(data)))

    def deliver(self):
        keep = []
        for t, data in self.later:
            if t <= self.ticks:
                self.rx.extend(data)
            else:
                keep.append((t, data))
        self.later = keep

    # --- run --------------------------------------------------------------
    def reset(self):
        self.uc.reg_write(UC_X86_REG_CS, 0xF000)
        self.uc.reg_write(UC_X86_REG_IP, 0xFFF0)

    def where(self):
        return self.uc.reg_read(UC_X86_REG_CS), self.uc.reg_read(UC_X86_REG_IP)

    ticks = 0

    def interrupt(self, vector):
        uc = self.uc
        fl = uc.reg_read(UC_X86_REG_EFLAGS)
        if not fl & 0x200:  # IF clear: not now
            return False
        ss, sp = uc.reg_read(UC_X86_REG_SS), uc.reg_read(UC_X86_REG_SP)
        cs, ip = self.where()
        for w in (fl & 0xFFFF, cs, ip):
            sp = (sp - 2) & 0xFFFF
            uc.mem_write(ss * 16 + sp, struct.pack("<H", w))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_EFLAGS, fl & ~0x300)
        off, seg = struct.unpack("<HH", bytes(uc.mem_read(vector * 4, 4)))
        uc.reg_write(UC_X86_REG_CS, seg)
        uc.reg_write(UC_X86_REG_IP, off)
        return True

    def run(self, insns):
        cs, ip = self.where()
        self.uc.emu_start(cs * 16 + ip, 0x100000, count=insns)

    def run_ticks(self, n):
        """Run n timer ticks; Timer 1 fires once per tick once the ROM has enabled it."""
        for _ in range(n):
            self.run(INSNS_PER_TICK)
            self.ticks += 1
            self.ld_tick()
            self.deliver()
            if self.pcb.get(0x5E, 0) & 0x8000:  # Timer 1 control: EN
                for _ in range(200):  # wait for IF if the code has interrupts off
                    if self.interrupt(TIMER1_VECTOR):
                        break
                    self.run(20)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--trace-io", action="store_true")
    # MAME defaults (reference/mame/timetrv.cpp:226-285): DSW1 0x06, DSW2 0x04, DSW3 0x7F; 0x7E = free play.
    ap.add_argument("--dips", default="06,04,7E", help="DSW1,DSW2,DSW3 hex (raw port values)")
    ap.add_argument("--show", type=int, default=60)
    ap.add_argument("--start", type=float, default=None, help="press 1P Start at this many seconds")
    a = ap.parse_args()
    rom = open(os.path.join(ROOT, "rom", "tt061891.bin"), "rb").read()
    m = Machine(rom, a.trace_io)
    m.dips = [int(x, 16) for x in a.dips.split(",")]
    m.reset()
    if a.start is not None:
        t0 = int(a.start * TICK_HZ)
        m.script = {t0: ("in0", 0x10, True), t0 + 6: ("in0", 0x10, False)}  # IN0 bit 4 = START1 (active low)
    pcs = collections.Counter()
    for i in range(int(a.seconds * TICK_HZ)):
        try:
            m.run_ticks(1)
        except UcError as e:
            cs, ip = m.where()
            print(f"stopped: {e} at {cs:04X}:{ip:04X} tick {m.ticks}")
            break
        pcs[m.where()] += 1
        ev = getattr(m, "script", {}).get(m.ticks)
        if ev:
            reg, bit, down = ev
            v = getattr(m, reg)
            setattr(m, reg, (v & ~bit) if down else (v | bit))
    print("LD commands:")
    for t, line in m.ld_log[: a.show]:
        print(f"  t={t / TICK_HZ:7.2f}s  {line}")
    print("hot PCs:", [(f"{c:04X}:{i:04X}", n) for (c, i), n in pcs.most_common(5)])


if __name__ == "__main__":
    main()
