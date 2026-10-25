# Time Traveler → Nintendo 3DS

Reverse engineer the 1991 Sega / Virtual Image Productions laserdisc arcade game *Time Traveler* from its 80188 program ROM, and rebuild it on the 3DS as a data-driven FMV player. The 3DS autostereo display stands in for the cabinet's "hologram" (a CRT reflected in a concave mirror).

The port is **not** a CPU emulator. The ROM is mined for its scene graph (clip frame ranges, input windows, branch/death/success targets, scoring, cube/devil rules). That data drives a native 3DS player that plays the original laserdisc footage (from the CHD; the 2001 Digital Leisure DVD was the earlier source).

## Sources
- **Laserdisc CHD** (`rom/timetrv.chd`, supplied by the maintainer 2026-10-07; header and data SHA1 match MAME `timetrv.cpp:337`): the arcade footage itself, actors on black, stored upside down for the cabinet mirror. **Video source of the port since 2026-10-07** (`tools/encode_ld.py`). VBI picture p = CHD frame p − 1 (`tools/chd_frames.py`), so ROM frame numbers need no mapping.
- **Arcade program ROM** (`rom/tt061891.bin`): authoritative game logic.
- **Windows PC version** (`video/HTT/`): same footage pre-cut into per-scene Indeo AVIs (320×240, 15 fps), plus human-readable input tables (`httNN.txt`). Format notes in `video/README.md`. Use it as a cross-check and prototyping source. None of it ships.
- **2001 Digital Leisure DVD** (`video/dvd/Hologram Time Traveller.iso`): MPEG-2 720×480i. 2D and red/cyan 3D are angles 1/2 of the same PGCs. The 3D angle is two flat layers (actors at 0 px, tunnel background ~24 px), not real stereo. It composites the LD footage at 0.90× over added CG worlds. Former video source (`tools/encode_clip.py`, `ENC=build/enc`). See `video/README.md`.
- **DVD manual** (`docs/reference/`, local only): rules and controls, summarized with page cites in `docs/GAME_RULES.md`.

## Read first
- `docs/ARCHITECTURE.md` and `CONTRIBUTING.md`: how the code fits together, setup, and the checks every commit passes.
- `docs/PORT_PLAN.md`: 3DS toolchain, target hardware (New 3DS), architecture, "mock hologram" design, milestones M0–M6. Red Viper is the main code reference.
- `docs/GAME_RULES.md`: cited rules (lives, cubes, Time Map, Trader, Hellgate, move types).
- `docs/ROM_FINDINGS.md`: hardware facts, ROM layout, decoded functions, extracted tables. Treat it as the source of truth and update it as you learn.
- `docs/TASKS.md`: phased backlog. Work top-down and tick items off there.
- `reference/mame/timetrv.cpp`, `reference/mame/ldv4200hle.*`: MAME driver and Pioneer LD-V4200 command parser (BSD-3, pinned to mame `0fed553720ab`).

## Hard rules
1. **No guessing on hardware.** Pinouts, port meanings, protocol bytes and timings must come from a cited source (MAME file + line, datasheet) or from ROM code at a stated address. Otherwise write UNVERIFIED and stop rather than infer.
2. **Copyrighted inputs never enter git.** ROM, CHD, DVD rips and extracted frames stay in ignored paths (`rom/`, `video/source/`, `video/frames/`). See `.gitignore`. Do not download media yourself; the maintainer supplies it.
3. Addresses are written as real-mode `SEG:OFF` (e.g. `D37E:03AB`), with the linear address alongside when it helps (`D3B8B`). Linear = SEG×16 + OFF. ROM file offset = linear − 0xC0000.
4. Extraction must be reproducible. Any table you decode gets a reader in `tools/extract_tables.py` (hash-checked), not a hand-copied JSON.
5. Names in Ghidra go in `re/ghidra/scripts/TimeTravelerLabels.java` so the project can be rebuilt headless. Prefix speculative names `maybe_`.

## Layout
```
CLAUDE.md               this file
Makefile                entry point for everything below
rom/                    tt061891.bin unpacked here by `make rom` (ignored)
re/capstone/trace.py    standalone recursive-descent disassembler (pass 1)
re/ghidra/ghidra.sh     headless import / labels / decomp export
re/ghidra/scripts/      TimeTravelerLabels.java, ExportDecomp.java
tools/extract_tables.py ROM -> data/*.json|csv
data/                   extracted game tables (checked in; derived facts, no media)
docs/                   ROM_FINDINGS.md, TASKS.md
reference/mame/         upstream driver source
video/                  video sources: laserdisc CHD notes, the DVD mapping (future CD builds); media ignored
port/3ds/               the 3DS game: portable engine (engine.c) + 3DS front end, host tests in host/
build/                  all generated output (ignored)
```

## Commands
```
make rom            # unzip ../timetrv.zip -> rom/, verify SHA1
make disasm         # Capstone listing -> build/capstone/
make tables         # regenerate data/ from ROM
make ghidra-import  # headless import + auto-analysis + labels -> build/ghidra/TimeTraveler.gpr
make ghidra-labels  # re-apply labels after editing TimeTravelerLabels.java
make ghidra-decomp  # decompiled C per function -> build/ghidra/decomp/ (+ INDEX.tsv)
make ghidra-gui     # open the project interactively
make 3ds            # needs DEVKITARM
make deploy         # build + stage + FTP changed files to the 3DS (ftpd)
make run            # build + launch the .3dsx via 3dslink (Homebrew Launcher, press Y; ftpd closed)
make test           # sim --check, C/Python parity, host golden outputs, ROM differential (no 3DS)
make game           # everything from rom/timetrv.chd + ../timetrv.zip: doctor, tables, encode, stage, release
make doctor         # check tools (chdman, ffmpeg/libx264, venv, devkitARM) and inputs (CHD SHA1)
make encode         # encode what is missing from the CHD into build/enc_ld
make release        # one-file test build: build/release/timetrv3ds.3dsx, all data in RomFS (never shared)
make art            # local art for splash/menu/icon (docs/ART.md; never committed)
```
Mac tools: hdiutil, MKVToolNix, ffmpeg, aria2, chdman. Ghidra: the shared install, run by `re/ghidra/ghidra.sh` through `ghidra-run` (`~/tools/ghidra/AGENTS.md`); the project is pinned to 11.2.1 in `build/ghidra/.ghidra-version`. Python deps: `.venv/bin/pip install -r requirements.txt`.

## Key facts (details and citations in ROM_FINDINGS.md)
- CPU Intel 80188 @ 16 MHz; ROM 256 KB at `C0000–FFFFF`; reset `F000:FFF0` → `C000:0000`.
- Microsoft C, large model. Source modules from embedded `c:\mt\src\*.c`: attract, chorder, episode, asset, input, specobj, clock, sound, storage, pioneer, display, learn, chapter, map, account.
- All port I/O is via the C runtime `inp` (`DB9B:014A`) and `outp` (`DB9B:0158`). Search for pushed port constants, not `in`/`out` opcodes.
- Laserdisc: UART at I/O `0x1100`, 4800 8N1. `D37E:03AB ld_search_play_to(start, stop)` is the core playback call. `g[0x151A]==1` → Sony protocol, else Pioneer.
- Master clip table `CC1A:0000`: 348 × `{type, id, start_frame, stop_frame}` → `data/clips.json`.
- DGROUP image at ROM `DBE1:0000` is copied to RAM `01EB:0000` at boot. DS-relative pointers in code resolve against that.

## Hardware
- Test device: the maintainer's **New 3DS XL** (has MVD H.264 decoder and stereo screen). Has custom firmware: `.3dsx` builds can be side-loaded and run from Homebrew Launcher. Deploy over Wi-Fi: open ftpd on the 3DS, then `make deploy` (files changed on that SD card only; the card carries an id in `timetrv/.card-id`, so a swapped card gets everything) or `make deploy-app` (.3dsx only). Set the console's `DS_HOST=ip:port` in `local.mk`, or per run: `make deploy DS_HOST=ip:port`.

## Status
- Pass 1 done with Capstone.
- Ghidra import verified 2026-10-06 (Ghidra 11.2.1, JDK 21 via `JAVA_HOME_OVERRIDE=/opt/homebrew/opt/openjdk@21` in Ghidra's `support/launch.properties`; system `java` is 1.8 and is not used).
  - `-loader-baseAddr c000:0000` is accepted by BinaryLoader (log: `Successfully applied "-loader-baseAddr" ... new: "c000:0000"`). `TT_BASEADDR=0xC0000` fallback not needed.
  - Memory block `c000:0000 - f000:ffff`. `C000:0000` disassembles as `CLI; CLD; XOR DX,DX; MOV DX,0xffa2; MOV AX,0xffc; OUT DX,AX ...`.
  - Labels: `TimeTravelerLabels: applied 31, failed 0`.
  - Ghidra normalizes seg:off to segment `d000`/`c000` etc. in listings and decomp filenames: `D37E:03AB` shows as `d000:3b8b` (same linear `D3B8B`). Map back with linear = SEG×16 + OFF.
  - `make ghidra-decomp`: 407 functions, 404 ok, 3 failed (`d000:0556`, `d000:94f5`, `d000:9529`; AddressOutOfBounds in decompiler symbol mapping). `ld_search_play_to` decompiles.
- Ghidra 12.2 verified 2026-10-24 on copies of the project: the three Java scripts run unchanged (Java needs no PyGhidra). Fresh import gives `TimeTravelerLabels: applied 112, failed 0` and `TimeTravelerModules: renamed 307, kept 100`, same as 11.2.1. Decomp is 404 ok, 3 failed (same three) and `INDEX.tsv` is byte-identical to 11.2.1, so `tools/check_labels.py` is unaffected. 102 of 404 C bodies differ: 66 only in type spelling and variable numbering, 36 with reordered branches or `CONCAT` forms of the same logic. 12.2 needs JDK 25 and self-built macOS natives (both handled by `ghidra-update`). Opening `build/ghidra/TimeTraveler.gpr` with 12.2 upgrades it one way.
- Python: use the repo venv. `python3 -m venv .venv && .venv/bin/pip install -r requirements.txt`; the Makefile uses it automatically (`PY`).
