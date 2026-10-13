#!/usr/bin/env python3
"""The patcher in a window: pick the ROM zip, the CHD and an output folder, press Build.

    python tools/patcher_gui.py          (a developer setup; players get the one-file download)
    timetrv-patcher --selftest           (the release build's smoke test: resources, GUI, then exit)
    timetrv-patcher --rom ... --chd ...  (no window: the command-line patcher, tools/patcher.py)

The window runs tools/patcher.py's main() on a worker thread and shows its output; the encode's per-clip
lines drive the progress bar. Everything the patcher prints goes to the log, and a failure (the patcher
exits with a message) is shown in red with that message.
"""

import io
import json
import os
import queue
import sys
import threading
import tkinter as tk
from tkinter import filedialog, ttk

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import patcher  # noqa: E402
from common import ROOT  # noqa: E402

TITLE = "Time Traveler 3DS Patcher"
ENCODES = 429  # clips and segments the game opens (check_media.expected_frames)

if sys.platform == "win32":  # no console window flashing up for every chdman / ffmpeg call
    import subprocess

    _popen_init = subprocess.Popen.__init__

    def _quiet_init(self, *a, **kw):
        kw.setdefault("creationflags", 0x08000000)  # CREATE_NO_WINDOW
        _popen_init(self, *a, **kw)

    subprocess.Popen.__init__ = _quiet_init


class Lines(io.TextIOBase):
    """A stdout that hands each complete line to a queue (the GUI thread reads it)."""

    def __init__(self, q):
        self.q, self.buf = q, ""

    def write(self, s):
        self.buf += s
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            self.q.put(("line", line))
        return len(s)


class App:
    def __init__(self, root):
        self.root, self.q, self.worker, self.done = root, queue.Queue(), None, 0
        root.title(TITLE)
        root.minsize(640, 420)
        f = ttk.Frame(root, padding=12)
        f.pack(fill="both", expand=True)
        f.columnconfigure(1, weight=1)
        self.rom, self.chd = tk.StringVar(), tk.StringVar()
        self.out = tk.StringVar(value=os.path.join(os.path.expanduser("~"), "TimeTraveler3DS"))
        rows = [
            ("ROM (timetrv.zip)", self.rom, self.pick_rom),
            ("Laserdisc (timetrv.chd)", self.chd, self.pick_chd),
            ("Output folder", self.out, self.pick_out),
        ]
        for r, (label, var, pick) in enumerate(rows):
            ttk.Label(f, text=label).grid(row=r, column=0, sticky="w", pady=3)
            ttk.Entry(f, textvariable=var).grid(row=r, column=1, sticky="ew", padx=6)
            ttk.Button(f, text="Choose...", command=pick).grid(row=r, column=2)
        self.build = ttk.Button(f, text="Build the game", command=self.start)
        self.build.grid(row=3, column=0, columnspan=3, pady=(10, 6), sticky="ew")
        self.bar = ttk.Progressbar(f, maximum=ENCODES)
        self.bar.grid(row=4, column=0, columnspan=3, sticky="ew")
        self.status = ttk.Label(f, text="Pick your files, then Build. Your files never leave this computer.")
        self.status.grid(row=5, column=0, columnspan=3, sticky="w", pady=4)
        self.log = tk.Text(f, height=14, wrap="word", state="disabled")
        self.log.grid(row=6, column=0, columnspan=3, sticky="nsew")
        self.log.tag_configure("err", foreground="#c0392b")
        self.log.tag_configure("ok", foreground="#1e8449")
        f.rowconfigure(6, weight=1)
        root.after(100, self.poll)

    def pick_rom(self):
        p = filedialog.askopenfilename(title="MAME's timetrv.zip", filetypes=[("ROM zip", "*.zip"), ("All", "*")])
        if p:
            self.rom.set(p)

    def pick_chd(self):
        p = filedialog.askopenfilename(title="timetrv.chd", filetypes=[("Laserdisc image", "*.chd"), ("All", "*")])
        if p:
            self.chd.set(p)

    def pick_out(self):
        p = filedialog.askdirectory(title="Where the game goes")
        if p:
            self.out.set(p)

    def say(self, text, tag=None):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n", tag)
        self.log.see("end")
        self.log.configure(state="disabled")

    def start(self):
        if not (self.rom.get() and self.chd.get()):
            self.status.configure(text="Choose the ROM zip and the CHD first.")
            return
        self.build.state(["disabled"])
        self.done = 0
        self.bar.configure(value=0)
        argv = ["--rom", self.rom.get(), "--chd", self.chd.get(), "--out", self.out.get()]
        self.worker = threading.Thread(target=self.run, args=(argv,), daemon=True)
        self.worker.start()

    def run(self, argv):
        out = Lines(self.q)
        sys.stdout = sys.stderr = out
        try:
            patcher.main(argv)
            self.q.put(("end", None))
        except SystemExit as e:  # the patcher's plain-language failures
            self.q.put(("fail", str(e.code) if e.code not in (None, 0) else None))
        except Exception as e:  # a bug: show it rather than dying silently
            self.q.put(("fail", f"{type(e).__name__}: {e}"))
        finally:
            sys.stdout, sys.stderr = sys.__stdout__, sys.__stderr__

    def poll(self):
        try:
            while True:
                kind, val = self.q.get_nowait()
                if kind == "line":
                    if val.startswith("{"):  # one encode finished (encode_ld prints a JSON line per clip)
                        self.done += 1
                        self.bar.configure(value=self.done)
                        self.status.configure(text=f"Encoding the footage: {self.done} of {ENCODES}")
                        try:
                            json.loads(val)
                        except ValueError:
                            self.say(val)
                    elif val.strip():
                        self.say(val, "ok" if val.endswith(("(install with FBI)", "Homebrew Launcher)")) else None)
                        if val.startswith("== "):
                            self.status.configure(text=val[3:])
                elif kind == "end":
                    self.bar.configure(value=ENCODES)
                    self.status.configure(text=f"Done. The game is in {self.out.get()}")
                    self.build.state(["!disabled"])
                elif kind == "fail":
                    if val:
                        self.say(val, "err")
                    self.status.configure(text="Stopped: see the red lines above.")
                    self.build.state(["!disabled"])
        except queue.Empty:
            pass
        self.root.after(100, self.poll)


def selftest():
    """The release build's check: the shipped files are there and a window opens."""
    need = ["data/game.json", "tools/fonts/MrDafoe-Regular.ttf", "port/3ds/cia/timetrv.rsf", "app/timetrv3ds.3dsx"]
    missing = [p for p in need if not os.path.isfile(os.path.join(ROOT, p))]
    root = tk.Tk()
    App(root)
    root.update()
    root.destroy()
    print("selftest:", "missing " + ", ".join(missing) if missing else "ok")
    sys.exit(1 if missing else 0)


def console():
    """A windowed Windows .exe has no stdout: borrow the terminal it was started from, if any, so --selftest
    and the command-line mode can print (intv2convert does the same in C++)."""
    if sys.platform == "win32" and sys.stdout is None:
        import ctypes

        if ctypes.windll.kernel32.AttachConsole(-1):  # ATTACH_PARENT_PROCESS
            sys.stdout = sys.stderr = open("CONOUT$", "w")
    if sys.stdout is None:  # started without a terminal: nowhere to print
        sys.stdout = sys.stderr = open(os.devnull, "w")


def main():
    if len(sys.argv) > 1:
        console()
    if "--selftest" in sys.argv:
        selftest()
    if "--rom" in sys.argv:
        patcher.main(sys.argv[1:])
        return
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
