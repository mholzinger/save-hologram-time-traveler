# 3DS port plan

Status: written as a plan (2026-10-06). The game runs on the New 3DS XL since 2026-10-08 (553f8d4); §5b has the current state. Facts are cited; design choices are marked as proposals.

## 1. What "hologram" means on this hardware

**Arcade:** a CRT image reflected by a concave mirror so the actor appears to float in space in front of a black void. Footage is live actors shot against black. [Sega Retro; Giant Bomb, see `ROM_FINDINGS.md`]

**3DS top screen:** autostereoscopic **parallax barrier** (not a lenticular lens), 800×240 = 400×240 per eye. [Wikipedia: Nintendo 3DS] Consequences:
- Exactly two views (one L/R pair) from a fixed sweet spot. No multi-view "look around" from the display itself. That would need a lenticular/multi-view panel.
- New 3DS adds face tracking ("Super-Stable 3D"). It keeps the barrier aligned to your eyes. It doesn't change the rendered viewpoint. [Wikipedia]
- All models have a 3-axis gyroscope + 3-axis accelerometer. [Wikipedia]

**Decision (2026-10-07, the maintainer): option A, a single keyed actor plane.** It's already implemented offline by `tools/encode_clip.py` (`--parallax`, default 6 px crossed). B (depth-estimated L/R) is dropped for now. C is ruled out (the DVD anaglyph is two flat layers).

**Proposal: the "mock hologram" is three layers.**
1. **Void:** black (or near-black with faint depth cues) at or behind screen depth.
2. **Actor plane:** the footage, keyed off black, placed at **negative parallax** (in front of the screen). This is the floating-in-space effect the cabinet sold. The parallax amount is per clip, tunable and scaled by the 3D slider.
3. **Motion parallax via gyro:** small console tilts shift the actor plane against the void (and optionally rotate it slightly). This fakes the look-around a lenticular display would give, which the barrier can't. It's an optional toggle, because big shifts fight the stereo sweet spot.

If the DVD 3D (anaglyph) encode turns out to be real stereo, layer 2 becomes a true L/R pair instead of one flat plane.

Bottom screen: score / lives / cubes / era as the cabinet's 16-character 14-segment LED panel. The segment font is in MAME `timetrv.cpp` `led_w`, and the LED window positions in `timetrv.lay`.

## 2. Toolchain

| Piece | What | Source |
|---|---|---|
| devkitPro pacman | macOS pkg installer. Needs `xcode-select --install` first, then a reboot | [devkitPro Getting Started] |
| `3ds-dev` | package group: devkitARM, libctru, 3DS build rules. Install: `sudo dkp-pacman -S 3ds-dev` | [devkitPro Getting Started] |
| libctru | system library. Confirmed in master @9b55eda (2026-09-30): `gfx.h` (`gfxSet3D`), `os.h` (`osGet3DSliderState`), `services/mvd.h` (H.264 HW decode), `services/y2r.h` (HW YUV→RGB), `hid.h` (buttons, gyro) | local clone of devkitPro/libctru |
| citro3d / citro2d | GPU rendering (textured quads per eye). Available: the cabinet stage (`port/3ds/source/stage.c`) uses citro3d (7b1a066) | — |
| Azahar | open-source 3DS emulator (Citra → Lime3DS → Azahar), macOS build, first stable release 2025-03-21. For fast iteration on logic/UI | [Notebookcheck] |
| Real hardware | required for stereo tuning and for MVD performance. Azahar has no MVD decoder (answered; stage test frames are used there instead) | — |
| Packaging | `.3dsx` (Homebrew Launcher) first. `.cia` later via makerom + bannertool (separate tools, not verified in `3ds-dev`) | — |

**Reference implementation:** *Video player for 3DS* (Core-2-Extreme, forked as Tescu48/Video_player_for_3DS). ffmpeg-based, with MVD hardware decode on New 3DS. Reported H.264 854×480: **5.4 fps software on Old 3DS, 114.7 fps hardware on New 3DS**. Supports 3D video (software decode only). [GitHub: Tescu48/Video_player_for_3DS] Read its MVD path before writing ours. Check its license before copying anything.

**Primary reference: Red Viper** (skyfloogle/red-viper, Virtual Boy emulator for 3DS; checked @e28d8f5, 2026-09-19). A shipped, well-regarded app solving the closest problem to ours: presenting a stereo image from a source with no depth scale.
- Per-eye citro3d render targets: `finalScreen[0]` → `GFX_TOP, GFX_LEFT`, `finalScreen[1]` → `GFX_TOP, GFX_RIGHT` (`source/3ds/citro3d.c:181-184`). Copy this structure for `stereo.c`.
- Depth slider: since VB games have no depth scale, it shifts the whole image back/forward like the VB IPD slider (README FAQ). Same model as our flat actor plane.
- Display retiming: writes top/bottom LCD VTotal via `GSPGPU_WriteHWRegs(0x400424 / 0x400524, …)` to run the panel at ~50 Hz (`source/3ds/gui_hard.c:3753-3774`). Proposal: use the same mechanism to lock the panel to 59.94 Hz so 29.97 fps footage gets an exact 2:1 cadence. UNVERIFIED for our values: derive the VTotal from the panel's dot clock with a cited source, and test on hardware before relying on it. Superseded: video is paced by wall clock (`osGetTime`, e11ea38), no retiming.
- Ships as `.3dsx` + `.cia`, distributed through Universal Updater (README). Same channel as ours.
- **License is mixed:** some files are MIT (marked per file); the Reality Boy-derived code is non-commercial with attribution required (README "License"). Read freely; copy only from MIT-marked files, with attribution.

## 3. Target hardware

**Target: New 3DS XL (the maintainer owns one; confirmed 2026-10-06).** Custom firmware installed (the maintainer, 2026-10-06), so `.3dsx` runs via Homebrew Launcher. Exact Luma3DS version not yet recorded.
- H.264 via MVD is roughly 20× the Old 3DS software rate at 854×480 (figures above). Old 3DS can't play full-rate stereo H.264.
- MVD: New 3DS only; H.264 input; output YUYV422 / RGB565 / BGR565; one session per MVD service at a time. [3dbrew: MVD Services; libctru `mvd.h`]
- Old 3DS fallback (later, optional): lower resolution or frame rate with software decode. Measure before promising anything.

## 4. Architecture

```
data/game.json          scene graph (from ROM + PC tables): clips, input windows, outcomes, rules
romfs/ or sdmc:/timetrv/  per-clip video files (built locally, never in git)
port/3ds/source/
  main.c         init, main loop, 3D slider, suspend/resume
  engine.c       game state machine (port of tools/sim.py): eras, lives, cubes, Trader, Hellgate, random scene order
  input.c        HID → {U,D,L,R,FIRE,L+FIRE,R+FIRE,NONE} per window; Cube = separate button
  video.c        MVD H.264 decode → RGB565 frames, prefetch of next clip
  stereo.c       compositor: void + keyed actor plane + parallax + gyro offset, per eye
  audio.c        NDSP playback of each clip's audio channel
  hud.c          bottom-screen 14-segment LED panel
```

**Video format proposal:**
- One H.264 stream per clip, **side-by-side 800×240 (L | R)**. One MVD session decodes both eyes in one pass. Split into two textures on the GPU. Avoids needing two concurrent decoder sessions.
- For flat sources (options A/B in `TASKS.md`), the build pipeline renders L/R offline. That moves all keying and depth work off the console.
- Clip files map 1:1 to ROM clip IDs (`data/clips.csv`) so `engine.c` speaks the ROM's vocabulary.
- Size check: the LD side is 53,339 frames ≈ 29.7 min at 29.97 fps. At ~1–2 Mbps that's roughly 225–450 MB. Fine on SD; too big to embed in a `.3dsx` romfs comfortably, so default to `sdmc:/timetrv/`.

**Branching latency:** input windows are clip-relative frames. On window close, the next clip (success or death) must start without a visible gap. The engine prefetches both candidate clips' first GOPs while the window is open. Measure MVD start-up cost on hardware early (milestone M2).

## 5. Milestones (each has an exit test)

| M | Deliverable | Exit test |
|---|---|---|
| M0 | Toolchain | `make 3ds` builds the existing stereo-plane stub. Runs in Azahar and on hardware. Slider changes depth visibly |
| M1 | Stereo + gyro test | Stub gains gyro offset. Record comfortable disparity range and gyro gain in this doc |
| M2 | Video spike | Play one clip (H.264 SBS 800×240) via MVD at full rate with audio. Measure decode fps, start-up latency, memory |
| M3 | Hologram look | One real clip keyed off black, floating at negative parallax over the void. Compare against arcade footage/photos |
| M4 | Engine on desktop | `tools/sim.py` plays the whole game from `data/game.json` in a terminal (depends on Phase 1) |
| M5 | Engine on 3DS | Scene 1 (1873) fully playable: windows, deaths, cube rewind |
| M6 | Full game | All eras, Time Map, Trader, Hellgate, attract mode, settings mirroring arcade DIPs |

M0–M3 don't depend on the ROM work and can run in parallel with Phase 1.

## 5b. Current state (2026-10-12)
- **Runs on hardware** (New 3DS XL, since 2026-10-08, 553f8d4): MVD video, NDSP audio, LED panel, attract, settings, two players, cube rewind, Time Map, Trader, Hellgate, death clips with the cube offer.
- **Video from the laserdisc CHD:** all 347 clips plus 24 named segments, 44 Time Map stills and 28 hint objects, encoded by `tools/encode_ld.py` into `build/enc_ld`. `make game` builds everything from the CHD + ROM (doctor, tables, encode `--missing`, stage with `tools/check_media.py`, release as a one-file `.3dsx`). No DVD step remains in this path.
- **Engine:** `port/3ds/source/engine.c` (C99) reads the packed graph from `tools/pack_game.py`; `make -C port/3ds/host parity` keeps it identical to `tools/sim.py` (900/900 after the 2026-10-12 parity sweep). ROM checks: `tools/romdiff.py` perfect 46/46, intro skip 40/40, no input 15/15, random 103/103.
- **Cabinet stage:** citro3d stereo scene with the props, floor reflection and LED line (`stage.c`, see TASKS Phase 3).
- **Emulator:** Azahar has no MVD decoder; the app uses stage test frames there. Setup notes below.
- Still to measure on hardware: input feel, A/V sync, stereo depth, MVD seek time on rewinds (TASKS "Fidelity checks").

### Earlier offline notes (2026-10-06/07, superseded)
Kept for history. "Not run", "M0 blocked" and "death clips aren't shown" no longer hold: everything below now runs on hardware, and the DVD encodes (`build/enc`) are replaced by the CHD encodes.

- **M0 blocked on the user:** devkitPro is present (`/opt/devkitpro`, devkitARM r67.1) but only the NDS packages are installed. `3ds-dev` (libctru, 3ds_rules) is missing, so `make 3ds` fails at `#include <3ds.h>`. Install needs root: `sudo dkp-pacman -S 3ds-dev`. Azahar isn't installed either.
- **M2 encode side done offline:** `tools/encode_clip.py CLIP_ID` cuts a ROM clip from the DVD (via `data/ld_to_dvd.json`) and writes 800×240 SBS H.264 Constrained Baseline (CRF 23, GOP 30) plus 32 kHz mono PCM WAV of the clip's LD audio channel to `build/enc/`. Clip 28 (C1 E20, 7.9 s): 305 KB video (≈310 kbps). Projection for the whole 53,339-frame LD side: ≈ 69 MB video + ≈ 114 MB PCM. MVD support for Baseline profile and decode speed are still to be measured on hardware.
- **M3 look, offline prototype:** the DVD's 2D game footage carries a static CG overlay (four rotating prop shapes along the top, a perspective grid; identical across titles). `tools/dvd_overlay.py` builds a median plate and a prop mask. The encoder blanks the prop boxes, removes grid pixels that match the plate, and keys near-black to pure black. The actor plane then sits at 6 px crossed disparity (`--parallax`) on a black void. Result: actors floating on black with the overlay gone. Floor reflections under the actors remain (arcade or DVD CG? UNVERIFIED). Actors passing through a prop box lose those pixels (11% of the frame, top band).

- **M4 done on the host:** `tools/sim.py` plays the whole game in a terminal. `port/3ds/source/engine.c` is a C99 port of it (no libctru). It reads the 33 KB packed graph `build/timetrv.ttg` (`tools/pack_game.py`; format in its docstring and `engine.h`). `make -C port/3ds/host parity` runs 300 perfect-policy and 300 no-input games in both and diffs the traces: identical, so the 3DS engine is pinned to the documented rules. Front-end callbacks: `choose_input`, `use_cube`, `accept_hellgate`, `map_choice`, `on_episode`.

- **M1/M5 code written, not run:** `port/3ds/source/main.c` now has a menu (A = play, Y = stereo test). The stereo test adds a gyro motion-parallax offset (`hidGyroRead` y-rate, leaky integrator, gain `1/400`, ±20 px; to tune on hardware). `frontend.c` drives `engine.c` with a stand-in picture: windows run in real time at clip pacing (2 vblanks per LD frame), D-pad/Circle Pad = joystick, A = Attack, B/R = Cube, bottom-screen prompts for cube use, Hellgate and Time Map, text HUD. The game data comes from `sdmc:/timetrv/timetrv.ttg` (`make stage` builds the SD layout). `make 3ds-syntax` passes against libctru @9b55eda with devkitARM's gcc (`-Wall -Wextra`, no warnings). It can't be linked until `3ds-dev` is installed.

- **M2 console side written, not run:** `video.c` loads a clip's raw Annex-B stream (`build/enc/clipNNN.h264`, made by `encode_clip.py`) into linear memory. It feeds NAL units (00 00 01 prefix) to `mvdstdProcessVideoFrame` and calls `mvdstdRenderVideoFrame` on `MVD_STATUS_FRAMEREADY`. The 800×240 RGB565 output is blitted L|R into the top BGR8 eye buffers. `audio.c` plays the clip's mono PCM16 WAV through NDSP channel 0; this needs the user's own `sdmc:/3ds/dspfirm.cdc` dump. The front end opens both per episode and advances one LD frame per 2 vblanks; it falls back to the stand-in picture on Old 3DS or when a clip is missing. `make stage` → `build/sdmc/timetrv/` (scene graph + 231 clips, ≈130 MB). Death clips aren't shown yet: the engine has no "death playing" callback. To measure on hardware: decode fps, MVD start-up per clip, the CPU cost of the colour-convert blit.

- **Emulator (2026-10-07):** Azahar 2126.1.2 macOS arm64 (GitHub release `azahar-emu/azahar`, not in Homebrew) is installed at `~/Applications/Azahar.app` (ad-hoc signed). Its virtual SD card is `~/Library/Application Support/Azahar/sdmc/`; `rsync -a build/sdmc/ "$HOME/Library/Application Support/Azahar/sdmc/"` mirrors the staged files. Launch: `open -a ~/Applications/Azahar.app --args "<sdmc>/3ds/timetrv3ds.3dsx"`. Whether Azahar emulates MVD was unknown (answered: it does not); if `mvdstdInit` fails, the app falls back to the stand-in picture and the LED shows "NO MVD".

## 6. Open risks
- ~~Azahar's MVD support~~: answered, Azahar has no MVD decoder; video is tested on hardware.
- Stereo comfort: strong negative parallax over a full-screen actor can break fusion at the frame edges. Keying (black void around the actor) helps; M1/M3 decide the limits.
- Gyro motion parallax vs the barrier sweet spot: keep offsets small; make it a toggle.
- ~~Whether DVD audio preserves the LD's two channels~~: moot, audio now comes from the CHD.
- Distribution: the port ships code only. Users build their own clip set from their own disc via the pipeline.

## Sources
- devkitPro, Getting Started: https://devkitpro.org/wiki/Getting_Started
- 3dbrew, MVD Services: https://www.3dbrew.org/wiki/MVD_Services
- Wikipedia, Nintendo 3DS: https://en.wikipedia.org/wiki/Nintendo_3DS
- Tescu48/Video_player_for_3DS (fork of Core-2-Extreme): https://github.com/Tescu48/Video_player_for_3DS
- Notebookcheck, Azahar first release: https://www.notebookcheck.net/Open-source-free-Nintendo-3DS-emulator-launches-first-release-with-new-mode-for-50-performance-improvement.979893.0.html
- devkitPro/libctru master @9b55eda: https://github.com/devkitPro/libctru
- skyfloogle/red-viper @e28d8f5: https://github.com/skyfloogle/red-viper
