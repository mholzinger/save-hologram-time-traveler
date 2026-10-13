#!/usr/bin/env python3
"""Locate every PC clip inside the DVD 2D (angle 1) title MKVs by frame fingerprints.

    .venv/bin/python -I tools/match_pc_dvd.py build/fp data

Inputs: build/fp/{pc,dvd}/*.npy from tools/fingerprint.py. PC video is every other NTSC frame,
so PC frame i is compared with DVD frame t0 + 2*i. Score = mean Pearson correlation of the
16x12 grayscale signatures over the whole clip. Output: data/pc_to_dvd.json (numbers only).
"""

import glob
import json
import os
import sys

import numpy as np


def norm(a):
    a = a.astype(np.float32)
    a -= a.mean(1, keepdims=True)
    a /= np.linalg.norm(a, axis=1, keepdims=True) + 1e-3
    return a


def main():
    fp, out = sys.argv[1], sys.argv[2]
    dvd_names, chunks, starts = [], [], []
    n = 0
    for p in sorted(glob.glob(os.path.join(fp, "dvd", "*.npy"))):
        a = np.load(p)
        if len(a) < 30:
            continue
        dvd_names.append(os.path.basename(p)[:-4])
        starts.append(n)
        chunks.append(norm(a))
        n += len(a)
    D = np.concatenate(chunks)
    starts = np.array(starts + [n])
    file_of = np.searchsorted(starts, np.arange(n), side="right") - 1
    res = {}
    for p in sorted(glob.glob(os.path.join(fp, "pc", "*.npy"))):
        name = os.path.basename(p)[:-4]
        P = norm(np.load(p))
        if len(P) < 4:
            continue
        var = np.load(p).astype(np.float32).std(1)
        k = int(np.argmax(var[len(P) // 8 : len(P) - len(P) // 8 or None])) + len(P) // 8
        c = D @ P[k]
        best = None
        for t in np.argsort(c)[-40:]:
            for ph in (0, 1):
                t0 = int(t) - 2 * k + ph * 0
                idx = t0 + 2 * np.arange(len(P))
                if idx[0] < 0 or idx[-1] >= n or file_of[idx[0]] != file_of[idx[-1]]:
                    continue
                s = float(np.mean(np.sum(D[idx] * P, 1)))
                if best is None or s > best[0]:
                    best = (s, t0)
        if best is None:
            res[name] = None
            continue
        s, t0 = best
        f = int(file_of[t0])
        res[name] = {"dvd": dvd_names[f], "frame": int(t0 - starts[f]), "frames": 2 * len(P), "score": round(s, 3)}
        print(name, res[name], flush=True)
    json.dump(
        {"source": "tools/match_pc_dvd.py; frame = DVD frame index (29.97 fps) of PC frame 0", "matches": res},
        open(os.path.join(out, "pc_to_dvd.json"), "w"),
        indent=1,
    )
    good = sum(1 for v in res.values() if v and v["score"] >= 0.9)
    print(f"{len(res)} PC clips: {good} with score >= 0.9")


if __name__ == "__main__":
    main()
