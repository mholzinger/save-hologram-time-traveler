# Tools

Run every tool from the repo root with the venv: `.venv/bin/python -I tools/<tool>.py`. The `make` targets already do this (`PY` in the Makefile). Setup is in the top-level README.

`common.py` holds what the tools share: `ROOT` (the repo) and `load_json`. A tool imports it after adding its own folder to `sys.path`, because `python -I` does not.

## Laserdisc build (`make game`)

| Tool | Does |
|---|---|
| `extract_tables.py` | Program ROM to `data/*.json` and `build/led_messages.json`: the decoded tables, hash-checked (`make tables`) |
| `pack_game.py` | `data/` to `build/timetrv.ttg`, the binary scene graph the engine loads (`make pack`) |
| `encode_ld.py` | Laserdisc CHD to H.264 side-by-side clips and WAV audio in `build/enc_ld` (`make encode`) |
| `check_media.py` | Checks that every clip and named segment the game opens is encoded (`make stage`) |
| `art_convert.py` | Local art to the screens the port loads (`make art`, docs/ART.md) |
| `stage_test_frame.py` | One decoded frame for the Azahar stage test (`TT_STAGE_TEST`; Azahar has no H.264 decoder) |

## Reference model and ROM tests (`make test`)

| Tool | Does |
|---|---|
| `sim.py` | The decoded rules in Python: the reference that `port/3ds/source/engine.c` mirrors function for function; `--check` validates the data |
| `romrun.py` | Runs the real program ROM on Unicorn (80188) with faked I/O and a laserdisc command log |
| `romdiff.py` | Drives the ROM (`--perfect`, `--random`, `--press`, `--watch` ...) and checks every scene change and score against `sim.py` |

## Reverse engineering

| Tool | Does |
|---|---|
| `romled.py` | Compares the ROM's own LED output with the port's message system |
| `chd_frames.py` | Maps CHD frames to laserdisc picture numbers from the VBI |
| `check_labels.py` | Checks Ghidra label addresses against the Capstone function starts (`make check-labels`) |
| `show_episode.py` | Prints decoded episodes and their rules |

## DVD source (for the future CD-system builds, docs/TASKS.md)

| Tool | Does |
|---|---|
| `dvd_intake.py`, `dvd_ifo.py`, `dvd_angle.py` | DVD rip intake: titles, program chains, the 2D/3D angles |
| `dvd_logic.py` | The DVD's own game logic (button highlights, GPRM, cell links) to `data/dvd_logic.json` |
| `fingerprint.py`, `build_ld_to_dvd.py`, `dvd_mains.py` | Places laserdisc clips on the DVD (`data/ld_to_dvd.json`) |
| `encode_clip.py`, `dvd_overlay.py` | DVD footage to clips, removing the DVD's CG overlay |
| `parse_pc_tables.py`, `pc_clips.py`, `match_pc_ld.py`, `match_pc_dvd.py` | The PC version's tables and clips: inputs to `build_ld_to_dvd.py` |
