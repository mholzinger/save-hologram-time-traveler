# Time Traveler 3D

Reverse engineering of *Time Traveler* (Sega / Virtual Image Productions, 1991, laserdisc "hologram" arcade) and a stereoscopic Nintendo 3DS port driven by the recovered scene data.

No ROM, laserdisc or DVD content is distributed in this repository. You need your own program ROM `tt061891.bin` and laserdisc image `timetrv.chd` (MAME set `timetrv`).

## Play it: bring the ingredients, the patcher builds the game

The reverse engineering is done. Every scene, input window, rule and laserdisc frame offset is already
decoded and ships with the patcher. You only supply the arcade files; the patcher turns them into a 3DS
game on your machine.

You can't build the game unless you have all of these:

| Ingredient | Why | Get it |
|---|---|---|
| `timetrv.zip` (program ROM `tt061891.bin`) and `timetrv.chd` (laserdisc) | the game itself | your own MAME set `timetrv`; never included here |
| `chdman` | reads the laserdisc image | MAME's tools (`brew install mame`, the MAME Windows package, `mame-tools` on Linux) |
| `ffmpeg` with libx264 | encodes the footage for the 3DS | ffmpeg.org, Homebrew, your Linux package manager |
| `makerom` | only for the `.cia` | github.com/3DSGuy/Project_CTR releases |

```
timetrv-patcher --rom timetrv.zip --chd timetrv.chd --out out
```

Output: `out/timetrv3ds.3dsx` for the Homebrew Launcher (copy it to `/3ds/` on the SD card), and soon
`timetrv3ds.cia` (install with FBI). No compiler, Ghidra or devkitPro needed. It checks your files and
tools first and says plainly what is missing. The footage encode is the slow part; stop it any time and
run it again to carry on.

Status: the `.3dsx` patcher works today as `tools/patcher.py` (Python 3.13 with `requirements.txt`, or
`make patch` in a developer setup). The `.cia` and a download that needs no Python are next
(`docs/PATCHER.md`).

## Work on it: developer setup

Python 3.13 with `requirements.txt` and devkitPro `3ds-dev`, plus the ingredients above. Ghidra only if
you dig into the ROM itself. `make doctor` checks all of it and names what is missing.

### Quick start

```
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
make rom        # rom/tt061891.bin from ../timetrv.zip, SHA-1 checked
make test       # every check that needs no 3DS
make doctor     # what make game still needs (devkitPro, chdman, ffmpeg, rom/timetrv.chd)
make game       # the whole game as one .3dsx in build/release/
```

`make help` lists every target. Which targets need what is in `CONTRIBUTING.md`.

## Read next

- `docs/ARCHITECTURE.md`: how the pieces fit, from ROM to 3DS.
- `CONTRIBUTING.md`: setup, the checks to run, the rules.
- `docs/ROM_FINDINGS.md`, `docs/TASKS.md`: the ROM facts and the backlog.

## License

BSD 3-Clause, see `LICENSE`. It covers this repository's code and data only.

- `reference/mame/`: files from MAME, under their own headers (BSD-3-Clause; `timetrv.lay` CC0-1.0).
  `port/3ds/source/led.c` uses the LED glyph table from MAME's `timetrv.cpp` (BSD-3-Clause).
- `tools/fonts/`: Mr Dafoe (Alejandro Paul) for the title and Michroma (Vernon Adams) for the tagline, both
  SIL Open Font License 1.1 (`tools/fonts/OFL-*.txt`).
- *Time Traveler*, Sega and Hologram are trademarks of their owners. This project is not affiliated with
  or endorsed by them, and it contains none of their ROM, laserdisc, DVD or artwork.
