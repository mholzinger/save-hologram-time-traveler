#!/usr/bin/env python3
"""Parse the PC version's input tables (video/HTT/tt/httNN.txt, *.hot) -> data/pc_scenes.json.

    python3 -I tools/parse_pc_tables.py video/HTT/tt data

Row format (video/README.md): `<a> <frame> <b> <c1> <c2> <c3> <c4> <c5> <c6>`.
Outcome tokens: `00` accepted, `dK` this scene's death clip K, `NNdK` death clip K of scene NN,
`p1` branch to the scene's P1 clip. Any other token is an error. Column meanings are UNVERIFIED
(see video/README.md); this tool only records what is in the files.
The input files are third-party data and stay out of git; the derived JSON holds numbers only.
"""

import json
import os
import re
import sys

TOKEN = re.compile(r"^(00|p1|d\d|\d{2}d\d)$", re.I)


def outcome(tok, scene):
    t = tok.lower()
    if t == "00":
        return {"ok": True}
    if t == "p1":
        return {"branch": "p1"}
    m = re.fullmatch(r"(\d{2})?d(\d)", t)
    return {"death": int(m.group(2)), "scene": int(m.group(1)) if m.group(1) else scene}


def main():
    src, out = sys.argv[1], sys.argv[2]
    names = sorted(os.listdir(src), key=str.lower)
    avis = {n.lower() for n in names if n.lower().endswith(".avi")}
    scenes, problems = {}, []
    for n in names:
        m = re.fullmatch(r"htt(\d+)\.txt", n, re.I)
        if not m:
            continue
        scene = int(m.group(1))
        rows = []
        for ln, line in enumerate(open(os.path.join(src, n), encoding="latin-1"), 1):
            f = line.split()
            if not f:
                continue
            if len(f) != 9:
                sys.exit(f"{n}:{ln}: expected 9 fields, got {len(f)}: {line.rstrip()}")
            a, frame, b = (int(x) for x in f[:3])
            for t in f[3:]:
                if not TOKEN.match(t):
                    sys.exit(f"{n}:{ln}: unknown outcome token {t!r}")
            outs = [outcome(t, scene) for t in f[3:]]
            rows.append({"a": a, "frame": frame, "b": b, "c": outs})
            for o in outs:
                if "death" in o:
                    clip = f"htt2{o['scene']:02d}d{o['death']}.avi"
                    if clip not in avis:
                        problems.append(f"{n}: refers to missing {clip}")
                if o.get("branch") == "p1" and f"htt2{scene:02d}p1.avi" not in avis:
                    problems.append(f"{n}:{ln}: p1 but no htt2{scene:02d}p1.avi")
        main_clip = f"htt2{scene:02d}.avi"
        scenes[str(scene)] = {"file": n, "main_clip": main_clip if main_clip in avis else None, "rows": rows}
    hot = {}
    for n in names:
        if n.lower().endswith(".hot"):
            hot[n] = [int(x) for x in open(os.path.join(src, n)).read().split()]
    problems = sorted(set(problems))
    json.dump(
        {
            "source": "video/HTT/tt/httNN.txt + *.hot (PC version), parsed by tools/parse_pc_tables.py",
            "scenes": scenes,
            "hot": hot,
            "problems": problems,
        },
        open(os.path.join(out, "pc_scenes.json"), "w"),
        indent=1,
    )
    print(
        f"{len(scenes)} scene tables, {sum(len(s['rows']) for s in scenes.values())} rows, "
        f"{len(hot)} .hot files, {len(problems)} problems"
    )
    for p in problems:
        print("  ", p)


if __name__ == "__main__":
    main()
