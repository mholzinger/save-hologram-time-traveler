#!/usr/bin/env python3
"""Check that every function label in TimeTravelerLabels.java sits on a known function start.

    python3 tools/check_labels.py      # after `make disasm` (uses build/capstone/funcs.txt)

The Ghidra label script creates a function wherever it is told, so a mistyped SEG:OFF
silently splits a real function. This cross-checks against the independent Capstone pass
and, if present, Ghidra's own function index from `make ghidra-decomp`.
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT  # noqa: E402

ALLOW = {"reset_vector"}

starts = set()
for l in open(os.path.join(ROOT, "build/capstone/funcs.txt")):
    m = re.search(r"\b([0-9A-F]{5})\b", l)
    if m:
        starts.add(int(m.group(1), 16))
idx = os.path.join(ROOT, "build/ghidra/decomp/INDEX.tsv")
if os.path.exists(idx):
    for l in open(idx).read().splitlines()[1:]:
        starts.add(int(l.split("\t")[0].replace(":", ""), 16))

src = open(os.path.join(ROOT, "re/ghidra/scripts/TimeTravelerLabels.java")).read()
funcs = src[src.index("FUNCS") : src.index("DATA")]
bad = []
for seg, off, name in re.findall(r'\{"([0-9A-F]{4}):([0-9A-F]{4})", "(\w+)"', funcs):
    lin = int(seg, 16) * 16 + int(off, 16)
    if lin not in starts and name not in ALLOW:
        bad.append(f"{seg}:{off} {name} (linear {lin:05X})")
for b in bad:
    print("not a function start:", b)
print(f"checked {len(re.findall(r'{\"[0-9A-F]{4}:', funcs))} function labels, {len(bad)} bad")
sys.exit(1 if bad else 0)
