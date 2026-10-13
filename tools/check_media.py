#!/usr/bin/env python3
"""Check that every clip and named segment the 3DS game opens is encoded, before staging.

    .venv/bin/python -I tools/check_media.py [ENC_DIR]      (default build/enc_ld)
    .venv/bin/python -I tools/check_media.py --manifest OUT  (frame counts only; no media needed)

Needed: clipNNN.h264 + clipNNN.wav for every clip a scene or death record of data/game.json plays, and
NAME.h264 + NAME_1/_2/_3.wav for every named segment tools/encode_ld.py encodes (EXTRA, the Time Map
stills, the hint objects), which the front end (port/3ds/source/fe_ld.c segs[]) opens by name. Exits 1 and lists what is
missing; make stage runs it so a gap fails the build instead of showing up as black footage on the 3DS.
It also checks each encode's frame count against expected_frames(), the count the host front-end test
(port/3ds/host) takes from --manifest so it runs without any footage.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import ROOT, load_json  # noqa: E402
import encode_ld  # noqa: E402  (tables only; nothing runs on import)


def game_clips():
    """Every clip a scene or death record plays."""
    g = load_json(os.path.join(ROOT, "data", "game.json"))
    return sorted(
        {
            m["clip"]
            for ep in g["episodes"]
            for r in [ep] + list(ep["deaths"].values())
            for m in r["media"]
            if "clip" in m
        }
    )


def named_ranges():
    return {**encode_ld.EXTRA, **encode_ld.map_ranges(), **encode_ld.spec_ranges()}


def expected_frames():
    """{video name: frames} as tools/encode_ld.py encodes them: LD start..stop inclusive, a palindrome
    twice. Checked against all 429 encodes (2026-10-12)."""
    clips = {c["id"]: c for c in load_json(os.path.join(ROOT, "data", "clips.json"))["records"]}
    out = {f"clip{c:03d}": clips[c]["stop"] - clips[c]["start"] + 1 for c in game_clips()}
    for n, (s, e) in named_ranges().items():
        out[n] = (e - s + 1) * (2 if n in encode_ld.PALINDROME else 1)
    return out


def stream_frames(path):
    """Pictures in an H.264 stream: its slice NAL units (types 1 and 5)."""
    b, n, i = open(path, "rb").read(), 0, 0
    while (i := b.find(b"\x00\x00\x01", i)) >= 0 and i + 3 < len(b):
        n += (b[i + 3] & 0x1F) in (1, 5)
        i += 3
    return n


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ["--manifest"]:
        with open(argv[1], "w") as f:
            for n, frames in sorted(expected_frames().items()):
                f.write(f"{n} {frames}\n")
        return
    enc = argv[0] if argv else os.path.join(ROOT, "build", "enc_ld")
    have = set(os.listdir(enc)) if os.path.isdir(enc) else set()
    clips = game_clips()
    need = [f"clip{c:03d}{x}" for c in clips for x in (".h264", ".wav")]
    named = named_ranges()
    need += [f"{n}{x}" for n in named for x in (".h264", "_1.wav", "_2.wav", "_3.wav")]
    wrong = [
        f"{n}.h264: {stream_frames(os.path.join(enc, n + '.h264'))} frames, expected {f}"
        for n, f in expected_frames().items()
        if n + ".h264" in have and stream_frames(os.path.join(enc, n + ".h264")) != f
    ]
    if wrong:
        print("frame counts differ from the clip table (tools/encode_ld.py changed?):")
        for w in wrong[:20]:
            print("  " + w)
        sys.exit(1)
    missing = [n for n in need if n not in have]
    if missing:
        print(f"{len(missing)} of {len(need)} media files missing in {enc}:")
        for n in missing[:40]:
            print("  " + n)
        if len(missing) > 40:
            print(f"  ... and {len(missing) - 40} more")
        print("encode them with: make encode")
        sys.exit(1)
    print(f"media complete: {len(clips)} clips, {len(named)} named segments ({len(need)} files) in {enc}")


if __name__ == "__main__":
    main()
