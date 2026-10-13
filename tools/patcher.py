#!/usr/bin/env python3
"""The patcher: the user's own arcade files in, a 3DS game out (docs/PATCHER.md).

    .venv/bin/python -I tools/patcher.py --rom timetrv.zip --chd timetrv.chd [--out out]

Writes OUT/timetrv3ds.3dsx (Homebrew Launcher) and, when makerom is installed, OUT/timetrv3ds.cia
(install with FBI). Work files go to OUT/work; run it again after an interruption and it carries on
where the encode stopped.

Needs chdman (MAME's tools) and ffmpeg with libx264 on the PATH. The decoded game (data/), the
encoder settings and the code-only app ship with the patcher; nothing from the ROM or the laserdisc
does.
"""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import DATA, ROOT  # noqa: E402
import check_media  # noqa: E402
import encode_ld  # noqa: E402
import extract_tables  # noqa: E402
import pack_game  # noqa: E402
import romfs  # noqa: E402
import smdh  # noqa: E402
import title_art  # noqa: E402

ROM_NAME = "tt061891.bin"  # the program ROM inside MAME's timetrv.zip
CHD_SHA1 = "8abb5e6aa58ab49477ef89f507264d35454f99d3"  # MAME timetrv.cpp:337, as make doctor checks
# The code-only app: shipped next to the patcher (app/), or the developer's own build.
APP_PLACES = [os.path.join(ROOT, "app", "timetrv3ds.3dsx"), os.path.join(ROOT, "port", "3ds", "timetrv3ds.3dsx")]
CIA_DIR = os.path.join(ROOT, "port", "3ds", "cia")  # timetrv.rsf (makerom settings) and banner.bnr
MAKEROM_HELP = "makerom not found: the .cia is skipped (get makerom from github.com/3DSGuy/Project_CTR releases)"


def step(text):
    print(f"\n== {text}", flush=True)


def find_tools_too_in(*dirs):
    """Also look for chdman, ffmpeg and makerom in these folders: next to the patcher (players can drop
    them there) and, on macOS, Homebrew's (an app started from Finder does not get the shell's PATH)."""
    extra = [d for d in dirs if d and os.path.isdir(d)]
    os.environ["PATH"] = os.pathsep.join(extra + [os.environ.get("PATH", "")])


def missing_tools():
    """What is not installed, as lines to show the user (empty when all is there)."""
    out = []
    if not shutil.which("chdman"):
        out.append("chdman not found: install MAME's tools (brew install mame, the MAME Windows package, mame-tools)")
    if not shutil.which("ffmpeg"):
        out.append("ffmpeg not found: install ffmpeg with libx264 (ffmpeg.org, Homebrew, your package manager)")
    else:
        enc = subprocess.run(["ffmpeg", "-hide_banner", "-encoders"], capture_output=True, text=True).stdout
        if "libx264" not in enc:
            out.append("ffmpeg has no libx264 encoder: install a build with libx264")
    return out


def read_rom(path):
    """The program ROM's bytes from MAME's timetrv.zip (or the bare tt061891.bin), SHA-1 checked."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as z:
            names = [n for n in z.namelist() if os.path.basename(n).lower() == ROM_NAME]
            if not names:
                sys.exit(f"{path} has no {ROM_NAME}: it is not MAME's timetrv set")
            rom = z.read(names[0])
    else:
        with open(path, "rb") as f:
            rom = f.read()
    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != extract_tables.EXPECTED_SHA1:
        sys.exit(f"{path}: {ROM_NAME} SHA-1 {sha1}, expected {extract_tables.EXPECTED_SHA1} (MAME set timetrv)")
    return rom


def check_chd(path):
    if not os.path.isfile(path):
        sys.exit(f"{path} not found")
    info = subprocess.run(["chdman", "info", "-i", path], capture_output=True, text=True).stdout
    sha1 = next((line.split()[1] for line in info.splitlines() if line.startswith("SHA1:")), None)
    if sha1 != CHD_SHA1:
        sys.exit(f"{path}: SHA-1 {sha1}, expected {CHD_SHA1} (MAME set timetrv's laserdisc)")


def find_app(arg):
    """The code-only .3dsx (it must not carry a RomFS yet)."""
    for p in [arg] if arg else APP_PLACES:
        if p and os.path.isfile(p):
            with open(p, "rb") as f:
                app = f.read()
            if app[:4] != b"3DSX" or struct.unpack_from("<I", app, 0x28)[0] != 0:
                sys.exit(f"{p} is not the code-only timetrv3ds.3dsx")
            return p, app
    sys.exit("the code-only app (app/timetrv3ds.3dsx) is missing: get it from the release, or run make 3ds")


def with_icon(app, icon):
    """The .3dsx with its SMDH (icon and titles) replaced by ours, so it never carries a local icon."""
    off, size = struct.unpack_from("<II", app, 0x20)
    assert size == len(icon), "unexpected SMDH size in the app"
    return app[:off] + icon + app[off + size :]


def make_cia(app_path, folder, icon_path, out):
    """makerom: the app's .elf (next to the .3dsx), our RSF, icon and banner, and the game folder as RomFS."""
    elf = os.path.splitext(app_path)[0] + ".elf"
    if not os.path.isfile(elf):
        sys.exit(f"{elf} is missing: the .cia is built from the app's .elf")
    cmd = ["makerom", "-f", "cia", "-o", out, "-target", "t", "-exefslogo", "-elf", elf]
    cmd += ["-rsf", os.path.join(CIA_DIR, "timetrv.rsf"), "-icon", icon_path]
    cmd += ["-banner", os.path.join(CIA_DIR, "banner.bnr"), f"-DAPP_ROMFS={os.path.abspath(folder)}"]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.isfile(out):
        sys.exit("makerom failed:\n" + r.stdout + r.stderr)


def link(src, dst):
    """dst as a hard link to src (no copy), or a copy where links are not possible."""
    if os.path.exists(dst):
        os.remove(dst)
    try:
        os.link(src, dst)
    except OSError:
        shutil.copyfile(src, dst)


def game_folder(work, ttg_dir, enc):
    """work/game/timetrv: the .ttg and every clip the game opens, as the app finds them in romfs:/timetrv."""
    clips = os.path.join(ttg_dir, "clips")
    os.makedirs(clips, exist_ok=True)
    for name in os.listdir(enc):
        if name.endswith((".h264", ".wav")):
            link(os.path.join(enc, name), os.path.join(clips, name))
    return os.path.dirname(ttg_dir)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Build the 3DS game from your own MAME timetrv set.")
    ap.add_argument("--rom", required=True, help="MAME's timetrv.zip (or tt061891.bin)")
    ap.add_argument("--chd", required=True, help="the laserdisc image, timetrv.chd")
    ap.add_argument("--out", default="out", help="output folder (default ./out)")
    ap.add_argument("--app", help="the code-only timetrv3ds.3dsx (default: the one shipped with the patcher)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4, help="encodes run at once")
    a = ap.parse_args(argv)
    t0 = time.time()

    step("Checking what you have")
    here = os.path.dirname(os.path.abspath(sys.executable if getattr(sys, "frozen", False) else __file__))
    app_dir = os.path.dirname(os.path.dirname(os.path.dirname(here))) if here.endswith("Contents/MacOS") else None
    find_tools_too_in(here, app_dir, "/opt/homebrew/bin", "/usr/local/bin")
    problems = missing_tools()
    if problems:
        sys.exit("Can't build yet:\n  " + "\n  ".join(problems))
    cia = shutil.which("makerom") is not None
    if not cia:
        print(MAKEROM_HELP)
    rom = read_rom(a.rom)
    check_chd(a.chd)
    app_path, app = find_app(a.app)
    print(f"ROM and laserdisc are MAME's timetrv; app {app_path}")
    work = os.path.join(a.out, "work")
    ttg_dir = os.path.join(work, "game", "timetrv")
    os.makedirs(ttg_dir, exist_ok=True)

    step("Game data (scene graph + LED text from your ROM)")
    led = os.path.join(work, "led_messages.json")
    with open(led, "w") as f:
        json.dump(extract_tables.led_json(rom), f, indent=1)
    pack_game.main([DATA, os.path.join(ttg_dir, "timetrv.ttg"), led])

    step("Footage from your laserdisc (the slow part; resumes if stopped)")
    enc = os.path.join(work, "enc")
    encode_ld.main(
        ["--all", "--extra", "--missing", "--out", enc, "--chd", a.chd, "--tmp", os.path.join(work, "tmp")]
        + ["--jobs", str(a.jobs)]
    )
    check_media.main([enc])

    step("Title and menu screens (your laserdisc's pictures, our lettering)")
    title_art.main([enc, os.path.join(ttg_dir, "art")])

    step("Writing the game")
    folder = game_folder(work, ttg_dir, enc)
    icon = smdh.build(title_art.icon())
    icon_path = os.path.join(work, "icon.smdh")
    with open(icon_path, "wb") as f:
        f.write(icon)
    out = os.path.join(a.out, "timetrv3ds.3dsx")
    with open(out, "wb") as f:
        f.write(romfs.attach(with_icon(app, icon), romfs.build(folder)))
    print(
        f"{out}: {os.path.getsize(out) / 1e6:.0f} MB  (copy to /3ds/ on the SD card, start from the Homebrew Launcher)"
    )
    if cia:
        out = os.path.join(a.out, "timetrv3ds.cia")
        make_cia(app_path, folder, icon_path, out)
        print(f"{out}: {os.path.getsize(out) / 1e6:.0f} MB  (install with FBI)")
    print(f"Done in {(time.time() - t0) / 60:.0f} min.")


if __name__ == "__main__":
    main()
