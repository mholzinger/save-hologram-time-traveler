#!/usr/bin/env python3
"""Estimate the DVD's static CG overlay (prop shapes + perspective grid) -> build/overlay/plate.npy, props.npy.

    .venv/bin/python -I tools/dvd_overlay.py

The Digital Leisure DVD draws the same overlay on every 2D game title (title medians differ by
< 1 gray level). plate = per-pixel median over sampled frames of all angle-1 game titles at
320x240 after deinterlacing. props = bright plate pixels in the top 45%, opened (radius 3)
to drop the thin grid, then dilated by 6 px so the shapes' rotation stays inside the mask. Derived data only, in build/.
"""

import glob
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT  # noqa: E402

W, H = 320, 240


def frames(path, step=61):
    raw = subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            path,
            "-an",
            "-vf",
            f"bwdif=mode=send_frame:parity=bff,select='not(mod(n\\,{step}))',scale={W}:{H},format=gray",
            "-fps_mode",
            "passthrough",
            "-f",
            "rawvideo",
            "-",
        ],
        capture_output=True,
        check=True,
    ).stdout
    return np.frombuffer(raw, np.uint8).reshape(-1, H, W)


def dilate(m, r):
    out = m.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            out |= np.roll(np.roll(m, dy, 0), dx, 1)
    return out


def main():
    stack = []
    for p in sorted(glob.glob(os.path.join(ROOT, "video", "dvd", "vts*_pgc01_a1.mkv"))):
        stack.append(frames(p))
    allf = np.concatenate(stack)
    plate = np.median(allf, 0).astype(np.uint8)
    yy = np.arange(H)[:, None] * np.ones((1, W), bool)
    bright = (plate > 40) & (yy < 0.45 * H)
    opened = dilate(~dilate(~bright, 3), 3)  # opening: drops thin grid lines and crossings
    props = dilate(opened, 6)
    os.makedirs(os.path.join(ROOT, "build", "overlay"), exist_ok=True)
    np.save(os.path.join(ROOT, "build", "overlay", "plate.npy"), plate)
    np.save(os.path.join(ROOT, "build", "overlay", "props.npy"), props)
    vis = np.hstack([plate, (props * 255).astype(np.uint8)])
    subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-y",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "gray",
            "-s",
            f"{2 * W}x{H}",
            "-i",
            "-",
            os.path.join(ROOT, "build", "overlay", "plate_props.png"),
        ],
        input=vis.tobytes(),
        check=True,
    )
    print(f"{len(allf)} frames sampled; props cover {props.mean():.1%} of the frame")


if __name__ == "__main__":
    main()
