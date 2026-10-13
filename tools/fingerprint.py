#!/usr/bin/env python3
"""Per-frame 16x12 grayscale fingerprints of video files -> build/fp/<name>.npy (uint8, frames x 192).

    .venv/bin/python -I tools/fingerprint.py OUTDIR FILE...

Fingerprints are derived numbers in build/ (ignored); no frames are stored.
"""

import os
import subprocess
import sys

import numpy as np

W, H = 16, 12


def fp(path, crop=None):
    vf = (crop + "," if crop else "") + f"scale={W}:{H}:flags=area,format=gray"
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-an", "-vf", vf, "-f", "rawvideo", "-"], capture_output=True, check=True
    ).stdout
    return np.frombuffer(raw, np.uint8).reshape(-1, W * H)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for p in sys.argv[2:]:
        name = os.path.splitext(os.path.basename(p))[0].lower()
        dst = os.path.join(out, name + ".npy")
        if os.path.exists(dst):
            continue
        # DVD is 720x480 with SAR 8:9 and the PC is 320x240 square: both 4:3, so scale only.
        np.save(dst, fp(p))
        print(name, np.load(dst).shape[0], flush=True)


if __name__ == "__main__":
    main()
