#!/usr/bin/env python3
"""Prototype encode of one ROM clip for the 3DS (PORT_PLAN.md M2/M3, offline half).

    python3 -I tools/encode_clip.py CLIP_ID [--parallax 6] [--key 0.10] [--out build/enc]

Source: data/ld_to_dvd.json -> DVD angle-1 MKV range. Steps (all ffmpeg):
  1. cut the clip, deinterlace (bwdif, bottom field first per video/README.md), 4:3 -> 320x240,
  2. remove the DVD's static CG overlay (tools/dvd_overlay.py: prop boxes always blanked; grid
     pixels blanked where the frame matches the plate) and luma-key near-black to pure black,
  3. place the 320x240 picture on a 400x240 black eye canvas, shifted +p/2 px in the left eye and
     -p/2 in the right (crossed disparity = actor in front of the screen; proposal, PORT_PLAN.md §1),
  4. stack L|R into 800x240 side-by-side, H.264 Constrained Baseline (MVD profile support UNVERIFIED),
  5. audio: the LD channel(s) the ROM selects (clip audio_mask; DVD stream 0x80 L = ch1, R = ch2 is
     an UNVERIFIED assumption, video/README.md), 16-bit PCM mono WAV at 32000 Hz for NDSP.
Outputs stay in build/ (ignored by git).
"""

import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT, load_json  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("clip", type=int)
    ap.add_argument("--parallax", type=int, default=6)
    ap.add_argument("--key", type=float, default=0.10)
    ap.add_argument("--crf", type=int, default=23)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "enc"))
    a = ap.parse_args()
    m = load_json(os.path.join(ROOT, "data", "ld_to_dvd.json"))["clips"].get(str(a.clip))
    if not m:
        sys.exit(f"clip {a.clip} has no DVD mapping")
    src = os.path.join(ROOT, "video", "dvd", m["dvd"] + ".mkv")
    os.makedirs(a.out, exist_ok=True)
    fps = 30000 / 1001
    ss, dur = m["dvd_start"] / fps, m["dvd_frames"] / fps
    h = a.parallax // 2
    base = os.path.join(a.out, f"clip{a.clip:03d}")
    import numpy as np

    ov = os.path.join(ROOT, "build", "overlay")
    plate = np.load(os.path.join(ov, "plate.npy")).astype(np.int16)
    props = np.load(os.path.join(ov, "props.npy"))
    grid = (plate > 15) & ~props
    W, H = 320, 240
    dec = subprocess.Popen(
        [
            "ffmpeg",
            "-v",
            "error",
            "-ss",
            f"{ss:.3f}",
            "-i",
            src,
            "-t",
            f"{dur:.3f}",
            "-an",
            "-vf",
            f"bwdif=mode=send_frame:parity=bff,scale={W}:{H}",
            "-pix_fmt",
            "rgb24",
            "-f",
            "rawvideo",
            "-",
        ],
        stdout=subprocess.PIPE,
    )
    enc = subprocess.Popen(
        [
            "ffmpeg",
            "-v",
            "error",
            "-y",
            "-f",
            "rawvideo",
            "-pix_fmt",
            "rgb24",
            "-s",
            "800x240",
            "-r",
            "30000/1001",
            "-i",
            "-",
            "-c:v",
            "libx264",
            "-profile:v",
            "baseline",
            "-level",
            "3.1",
            "-pix_fmt",
            "yuv420p",
            "-crf",
            str(a.crf),
            "-g",
            "30",
            base + ".h264.mp4",
        ],
        stdin=subprocess.PIPE,
    )
    key = a.key * 255
    n = 0
    while True:
        buf = dec.stdout.read(W * H * 3)
        if len(buf) < W * H * 3:
            break
        f = np.frombuffer(buf, np.uint8).reshape(H, W, 3).copy()
        lum = (f.astype(np.int32) @ np.array([77, 150, 29], np.int32)) >> 8
        void = (lum < key) | props | (grid & (np.abs(lum - plate) < 18))
        f[void] = 0
        out = np.zeros((H, 800, 3), np.uint8)
        out[:, 40 + h : 40 + h + W] = f  # left eye: actor shifted right
        out[:, 400 + 40 - h : 400 + 40 - h + W] = f  # right eye: shifted left -> crossed disparity
        enc.stdin.write(out.tobytes())
        n += 1
    enc.stdin.close()
    enc.wait()
    dec.wait()
    # Raw Annex-B copy for the 3DS player (port/3ds/source/video.c splits NAL units itself).
    subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-y",
            "-i",
            base + ".h264.mp4",
            "-c:v",
            "copy",
            "-bsf:v",
            "h264_mp4toannexb",
            "-f",
            "h264",
            base + ".h264",
        ],
        check=True,
    )
    pan = {1: "c0=c0", 2: "c0=c1", 3: "c0=0.5*c0+0.5*c1"}.get(m["audio"], "c0=0.5*c0+0.5*c1")
    subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-y",
            "-ss",
            f"{ss:.3f}",
            "-i",
            src,
            "-t",
            f"{dur:.3f}",
            "-map",
            "0:a:0",
            "-af",
            f"pan=mono|{pan}",
            "-ar",
            "32000",
            "-c:a",
            "pcm_s16le",
            base + ".wav",
        ],
        check=True,
    )
    subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-y",
            "-ss",
            f"{dur / 2:.3f}",
            "-i",
            base + ".h264.mp4",
            "-frames:v",
            "1",
            base + "_mid.png",
        ],
        check=True,
    )
    vs, au = os.path.getsize(base + ".h264.mp4"), os.path.getsize(base + ".wav")
    print(
        json.dumps(
            {
                "clip": a.clip,
                "seconds": round(dur, 2),
                "video_bytes": vs,
                "video_kbps": round(vs * 8 / dur / 1000),
                "audio_bytes": au,
                "out": base,
            }
        )
    )


if __name__ == "__main__":
    main()
