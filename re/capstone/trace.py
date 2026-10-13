"""Recursive-descent disassembler for the Time Traveler 80188 ROM.
Linear address = seg*16 + off. ROM covers 0xC0000-0xFFFFF.
Usage: trace.py <rom.bin> [outdir]
Outputs: funcs.txt (entry points), io.txt (port accesses), listing.asm.
"""

import sys
import re
import collections
import capstone
from capstone.x86 import X86_OP_IMM

ROM = open(sys.argv[1], "rb").read()
import os

OUT = sys.argv[2] if len(sys.argv) > 2 else "."
os.makedirs(OUT, exist_ok=True)
BASE = 0xC0000
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
md.detail = True


def rd(lin, n):
    o = lin - BASE
    return ROM[o : o + n] if 0 <= o < len(ROM) else b''


insns = {}  # lin -> (seg, off, insn)
funcs = {}  # lin -> (seg, off) entry
far_entries = set()
xrefs = collections.defaultdict(set)  # target lin -> {from lin}
work = []


def add_func(seg, off, frm=None):
    lin = (seg << 4) + off
    if not (BASE <= lin < BASE + len(ROM)):
        return
    if frm is not None:
        xrefs[lin].add(frm)
    if lin not in funcs:
        funcs[lin] = (seg, off)
        work.append((seg, off))


def trace(seg, off):
    q = [off]
    while q:
        o = q.pop()
        while True:
            lin = (seg << 4) + o
            if lin in insns or not (BASE <= lin < BASE + len(ROM)):
                break
            code = rd(lin, 16)
            try:
                i = next(md.disasm(code, o))
            except StopIteration:
                break
            insns[lin] = (seg, o, i)
            m = i.mnemonic
            nxt = (o + i.size) & 0xFFFF
            ops = i.operands
            if m in ('ret', 'retf', 'iret', 'hlt'):
                break
            if m == 'ljmp' or m == 'lcall':
                if len(ops) == 2 and ops[0].type == X86_OP_IMM:
                    s2, o2 = ops[0].imm & 0xFFFF, ops[1].imm & 0xFFFF
                    add_func(s2, o2, lin)
                    far_entries.add((s2 << 4) + o2)
                if m == 'ljmp':
                    break
                o = nxt
                continue
            if m == 'call':
                if ops and ops[0].type == X86_OP_IMM:
                    add_func(seg, ops[0].imm & 0xFFFF, lin)
                o = nxt
                continue
            if m == 'jmp':
                if ops and ops[0].type == X86_OP_IMM:
                    o = ops[0].imm & 0xFFFF
                    continue
                break  # indirect jmp: jump table, stop
            if m.startswith('j') or m.startswith('loop') or m == 'jcxz':
                if ops and ops[0].type == X86_OP_IMM:
                    q.append(ops[0].imm & 0xFFFF)
                o = nxt
                continue
            o = nxt


add_func(0xC000, 0x0000)
while work:
    s, o = work.pop()
    trace(s, o)

# Second pass: scan for MSC far-function prologues not yet reached
# (push bp; mov bp,sp) = 55 8B EC  — candidate entries reached via pointers.
pro = re.compile(rb'\x55\x8b\xec')
found_new = 0
for mt in pro.finditer(ROM):
    lin = BASE + mt.start()
    if lin not in insns:
        # guess segment: use the segment of nearest traced insn before it
        prev = max((l for l in insns if l < lin), default=None)
        seg = insns[prev][0] if prev else 0xC000
        add_func(seg, lin - (seg << 4))
        found_new += 1
while work:
    s, o = work.pop()
    trace(s, o)

# I/O analysis: track last "mov dx, imm" in each linear run
io = collections.defaultdict(list)
dx = None
for lin in sorted(insns):
    s, o, i = insns[lin]
    if i.mnemonic == 'mov' and i.op_str.startswith('dx, ') and i.operands[1].type == X86_OP_IMM:
        dx = i.operands[1].imm & 0xFFFF
    elif i.mnemonic in ('in', 'out', 'insb', 'outsb', 'insw', 'outsw'):
        if 'dx' in i.op_str and dx is not None:
            io[dx].append((lin, i.mnemonic))
        else:
            for op in i.operands:
                if op.type == X86_OP_IMM:
                    io[op.imm & 0xFFFF].append((lin, i.mnemonic))
    if i.mnemonic in ('ret', 'retf', 'jmp', 'ljmp', 'call', 'lcall'):
        dx = None

segs = collections.Counter(s for (s, o) in funcs.values())
with open(os.path.join(OUT, 'funcs.txt'), 'w') as f:
    for lin in sorted(funcs):
        s, o = funcs[lin]
        f.write(f"{lin:05X} {s:04X}:{o:04X} far={'Y' if lin in far_entries else 'n'} xrefs={len(xrefs[lin])}\n")
with open(os.path.join(OUT, 'io.txt'), 'w') as f:
    for p in sorted(io):
        f.write(f"port {p:04X}: " + ' '.join(f"{l:05X}/{m}" for l, m in io[p]) + "\n")
with open(os.path.join(OUT, 'listing.asm'), 'w') as f:
    for lin in sorted(insns):
        s, o, i = insns[lin]
        tag = ''
        if lin in funcs:
            f.write(
                f"\n; ---- sub_{lin:05X} ({s:04X}:{o:04X}) xrefs: "
                + ' '.join(f"{x:05X}" for x in sorted(xrefs[lin]))
                + "\n"
            )
        f.write(f"{lin:05X} {s:04X}:{o:04X}  {i.bytes.hex():14} {i.mnemonic} {i.op_str}\n")

cov = sum(i.size for (_, _, i) in insns.values())
print(f"funcs={len(funcs)} insns={len(insns)} bytes_covered={cov} ({cov * 100 // len(ROM)}%) prologue_adds={found_new}")
print("segments:", {f'{k:04X}': v for k, v in segs.most_common(12)})
