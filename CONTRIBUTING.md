# Contributing

Read `docs/ARCHITECTURE.md` first: it shows where each piece lives and how the checks fit together.

## Setup

What you need depends on what you work on:

| Work | Needs |
|---|---|
| Engine, `sim.py`, tools, `make test` | Python 3.13 venv, a C compiler, the program ROM `tt061891.bin` |
| Reverse engineering | The above, plus Ghidra 11.2.1 with JDK 21 (`make ghidra-import`) |
| The 3DS build | The above, plus devkitPro `3ds-dev` (`DEVKITARM` set) |
| The full game with footage | The above, plus the laserdisc image `rom/timetrv.chd`, `chdman` and `ffmpeg` with libx264 |

The ROM and the laserdisc image are not in this repository and never will be: supply your own (MAME set
`timetrv`). `make rom` unzips `../timetrv.zip` and checks its SHA-1; `make doctor` checks everything
`make game` needs.

Settings for your machine go in `local.mk` (not in git), for example:

```
DS_HOST := 192.168.1.20:5000                      # the 3DS running ftpd, for make deploy / run / log
export GHIDRA_INSTALL_DIR := $(HOME)/tools/ghidra/current
```

```
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
make rom        # rom/tt061891.bin from ../timetrv.zip, SHA-1 checked
make test       # the checks below; no 3DS needed
make 3ds        # the .3dsx (devkitPro)
make game       # everything, footage included, into one .3dsx
```

## Before every commit

1. `make format` (clang-format for C, ruff for Python; the versions are pinned in `requirements.txt`).
2. `make test`, which must pass:
   - `sim.py --check`,
   - C/Python parity (900 games),
   - the host golden outputs (front end, 2 players, LED),
   - the ROM differential tests.
3. `make 3ds` with no warnings, if you changed `port/3ds/`.

If a change is meant to alter the front end's or the LED's output, run
`make -C port/3ds/host golden-update` and review the diff of `port/3ds/host/expected/` in the commit.

## Rules

1. **No copyrighted media in git.** ROM, laserdisc, DVD, manual scans, extracted frames and the ROM's own
   text (LED messages) stay in ignored paths (`rom/`, `video/`, `build/`, `docs/reference/`).
2. **No guessing about the arcade.** A hardware or game fact needs a source: a ROM address, a MAME file and
   line, or a datasheet. Without one, write `UNVERIFIED`.
3. **Tables come from code.** A table decoded from the ROM gets a reader in `tools/extract_tables.py`, not a
   hand-copied JSON.
4. **Engine and model move together.** A rule change goes into `port/3ds/source/engine.c` and
   `tools/sim.py` in the same commit, with the same function names; parity proves they agree.
5. **Keep refactors and fixes apart.** A readability commit changes no behaviour (`make test` shows it). A
   bug found on the way gets its own commit, or a line in `docs/TASKS.md`.
6. Addresses are `SEG:OFF` (`D37E:03AB`). Comments say what the ROM does and where; history goes in git.

## Where things are

- `docs/TASKS.md`: the backlog.
- `docs/ROM_FINDINGS.md`: what is known about the ROM, with addresses.
- `docs/GAME_RULES.md`: the rules as the manual states them.
- `docs/PORT_PLAN.md`: the 3DS design.
- `tools/README.md`: what each tool does.
