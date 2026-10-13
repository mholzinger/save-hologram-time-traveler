#!/usr/bin/env python3
"""Lossless DVD intake: every PGC of every title set -> video/dvd/*.mkv (+ chapters).

    python3 -I tools/dvd_intake.py /Volumes/DVDVolume/VIDEO_TS video/dvd

PGCs with an angle block are written once per angle (`_a1`, `_a2`); others once.
PGCs shorter than --min-seconds (menu stubs, 0.4 s still cells) are skipped.
Chapters = PGC programs, timed by summing the selected cells' IFO playback times.
Writes OUT/intake.json (the IFO dump plus the files produced).
"""

import argparse
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import dvd_angle  # noqa: E402
import dvd_ifo  # noqa: E402


def chapters_txt(pgc, angle):
    """OGM simple chapter file from program starts (program_map gives 1-based cell index)."""
    starts, t = {}, 0.0
    sel = {id(c) for c, _ in dvd_angle.select_cells(pgc, angle)}
    for i, c in enumerate(pgc["cells"], 1):
        if i in pgc["program_map"]:
            starts[i] = t
        if id(c) in sel:
            t += c["time"]["seconds"]
    lines = []
    for n, i in enumerate(sorted(starts), 1):
        s = starts[i]
        lines.append("CHAPTER%02d=%02d:%02d:%06.3f" % (n, s // 3600, s % 3600 // 60, s % 60))
        lines.append("CHAPTER%02dNAME=Program %d" % (n, n))
    return "\n".join(lines) + "\n", t


def has_angles(pgc):
    return any((int(c["category"], 16) >> 28) & 3 == 1 for c in pgc["cells"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("video_ts")
    ap.add_argument("out")
    ap.add_argument("--tmp", default="build/dvd/tmp")
    ap.add_argument("--min-seconds", type=float, default=1.0)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    os.makedirs(a.tmp, exist_ok=True)

    info = {"vmg": dvd_ifo.read_vmg(os.path.join(a.video_ts, "VIDEO_TS.IFO")), "vts": {}}
    for k in range(1, info["vmg"]["nr_vts"] + 1):
        info["vts"]["%02d" % k] = dvd_ifo.read_vts(os.path.join(a.video_ts, "VTS_%02d_0.IFO" % k))

    produced = []
    for vk, vts in info["vts"].items():
        vobs = dvd_angle.Vobs(a.video_ts, int(vk))
        for pi, pgc in enumerate(vts["pgcs"], 1):
            if pgc["time"]["seconds"] < a.min_seconds:
                continue
            angles = [1, 2] if has_angles(pgc) else [1]
            for ang in angles:
                tag = "vts%s_pgc%02d%s" % (vk, pi, "_a%d" % ang if len(angles) > 1 else "")
                vob = os.path.join(a.tmp, tag + ".vob")
                chap = os.path.join(a.tmp, tag + ".chapters.txt")
                mkv = os.path.join(a.out, tag + ".mkv")
                txt, secs = chapters_txt(pgc, ang)
                open(chap, "w").write(txt)
                with open(vob, "wb") as f:
                    for c, inter in dvd_angle.select_cells(pgc, ang):
                        for lbn, n in dvd_angle.walk_cell(vobs, c["first_sector"], c["last_sector"], inter):
                            f.write(vobs.read(lbn, n))
                # Tag languages from the IFO audio attributes: AC-3 sub-stream 0x80+i = attr i.
                lang_args = []
                ident = json.loads(subprocess.check_output(["mkvmerge", "-J", vob]))
                for t in ident["tracks"]:
                    ss = t["properties"].get("sub_stream_id")
                    if t["type"] == "audio" and ss is not None and 0 <= ss - 0x80 < len(vts["audio"]):
                        lang = vts["audio"][ss - 0x80]["lang"]
                        if lang:
                            lang_args += ["--language", "%d:%s" % (t["id"], lang)]
                        lang_args += ["--track-name", "%d:DVD audio stream 0x%02x" % (t["id"], ss)]
                r = subprocess.run(["mkvmerge", "-q", "-o", mkv, "--chapters", chap] + lang_args + [vob])
                if r.returncode > 1:  # 1 = warnings
                    sys.exit("mkvmerge failed on %s" % vob)
                os.remove(vob)
                produced.append(
                    {
                        "file": os.path.basename(mkv),
                        "vts": int(vk),
                        "pgc": pi,
                        "angle": ang if len(angles) > 1 else None,
                        "ifo_seconds": round(secs, 3),
                        "programs": pgc["programs"],
                    }
                )
                print("%-24s %8.1fs %3d ch" % (tag, secs, pgc["programs"]), flush=True)
    info["produced"] = produced
    json.dump(info, open(os.path.join(a.out, "intake.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
