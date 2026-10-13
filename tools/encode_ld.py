#!/usr/bin/env python3
"""Encode ROM clips for the 3DS straight from the laserdisc CHD (the arcade footage itself).

    .venv/bin/python -I tools/encode_ld.py [CLIP_ID ...] [--all] [--jobs 4] [--out build/enc]

ROM frame n is disc picture n + 1 (MAME ldv4200hle.cpp:623/:488) = CHD frame n (tools/chd_frames.py:
picture p = CHD frame p - 1), so no DVD mapping is needed. Per clip (data/clips.json start..stop, inclusive):
  1. chdman extractld the range to a temporary AVI under video/source/chd/ (ignored, deleted after),
  2. crop the 480 active lines (44 VBI lines on top), keep one field (240 lines: the 3DS height,
     no deinterlacing artefacts), flip upright (the disc is stored upside down for the cabinet
     mirror), 4:3 -> 320x240,
  3. key near-black to pure black (the cabinet's void),
  4. 400x240 eye canvas per eye, picture shifted +p/2 left eye / -p/2 right eye (crossed disparity:
     the actors float in front of the screen, PORT_PLAN.md §1), L|R side by side 800x240,
     H.264 Constrained Baseline for MVD,
  5. audio: the LD channel(s) the clip selects (clips.json audio_mask: 1 = ch1/L, 2 = ch2/R,
     3 = both), mono PCM16 32 kHz WAV for NDSP.
Outputs: build/enc_ld/clipNNN.h264, clipNNN.wav (+ .h264.mp4, _mid.png); with --extra also NAME.h264 +
NAME_1.wav / NAME_2.wav / NAME_3.wav (both) for EXTRA and mapKKKK (KKKK = Time Map key). All in ignored paths.
"""

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT, load_json  # noqa: E402

CHD = os.path.join(ROOT, "rom", "timetrv.chd")  # --chd
TMP = os.path.join(ROOT, "video", "source", "chd")  # --tmp: one extracted AVI per clip while it encodes
W, H = 320, 240


# Footage the ROM plays from hardcoded frame numbers (docs/ROM_FINDINGS.md "Lives, cubes, Trader, Hellgate",
# tools/romdiff.py --trader/--map-keys runs). Name -> (first, last) LD frame. Audio is written per channel
# (NAME_1.wav, NAME_2.wav) because the ROM picks the channel at run time.
EXTRA = {
    "trader_intro": (27764, 27959),  # CEDF:075C
    "trader_wait": (27961, 28039),  # ping-pong 27961-28014 (CEDF:0909/0933) and the still 28039
    "trader_buy": (28053, 28188),  # each coin (0x6D95-0x6E1C)
    "hg_offer": (23651, 23980),  # D0F0:029D
    "hg_offer2": (23982, 24184),  # second offer after a jackpot (D0F0: ld_search_play_to 0x5dce-0x5e78)
    "hg_jackpot": (24187, 24337),
    "hg_cubes": (24340, 24501),
    "hg_life": (24505, 24667),
    "hg_after": (24669, 24838),  # ch1 won / ch2 lost
    "hg_lose1": (24841, 24999),
    "hg_lose2": (25003, 25163),
    "hg_devil": (25164, 25516),  # 25164-25323 then 25325-25516
    "hiscore": (47768, 48040),  # DA9D:074E
    "gameover": (48215, 48382),  # D7C0:05F7, after the hall of fame (1 player); LD audio channel unchanged
    "trap_a": (23581, 23649),  # DA9D:0E6D (C3 E82 trap), first part
    "trap_b": (26062, 26305),  # DA9D:0E6D, second part
    "trader_loop": (27961, 28014),  # played forward then backward (CEDF:0909-0946); stored as a palindrome
    "handover_p1": (15330, 15450),  # D7C0:04BA: 2 players, player 1 takes over (and at 2P game start, D668:036F)
    "handover_p2": (15612, 15732),  # D7C0:04BA: player 2 takes over
    "intro_skip": (248, 350),  # CE55:0138 (input_event c000_e619): 1P+2P Start together in chapter 0
    "egg_up": (53469, 53618),  # C2BA:0019: attract, Attack+Cube held and Up (once per attract)
    "egg_down": (53646, 53884),  # C2BA:0019: the same with Down
    "price_28049": (28049, 28053),  # Trader price still for 1 credit / 3 cubes and 2 / 3 (specobj_CEDF_0EC0)
    "price_26902": (26902, 26906),  # Trader price still for the alternating 2/1 setting
}
PALINDROME = {"trader_loop"}  # MVD cannot decode backwards, so the reversed half is encoded after the forward one


def spec_ranges():
    """Special objects (hint clips, D47D:1D6C; data/specobj_clips.json): keys come in pairs that share
    frames and differ in audio channel, so each odd key's range is encoded once as specKK."""
    recs = load_json(os.path.join(ROOT, "data", "specobj_clips.json"))["records"]
    return {"spec%02d" % r["key"]: (r["start"], r["stop"]) for r in recs if r["key"] % 2 == 1}


def map_ranges():
    """Each Time Map mask's stills as one range: first option's still .. last option's still + 4."""
    tm = load_json(os.path.join(ROOT, "data", "time_map.json"))["options"]
    out = {}
    for k, v in tm.items():
        lo, hi = min(o["still"][0] for o in v), max(o["still"][1] for o in v)
        out["map" + k] = (lo, hi)
    return out


def encode(cid, clip, out, parallax, key, crf):
    s, e = clip["start"], clip["stop"]
    n = e - s + 1
    tmp = os.path.join(TMP, f"enc_{cid}.avi")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    # ROM frame n is disc picture n + 1: MAME's LD-V4200 searches to value + 1 and stops at value + 1
    # (reference/mame/ldv4200hle.cpp:623, :488). The Time Map art confirms it: ROM still 26425 is
    # picture 26426, the first highlighted frame (picture 26425 is black). Picture p = CHD frame p - 1.
    subprocess.run(
        ["chdman", "extractld", "-i", CHD, "-o", tmp, "-isf", str(s), "-if", str(n), "-f"],
        capture_output=True,
        check=True,
    )
    base = os.path.join(out, cid if isinstance(cid, str) else f"clip{cid:03d}")
    h = parallax // 2
    dec = subprocess.Popen(
        [
            "ffmpeg",
            "-nostdin",
            "-v",
            "error",
            "-i",
            tmp,
            "-an",
            "-vf",
            f"crop=720:480:0:44,field=top,vflip,scale={W}:{H}:flags=lanczos",
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
            str(crf),
            "-g",
            "30",
            base + ".h264.mp4",
        ],
        stdin=subprocess.PIPE,
    )
    k = key * 255
    frames = 0
    while True:
        buf = dec.stdout.read(W * H * 3)
        if len(buf) < W * H * 3:
            break
        f = np.frombuffer(buf, np.uint8).reshape(H, W, 3).copy()
        lum = (f.astype(np.int32) @ np.array([77, 150, 29], np.int32)) >> 8
        f[lum < k] = 0
        o = np.zeros((H, 800, 3), np.uint8)
        o[:, 40 + h : 40 + h + W] = f
        o[:, 440 - h : 440 - h + W] = f
        enc.stdin.write(o.tobytes())
        frames += 1
    enc.stdin.close()
    enc.wait()
    dec.wait()
    if cid in PALINDROME:
        fwd = base + "_fwd.mp4"
        os.replace(base + ".h264.mp4", fwd)
        subprocess.run(
            [
                "ffmpeg",
                "-nostdin",
                "-v",
                "error",
                "-y",
                "-i",
                fwd,
                "-filter_complex",
                "[0:v]split[a][b];[b]reverse[r];[a][r]concat=n=2:v=1:a=0",
                "-c:v",
                "libx264",
                "-profile:v",
                "baseline",
                "-level",
                "3.1",
                "-pix_fmt",
                "yuv420p",
                "-crf",
                str(crf),
                "-g",
                "30",
                base + ".h264.mp4",
            ],
            check=True,
        )
        os.remove(fwd)
        frames *= 2
    subprocess.run(
        [
            "ffmpeg",
            "-nostdin",
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
    pans = (
        {"": {1: "c0=c0", 2: "c0=c1"}.get(clip["audio_mask"], "c0=0.5*c0+0.5*c1")}
        if clip["audio_mask"]
        else {"_1": "c0=c0", "_2": "c0=c1", "_3": "c0=0.5*c0+0.5*c1"}
    )
    for suffix, pan in pans.items():
        subprocess.run(
            [
                "ffmpeg",
                "-nostdin",
                "-v",
                "error",
                "-y",
                "-i",
                tmp,
                "-vn",
                "-af",
                f"pan=mono|{pan}",
                "-ar",
                "32000",
                "-c:a",
                "pcm_s16le",
                base + suffix + ".wav",
            ],
            check=True,
        )
    subprocess.run(
        [
            "ffmpeg",
            "-nostdin",
            "-v",
            "error",
            "-y",
            "-ss",
            f"{frames / 2 / 29.97:.3f}",
            "-i",
            base + ".h264.mp4",
            "-frames:v",
            "1",
            base + "_mid.png",
        ],
        check=True,
    )
    os.remove(tmp)
    return {
        "clip": cid,
        "ld": [s, e],
        "frames": frames,
        "video_bytes": os.path.getsize(base + ".h264"),
        "audio_bytes": sum(os.path.getsize(base + x + ".wav") for x in pans),
    }


def main(argv=None):
    global CHD, TMP
    ap = argparse.ArgumentParser()
    ap.add_argument("clips", nargs="*", type=int)
    ap.add_argument("--all", action="store_true", help="every clip the game uses (data/game.json)")
    ap.add_argument("--extra", action="store_true", help="the hardcoded ranges (EXTRA) and every Time Map")
    ap.add_argument("--missing", action="store_true", help="skip clips and segments already encoded in --out")
    ap.add_argument("--only", help="with --extra: comma list of names to encode (e.g. handover_p1,handover_p2)")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--parallax", type=int, default=6)
    ap.add_argument("--key", type=float, default=0.10)
    ap.add_argument("--crf", type=int, default=23)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "enc_ld"))
    ap.add_argument("--chd", default=CHD, help="the laserdisc image (default rom/timetrv.chd)")
    ap.add_argument("--tmp", default=TMP, help="folder for the per-clip AVIs chdman extracts")
    a = ap.parse_args(argv)
    CHD, TMP = a.chd, a.tmp
    if not os.path.exists(CHD):
        sys.exit(f"{CHD} missing")
    clips = {c["id"]: c for c in load_json(os.path.join(ROOT, "data", "clips.json"))["records"]}
    want = list(a.clips)
    if a.all:
        g = load_json(os.path.join(ROOT, "data", "game.json"))
        want = sorted(
            {
                m["clip"]
                for ep in g["episodes"]
                for r in [ep] + list(ep["deaths"].values())
                for m in r["media"]
                if "clip" in m
            }
        )
    jobs = [(c, clips[c]) for c in want]
    if a.extra:
        for name, (lo, hi) in {**EXTRA, **map_ranges(), **spec_ranges()}.items():
            if a.only and name not in a.only.split(","):
                continue
            jobs.append((name, {"start": lo, "stop": hi, "audio_mask": 0}))
    os.makedirs(a.out, exist_ok=True)
    if a.missing:

        def done(c):
            base = os.path.join(a.out, c if isinstance(c, str) else "clip%03d" % c)
            return os.path.exists(base + ".h264") and os.path.exists(
                base + ".wav" if not isinstance(c, str) else base + "_3.wav"
            )

        jobs = [j for j in jobs if not done(j[0])]
        print(f"{len(jobs)} to encode (others already in {a.out})", flush=True)
    total = [0, 0]
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        futs = {ex.submit(encode, c, cl, a.out, a.parallax, a.key, a.crf): c for c, cl in jobs}
        for f in concurrent.futures.as_completed(futs):
            r = f.result()
            total[0] += r["video_bytes"]
            total[1] += r["audio_bytes"]
            print(json.dumps(r), flush=True)
    print(f"{len(jobs)} encodes: video {total[0] / 1e6:.1f} MB, audio {total[1] / 1e6:.1f} MB")


if __name__ == "__main__":
    main()
