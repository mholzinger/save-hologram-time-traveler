# Patcher design

A program anyone can run on Windows, macOS or Linux: it takes the user's own arcade files and writes
`timetrv3ds.3dsx` and `timetrv3ds.cia`. Nothing copyrighted is ever shipped; the user's files go in,
a game comes out on their machine.

```
timetrv-patcher --rom timetrv.zip --chd timetrv.chd --out out/
  -> out/timetrv3ds.3dsx   (Homebrew Launcher)
  -> out/timetrv3ds.cia    (installed with FBI; needs makerom)
```

## What the user needs

| Need | For | Where |
|---|---|---|
| `timetrv.zip` (holds `tt061891.bin`) and `timetrv.chd` | the game | their own MAME set `timetrv` |
| `chdman` | reading the laserdisc | MAME's tools (`brew install mame`, the MAME Windows package, `mame-tools` on Linux) |
| `ffmpeg` with libx264 | encoding the footage for the 3DS | ffmpeg.org builds, Homebrew, the Linux package |
| `makerom` | the `.cia` only | github.com/3DSGuy/Project_CTR releases (`makerom-v0.19.0` for Windows, macOS, Linux) |

No compiler and no devkitPro: the app itself comes prebuilt.

## How it works

The 3DS app holds no game data. Our GitHub release ships it without media (code only, about 300 KB,
free to share), and the patcher adds the user's data to it:

1. **Check the inputs.** SHA-1 of `tt061891.bin` (`7c5003b6...`) and of the CHD (`8abb5e6a...`, as
   `make doctor`); find `chdman`, `ffmpeg` and `makerom`, and say plainly which is missing.
2. **Tables.** `tools/extract_tables.py` (ROM to LED text) and `tools/pack_game.py` (to `timetrv.ttg`).
   The scene graph in `data/` ships with the patcher; it holds no ROM text.
3. **Footage.** `tools/encode_ld.py` through `chdman` and `ffmpeg`, as `make encode`: 347 clips and 82
   segments, about 470 MB of work files and 310 MB in the game. This is the slow step. It resumes:
   files already encoded are skipped.
4. **`.3dsx`.** A RomFS image of the game folder (`timetrv/`: the `.ttg` and the clips), appended to
   the code-only `.3dsx`, with the RomFS offset written into its header.
5. **`.cia`.** `makerom` with the app's `.elf`, our RSF (title ID, permissions), the shipped icon and
   banner, and the same game folder as RomFS.

### The .3dsx RomFS (checked on our release build, 2026-10-12)

- The `.3dsx` extended header (44 bytes) holds `smdh_offset, smdh_size, romfs_offset` at `0x20`. The
  code-only build has `romfs_offset = 0`.
- In `build/release/timetrv3ds.3dsx` the RomFS starts right after the SMDH (`0x47FC0`) and runs to the
  end of the file: a level-3 RomFS image (0x28-byte header, directory and file hash tables and metadata,
  then the file data), the format libctru's `romfsInit` reads.
- The patcher builds that image in Python (`tools/romfs.py`). Test: byte for byte the same as
  `3dsxtool`'s for the same folder, and the game runs from it on hardware.

### The .cia

- Built from the `.elf` (shipped next to the code-only `.3dsx`), not from the `.3dsx`.
- Icon (`.smdh`) and banner (`.bnr`): generic art made once with `bannertool` (MIT) and shipped; no
  Sega artwork.
- Title ID: one fixed homebrew ID in the unique-ID range `0xF0000`-`0xFFFFF`, chosen once and kept, so
  a new patch installs over the old one.
- The app already finds its data in `romfs:/timetrv` (`port/3ds/source/data.c`), which is where both
  packages put it.

## Shipping the patcher

- One Python program (`tools/patcher.py`) on top of the existing tools, run with Python 3.13 and the
  packages in `requirements.txt` for now.
- Later: PyInstaller single-file executables for Windows, macOS and Linux, built by GitHub Actions,
  so users need no Python. The same workflow builds the code-only `.3dsx` and `.elf` in the devkitPro
  Docker image and attaches all of it to a GitHub Release.

## Steps (one prompt each)

1. **patcher core**: `tools/romfs.py`, `tools/patcher.py` (checks, tables, encode, `.3dsx`). Done when
   its `.3dsx` matches `make release`'s RomFS and runs on the 3DS.
2. **patcher cia**: RSF, title ID, generic icon and banner, the `makerom` call. Done when the `.cia`
   installs with FBI and runs.
3. **patcher ci**: GitHub Actions for the code-only app and the PyInstaller executables, attached to a
   Release; README download and run steps.

Open: how long the encode takes on a typical machine (UNVERIFIED; measure in step 1).
