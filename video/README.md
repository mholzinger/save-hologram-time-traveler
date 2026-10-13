# video/

Local media and reference builds. Git tracks only this README (see `.gitignore`).

## Laserdisc CHD: the video source (2026-10-07)

- `rom/timetrv.chd` (11.6 GB, ignored): CHD v5, A/V Huffman, 720×262 fields at 59.94, 2 audio channels at 44.1 kHz. `chdman verify`: raw and overall SHA1 OK. The SHA1 `8abb5e6a…` matches `reference/mame/timetrv.cpp:337`.
- **Frame numbers.** The AVLD metadata (16 bytes per field) carries the VBI picture numbers. `tools/chd_frames.py` reads them: pictures 1–53886 with no gaps, picture p at field 2(p−1), i.e. `chdman extractld -isf p-1`. The ROM's frame n is picture **n + 1**: MAME's LD-V4200 searches and stops at value + 1 (`ldv4200hle.cpp:623`, `:488`). The Time Map art confirms it: picture 26425 is black and 26426 is the first frame with option 1 highlighted, while the ROM shows option 1 with `26425SE`. So ROM frame n = `extractld -isf n`.
- **Picture.** `extractld` gives 720×524 frames (two fields with 44 VBI lines on top). The image is upside down: the cabinet's concave mirror flips it back. The actors are on black with floor reflections, and there is no grid or CG set.
- **The DVD** (Digital Leisure, 2001) composites the same footage at 0.90× scale, 32 lines lower, over CG worlds. Clip 50 lines up with `vts12_pgc01_a1` @9 at masked correlation 0.67 (180×120). Not every old DVD mapping was checked this way. Clip 10 (C0 E20) looks different on the two sources.
- **Encode** (`tools/encode_ld.py --all`, `make stage` uses `build/enc_ld`):
  - per clip, `extractld` start..stop inclusive,
  - crop 480 active lines, keep one field (240 lines, no combing), vflip, scale to 320×240,
  - near-black keyed to black,
  - stereo pair 800×240, H.264 baseline,
  - the clip's own LD audio channel(s) (`clips.json` `audio_mask`).
  - All 347 game clips: video 126 MB, audio 143 MB.

## Inventory (2026-10-06)

| Path | What it is |
|---|---|
| `HTT/` | Windows PC version, *Hologram Time Traveler* (Digital Leisure era; `htt.exe`, MFC42/MSVCRT). Already unpacked. |
| `HTT/tt/` | Game data: per-scene AVIs, input tables, bitmaps (see below) |
| `HTT/sav/` | Second copy of the `httNN.txt` input tables (81 files) |
| `HTT/SPThtt.exe`, `SPThtt.c00–c36` | Multi-volume ACE self-extracting archive of the same install |
| `Hologram-Time-Traveler_Win_EN_CD/rzr-htt.bin/.cue/.nfo` | CD image, MODE1/2352 single track, 770 MB |
| `Hologram-Time-Traveler_Win_EN_CD.zip`, `..._Win_EN_Rip.zip` | Zipped copies of the above |
| `time.zip` | `TT061891.BIN` (the arcade program ROM, dated 2001-06-25) + `readme.txt` |

The CD image and the ACE set carry scene-release tags (`rzr`, `SpEcTeR`). That doesn't matter for local analysis, but none of these assets (and nothing derived frame-for-frame from them) can ship with the port.

## PC version data format (from `HTT/tt/`)

**Video:** Indeo 3 (`IV32`), 320×240, 14.985 fps (2997/200, i.e. every other NTSC frame). Audio is mono 22.05 kHz, MS ADPCM (scene clips) or IMA ADPCM (`wiz*`). ffmpeg decodes it.

**Naming (consistent across the set, semantics partly UNVERIFIED):**
- `htt2NN.avi`: main clip for scene NN (76 scenes)
- `htt2NNDk.avi`: death clip k for scene NN (125 total)
- `htt2NNP1.avi`: alternate path clip (`htt228P1`, `htt252P1`, `htt280p1`)
- `httNN.txt`: input windows for scene NN
- `htt280.hot`: list of frame numbers (`0 459 682 870 9999`). Hypothesis: checkpoint/restart points within `htt280`.
- `wiz2NN.avi`: likely the Hellgate/"wizard" interstitials (UNVERIFIED)
- `Map*.bmp`, `lives2N.bmp`, `TTMain.bmp`, `Cursor*/Mask*.bmp`: UI

**`httNN.txt` row format:** `<a> <frame> <b> <c1> <c2> <c3> <c4> <c5> <c6>`
- `frame`: window position. Every value checked is below that scene clip's own frame count (e.g. `htt80.txt` max 856, `htt280.avi` has 945 frames), so these are clip-local frames at 14.985 fps. Inference, not yet confirmed in `htt.exe`.
- `c1..c6`: outcome per action. `00` = accepted move. `dK` = play this scene's death clip K. `NNdK` = death clip K of scene NN (e.g. `33d1` → `htt233D1.avi`). `p1` = branch to the P1 clip.
- Column → action: **resolved below** (c1 none, c2 Up, c3 Down, c4 Left, c5 Right, c6 Fire). Earlier guess: `ttfirst.txt` says scene 1 is "fire" then "Right/Fire". `htt01.txt` rows are `d1 d1 d1 d1 d1 00`, then `d3 d3 d3 00 d3 00`. That fits c6 = Fire and c4 = Right. c1–c3 are probably Up/Down/Left by the help file's order. c5 is unknown (no-move / cube?). Confirm by reversing the parser in `htt.exe`.
- `a`, `b`: usually 0. Rows like `65 200 1 00 00 00 00 00 p1` (`HTT70.txt`) and `75 200 1 … p1` (`htt59.txt`) differ. Meaning unknown.

## Parsed PC data (2026-10-06, loop iteration 2)
- `tools/parse_pc_tables.py` → `data/pc_scenes.json`: 78 tables, 238 rows, every token is `00`, `dK`, `NNdK` or `p1`. Problems: `htt29.TXT` refers to `htt229d1–3.avi` and there is no `htt229*.avi` at all (scene 29 missing from the PC build). `p1` rows in scenes 59, 60, 70, 71, 72, 81 have no `htt2NNp1.avi`, so in those rows `p1` isn't "branch to the P1 clip" (meaning UNVERIFIED).
- `tools/pc_clips.py` → `data/pc_clips.csv`: 233 AVIs (79 main, 125 death, 3 branch, 26 other). 229 are IV32 320×240 at 14.985 fps; 4 run at 12 fps.
- `tools/match_pc_ld.py` → `data/pc_to_ld.json`: duration-only match against ROM episodes, 17 of 79 confident. Example that checks out: scene 1 ↔ C1 E20 (LD 2879–3113): `htt01.txt` frames 60 and 97 ×2 = LD 120 and 194, inside ROM windows 97–130 and 165–196.
- **Column → key, from `htt.exe` (resolved 2026-10-06).** `fscanf(f, "%d %d %d %s %s %s %s %s %s")` at VA `0x4015CE` fills 0xFC-byte rows at `0x482FF0`: `a` (+0) = window start, `frame` (+4) = window end (a row is live while `a <= cur <= frame`, compare at `0x401E2E–0x401E42`), `b` (+8), then six 40-byte strings c1–c6 at +0x0C, +0x34, +0x5C, +0x84, +0xAC, +0xD4. Key handlers (messages "made move …" at `0x4150A8–0x4150D8`) read: **Up → c2** (`0x40209D`), **Down → c3** (`0x40213A`), **Left → c4** (`0x401FA1`), **Right → c5** (`0x401EA8`), **Fire → c6** (`0x4021DF`; also checks c4/c5 for direction+fire). c1 is never read by a key handler, so it is the no-input outcome (UNVERIFIED: timeout path not traced). Scene 1 row 2 `d3 d3 d3 00 d3 00` is therefore **Left then Fire**, matching the ROM (move 3 = joystick bit 4 = Left per `reference/mame/timetrv.cpp:213`). The DVD manual's "Right/Fire" disagrees with both programs.

**ROM vs PC cross-check** (55 confident scene matches): per-key outcomes agree 803/989 (81%). PC row `frame` ×2 lands within ±10 LD frames of a ROM window end in 128/157 rows (median −2), so PC rows mark window ends.

## Relationship to the arcade ROM

The ROM addresses one 53,339-frame CAV disc through `data/clips.csv` (348 clips, 29.97 fps LD frames). The PC version is the same footage pre-cut into files at half frame rate. Cross-referencing the two gives:
- LD frame ranges → PC file + local frame (map each `htt2NN*.avi` to a ROM clip id by duration and order)
- a check on the ROM decoding (input windows in `C5C00`, branch graph in `C9000`/`CB000`) against the PC tables

## DVD intake (2001 Digital Leisure DVD, archive.org item `hologramtimetraveller`)

Image: `video/dvd/Hologram Time Traveller.iso` (4,606,111,744 bytes, ignored). Volume label `DVDVolume`, files dated 2001-04-22. 18 title sets, 74 VMG titles, NTSC.

### How to reproduce
```
hdiutil attach -readonly -nobrowse "video/dvd/Hologram Time Traveller.iso"     # -> /Volumes/DVDVolume
python3 -I tools/dvd_ifo.py /Volumes/DVDVolume/VIDEO_TS > build/dvd/ifo.json    # IFO structure only
python3 -I tools/dvd_intake.py /Volumes/DVDVolume/VIDEO_TS video/dvd            # ~5 min, 8.5 GB
hdiutil detach /Volumes/DVDVolume
```
`dvd_intake.py` writes one MKV per PGC (per angle where there is an angle block) plus `video/dvd/intake.json`. Video and AC-3 packs are copied unchanged (lossless). Chapters = PGC programs. Audio tracks get the IFO language and a `DVD audio stream 0xNN` name.

Tools that did **not** work here (2026-10-06): mkvmerge v100 opens `VTS_xx_0.IFO` as an empty MPEG-PS (no tracks, no chapters). MakeMKV 1.18.4 refuses to run (expired beta key). Homebrew ffmpeg 8.1.2 has no `dvdvideo` demuxer. Hence the two small readers in `tools/`.

### Structure: 2D and 3D are angles, not titles
The 2D and red/cyan 3D encodes are **angle 1 and angle 2 of the same PGC**, interleaved (ILVU) on disc. Evidence: VMG title table lists 2 angles for every game title. Cell categories alternate `0x57…` (first cell of an angle block) / `0xd7…` (last cell), block type = angle, interleaved bit set, one 2-cell block per program. A frame from `vts01_pgc01_a2` is red/cyan anaglyph; `_a1` is plain. Per-file anaglyph score (mean |R − (G+B)/2| on a 96×64 mid-file frame): angle 2 scores 17–22, angle 1 scores 4–9 across every 2-angle game title (VTS03/05 sample frames were black, score 0). Both angles have the same cell times, so a frame at time t in `_a1` corresponds to time t in `_a2` (still to check frame by frame).

`tools/dvd_angle.py` extracts one angle by walking each VOBU's DSI: next VOBU = this + `vobu_ea`+1 inside an ILVU; at the ILVU's last VOBU (`sml_pbi.category` bit 12) jump by `sml_pbi.ilvu_sa` (offsets per libdvdread `nav_types.h`).

### Common stream properties (all titles)
- Video: MPEG-2, 720×480, 4:3 (SAR 8:9), NTSC 29.97 fps (30000/1001), **interlaced, bottom field first** (ffprobe `field_order=bb`; IFO film_mode=0 camera). Exceptions flagged progressive by ffprobe: `VIDEO_TS.VOB`, VTS04, VTS16, VTS18.
- Audio: AC-3 48 kHz 2.0. Game title sets VTS01–04, 06–15 carry two tracks: stream `0x80` = `en`, stream `0x81` = `ja` (IFO audio attributes). VTS05 `0x80` untagged (second attr says 8 ch but no `0x81` stream in its VOB). VTS16 `0x80` untagged; VTS17 `0x80` `en`; VTS18 no audio.
- Subpictures: 1 stream declared in all VTS except VTS04 (likely button highlights; not inspected).
- Duration check: MKV duration − IFO PGC time is +0.01…+0.73 s on every file except still-frame PGCs (`vts05_pgc05`, `vts16_pgc01`, `vts17_pgc12/14`), where the IFO time includes still time. `vts01_pgc01_a1`: 3,470 decoded frames vs IFO 1:55.20 (≈3,467).

### Inventory

Content column = one frame at 30 % of the file, by eye. "Game" = Time Map cubes over grid (in-game footage). Not yet matched to ROM clips.

| File(s) | IFO time | Ch. | Audio | Content |
|---|---|---|---|---|
| `vts01_pgc01_a1/_a2` | 1:55.7 | 20 | en, ja | Game (2D/3D) |
| `vts01_pgc03` | 1:29.9 | 1 | en | Behind the scenes, live-action blue-screen shoot |
| `vts02_pgc01_a1/_a2` | 1:00.7 | 19 | en, ja | Game |
| `vts02_pgc04` | 1:45.7 | 1 | en | Interview, seated man |
| `vts03_pgc01` | 0:16.3 | 1 | none | Digital Leisure logo + URL |
| `vts03_pgc03_a1/_a2` | 3:13.3 | 7 | en, ja | Game |
| `vts03_pgc04` | 3:26.6 | 1 | en | Presenter against blue background |
| `vts04_pgc02_a1/_a2` | 1:15.3 | 22 | en, ja | Game (wizard figure) |
| `vts05_pgc01_a1/_a2` | 0:16.0 | 7 | 0x80 | Era title card ("50,000 B.C.") |
| `vts05_pgc05` | 0:08.0 (still) | 20 | none | Time Map screen (dates, score, "TRADER") |
| `vts06_pgc01_a1/_a2` | 2:56.0 | 58 | en, ja | Game |
| `vts07_pgc01_a1/_a2` | 3:04.0 | 45 | en, ja | Game |
| `vts08_pgc01_a1/_a2` | 4:01.0 | 64 | en, ja | Game |
| `vts09_pgc01_a1/_a2` | 2:40.7 | 51 | en, ja | Game |
| `vts09_pgc05` | 3:10.5 | 1 | en | Interview, seated man |
| `vts10_pgc01_a1/_a2` | 3:20.4 | 66 | en, ja | Game |
| `vts11_pgc01_a1/_a2` | 3:30.4 | 66 | en, ja | Game |
| `vts12_pgc01_a1/_a2` | 12:12.8 | 7 | en, ja | Game (longest; 7 programs) |
| `vts13_pgc01_a1/_a2` | 0:27.3 | 4 | en, ja | Game |
| `vts13_pgc05_a1/_a2` | 0:05.0 | 1 | en, ja | Group of cast figures + face portraits (credits/finale?) |
| `vts14_pgc01_a1/_a2` | 1:10.3 | 10 | en, ja | Hellgate logo |
| `vts14_pgc04_a1/_a2` | 0:08.0 | 1 | en, ja | Game |
| `vts15_pgc01_a1/_a2` | 2:07.7 | 18 | en, ja | Game |
| `vts15_pgc05` | 5:13.3 | 1 | en | Interview, seated woman |
| `vts15_pgc06` | 1:34.5 | 1 | en | Interview, seated man |
| `vts16_pgc02..05` | 0:43–1:04 | 1 | 0x80 | Cel-animated footage (other Digital Leisure titles; trailers?) |
| `vts16_pgc01` | 0:01.2 (still) | 3 | none | Still |
| `vts17_pgc01,03,05,07,09,11` | 0:43–6:16 | 1 | en | Extras: CG render, developer at desk, man beside a *Time Traveler* Sega cabinet |
| `vts17_pgc12/14` | 0:01.2 (still) | 3 | none | Stills |
| `vts18_pgc02_a1/_a2` | 0:17.0 | 15 | none | "Lives Remaining: N" cards (15 programs) |

Single-track extras carry stream `0x80` only (tagged `en` from the IFO).

### Audio: does the DVD keep LD channels 1/2?
Not settled. `vts01_pgc01_a1` stream `0x80`: L −27.5 dB, R −28.8 dB, L−R −30.8 dB mean, so L and R carry substantially different signals. Stream `0x81` (ja) vs `0x80` (en) left channels: difference −24.8 dB, louder than either alone, so mostly different content. Whether `0x80` L/R equals LD ch1/ch2 or is a stereo mix is **UNVERIFIED**. Needs a listen, or a per-clip check against the ROM's `ld_audio(mask)`.

### Cross-check against MakeMKV (`video/dvd/mkv/`, made by the user with MakeMKV, 51 files, 2.3 GB)
MakeMKV follows the disc's playback chains, so most game PGCs come out as short fragments (e.g. title 1: `A1_t00/t01` 31 s vs the 115.7 s PGC), and the VTS06–11 game PGCs (2:56–4:01 each) are missing entirely. **Use `video/dvd/vtsNN_*.mkv` as the complete source.** Where the two sets overlap, decoded video is identical (framemd5 of the first 600 frames): `vts12_pgc01_a1` = `A44_t27`, `vts12_pgc01_a2` = `A44_t28`, `vts01_pgc03` = `A3_t02`. MakeMKV numbers the angles in the same order (lower `tNN` = angle 1 = 2D).

### 3D angle: layered, not real stereo (2026-10-06)
Method: 16 frame-exact pairs from `vts12_pgc01_a1/_a2` (frames 1300, 2600 … 20800; `select` by frame index, bottom field only, 720×240). 3D frame split into R and cyan = (G+B)/2. Horizontal edge maps compared by normalized cross-correlation, per tile (96×48 and 32×24, ±48 px search, NCC ≥ 0.6). Stills and channel splits are in `build/anaglyph/` (ignored).

- The 3D angle is a **re-composite**, not a tinted 2D frame. The Time Map cubes and grid are replaced by a red/cyan spiral tunnel behind the same actor footage. Actors and their floor reflections are pixel-aligned with the 2D angle.
- Red→cyan shift is **bimodal in every frame**. Actors and floor: **0 px**. Tunnel background: **23–26 px** (cyan to the right of red, i.e. uncrossed, behind the screen with red = left eye). No tile falls between the two.
- The background shift doesn't vary with depth cue: shift vs radius from tunnel centre gives corr 0.12 (mean 24.00 / 24.08 / 24.25 px for r = 0.4–0.7 / 0.7–1 / >1). A tunnel with real depth would show a gradient.
- Conclusion: **two flat planes** (actors at the screen plane, tunnel pushed back about 24 px at 720 wide). Fake stereo. No usable per-pixel depth for the actors. Recovering L/R from the anaglyph (Phase 3 option C) would give the same result as compositing the 2D actors over a shifted background on the 3DS, with worse colour.

### LD → DVD mapping (2026-10-06, `data/ld_to_dvd.json`)
Pipeline (all reproducible, outputs are numbers only):
1. `tools/fingerprint.py` → `build/fp/{pc,dvd}/*.npy`: 16×12 grayscale signature per frame (PC AVIs, DVD angle-1 MKVs).
2. `tools/match_pc_dvd.py` → `data/pc_to_dvd.json`: each PC AVI located on the DVD by correlation (PC frame i ↔ DVD frame t0 + 2i). **229 of 233 PC clips score ≥ 0.9.**
3. `tools/match_pc_ld.py` → `data/pc_to_ld.json`: PC scene ↔ ROM episode by outcome agreement per key (PC columns from `htt.exe`) plus main/death lengths. **55 of 79 scenes confident.**
4. `tools/build_ld_to_dvd.py` → `data/ld_to_dvd.json`, per ROM clip id: `dvd` (angle-1 MKV name; the `_a2` file has the 3D version at the same frame index), `dvd_start`, `dvd_frames`, plus the snapped program extent.
   - Pass 1: chain ROM clip → PC clip → DVD (confident scenes, then length-consistent weak ones).
   - Same-LD-range clips share a mapping.
   - Pass 2 (bracketed): each DVD program is one clip (138 of 152 pass-1 clips start within 10 frames of a program boundary), and inside a title the DVD mostly keeps LD order (93 of 119 consecutive pass-1 triples). An unmapped clip takes an unused program between its mapped LD neighbours' DVD positions, best length fit (`via: program-bracket`, `confident: false`). A length-only version was tried and dropped: 65 of its 80 placements broke LD order.

Program starts are refined from the IFO's 10-frame cell times to the nearest shot cut (±12 frames).

Result: **231 of 347 game clips, 42,157 / 66,467 LD frames (63%)**. Episodes with main + all deaths mapped: 55 of 105 (ch0 4/9, ch1 8/19, ch2 5/14, ch3 8/11, ch4 8/12, ch5 7/13, ch6 6/11, ch7 6/9, ch8 2/2, ch20 1/5). DVD ranges run a median 6 frames longer than LD (padding). Death clips are grouped in their own DVD stretches. Bracketed (pass-2) placements are length-only within the bracket (UNVERIFIED visually).

### DVD game logic (found 2026-10-06; decoder in progress)
- The DVD implements the game with **button highlights** in each VOBU's PCI (offsets per libdvdread `nav_types.h`: PCI data at sector +0x2D, HLI at PCI +96, buttons at HLI +46, 18 bytes each). A window = one highlight interval, cell-relative (PTM resets per cell). Typical game window: 5 buttons. Button 1 is a hub whose up/down/left/right links point at buttons 2–5. The correct button carries `SetGPRMMD GPRM0 = code` (`53 00 cccc 0000…`, codes like 1001, 1003, 1005, 2, 4); the others have no command.
- Cell commands test GPRM0 at cell end: `20 45 0000 0001 00pp` = if GPRM0 ≥ 1 LinkPTTN pp (success), `20 75 … 00pp` = if GPRM0 < 1 LinkPTTN pp (failure → death). **So a DVD program is one decision segment, and a ROM clip with several windows is a chain of segments.** Only the chain's first segment starts where the ROM clip starts.
- Consequence for mapping: length-matching ROM clips to single programs is invalid for multi-window clips. Measured: leave-one-out on PC-verified deaths, length-only placement scored 36 right / 109 wrong at the loosest threshold, and 8/10 at the strictest. The plan is to decode each PGC into a segment graph (window, correct direction, success/failure targets), walk it from each PC-anchored main clip, and map ROM deaths through the failure links.

Results (2026-10-06):
- `tools/dvd_logic.py` → `data/dvd_logic.json`: every game PGC decoded into segments, 341 windows, with correct direction, GPRM0 code and success/failure links. **DVD directions match the ROM's in the same sense** (on PC-verified pairs: down/down 27, left/left 19, right/right 19, up/up 11; `enter` where the ROM pairs a direction with Attack). So the DVD, the PC version and the ROM agree, and the manual's "Right/Fire" for scene 1 is wrong.
- A death mapper (an experiment, since removed: walk from placed mains, map ROM deaths via failure links): 37 proposals, 28 agree with PC-verified deaths, 9 disagree, 0 new. Not used: the unmapped deaths mostly belong to unplaced mains (54 of 70).
- `tools/dvd_mains.py` (place mains by window signature): 19 unique matches, 5 agree / 11 disagree with PC-verified as a standalone placer. It is used as pass 3 of `tools/build_ld_to_dvd.py` (unique matches among unused programs, 18/21 agree with PC-verified). DVD windows sit −20…+15 frames off the ROM's, and short direction sequences aren't distinctive.
- **Re-evaluation (iteration 5):** the DVD often holds a clip twice (game title + the VTS12 "Watch" reel), so placements must be compared by **footage** (fingerprint correlation ≥ 0.85), not by location. On that basis:
  - the strict main matcher (chain length within 15 frames + equal window count + timing + direction, unique match) is 18/21 correct;
  - death placement via failure links is 29/38.
  - The strict matcher now runs as **pass 3** in `build_ld_to_dvd.py` (`via: dvd-logic`, `confident: false`), +6 clips. The death walk still adds no new clips.
- `data/ld_to_dvd.json`: **237 of 347 clips, 44,260 / 66,467 LD frames (66%)**. A better route to the rest: follow the DVD graph's success/failure links from *placed* segments to neighbouring segments, and use segment lengths and windows jointly. Or a human pass with a contact sheet.

### DVD Time Map stills (VTS05 PGC5)
20 stills: 10 2D + 10 3D. Each shows a combination of completed eras (dimmed) and a lit option (e.g. stills 6–8 dim 50,000 B.C., 2173 and 1998 and light the Trader, 1350 and the glyph era in turn). The DVD covers only 10 of the ROM's 44 map states, so there is no complete per-state mapping. The DVD-era map screen used still 0 as a background (`tools/legacy/export_map_still.py`); the laserdisc build plays the ROM's own map stills.

### Next intake steps
1. Fingerprint-match `HTT/tt/htt2NN*.avi` against the game MKVs to place each PC scene, then link to ROM LD frames → `data/ld_to_dvd.json`.
2. Confirm frame-exact sync between `_a1` and `_a2`.

## Still wanted

Nothing for video: the DVD is in hand (see above). The PC AVIs remain a cross-check only.
