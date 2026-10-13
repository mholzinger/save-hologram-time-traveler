#!/usr/bin/env python3
"""Build the one-file patcher (PyInstaller) for this OS: dist/timetrv-patcher(.exe / .app).

    python tools/build_patcher.py APP_DIR      (APP_DIR holds the code-only timetrv3ds.3dsx and .elf)

The same command runs on the GitHub runners (.github/workflows/release.yml) and locally. It bundles
Python, Tk, numpy and Pillow with the shipped files (data/, tools/fonts/, port/3ds/cia/, the app), so
players install nothing but chdman, ffmpeg and makerom.
"""

import os
import sys

import PyInstaller.__main__

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def main():
    app = os.path.abspath(sys.argv[1])
    for f in ("timetrv3ds.3dsx", "timetrv3ds.elf"):
        if not os.path.isfile(os.path.join(app, f)):
            sys.exit(f"{app} has no {f}: build the app first (make 3ds)")

    def data(src, dest):  # PyInstaller's SRC<sep>DEST, with ';' on Windows and ':' elsewhere
        return ["--add-data", f"{src}{os.pathsep}{dest}"]

    args = ["--noconfirm", "--clean", "--onefile", "--windowed", "--name", "timetrv-patcher"]
    args += ["--distpath", os.path.join(ROOT, "dist"), "--workpath", os.path.join(ROOT, "build", "pyinstaller")]
    args += ["--specpath", os.path.join(ROOT, "build", "pyinstaller"), "--paths", os.path.join(ROOT, "tools")]
    args += data(os.path.join(ROOT, "data"), "data")
    args += data(os.path.join(ROOT, "tools", "fonts"), os.path.join("tools", "fonts"))
    args += data(os.path.join(ROOT, "port", "3ds", "cia"), os.path.join("port", "3ds", "cia"))
    args += data(app, "app")
    PyInstaller.__main__.run(args + [os.path.join(ROOT, "tools", "patcher_gui.py")])


if __name__ == "__main__":
    main()
