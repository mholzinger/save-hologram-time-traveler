# Time Traveler 3D

Reverse engineering of *Time Traveler* (Sega / Virtual Image Productions, 1991, laserdisc "hologram" arcade) and a stereoscopic Nintendo 3DS port driven by the recovered scene data.

No ROM, laserdisc or DVD content is distributed in this repository. You need your own program ROM `tt061891.bin` and laserdisc image `timetrv.chd` (MAME set `timetrv`).

## Quick start

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
- *Time Traveler*, Sega and Hologram are trademarks of their owners. This project is not affiliated with
  or endorsed by them, and it contains none of their ROM, laserdisc, DVD or artwork.
