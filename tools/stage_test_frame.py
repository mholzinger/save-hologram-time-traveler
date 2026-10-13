#!/usr/bin/env python3
"""Write one encoded frame in the 3DS MVD output format, for the stage test build (port/3ds main.c
TT_STAGE_TEST, Azahar has no MVD decoder).

    .venv/bin/python -I tools/stage_test_frame.py build/enc_ld/clip028.h264.mp4 OUT.bin [--t 3.0]

Output: 800x240 RGB565 little-endian with red in the low 5 bits and blue in the high 5, as MVD writes it
on hardware (video.c blit). Derived from ignored encodes; the output belongs in ignored paths.
"""

import argparse
import subprocess

import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("mp4")
ap.add_argument("out")
ap.add_argument("--t", type=float, default=3.0)
a = ap.parse_args()
raw = subprocess.run(
    [
        "ffmpeg",
        "-nostdin",
        "-v",
        "error",
        "-ss",
        str(a.t),
        "-i",
        a.mp4,
        "-frames:v",
        "1",
        "-pix_fmt",
        "rgb24",
        "-f",
        "rawvideo",
        "-",
    ],
    capture_output=True,
    check=True,
).stdout
f = np.frombuffer(raw, np.uint8).reshape(240, 800, 3).astype(np.uint16)
v = (f[:, :, 2] >> 3) << 11 | (f[:, :, 1] >> 2) << 5 | (f[:, :, 0] >> 3)
v.astype("<u2").tofile(a.out)
