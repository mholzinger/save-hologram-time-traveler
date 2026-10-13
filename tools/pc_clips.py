#!/usr/bin/env python3
"""Inventory the PC version's AVIs with ffprobe -> data/pc_clips.csv (numbers only, no media).

python3 -I tools/pc_clips.py video/HTT/tt data
"""

import csv
import json
import os
import re
import subprocess
import sys


def probe(path):
    j = json.loads(
        subprocess.check_output(
            [
                "ffprobe",
                "-v",
                "error",
                "-count_packets",
                "-show_entries",
                "stream=codec_type,codec_name,codec_tag_string,width,height,r_frame_rate,nb_read_packets,sample_rate,channels"
                ":format=duration",
                "-of",
                "json",
                path,
            ]
        )
    )
    v = next((s for s in j["streams"] if s["codec_type"] == "video"), {})
    a = next((s for s in j["streams"] if s["codec_type"] == "audio"), {})
    num, den = (int(x) for x in v.get("r_frame_rate", "0/1").split("/"))
    return {
        "frames": int(v.get("nb_read_packets", 0)),
        "fps": round(num / den, 3) if den else 0,
        "width": v.get("width"),
        "height": v.get("height"),
        "vcodec": v.get("codec_tag_string"),
        "duration": round(float(j["format"].get("duration", 0)), 3),
        "acodec": a.get("codec_name"),
        "rate": a.get("sample_rate"),
        "ch": a.get("channels"),
    }


def main():
    src, out = sys.argv[1], sys.argv[2]
    rows = []
    for n in sorted(os.listdir(src), key=str.lower):
        if not n.lower().endswith(".avi"):
            continue
        m = re.fullmatch(r"htt2(\d\d)(?:(d|p)(\d))?\.avi", n, re.I)
        kind, scene, k = ("other", None, None)
        if m:
            scene = int(m.group(1))
            kind = {"d": "death", "p": "branch", None: "main"}[m.group(2).lower() if m.group(2) else None]
            k = int(m.group(3)) if m.group(3) else None
        rows.append({"file": n, "kind": kind, "scene": scene, "k": k, **probe(os.path.join(src, n))})
    with open(os.path.join(out, "pc_clips.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0]))
        w.writeheader()
        w.writerows(rows)
    print(f"{len(rows)} AVIs -> {out}/pc_clips.csv")


if __name__ == "__main__":
    main()
