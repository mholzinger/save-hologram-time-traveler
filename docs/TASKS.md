# Backlog

Tick items as they land. Record every new fact in `ROM_FINDINGS.md` with its address or citation.

## Phase 0 — Project bring-up
- [x] `git init`, first commit (check that `git status` shows no `.bin`/`.zip`)
- [x] `pip install capstone`; `make rom` passes SHA1 check
- [x] `make disasm` reproduces pass-1 numbers (412 funcs, ~24% byte coverage)
- [x] `make tables` reproduces `data/clips.json` byte-for-byte (`git diff --exit-code data/`)
- [x] `make ghidra-import` succeeds. Confirm base address, labels applied, and that `ld_search_play_to` decompiles cleanly. Note the working `TT_BASEADDR` in CLAUDE.md.
- [x] `make ghidra-decomp` produces `build/ghidra/decomp/INDEX.tsv`

## Phase 1 — Recover the game logic (ROM)
- [x] Find every reader of `clip_table` (`CC1A:0000`). Name the clip-lookup function. → `C4B6:00CB clip_find`; table is really at `CC1A2`.
- [x] Map each embedded `__FILE__` string to its code segment and tag functions per module. → Segment map in ROM_FINDINGS; `re/ghidra/scripts/TimeTravelerModules.java` renames 318 default `FUN_*` to `<module>_SEG_OFF` (`maybe_` where the module is inferred from link order). The 7 modules between display and account (`seg_D604` … `seg_D843`) were later assigned (48299ad; ROM_FINDINGS "Source modules").
- [x] Decode `CBFC0` 12-byte records. → Asset table at `CB0D6`, `data/assets.json`.
- [x] Decode `C5C00` 10-byte records. → Window table at `C5AA4`, `data/windows.json`. Plus input rules (`C8296`), moves (`CCC8A`), actions (`C801A`).
- [x] Decode `C9000` / `CB000` 8-byte records. → Not a separate table: both addresses fall inside the input-rule table `C8296–CB0C5` (`{key, move, window, action}`); `0xFFFF` = key of 105 global rules.
- [x] Confirm or refute: clip `type` high byte = audio-channel mask passed to `ld_audio`. → Confirmed (byte +7 of the realigned record, `C4B6:020B`).
- [x] Decode the input model: joystick direction plus Attack/Cube per window. (Windows, rules, moves, built-in actions, input bits: see ROM_FINDINGS.)
- [x] Decode how `chorder.c` / `odds.c` pick randomized sequences. → `C359:0034` game_choose; f2 = weight; bonus chapter; rare substitutions (`data/substitutions.json`).
- [x] Decode the rules: lives, cube, Hellgate, scoring, level select, chapter order, Time Map entry (see ROM_FINDINGS).
- [x] Game-over / mercy-life flow (`D7C0:0616`, `D7C0:0791`, `D7C0:052D`), 2-player hand-over entry points (ROM_FINDINGS "Game over and the mercy life").
- [x] Time Map options table (`D843:0B33` → `data/time_map.json`, used by sim.py and engine.c; 0 misses in 300 games).
- [x] 2-player hand-over (`D7C0:038F` switch_player, `D7C0:068B` set_player_count): players alternate after each life lost (ROM_FINDINGS "Two players").
- [x] Add a reader for each decoded table to `tools/extract_tables.py`. Emit `data/game.json`: one self-contained scene graph.
- [x] Write `tools/sim.py`: a text-mode simulator that walks `data/game.json` with keyboard input. `--check`: 0 errors. `--auto 1000 --policy perfect`: all games finish, 100/104 episodes reached (4 weight-0 episodes need an undecoded path).
- [x] Find how C0 E30/E40, C1 E50, C5 E50 (weight 0) are reached in normal play. → Not reachable except via level select; the only candidate path (`C359:05AE`) is dead code (ROM_FINDINGS).

- [x] Differential test against the real ROM: `tools/romrun.py` runs tt061891.bin on Unicorn with faked I/O; `tools/romdiff.py` compares its laserdisc command stream with `tools/sim.py`. No-input game: scene order identical, 15/15 transitions. Fixed four rules (mercy timer, chapter-1 flow, one pass per visit, chapter completion) in sim.py and engine.c.
- [x] Extend the differential test past chapter 1. → `romdiff.py --perfect`: the real ROM plays to the ending (chapters 0,1,5,3,4,2,6,7,8,20), 46/46 transitions agree.
- [x] Name the non-scene plays: Trader 27764–28014, Hellgate intro 23651–24012, high-score entry 47768–48040 (`DA9D:064D`). 36934–37057 is C5 E40 `replay_from`.
- [x] Trader, Hellgate and Time Map footage from the laserdisc, played on the 3DS. Trader rules and map controls measured on the ROM (`romdiff.py --trader / --map-keys`).
- [x] High-score entry and table (`DA9D:064D`): clip, ROM messages, initials (Up/Down, A), FUK→YOU, top 3 on SD. Up = +1 and the 0.5 s blink are from the ROM (`D0F0:0A0A`, `D17E:039E`).
- [x] Drive Hellgate accept from the harness and compare the roll outcomes (Down accepts; 5 runs match, `romdiff.py --hellgate`).

## Phase 1b — PC version as a second source (`video/HTT/`, see `video/README.md`)
- [x] `tools/parse_pc_tables.py`: parse all `httNN.txt` + `*.hot` → `data/pc_scenes.json` (78 tables, 238 rows, no unknown tokens; 9 problems listed in the JSON).
- [x] Inventory every `htt2NN*.avi`: frames, duration, audio → `data/pc_clips.csv` (`tools/pc_clips.py`, 233 AVIs).
- [x] Find the `httNN.txt` parser in `htt.exe` and pin down column → action and field `a`. → c1 none, c2 Up, c3 Down, c4 Left, c5 Right, c6 Fire; a/frame = window start/end (capstone on the PE; see video/README.md).
- [x] Match PC clips to ROM clips → `data/pc_to_ld.json` (structural: per-key outcomes + lengths, 55/79 confident) and PC clips to DVD → `data/pc_to_dvd.json` (229/233 by fingerprint).
- [x] Cross-check ROM input-window decoding against the PC tables. → On the 55 confident scene matches: per-key outcomes agree 803/989 (81%); PC row end ×2 lands within ±10 LD frames of a ROM window end for 128/157 rows (median −2). The ROM decoding holds; the PC rows mark window *ends*.

## Phase 2 — Video (DVD → LD frame space)
- [x] DVD intake per `video/README.md` (mount ISO, lossless remux of 2D and 3D titles, inventory).
- [x] Build an LD-frame → DVD-timestamp mapping → `data/ld_to_dvd.json` (237/347 clips, 66% of frames; passes and measured accuracies in video/README.md).
- [x] ~~Map the remaining 116 clips to the DVD.~~ Superseded: all footage now comes from the laserdisc CHD (`tools/encode_ld.py`), no mapping needed.
- [x] Identify the DVD's inserted CG prop shapes; decide crop or mask per clip. → Static overlay (props + grid), same on every title: global mask via `tools/dvd_overlay.py`, applied in `tools/encode_clip.py` (see PORT_PLAN.md §5b).
- [x] Encode per-clip files for 3DS (resolution, codec, audio channel per `type`/`audio_mask`). → All 347/347 clips now come from the laserdisc CHD (`tools/encode_ld.py`, bd4ef8a; `make encode` → `build/enc_ld`), played through MVD on hardware. Legacy: the DVD encodes of 237 mapped clips (`tools/encode_clip.py`: H.264 Baseline SBS 800×240 ≈ 344 kbps + 32 kHz PCM, `build/enc`).

## Phase 3 — Stereo "hologram"
- [x] Analyse the DVD 3D (anaglyph) encode: real stereo vs uniform shift. **Result (2026-10-06): not real stereo.** Two flat layers: actors at 0 px, replacement tunnel background at ~24 px. Option C adds nothing over A. See `video/README.md` → "3D angle".
- [x] ~~Run `port/3ds` stereo-plane test on hardware.~~ The M0 stereo-plane prototype was removed (2026-10-12, superseded by the cabinet stage); depth tuning moved to Post first release (stage depth).
- [x] Prototype keying actors off black (luma key) on a few clips, offline, as L/R pairs. → `tools/encode_clip.py` (SBS 800×240, overlay removed, crossed disparity).
- [x] Decide A (single keyed plane) vs B (depth-estimated L/R) vs C (L/R recovered from DVD anaglyph). → **A**, decided by the maintainer 2026-10-07. Reasons: C adds nothing (the DVD anaglyph is two flat layers); A already works in the encodes (actors keyed off black, overlay removed, crossed disparity); B would double video size and needs an offline depth model. Disparity value to tune on hardware (stereo-plane test).
- [ ] **Cabinet stage (after parity).** The cabinet has physical props around the floating image; they are not on the laserdisc. Source: the maintainer's photos of a restored cabinet (Electric Starship Arcade video, 2026-10-11; the props in it are the restorer's painted wooden replicas, bag "HTT-033"). Layout seen from the player, on a glossy black oval floor (reflects props and image) with a magenta rim, dark back wall, the green LED panel mounted at the back centre above a dark box:
  - front left: yellow-green triangular prism, tilted, orange speckles;
  - back left: blue square slab, green speckles;
  - back centre: long yellow-green bar lying flat, orange speckles;
  - back right: tall red bar standing, yellow speckles;
  - right: grey half-disc standing, white speckles;
  - front right: green triangular prism, tilted.
  The image floats centre front and reflects in the floor. The DVD's CG worlds imitate these props (green/yellow/blue/red shapes over a grid). Plan: citro3d stereo scene with the video as a flat plane, the props as real geometry, floor reflection, LED panel in the scene.
  → Built 2026-10-11 in `port/3ds/source/stage.c` (X toggles in game): floor, rim (dims while video plays), six props (both "prisms" are flat triangular cut-outs; colours as lit, from the maintainer's reference video; one fleck colour each), prop and LED reflections, the LED line floating at the back (1.6x real size to stay readable; no housing, the dark box in the reference was a reflection). Open: hardware tuning, perspective-correct mode.

## Phase 5 — Beta for friends (after the stage)
- [~] **One-file release.** → `make release` builds `build/release/timetrv3ds.3dsx` (300 MB, RomFS; `port/3ds/source/data.c` reads RomFS when it holds the game, else the SD card; high scores stay on the SD card). Verified in Azahar with the SD data hidden (art loads from RomFS); gameplay from RomFS untested on hardware. `.cia` still open. `.3dsx` with the clips, map and game data packed in RomFS (read `romfs:/`, fall back to `sdmc:/timetrv/`); high scores and settings stay on the SD card (RomFS is read-only). `make release` builds it (about 300 MB; FAT32 limit is 4 GB). Then optionally a `.cia` (icon, banner, `makerom`) from the same RomFS for a HOME menu entry. Needs a New 3DS/2DS (MVD) and custom firmware.
- [ ] **Decide before sharing (the maintainer):** the file would contain Sega's laserdisc footage, i.e. distributing copyrighted media; the repo rule keeps media off git. Alternative: ship the app only and have each friend build clips from their own CHD.
- [ ] **Official art.** Splash/title screen and homebrew icon from official art (flyer, marquee, cabinet side art). the maintainer supplies the images (same rule as other media: they stay out of git, packed only into the release).
- [ ] **Bottom-screen menus.** Restyle the main menu and the settings (DIP) screen on the bottom display: drawn layout in the cabinet's style instead of the text console.

## Phase 4 — 3DS player (milestones and exit tests in `PORT_PLAN.md` §5; M0–M3 can start now, in parallel with Phase 1)
- [x] M0: install devkitPro (`3ds-dev`), build `port/3ds`, run in Azahar and on hardware. → 3ds-dev installed (libctru 2.7.0), `make 3ds` builds `timetrv3ds.3dsx` with no warnings (2026-10-07). Runs on hardware since 2026-10-08 (553f8d4).
- [x] Clip player: open a clip, decode, present to both eyes with chosen parallax, honour audio channel. → `port/3ds/source/video.c` (MVD) + `audio.c` (NDSP) + death clips via `on_death` (cube offered during the death clip). Runs on hardware (553f8d4; cube 982a3ea, rewind 4f37675).
- [x] Scene engine driven by `data/game.json` (port of `sim.py`). → `port/3ds/source/engine.{c,h}` (C99, no libctru) reads `build/timetrv.ttg` from `tools/pack_game.py`; `make -C port/3ds/host parity` = 600 games identical to `sim.py`. Synchronous callback API; the 3DS front end still has to drive it (thread or state machine).
- [x] Input mapping: Circle Pad / D-pad → joystick, A → Attack, B or R → Time Reversal Cube. → `port/3ds/source/frontend.c`. Runs on hardware; one event per stick change as `D0F0:0A0A` does (a8d62a2).
- [x] Bottom screen: score, lives, cubes, era (replaces the 14-segment LED panel). → `port/3ds/source/led.c` draws a 16-character 14-segment panel with MAME's glyph table; host render check `make -C port/3ds/host led` (build/host/led.png). Runs on hardware, driven by the ROM's message system (f3a1fd6, d445015).
- [x] Attract mode, settings menu mirroring the DIP options (lives, cube count, devil behaviour, difficulty). → `frontend_attract` (the ROM's A/B cycle, 73cb480) and `frontend_settings` (DIPs incl. difficulty, saved to settings.cfg, 78cf225) in `frontend.c`. Runs on hardware.

## Fidelity checks against the real ROM (tools/romdiff.py, tools/romrun.py)
- [x] No input: scene order identical, 15/15 transitions.
- [x] Perfect play to the ending: 46/46 transitions, 46/46 scores.
- [x] Random mistakes (`--random`, replaying the ROM's own input frames): 10 deep runs into chapters 3-5, 231/231 transitions and 231/231 scores agree (2026-10-08); found Cube-never-matches (CE55:01D7) and incomplete-chapter replay (D70D:0077).
- [x] Cube rewind, Trader, Time Map cursor, Hellgate, hints, attract cycle, two players, intro skip / Learn path.
- [ ] On hardware: input feel, A/V sync, stereo depth, MVD seek time on rewinds (needs the New 3DS).
- [x] 1P+2P Start together skipping the intro (CE55:0138): START+Y on the 3DS (a653053). Still not ported: coin/credit messages (free play only).
- [x] Laserdisc speed effects (fast forward x3 / slow motion x0.5 / PL) on the clip clock, beeps, death tune, alarm tunes (2026-10-11).
- [x] Rewind: plays the footage backwards at x2 like the ROM (speed 120 MR), from frames cached while seeking.
- [x] Seek blanking (0VD/0AD around searches): modelled with MAME's rule (ab292db). Real LD-V4200 seek times are still UNVERIFIED.
- Reverted 2026-10-11 (the maintainer: "match the arcade"): Right on the signpost cuts straight to chapter 1, as D659:000C does.
- [x] In-scene action footage: Left on the signpost plays LD 129-245 (D659:0041); the C3 E82 trap plays LD 23581-23649 and 26062-26305 (DA9D:0E6D); Start/Attack in C1 E5-E7 enters C1 E10 (DA9D:0EF8). 3DS Start = cabinet 1P Start; SELECT+START quits.


## Parity sweep (2026-10-12)
An exhaustive audit of the port against the ROM decompile, the MAME driver and the repo's open notes, run as four parallel read-only audits. Footage and data coverage is complete: all 347 clips, 17 named segments, 28 hints and 44 map stills are encoded and staged. Items are ordered by player impact. The gameplay items come from reading the decompile, so **confirm each with romrun/romdiff before changing the engine**.

**Gameplay (confirmed 2026-10-12)**
Fixed in `sim.py` and `engine.c`. After the fixes the ROM still agrees on perfect play (46/46), the intro-skip path (40/40), no input (15/15) and random runs 1-5 (103/103); C and Python agree on 900/900 games. The 2-player item moves to the 2P prompt.
- [x] Hard windows reset after chapter 1. `D70D:040E` reads the finished slot's chapter and clears `[0x13C4]` when it is 0/1. The port clears the flag only when chapters 0/1 start (engine.c ~492), so later eras stay hard. **CONFIRMED** (`romdiff --perfect --signpost right --watch 0x13C4,0x42E`): the flag sets at C1 E35 (5th good scene) and clears at C1 E40, the end of C1. It never sets again because the counter only passes the threshold.
- [x] C3 E82 trap. `DA9D:0E6D` marks E82 cleared, calls `lose_life`, sets the death flag `0x100A` and runs `episode_done` (hint, mercy, 2P switch). The port takes no life and marks E82 failed (engine.c ~269/419/522). **CONFIRMED by code** (d000_b83d: status = 1, `lose_life`, `0x100A = 1`, `episode_done`). No ROM run, because E82 is bonus-only (weight 24). It may take 2 lives; that part is still INFERRED.
- [x] Hellgate once per game. `hellgate` (`D0F0`, d000_115e) zeroes the slot `[0x1B2A]` on entry. The port never clears `hellgate_slot`, so a replayed chapter offers it again. **CONFIRMED** (`romdiff --watch 0x1B2A`: 6 at game start, 0 at tick 21053, the same tick the offer LD 23651 starts).
- [x] 2-player game over and hall of fame.
  - ROM: `D7C0:052D` with 2 players shows 0x3D/0x3E and waits. `D7C0:0791` sets players = 1. The survivor's game over enters both scores (`DA9D:064D`).
  - Port: a hall of fame for each player (frontend.c ~1225, marked UNVERIFIED).
  - **CONFIRMED by code:** d000_8391 / d000_7f8f / d000_828b, and d000_b01d loops over both scores, highest first. 038F also sets or clears the mercy flag `[p*2+0x34]` and plays the handover footage (04BA).
- [x] Hall of fame timeout and abort. The ROM inserts the score with "TEN" (DGROUP 0x16AB) before initials (`D945:0232`), so a timeout or abort keeps it. The port discards the entry on timeout and has no abort. **CONFIRMED by code** (d000_b01d): 'TEN' at DGROUP 0x16AB and 'AAA' at 0x173A are read from the ROM. A timeout (12 blinks without input) or an abort (event 0x20 or 4) returns, which keeps 'TEN'. An all-blank name becomes 'YOU' (0x1742).

**Footage and presentation**
- [x] 2P handover footage. Encoded as `handover_p1`/`handover_p2` (encode_ld.py EXTRA, `--only`). It plays at each switch for the incoming player, if they have lives, and for player 1 at a 2P start. The 2-player path is not covered by the host test (its thread fakes don't run); check it on hardware. `D7C0:04BA` plays LD 15330-15450 (P1) / 15612-15732 (P2) at each switch and at 2P start. Needs an encode_ld EXTRA plus playback.
- [x] Seek blanking. The ROM sends `0VD` before searches (`D37E:07C7`, about 10 call sites). MAME blanks searches over 100 frames (ldv4200hle.cpp:634-638). Done with MAME's rule: black for the halving fields (about 7-12 fields, 0.1-0.2 s). The TTG clip table now holds LD positions, replacing the unused DVD table. The real player's seek time is still UNVERIFIED. Maps and hints have no known LD start, so they don't blank.
- [x] Map still before chapter 7. `D843:0A3D` searches to the current mask's still and waits 3 s. The port shows black.
- [x] The "YOU SOLVED" 3 s pause belongs only on the route into chapter 7. The port pauses before every map (frontend.c ~787).
- [x] 2P handover message: the ROM shows 0x0E (`D7C0:038F`), the port shows 0x37.
- [x] Mercy flag `[0x34]` is cleared on the next `episode_done` after mercy expires. The port never clears `mercy_given`, so the LED says "EXTRA LIFE" instead of "LAST LIFE".

**Smaller rules**
- [x] Hellgate decline: any non-Down event declines (`device_D17E_039E(0x11)`). The port ignores those keys (frontend.c ~696).
- [x] Hellgate second offer:
  - waits 117 + 170 and plays LD 23982-24184;
  - after "devil takes all" it plays no after-clip (24669-24838) and no 0x0E.
- [x] Uncapped cube adds: Done for Hellgate and the Trader (trader c000_f509:90-153: buys while below max, plus a coin at full stock). The price DIP is under extras. Hellgate +2 and Trader purchases at max stock (`cubes_add`). The Trader price still follows the DIP (`CEDF:0EC0`: 28029/28039/28049/26902).
- [x] Incomplete 1873 replays chapter 1 directly (`chapter_advance` d000_70f1). The port sends it to the Time Map.
- [x] Mercy countdown: the port adds the whole clip before it plays (engine.c ~414). **Measured** (`romdiff --watch 0x150E`): 300 at chapter 0/1 entry, then -1 every 2 ticks (30 per second of play). The engine and sim now count frames actually played. ROM checks: 46/46 perfect, 19/19 for random runs 1 and 4.
- [x] Hellgate leaving 0 lives in 2P goes through `episode_done` (mercy, switch). INFERRED. Covered by the 2P flow: game over, then the handover; mercy is off after devil-takes-all.

**Extras the cabinet has**
- [x] Level Select (DSW2 bit 4): `tt_run_select` + picker (Settings > LEVEL SEL, or Extras > Scene select). System records are left out of the picker. every episode, the C/E test picker (`C40B:04D0`), no lives lost. It is the only route to C0 E30/E40, C1 E50 and C5 E50.
- [x] Intro skip: 1P+2P Start together in chapter 0 plays LD 248-350, then `chapter_next` (`CE55:0138`). Done: Y is 2P Start, so START+Y together (host test: the skip clip, then C1 E5).
- [x] Attract easter egg (`C2BA:0019`): hold Attack+Cube and push Up/Down for LD 53469-53618 / 53646-53884, once per attract. Done, from the code (c000_2bb9). Encoded egg_up (the team) and egg_down (the dedication). Attack/Cube/directions no longer end attract.
- [x] Optional cabinet DIPs: Demo Sounds (attract audio muted) and the Reversal Cube cost (8 settings, trader_buy c000_fbe5, the matching price still from specobj_CEDF_0EC0: 28029/28039/28049/26902). Settings is now a scrolling list, saved to settings.cfg. The cube lamp is not applicable: it lights the cabinet's Cube button, and the 3DS has no equivalent beyond the system notification LED. The LED panel and debug panel already show the cube count.

**Port**
- [x] Save the DIP settings to the SD card (today they live only in memory, frontend.c ~955).
- [x] `make game`, one command from the CHD + ROM. Done: doctor (tools + CHD SHA1), tables, encode (`--missing`), check_media in stage, release; requirements.txt; no DVD step left in the LD path.
  - drop the dead DVD `map.bgr` step (Makefile:69-70);
  - add an `encode` target with a stamp file;
  - check the CHD header SHA1 `8abb5e6a…` (timetrv.cpp:337);
  - add `doctor` tool checks and `requirements.txt`;
  - fail on missing clips instead of `|| true`.
- [x] Doc cleanup:
  - stale "not run" headers (frontend.c:8, main.c:3, video.c:3, video.h:6, audio.h:5);
  - the multispeed-audio comment (frontend.c ~518);
  - PORT_PLAN §5b and the old session notes;
  - [~] items in Phase 2/4 that are done on hardware.

## Post first release
- [ ] **Small code cleanups** (left from the 2026-10-12 code audit):
  - naming: `tt_data` uses the engine's prefix; the front end's `fx`/`hint`/`trader` share names with engine statics; `G` in ledmsg.c;
  - `video_exit` does not shut MVD down; engine.h exposes all of `tt_game`;
  - `play_record` recurses on a replay where a loop would do (change sim.py with it);
  - the DVD leftovers in the .ttg format (`tt_clip.source`, the titles section): drop at the next format change.
- [ ] **Stage depth and perspective-correct mode** (Phase 3). Tune the 3D depth (`IOD_MAX`, stage.c) by eye on hardware, and build the "perspective correct" camera mode the maintainer asked for early on. About 1 h plus tuning sessions.
- [ ] **Restyle the remaining screens** (Phase 5). The main menu uses the art; Settings, Video and Extras are still LED text. About 2 h.
- [ ] **Art refinement** (Phase 5). First pass is from the Digital Leisure sales sheet; swap in better scans (full-size flyers) when found.
- [ ] **Yellow lightning in C3 E50/E51** (`clip155` frame 197, starts at u 0.18, row 28). Find which prop it comes from in the arcade video; check with Extras > Scene select.
- [ ] **Hardware feel sign-off**: input timing, A/V sync, rewind seek time. the maintainer plays and reports; no code.
- [ ] **DVD build for flat-display systems** (the maintainer, 2026-10-12). A second build source for CD/DVD-based systems without a holographic or stereo display: the 2001 Digital Leisure DVD footage, which composites the actors over CG backgrounds and so suits a flat screen, instead of the laserdisc. Keep the DVD tools (encode_clip.py, dvd_*.py, data/ld_to_dvd.json) as a supported source, not legacy. The engine (engine.c, C99, no libctru) is already platform-independent. Targets (the maintainer, 2026-10-12): **Sega Saturn, Sega CD, Philips CD-i, and TurboGrafx-CD if possible** (all legacy CD systems). Open: DVD coverage gaps (ld_to_dvd.json maps 231/347 clips; video/README.md).
  - Feasibility from general knowledge, unverified here: Saturn good (2x CD, Cinepak FMV, C99 engine should port); Sega CD good at low resolution (Dragon's Lair and Space Ace shipped on it); CD-i good with the Digital Video Cartridge (MPEG-1), weak without; TurboGrafx-CD hard (8-bit CPU, small RAM, limited FMV), the engine may need a slimmed table format.
  - Test hardware (the maintainer): FPGA versions of all four consoles; the CD-i has MPEG (Digital Video Cartridge), so full-screen MPEG-1 is available there.
  - First step per system: a research prompt (toolchain, FMV codec and bitrate, RAM budget for the 46 KB scene graph, input mapping), recorded in a docs/PORT_<SYSTEM>.md before any code.
