# Time Traveler (Sega / Virtual Image Productions, 1991) — ROM & hardware findings

Ground truth for the project. Every line is either cited (MAME source in `reference/mame/`) or confirmed in the ROM disassembly at the address given. Anything else is marked UNVERIFIED or hypothesis.

## Sources in `reference/mame/`

Pulled from mamedev/mame master @ `0fed553720ab` on 2026-10-06. License: BSD-3-Clause (header of each file).

| File | Upstream path | What it gives you |
|---|---|---|
| `timetrv.cpp` | `src/mame/sega/timetrv.cpp` | Board memory map, I/O map, inputs, DIPs, ROM/CHD hashes |
| `timetrv.lay` | `src/mame/layout/timetrv.lay` | Cabinet artwork/LED layout |
| `ldv4200hle.cpp/.h` | `src/devices/machine/` | Pioneer LD-V4200 command parser: the protocol the game ROM speaks |

## Hardware (from `timetrv.cpp` — cited, not inferred)

- CPU: Intel 80188 @ 16 MHz XTAL. Ghidra language: `x86:LE:16:Real Mode`.
- Program ROM: 27C020, 256 KB, mapped at `0xC0000–0xFFFFF`. Reset vector is therefore at `0xFFFF0` (file offset `0x3FFF0`).
- RAM `0x00000–0x0FFFF` (IVT + work RAM). 2816 parallel EEPROM at `0x10000–0x107FF` (settings/bookkeeping).
- I/O space:
  - `0x1000–0x1003` i8255 PPI1: PA = IN0 (joystick, starts, coin, bill); PB = IN1 (Attack `0x10`, Reversal Cube `0x20`, Service `0x40`); PC out = lamps (bit3 2P start, bit4 1P start, bit5 cube lamp, bit6 coin "bip").
  - `0x1080–0x1083` i8255 PPI2: PA/PB/PC = DSW1/2/3.
  - `0x1100–0x1107` NS16450-compatible UART (P82050) → laserdisc player.
  - `0x1180–0x1187`, `0x1200–0x1207` 16-char 14-segment LED display (char codes 0x00–0x3F, ASCII-offset by 0x40).
  - `0xFF80–0xFFFF` 80188 peripheral control block. Interrupts come from the internal timers. Timer0 output drives a 1-bit DAC (beeps).
- No video generator on the board. All picture comes from the laserdisc. The overlay `screen_update` returns 0.
- Two sets: `timetrv` (ROM `tt061891.bin`, CRC a3d44219) and `timetrv2` Japan (`epr-72491.u9`, CRC c7998e2f). Each has its own laserdisc CHD hash.

## Laserdisc protocol (from `ldv4200hle.*`)

- Serial 4800 baud. ASCII commands, terminated with CR (`0x0D`), numeric argument *precedes* the two-letter opcode (e.g. `12345SE\r` = search to frame 12345).
- Opcodes the game logic will lean on: `SE` search, `PL` play, `ST` still, `PA` pause, `SM` stop marker (play until frame N), `FR` frame-number mode, `MF`/`MR` multispeed, `AD`/`VD` audio/video control, `?F` request current frame, `CL` clear.
- DSW3 bit4 selects Pioneer LD-V4200 vs Sony LDP-1450 protocol. MAME only emulates the Pioneer path. In Ghidra, expect two command-emitter routines keyed off that DIP. Only the Pioneer one has a reference implementation.

## ROM findings — pass 1 (2026-10-06, Capstone; regenerate with `make disasm` → `build/capstone/`)

ROM `timetrv.zip/tt061891.bin` verified: CRC32 a3d44219, SHA1 7c5003b6…f0dfb4 (matches MAME `timetrv`).

**Layout**
| Range | Contents |
|---|---|
| `C0000–C52E6` | code (CRT start at `C000:0000`) |
| `C52E7–CE54F` | far const data: attract text tables, input windows, episode nodes, clip table |
| `CE550–DBE0F` | code (C modules) |
| `DBE10–DDxxx` | DGROUP initial image (0x1C90 bytes) → copied to RAM `01EB:0000` (lin `0x1EB0`) |
| `DE000–FFFEF` | empty (0x00) |
| `FFFF0` | reset: `UMCS=0xC03C`, `ljmp C000:0000` |

**Toolchain:** Microsoft C, large model (far calls, `push bp / mov bp,sp / sub sp,N` prologues). Source path `c:\mt\src\`. Embedded `__FILE__`/`__DATE__`, 15 modules: attract, chorder, episode, asset, input, specobj, clock, sound, storage, pioneer, display, learn, chapter, map, account (build dates Apr 25 – Jun 18 1991).

**Serial / laserdisc (confirmed in code)**
- `D30C:0002` uart_init: LCR 0x83, divisor 10, LCR 0x03 → 768 kHz/16/10 = 4800 8N1 (agrees with MAME).
- `DB9B:014A` = `inp`, `DB9B:0158` = `outp`. All port I/O goes through these, which is why a naive `in`/`out` scan misses the UART.
- Global `g[0x151A]==1` selects **Sony** byte protocol; otherwise **Pioneer** ASCII.
- `D37E:03AB ld_search_play_to(start, stop)`: Pioneer `"<start>SE\r"` (wait, timeout 600) then `"<stop>SMPL1VD\r"`. `start == -1` skips the search. 14 callers.
- `D37E:0F2B ld_audio(mask)`: `"<mask>AD\r"`, bit0 = ch1, bit1 = ch2.
- Full name map in `re/ghidra/scripts/TimeTravelerLabels.java`.

**Game data extracted (`data/`, regenerate with `make tables`)**
- `clips.json` / `clips.csv`: master clip table. **Corrected in pass 2** (see below): it starts at `CC1A2` (`C52E:6EC2`), not `CC1A0`. Pass 1 was 2 bytes off and attached each record's attribute word to the next clip.
- `specobj_clips.json`: 28 system clips at DGROUP `0x117A` (ROM `DCF8A`), `{key, audio_mask, start, stop}`, played by `D47D:1D6C`. Pairs share frames and differ only in audio channel, which suggests a per-player (1P/2P) audio track.
- Hardcoded ranges in code, e.g. `C2EA4` plays 2146–3472 (likely attract), `CEDF:0764` plays 27764–27959.

## ROM findings — pass 2 (2026-10-06, Capstone listing + Ghidra)

**Const data segment.** All far tables in `C52E7–CE54F` are addressed as segment `C52E` (lin `C52E0`). Code builds far pointers on the stack: `mov [bp-4], OFF; mov [bp-2], 0xC52E`. No table address is stored as data. Targets found this way:

| Linear | `C52E:` | Used by | Status |
|---|---|---|---|
| `C52E8` | `0008` | `D47D:009A` | not decoded |
| `C5AA4` | `07C4` | `CFD9:0002` | **window table**, decoded |
| `C801A` | `2D3A` | `D047:000C` | **action table**, layout decoded |
| `C8296` | `2FB6` | `CE55:02F5`, `CE55:0341` | **input rule table**, layout decoded |
| `CB0D6` | `5DF6` | `C4B6:0035`, `C4B6:0081` | **asset table**, decoded |
| `CC1AA` | `6ECA` | `C4B6:00CB` | **clip table** (scan starts at id 1), decoded |
| `CCC8A` | `79AA` | `CE55:059F` | **move table**, decoded |
| `CD122` | `7E42` | 14 sites in `C359`, `C40B`, `D47D`, `D668`, `D70D` | **episode table**, layout known, fields not |

**Clip table** `CC1A2–CCC81`, `C52E:6EC2`. 348 × `{u16 id, u16 start_frame, u16 stop_frame, u8 kind, u8 audio_mask}`. Word 0 at `CCC82` ends it.
- `C4B6:00CB clip_find(id)`: starts at `C52E:6ECA` (id 1, so record 0 `{0,0,0,0,0}` is never matched), compares word +0, stride 8, stops on 0. Returns a far pointer or 0.
- `C4B6:020B clip_play(clip*, offset)`: `ld_audio(byte +7)` (`D37E:0F2B`), so **byte +7 is the LD audio-channel mask** (bit0 ch1, bit1 ch2). Exception: `ld_audio(0)` (mute) when a per-player word `== 0x2710` and `g[0x432] == 1`. Byte +6 is compared with 1/2/3 to set a timer word (`0x50`/`0x64`/`0x12C`), then `ld_search_play_to(start + D245:080A(offset), stop)`.
- Values: kind = 1 in all 347 real records. audio_mask: 3 (both) ×336, 1 (ch1) ×8, 2 (ch2) ×3. Answers the pass-1 open question: the "type high byte = audio mask" guess was right, but it applied to the previous record.

**Asset table** `CB0D6–CC19D`, `C52E:5DF6`. 358 × `{u16 asset_id, u16 type, u16 ref, u16 lo, u16 hi, u16 flags}`, key 0 ends → `data/assets.json`.
- `C4B6:0035 asset_find(id)`: first record with that id. `C4B6:0081`: returns 1 if record count < `0x1C2` (450).
- `C4B6:03FC asset_play_at(t)`: walks an asset's records and takes the one with `lo <= t` and (`t < hi` or `hi == 0`). Sets a per-player seen flag at `[0x1598 + idx + 0x1C2*player]`, then calls `asset_dispatch(type, ref, t - lo)`.
- `C4B6:0158 asset_dispatch(type, ref, off)`: type 1 → `clip_play(clip_find(ref), off)`; type 3 → `D47D:00F3(ref)`; type 4 → `DA9D:0002()`.
- Data: type 1 ×347 (ref = clip id 1–347, one per clip); type 3 ×11. lo = hi = 0 in every record, flags = `0x0800` in every record. Asset ids 1–351, 11 ids with a second (type 3) record.
- **LED messages.** `D47D:00F3 show(id)`: `D47D:009A` finds the first record with that id in the table at `C52E:0008`. Records are 10 bytes `{u16 id, u16 mode, u16 text_off, u16 text_seg, u16 time}`; 197 records end at `{0, 0}`. The further records with the same id are the message's next lines, and `D47D:015D`/`01A5` show each. `text_seg` 0x01EB is DGROUP, so the text sits in the ROM image at `DBE1:0000`. Reader: `tools/extract_tables.py` → `build/led_messages.json`. It goes to `build/` because it is the ROM's own wording.
  - Call sites → ids:
    - status 0x0E (game start, cube use, lost life, chapter);
    - death 0x8D (`D047:0626`);
    - Hellgate 0x17 "TO PLAY / PULL JOYSTICK", results 0x19/0x1B/0x1A/0x1C/0x1E/0x123 (`D0F0`);
    - Trader 0x41 (`CEDF:07B4`);
    - map 0x44 "TO PICK TIME ERA / PUSH BUTTON" (`D843:07AC`);
    - "YOU SOLVED THE <YEAR>" 0x3F (`D70D:0289`/`0361`);
    - game over 0x3D/0x3E (`D7C0`);
    - attract 0x0C (`C2BA:0371`);
    - coin/credit 0x0D/0x83/0x10–0x12/0xB5 (`D0F0`).
  - Scene records show their type-3 asset's message when they start (`C4B6:0158`): 0x40 "TIME WARP MALFUNCTION" (8 scenes), 0x42 "YOU WON", 0x43 "TO BUY CUBES / SEE THE TRADER", and 0x0B (hall of fame) on attract E20.
  - Fields like `<CUBES>`, `<SCORE>`, `<YEAR>` and `<HSCORE1>` are filled in at display time.

**Episode table** `CD122–CE541`, `C52E:7E42`. 368 × 7 × u16; record with `+6 == 0xFFFF` ends it → `data/episode_table.json` (raw `f0..f6`).
- `C40B:0004`: counts records, returns 1 if < `0x1C2`.
- **f0 = chapter, f3 = episode.** `C40B:04D0` (test-mode episode picker) formats `"C" itoa(f0) " E" itoa(f3)` from DGROUP `0x195` (`"C"`) and `0x197` (`" E"`) and shows it, stepping ±1 record with the joystick and skipping records with f1 = `0x2713` (10003) or `0x7D0` (2000).
- **f1 = record kind.** Counts: 10003 ×234, 1005 ×58, 1000 ×30, 2000 ×17, 10000 ×12, 1001 ×10, 1006 ×7. 10003 and 2000 are sub-records that follow an episode header (never selectable in `C40B:04D0`). 10000 appears only in chapter 10000.
- `C40B:0482 episode_find(ep)`: first record whose f3 equals the argument (f3 is not globally unique; it's unique among headers of a chapter only. UNVERIFIED).
- `C40B:004F maybe_episode_next()`: walks forward from the player's current record (per-player far pointer at `[player*4 + 0x0A]`, player index `g[0xC3C]`) to the next header with f0 == current chapter (per-player chapter list `[player*0x16 + 0x40 + 2*idx]`, idx `[player*2 + 0x3A]`). Skips headers already flagged in a per-player byte array `[player*0x1C2 + rec_index + 0xA0]`. Otherwise asks `D668:05BB(rec)` whether to take it, then flags it. Episodes 20 and 21 (`0x14`/`0x15`) get special handling via `D70D:0917(1, 0x23)` when `g[0x72] >= 2`.
- **f4 == f5 in all 368 records**, an asset id.
- Hypotheses from the data (were UNVERIFIED; resolved by the decode below and ROM-vs-sim parity: f2 = weight, see "Per-game episode selection"; 10003 = death branches, see "Built-in actions"; chapter 10000 = attract/system, see "Chapters in the episode table"):
  - f2 on headers = selection weight in %. Most are 100. Alternatives come in pairs summing to 100 (ch1 ep35 = 55, ep36 = 45).
  - 10003 sub-records numbered 1..N = the episode's death clips (1–3 s clips just before or after the main clip on the disc).
  - f6 on sub-records packs two bytes (e.g. `0x4F20`, `0x6320`, `0x4F39`, `0x6339`): which input or window leads to that death. f6 on headers (435, 485, 500, 550) may be a frame offset or a score.
  - Chapter 10000 = system/attract clips. Chapter 0 = intro episodes.

**Input model** (all four tables → `data/*.json` via `make tables`).

*Window table* `C5AA4–C800F`, `C52E:07C4` → `windows.json`. 958 × `{u16 id, open_a, close_a, open_b, close_b}`, id = index + 1 (`CFD9:0002 window_find` uses `id*10 + 0x7BA` as a fast path, then falls back to a scan). Values are frame offsets inside the playing clip (window 1 = 0–1517 = all of clip 1). `CFD9:0249 window_is_open(rule, t)` (read from the instructions; Ghidra's decompile drops a branch): when the per-player flag `[p*2+0x13C4] == 1` the window is `+6..+8` (variant b), otherwise `+2..+4` (variant a). Variant b is sometimes tighter (window 501: a = 271–302, b = 271–298). Variant b closes earlier than a in 223 windows (later in 5), so b is the harder one; the difficulty DIP decides when it applies (see "Difficulty" below).

*Input rule table* `C8296–CB0C5`, `C52E:2FB6` → `input_rules.json`. 1,478 × `{u16 key, u16 move, u16 window, u16 action}`, key 0 ends, count < `0x5DC`. `CE55:02F5 rule_find(key)` gives the first rule. `CFD9:0082 input_check_rules` walks consecutive rules with the same key. For each rule whose window is open it:
1. resolves `move_find(move & 0x7FFF)` and the window;
2. skips windows this player already cleared (per-player byte array `[player*0x5DC + window_idx + 0x43E]`);
3. calls `move_match(device, bits, move)`;
4. on a match, `rule_action(rule, 1)`. Returning 1 marks the window cleared, 2 returns at once. On no match, `rule_action(rule, 4)`.

Bit 15 of `move` = stop scanning this key after a miss. Key 0xFFFF ×105, the rest 1–351 (episode or asset ids, UNVERIFIED which).

*Move table* `CCC8A–CD119`, `C52E:79AA` → `moves.json`. 146 × `{u16 move_id, u16 mode, u16 device, u16 bits}`, 47 move ids. `CE55:03CC move_match(device, bits, move*)` accepts device ∈ {1, 4, 0x10, 0x20}:
- mode 1: device equal and `bits & row.bits`;
- mode 2: any row of this move id matches;
- mode 3: every row in order, each within a timer (`D245:04B1(t, 2)`). This is the "double move".

Data: device 1 rows use bits 1/2/4/8 (directions?), device 16 rows use `0x100`/`0x200` (Attack/Cube?). The mapping of logical bits to IN0/IN1 hardware bits was UNVERIFIED; settled by the "Input bits" table below (`D17E:02D5`, joystick port read at `D0F0:0A0A`) and ROM-vs-sim parity.

*Built-in actions* (`D047:00E6`, jump table at `D047:026A`, decoded from the capstone listing because Ghidra fails on it). `code = N<<12 | hi6<<6 | lo6`. Phase bit 1 = matching input inside the window (`CFD9:0082`). Phase bit 2 = window closing uncleared (`CFD9:0316 window_expire_check`, which fires only for NO-MOVE rules and nibble-6 actions).

| N | Phase 1 (input) | Phase 2 (window closed) |
|---|---|---|
| 2 | if hi6 == 0: beep + score | if lo6 == 0: score |
| 3 | — | if low12 == 0: score |
| 4 | if low12 == 0: beep + score | — |
| 5 | beep + `D7C0:0008` | — |
| 6 | beep + death(hi6) | death(lo6) |
| 7 | beep + death(low12) | — |
| 8 | — | death(low12) |

beep = `DA9D:0002` (tone `0x9C4`, length 4). score = `D047:0761`: adds the current episode record's f6 to the player's 32-bit score `[p*4 + 0x101C]`, ×2 while score ≤ 90,000, ×1 up to 100,000, ×10 above. So **episode f6 = points**. death(n) = `D047:04F2`: flags the episode failed (byte 2 at `[p*0x1C2 + idx + 0x102C]`), `C40B:0753 death_find(n)` (next kind-10003 record after the header with f3 == n), then `C40B:08A8 episode_enter(death)` and message `0x8D`. So **kind-10003 sub-records are death branches, numbered by f3**.

*Episode entry* `C40B:08A8 episode_enter(rec, t)`: saves the previous record at `[p*4 + 0x1582]`, makes `rec` current, `asset_play_at(t)` on `asset_find(rec.f5)` (`C40B:0AB2`), arms `rule_find(rec.f4)` (`C40B:0A45` → `CE55:038B`). So **f4 = input-rule key, f5 = asset id** (equal in every record, different roles).

*Input bits.* `D17E:0121` and `D17E:0002` poll the PPI and keep an inverted image in word `g[0xC34]`; `D17E:02D5 input_bits(device)` reads it back. Combined with MAME's port names (`reference/mame/timetrv.cpp:210-224`):

| device | bits in `g[0xC34]` | source | names |
|---|---|---|---|
| 1 | `0x01 0x02 0x04 0x08` | IN0 bits 0–3 | Up, Down, Left, Right |
| 0x20 | `0x10 0x20` | IN0 bits 4–5 | Start1, Start2 |
| 4 | `0x40 0x80` | IN0 bits 6–7 | Coin (+1 credit), Bill (+`g[0x8A]` credits) |
| 0x10 | `0x100 0x200` | IN1 bits 4–5 | Attack, Reversal Cube |

Move 9 = Attack *or* Cube. Most "double moves" in the data are two overlapping windows (direction, then Attack 1–5 frames later), not mode-3 sequences. `tools/show_episode.py [chapter [episode]]` prints every episode with its windows, moves and outcomes.

*Action table* `C801A–C8289`, `C52E:2D3A` → `actions.json`. 52 × `{u16 code, u8 phase, u8 argc, u16 fn_off, u16 fn_seg, u16 arg1, u16 arg2}`. `D047:0058 action_do(code, phase)`:
- `code & 0xF000 == 0`: run every table row with that code and phase through `D047:0284`, which calls the far function pointer.
- Otherwise: built-in `D047:00E6`. Ghidra fails to decompile it; codes seen in rules: `0x4000`, `0x6041`, `0x8001`, …
- Phases: 1 and 2 appear in the table. Rules call phase 1 on a match and 4 on a miss.

Example: codes 150–153 → `D37E:0F2B ld_audio` with arg 0–3. Function segments used: `D047`, `DA9D`, `D0F0`, `C40B`, `C2BA`, `D37E`, and `D659`. `D659` isn't in the pass-1 segment list; resolved: it is the learn module (see "Source modules").

**Operator settings** (`C2BA:0470 dips_read`, called at boot). Programs the PPI2 control port (`outp(0x1083, 0x9B)`), then reads DSW1/2/3 (`0x1080–0x1082`) into a 24-bit word: `g[0x2E]` = DSW1, `g[0x2F]` = DSW2, `g[0x30]` = DSW3. `C2BA:089E dip_field(mask_lo, mask_hi)` returns the masked field shifted down. Results, with MAME labels (`reference/mame/timetrv.cpp:226-285`):

| Global | From | Value | MAME label |
|---|---|---|---|
| `g[0x13C0]` | DSW1 `0x03` | field + 1 | Coinage |
| `g[0x1592]` | DSW1 `0x0C` | field + 2 (2–5) | **Lives** ✓ |
| `g[0x80]=30`, `g[0x2A]=-40`, `g[0x13CC]` = lives − field, `g[0x1000]` = DSW1 `0xC0` field + 1 | DSW1 `0x30` | only when field != 3 | MAME: SW1:5–8 *unused*. The ROM reads them. Meaning UNVERIFIED |
| `g[0x13BC]` | DSW2 bit 7 | 0/1 | not in MAME |
| `g[0x1926]` | DSW2 bit 4 | 1 = on | **Level Select**. Gates `episode_test_select` (`C40B:04D0`) in `maybe_episode_next` |
| `g[0x82]` | DSW3 bit 0 | 1 = free play off? (only when level select is off) | Free Play |
| `g[0x432]` | DSW3 bit 1 | 1 when bit clear | Demo Sounds **Off** → `clip_play` mutes LD audio in attract |
| `g[0x1B28]` | DSW2 bit 0 | 6 or **40** | Max Reversal Cubes "6 / 36". The ROM value is 40 (`0x28`); MAME's "36" label disagrees |
| `g[0x88]`, `g[0x1928]` | DSW2 `0x02`, `0x0C` | field + 1 | Reversal Cube Cost |
| `g[0x38]` | DSW2 `0x60` | 0 / 1 / 2 | **Devil Behavior**: can take lives / never / not in game |
| `g[0x1C]`, `g[0x2C]` via `C2BA:079D` | DSW3 `0x0C` | 0→(5,3) 1→(6,4) 3→(8,5) 2→(11,7) | Difficulty (0 Very Difficult … 2 Easy, 3 Medium). Decoded: `g[0x2C]` = good episodes before hard windows (3/4/5/7), `g[0x1C]` = revert threshold; see "Difficulty" |
| `g[0x151A]` | DSW3 bit 4 | 1 when clear | LD protocol: 1 = Sony ✓ |
| `g[0x8A]` | DSW3 `0xE0` | field 5 → 10, else field + 1 | Bill Multiplier. The ROM gives 0x40 → 3 and 0xA0 → 10; MAME lists 0x40 as "10". MAME label likely wrong |

**Episode flow** (partial).
- `D047:03E5(1)` (table action 20 at the end of every NO-MOVE clip, and action 30 phase 2 at the end of every death clip) → `D7C0:0008 episode_done`.
- `episode_done`: marks the current header cleared (byte 1 at `[p*0x1C2 + idx + 0x102C]`; 2 = failed, set by `episode_fail_to_death`). In chapter 1, finishing ep 30 also clears ep 20 and 21, and finishing ep 21 clears ep 20 (20/21 are identical clips; 21 has points 0, so 21 is likely the retry copy). Then `D7C0:0616` (chapter complete?) and `D7C0:02D0` (advance). Not yet read when written; chapter flow is now decoded ("Chapter order and Time Map", "Game over and the mercy life").
- **Reversal Cube** = action 30 phase 1 (input during a death clip) → `D047:0679`: re-enter the saved previous episode `[p*4 + 0x1582]` via `episode_enter(prev, max(0, g[0x16] − D245:07D8(3)))`. That restarts the failed scene slightly before the point of death. Where the cube count is checked and decremented: answered below (`CE55:00C9`, "Lives, cubes").

**Per-game episode selection** (odds/chorder, segment `C359`).
- `C359:0034 game_choose` (per player, at game start):
  1. `bonus = rand() % 4 + 2` → `[p*2 + 0x1578]` (a chapter from 2–5);
  2. `C359:04B1` clears the per-player chosen (`+0xA0`) and status (`+0x102C`) arrays;
  3. `C359:00B6 choose_episodes`;
  4. `C359:0620 choose_rare(bonus)`.

  `rand` = `DB9B:023C` (C runtime), and `C359:0000 chance(p)` = `rand() % 100 < p`.
- `choose_episodes` walks the episode table once and sets chosen = 1 by kind (f1):
  - 10000: always.
  - 1005: `chance(f2)`.
  - 1006: only if `f0 == bonus` (`C359:056E`), then `chance(f2)`.
  - A run of consecutive 1000/1001 headers in one chapter: **exactly one** is picked by weight. Cumulative f2 is compared against `rand() % 100`; a 1001 also requires `f0 == bonus`, else the pick falls through to the run's last 1000.
  - f1 < 101: chance(f1) for a block, skipping the block on failure (not present in this ROM's data).
  - 10003 / 2000: never.
  - With level select on (`g[0x1926]`), every header is chosen.
- **So f2 = selection weight in %.** Kinds 1001/1006 (chapters 2–5) are bonus-chapter-only extras.
- `choose_rare(bonus)` → `data/substitutions.json`. With 50% chance it does nothing. Otherwise it repeatedly scans 8 groups at DGROUP `0x00CA`. Each group gets a 3% chance per pass, and the lead must not be in the bonus chapter. The first hit switches the lead episode in and the group's other episodes out (e.g. `C2 E43` replaces `C2 E42`; `C5 E23` replaces `C5 E21` and `C5 E20`). Uses `D70D:0917(chapter, episode)` = find header.
- `D668:05BB` (asked by `maybe_episode_next` for unchosen headers): only C6 E50, offered when C6 E22 was chosen, C6 E30 or E40 was chosen, none of those three failed, and `chance(80)`.

**Lives, cubes, Trader, Hellgate.**
- *Lives* per player `[p*2 + 0x424]`, set from the DIP value `g[0x1592]` at game start (`D668:012B`). `DA9D:04AE lose_life`: `lives--` unless the current chapter is 0 (intro) or level select is on. After a death, `D7C0:06A5` calls `lose_life` **only if the failed episode's points (f6) are non-zero**, so deaths in zero-point episodes are free. Then game over when lives < 1 (`D7C0:0616` / `D7C0:0791`, not fully read).
- *Cubes* per player `[p*2 + 0x1516]`.
  - Game start: table action 240/250 → `DA9D:0F3A` sets cubes = 1.
  - **Use**: `CE55:00C9 input_event(device, bits)`. Cube (`0x200`) pressed while a death is playing (`g[0x100A] == 1`, set by `episode_fail_to_death`) with cubes > 0 → `cubes--`, then rewind (`cube_rewind`, previous section).
  - Lamp: `D17E:0858` drives PPI1 port C bit 5 (`outp(0x1002)`) on while cubes > 0 (MAME: cube lamp).
  - Max: `g[0x1B28]` (6 or 40).
- *Trader* = `CEDF:0719` (in specobj's segment). While cubes < max it loops LD 28053–28188 (`0x6D95–0x6E1C`) on audio ch1 and waits for a coin or button. At max it plays the same segment on ch2. Purchase `CEDF:0D80` → `CEDF:0DF5`: every `g[0x88]` credits (DIP) buys `g[0x1928]` cubes; setting 4 alternates 2/1 cubes per credit. Credits `g[0xC3A]`. **Arcade cubes cost coins.** The home versions' Trader rule comes from the manual, not the ROM.
- *Hellgate* = `D0F0:025E` (choice segment), reached via table action 40 (`D0F0:023A`).
  1. Message `0x17`; plays LD 23651–23980 (`0x5C63–0x5DCC`). After `D245:081F(0x7F)` (127 clock units, ~4.2 s), `wait_input(0x11, 301)`. Only a joystick event with code 2 (**Down**, the slot-machine lever) accepts (`D0F0:02B2-02FA`). Otherwise it declines.
     - Checked with `tools/romdiff.py --hellgate`: Down at +5 s starts the roll at once.
     - Five runs with different start ticks gave +life (24505, +1 life, +25,000), lose (25003 ×2, −1 life, −25,000), +2 cubes (24340, +25,000), and devil takes all (25164 then 25325, game over).
     - Each was followed by 24669. All match the table below.
     - Decline: any event other than Down declines (`D17E:039E` with 0x11).
  2. On accept: beep, `r = rand() % 100`. With "devil can take lives" (`g[0x38] == 0`) the thresholds are 45/55, otherwise 30/35:

| r | LD frames | Result |
|---|---|---|
| < 5 | 24187–24337 | **Jackpot**: credits += coinage (a free game), score +25,000, message `0x19` |
| < 45 (30) | 24505–24667 | **+1 life**, score +25,000, message `0x1B` |
| < 55 (35) | 24340–24501 | **+2 cubes**, score +25,000, message `0x1A` |
| < 60 | 24841–24999 | Lose: −1 life and score −25,000 (floor 0) if the devil can take lives; else message `0x123` only |
| < 90 | 25003–25163 | Same as above (life taken only if lives > 0) |
| < 100 | 25164–25323, then 25325–25516 | **Devil takes all lives** (lives = 0, score −25,000) if allowed; else message `0x123` |

  3. Afterwards: LD 24669–24838 on audio ch1 if won, ch2 if lost. Devil takes all has no after-clip.
  4. After a jackpot with the devil allowed, a second offer plays (LD 23982–24184) after a 117-unit wait, with a 170-unit input window. **Accepting it always ends in "devil takes all lives".**
- Score floor: subtracting 25,000 from a score below 25,001 sets it to 0.

**Trader and Time Map, measured on the ROM** (2026-10-08, `tools/romdiff.py --trader / --map-keys`, DIPs 06,04,7E).
- **Trader flag** `[p*2+0x1B24]`: 0 = Trader position on the map, 1 = not this visit, 2 = used for good.
  - Game start: 0 (`D668:01A7` → `D843:0AD9`).
  - Map entry (`D843:0127-0241`): if the current slot `[p*2+0x3A]` is 5, the chapter in that slot is completed (`D70D:0973`), the flag isn't 2 (`D843:0AFE`) and cubes < max, the Trader runs on its own (`D843:01B3`, arg 1) and the flag becomes 2. Then, unless the flag is 2, it is set to 0 if cubes < max, else 1.
  - The map skips the Trader position (chapter −1) unless the flag is 0 (`D843:098C`). Picking it (Attack, `D843:05E3`) runs the Trader (arg 0), sets the flag to 1 unless it is 2, and the map carries on.
- **Trader timeline:**
  - LD 27764–27959, audio ch2 when arg 1 and ch1 when arg 0 (`CEDF:0735`).
  - Then the still 28039 for 244 ticks.
  - Then 27961→28014 at normal speed and back (`60SP MR`), three times, muted.
  - After ~1296 ticks the map follows.
  - Attack does nothing. Each **coin** (IN0 bit 6) buys `g[0x1928]` cubes (+2 here) and plays 28053–28188. The buying stops at the maximum.
- **Map cursor:**
  - Opens with `<still>SE ST` on option 1.
  - Any joystick direction steps to the next option and reverses at the ends. Taps R, L, D, U, U from option 1 visit options 2, 3, 4, 3, 2.
  - Stepping on plays the highlight forward to the new option's first still frame (`PL`). Stepping back scans backwards at 120 to its last frame (`120SP MR`).
  - Attack picks.
  - With no input the current option is taken after 1566 ticks; 1437 ticks after the last move. After the automatic Trader it took 663 ticks (not explained).
- **Harness fix:** `tools/romrun.py` now models the LD-V4200 as MAME does:
  - marks at value + 1, crossed in either direction;
  - multispeed reverse (`MR`).
  - The Time Map hung without it. The 46/46 and 15/15 checks still pass.
- **Hellgate second offer:** after a jackpot with the devil allowed, accepting the second offer (LD 23982–24184, 117-unit wait, 170-unit window) always takes all lives and 25,000 points, with no after-clip. Any event other than Down declines. Now in `tools/sim.py` and `engine.c`.

**Hall of fame** (`DA9D:064D`, from `game_over`).
- Rank: the score is compared with the top-3 entries (`D945:01DD`), from 3rd place up.
- If it places:
  - `ld_audio(2)`, then LD 47768–48040 plays.
  - The LED messages scroll in turn: "CONGRATULATIONS", "PLAYER1"/"PLAYER2", "WELCOME TO THE", "HOLOGRAM", "HALL OF FAME.", "PLEASE ENTER", "YOUR INITIALS", "FOR ACHIEVING", "1ST"/"2ND"/"3RD", " PLACE." (DGROUP 0x16AF–0x172B).
- Initials: three letters, each starting at 'A' (` AAA  `).
  - Input event `[0x13D4] == 1` (joystick) with `[0x13D6] == 1` → letter + 1, `== 2` → letter − 1, wrapping through 26 letters (`DA9D:0ADF`/`0B13`).
  - `[0x13D4] == 0x10` (Attack) confirms (`DA9D:0B62`).
  - `0x20` or `4` aborts.
  - The letter blinks: one step per `wait_input` (`D17E:039E`) timeout of 15 clock units = 0.5 s (the clock counts 30 per second, `D245:07D8`). 12 steps without input end the entry (`DA9D:0A0C`).
  - Joystick events carry `~inp(0x1000) & 0xF` (`D0F0:0A0A-0A21`), so code 1 = Up and 2 = Down (MAME IN0 bits 0/1, `timetrv.cpp:211-212`). Device 0x10 carries the button bits `0x300` (`D0F0:0985`), and 0x20 carries IN0 `0x30` (Start buttons, `D0F0:09EF`).
  - "FUK" is replaced by "YOU" (`DA9D:0BC8`). The entry is stored with `D945:01AC`.
  - Correction (2026-10-12): the score is inserted as "TEN" (DGROUP 0x16AB) before the initials (`D945:0232`). A timeout or abort keeps it; the first confirm makes it "AAA" (0x173A). An all-blank name becomes "YOU" (0x1742).

**Chapter order and Time Map.**
- Per-player chapter list: 11 words at `[p*0x16 + 0x40]` (RAM far segment `0x37`, `g[0x1966]`), current slot `[p*2 + 0x3A]`.
- `C3CE:0055 chapter_order_init`:
  - clears the list (`C3CE:0368`);
  - fixed slots: 0 = 10000 (system), 1 = 0 (intro), 2 = 1 (1873), 8 = 7, 9 = 8, 10 = 20;
  - slots 3–7 filled at random from a slot × chapter % table in ROM segment `D372` (`g[0x196C]`), at `D372:0060 + 2*(slot*5 + chapter)`; `D372:0064 + …` = "slot has candidates"; `C3CE:02AE` = chapter not yet placed;
  - table values: slots 3–6 → chapters 2, 3, 4, 5 at 25% each; slot 7 → chapter 6 at 100%. So the default order is 1873, then a random permutation of 2–5, then 6, 7, 8, 20.
  - DSW2 bit 5 also sets `[p*2 + 0x157C] = rand() % 5 + 2` (else 0). Use UNVERIFIED.
- `D70D:0021 chapter_advance` (after a chapter):
  - **The next slot is the current slot + 1 only when the chapter just played is completed (`D70D:0973`). Otherwise the same slot is played again** (`D70D:0077-0094`).
  - Chapter 0 always moves on (`D70D:0118`). After chapter 20 the game is over (slot 0, `D70D:0179`).
  - If the slot to play holds a chapter > 7 it is played (a replay of chapter 8 if incomplete). If it holds 7, `D843:0A3D` runs first.
  - Otherwise, when the chapter just played is completed, message 0x3F "YOU SOLVED THE <YEAR>" shows, and the **Time Map** `D843:00B9` runs; its result overwrites the slot. **An incomplete era therefore brings the map back for the same slot.** The map still offers that era, cursor on option 1, and a revisit plays only the failed scenes (`D70D:06B7` with `D70D:0A84`).
  - Seen in `tools/romdiff.py --random 11/16`: C5 with E21/E60 failed → C5 again (E21, E60) → C3 (E23, E80 failed) → C3 (E23, E80) → C3 (E80) → C4. That is the first option of each map.
  - `tools/sim.py` / `engine.c` `run` follow this (they used to give an incomplete era its slot anyway).
- `episode_done` (`D70D:0C7A-0D60`): a completed C1 E30 also marks E20 and E21 done; a completed E21 marks E20 done.
- `D843:00B9 time_map`:
  - builds a bitmask of completed chapters (`D70D:0973(ch)`) plus the bonus chapter (`1 << (bonus + 8)`);
  - offers the Trader first if cubes < max (`D843:0AD9`; at slot 5 via `trader(1)`);
  - then drives an LD-still cursor loop over map positions (`D843:0889` picks the start entry, `D843:093B`/`D843:09B6`/`D843:07C2` step and test) and returns the chosen chapter (`ld_audio(3)` on accept).
  - **Options table `D843:0B33`** (lin `D8F63`, inside the map module's code segment) → `data/time_map.json`: 8-byte `{u16 key, u16 still_start, u16 still_stop, i16 chapter}`. Groups are separated by `{0, 1, 1, x}`; `{0, 0, 0, x}` ends it (lin `D944B`). `D843:0889` finds the first record whose key equals the mask built above: completed chapters 1–7 as bits, plus `1 << (bonus + 8)`. Each record is one cursor position. The LD still range is 5 frames of map art (26425–26904 and 38158–38242). `chapter` is the destination, and **−1 is the Trader**, skipped by `D843:093B` when `[p*2+0x1B24]` is set (Trader used) or `D843:0AFE` says cubes are full. 44 masks, 2–4 options each. The map also offers chapter 6 (and 7) before every era is done: mask `0x043A` (1, 3, 4, 5 done, bonus 2) offers {2, 6}.
  - Check: `tools/sim.py` now builds the mask at each era slot and takes options from this table. 0 lookups miss in 300 games, so the key layout is right.
- Chapters in the episode table:

| ch | headers | LD frames | points sum | role |
|---|---|---|---|---|
| 10000 | 12 | 688–49915 | 0 | system / attract |
| 0 | 9 | 1–2579 | 0 | intro (lives not lost here) |
| 1 | 19 | 1–26304 | 1970 | 1873, always first |
| 2–5 | 11–14 each | various | 3940–5430 | selectable eras |
| 6 | 11 | 38607–46028 | 4805 | always after 2–5 |
| 7 | 9 | 4255–46334 | 5840 | after 6 |
| 8 | 2 | 6520–46765 | 1425 | after 7 |
| 20 | 5 | 6000–53335 | 0 | ending (`romdiff --perfect` plays to it) |

  **Era per chapter** (2026-10-06). Each chapter's E10 clip (53–57 LD frames) is its era title card. Via `data/ld_to_dvd.json` they land in DVD title VTS05 PGC1 (the card reel), and the card frames read: **C1 = 1873** (manual p.2; C1 E10 is unmapped), **C2 = 1998**, **C3 = 2173**, **C4 = an era whose card is alien glyphs** (no year), **C5 = 50,000 B.C.**, **C6 = 2552**, **C7 = 1350**. C8 (2 episodes, after C7) and C20 (Hellgate and finale clips; LD up to 53335) have no card. Their roles are UNVERIFIED.

**Game over and the mercy life** (`D7C0` = branch).
- `D7C0:0616 maybe_mercy_open`: returns 0 (mercy available) when `[p*2+0x1594] == 1` (set at game start, cleared when the devil takes all lives) **and** the per-player countdown `[p*4+0x150E]` is non-zero **and** the counter `[p*2+0x1E] <= 6`. The countdown is loaded in `D70D:040E` from `g[0x1010]` (= 10 in `dips_read`) × 30 via `D245` (clock). Units measured (`romdiff --watch 0x150E`): 300 at chapter 0/1 entry, −1 per 1/30 s of play. The counter is reset at chapters 0/1 and incremented in `D7C0:02D0`, which runs after every successful `episode_done`, so it counts **episodes**, not chapters. (An earlier note said chapters; corrected.)
- `episode_done` (`D7C0:0008`) and `D7C0:038F`: when lives < 1 and mercy is available → `[p*2+0x34] = 1`, **lives += 1**, continue. Otherwise game over.
- `D7C0:0791 game_over_or_continue`: if the continue flag `[p*2+0x78]` is set (`D668:028E` copies `g[0x80]` into it at game start), `D0F0:0006` is asked. **`D0F0:0006` is a stub that returns 0** (raw bytes `B8 00 00`, `retf`), so this ROM never offers a continue. Otherwise, single player: `D7C0:052D game_over` (message `0x3D`/`0x3E` for player 1/2; compares the score with the high score via `D945:0779` and stores it via `D945:0797`; `DA9D:064D`; clears score words `g[0x101C..0x1022]`) then `D668:0406 back_to_attract` (re-flags chapter-10000 episodes, zeroes lives/cubes, rebuilds attract order via `C3CE:000E`). Two players (`g[0x72] >= 2`): `D7C0:038F` + `D7C0:068B(1)` hand over to the other player (`D668:036F set_player` sets `g[0xC3C]`). The survivor's final game over runs one hall of fame for both scores, higher first (`DA9D:064D`). `038F` plays the handover footage (`D7C0:04BA`: LD 15330–15450 / 15612–15732).

**Difficulty** (DSW3 bits 2–3 → `C2BA:079D` → `g[0x1C]` revert threshold, `g[0x2C]` harden threshold).
- `DA9D:0DE5` (from `D7C0:02D0`, after each episode completed without dying) increments the per-player good-episode counter `[p*2+0x42E]`. When it **equals** `g[0x2C]`, `DA9D:00D0` sets `[p*2+0x13C4] = 1`, and all windows switch to variant b.
- `episode_fail_to_death` (`D047:04F2`): if the counter is still `< g[0x1C]`, `DA9D:00F5` clears the flag (back to variant a). `chapter_next` (`D70D:040E`) also clears it when the chapter just played is 0 or 1 (corrected 2026-10-12; was "on entering"). Measured with `romdiff --watch 0x13C4`: set at C1 E35, cleared at the end of C1, never set again.
- Result per DIP value: 0 very difficult → hard after 3 good episodes, reverts on a death before 5; 1 difficult → 4 / 6; 3 medium → 5 / 8; 2 easy → 7 / 11. Since the counter only grows, hard windows become permanent after the revert threshold.
- Implemented in `engine.c` (`good_eps`, `hard`) and `fe_game.c` (window timing follows `hard`; difficulty in the settings menu).

**Two players.** `g[0x72]` = number of players, set by `D7C0:068B set_player_count(n)`. After a death that costs a life, `after_death` (`D7C0:06A5`) calls `D7C0:038F switch_player` when `g[0x72] >= 2`:
1. Mercy check as for one player.
2. If that player has no lives left: `game_over` (`D7C0:052D`); otherwise message `0x0E`.
3. Toggle `g[0xC3C]` (0 ↔ 1) and resume the other player's game via `D7C0:04BA`.

All per-player state above is indexed by `g[0xC3C]`, so each player keeps their own chapter order, chosen episodes, lives and cubes. **Players alternate after each life lost.**

ROM run (2026-10-08, `tools/romrun.py`, 2P Start, no input):
- P1 plays C0 (free deaths) and C1 to the E20 death (life lost, 126.4 s).
- P2 starts at C0 E10 and plays to its E20 death.
- P1 resumes at **C1 E35**. With two players the duel goes on to E35 after a death, not to E21 (`C40B:004F` → `D70D:0917(1, 0x23)` when `g[0x72] >= 2`).
- They then alternate on each lost life.
- P1 reaches 0 lives (game over) and P2 plays on alone to its own game over.

Port:
- `engine.c` gets `players` and a `life_lost` callback.
- The 3DS front end runs each player's game in its own thread (one at a time).
- `port/3ds/host/test_2p.c` reproduces the alternation above.

**The LED line** (2026-10-11).
- The cabinet has one 16-character line: ports `0x1180–0x1187` and `0x1200–0x1207`, MAME `timetrv.cpp:203-204`. `tools/romrun.py` captures it.
- Display module `D47D`:
  - `show(id)` (`D47D:00F3`) expands the record (`D47D:01A5`/`1646`/`0294`) into a 16-character buffer padded with spaces, setting mode `[0x1580]` and line time `[0x18]`.
  - The timer ISR's odd ticks count `[0x18]` down at 30 Hz and call `D47D:1280` on every other one (15 Hz, `D245:035E-0395`).
  - `D47D:1280`: mode 1 writes the next character, mode 2 the whole line, mode 8 scrolls. When the time is up, `D47D:107B` goes to the next record:
    - `{0,2,0}` restarts the message;
    - `{0,3,t}` is the special-object branch, otherwise a restart;
    - `{0,1}` ends it and blanks the line (`D47D:1237`).
  - `D47D:14AB` types a raw string at one character per 1/30 s (hall of fame).
- Tokens (`D47D:0294`/`0F7C`):
  - `SCORE` is 7 wide; `HSCORE` is "1ST " + initials, 16 wide;
  - `LIVES` is "N LIVES LEFT" / "LAST LIFE" / "EXTRA LIFE";
  - `CUBES` is "N CUBE(S)";
  - `YEAR` comes from the era names at DGROUP `0x1346`: LEARN, WILD WEST, AGE OF MAGIC, AGE OF SCIENCE, POST-HOLOCAUST, PREHISTORIC ERA, AGE OF WARRIORS;
  - the sound tokens `SOUND`/`BEEP`/`SSOUND`/`WARP`/`YEARDONE` play tunes.
- Default hall of fame: DJS 592410, STP 530602, TAK 122373 (ROM `DC955`, `D2229`/`D224E`/`D2273`).
- `port/3ds/source/ledmsg.c` follows this. `tools/romled.py` compares it with the ROM's own LED: attract message 0x0B 400/400 changes identical (text and tick), status loop 0x0E 5/5.
- **Cubes at game start are 0.** `0x37:0x1516` stays 0 until C1 E5's grant (`DA9D:0F3A`); the LED reads "0 CUBES". The port started with 1 and is fixed.

**Clip clock, laserdisc speed effects and sounds** (2026-10-11).
- **Clip clock:** the timer ISR (`D245:0127`, 60 Hz) adds `g[0x6C]` to the clip clock `0x37:0x192A` (1/100 frame) on every other tick (`[0x1018]` toggle, `D245:029E-02AF`). `clip_play` sets `g[0x6C] = 100` for every clip (all 347 clips have type byte 1, `C4B6:0303-0314`). So the input windows run on a clock that follows the effects below, not the disc's own frame count.
- **`DA9D:0C5E effect(n)`** (table action "effect"):
  - n = 300 / 1 / 4 → `g[0x6C] = 300` (clock ×3) and `D37E:0615` → `184SP MF` (fast forward).
  - n = 50 / 2 / 5 → 50 (×0.5) and `D37E:0666` → `30SP MF` (slow motion).
  - n = 100 / 3 / 6 → 100 and `D37E:06C9` → `PL` (normal play).
  - n 1–3 first play the warble tune (`D2DC:021C`); n 4–6 first beep (`DA9D:0002`).
  - Strings at DGROUP 0xD59/0xD67/0xD71. The Sony path sends `;`, `=`/`2@` and `:`.
- **Tones:**
  - `D2DC:0150 tone(freq, len)` sets timer 0 CMPA = CMPB = 2,000,000 / freq with CON 0xC003 (`D2DC:0273`). The 2,000,000 Hz timer clock is the ROM's own constant (the 60 Hz tick is set up from it at `D245:00C0`). In alternate-compare mode that is a square wave of freq / 2 Hz (80188 timer mode from Intel's datasheet: UNVERIFIED in this repo).
  - The length runs on the 30 Hz timer list (slot 5).
  - Beeps: `DA9D:0002` 2500/4, `0027` 2500/8, `004C` 2500/2. Low tones: `0086` 350/4, `00AB` 350/8.
  - Tunes `{freq, len}` until `{0, 0}`, played by `D2DC:0179`:
    - warble `D2DC:0110` (with effects 1–3);
    - death `D2DC:0024` (`death(n)` → `D2DC:0238`, `D047:053A`);
    - alarm `D372:0036` (`D2DC:01FD`: every Hellgate result `D0F0:03A0-0817`, high score `DA9D:07EA`);
    - short alarm `D372:000E` (`D2DC:0254`, `D47D:0EC5`).
  - Reader: `tools/extract_tables.py` → `data/sounds.json`.
- **Port:** the engine reports effects through `tt_frontend.fx`. The 3DS paces frames at ×3 / ×0.5, plays the sound of the frame on screen at normal pitch during multispeed, like MAME (ldv4200hle.cpp:652-657 does not squelch MF/MR; the real LD-V4200 is UNVERIFIED), and plays the tones on NDSP channel 1. Checked with `port/3ds/host/test_frontend`: a shot in C1 E20 at frame 113 gives the beep, ×3 to frame 131, then `PL`.

**Input dispatch** (`CE55:00C9 input_event(device, bits)`, switch at `CE55:02C6`).
- Device 1 (joystick) goes to rule matching (`CFD9:0082`) unless a death is playing.
- Device 8 (timer) runs the window-close check (`CFD9:0316`).
- Device 0x10 (buttons):
  - **An event with the Cube bit 0x200 never reaches rule matching** (`CE55:01D7` → `0247`). During a death with cubes it rewinds (`CE55:01E4-0222`); otherwise it is dropped. So rules that list Cube can't match it; `tools/extract_tables.py` marks Cube as a dead input.
  - Found by `tools/romdiff.py --random 4`: Cube pressed in C1 E35 window 120 and the ROM played death 3.
  - After the fix, six random-play runs agree on 92/92 transitions and 92/92 scores.
- Device 0x20 (Start buttons): **1P and 2P Start together** while the current slot's chapter is 0 play LD 248–350 and `chapter_next` (skip the intro, `CE55:0138-0190`). Otherwise, unless a death is playing, the event goes to rule matching (C1 E5–E7 "press start" windows).

**Remaining table-action targets** (named while building `data/game.json`):
- `C40B:07C6` next_episode (`maybe_episode_next` + `episode_enter`). `C40B:0889(t)` = beep + `C40B:0854` (re-enter current episode at offset t). C3 E62 uses it: Up/Left/Attack/Cube in window 402 jumps to frame 120. `C2BA:039F` attract cycle: `g[0x1A]--` after each sequence. At 0 it goes to E20 and resets to 20. At 1–10 it goes to E200, above 10 to E20. Starting at 20, the ROM plays sequence A (E20–E70) 10 times, then B (E200–E250) 10 times (`tools/romrun.py --seconds 1500`: A at 20.6 s … 749 s, B from 829.9 s). `C2BA:037F` attract next.
- **Intro skip and the Learn path** (checked 2026-10-08).
  - **C0 E10** (the opening, LD 1–245) is the PLAY / LEARN signpost: PLAY points right, LEARN points left (seen on hardware, 2026-10-11). Frames 14–155:
    - **Right** → `D659:000C`: beep, `D70D:040E chapter_next` (the rest of chapter 0 is skipped), learn flag `[p*2+0x74] = 0`.
    - **Left** → `D659:0041`: beep, the rest of the clip (LD 129–245), next scene, flag = 1.
  - Chapter 0 entry sets the flag to 1.
  - In chapter 1, `maybe_episode_next` (`C40B:01C4-0299`, 1 player) sends E20 success, E21 success, and E21 death **with the flag set** to E35 (and sets the replay flag `[p*2+0x13C8]`). E20 death walks on to E21.
  - **E21 death with the flag clear walks on into E22–E30, the Learn path.** It replays the intro footage, and **C1 E23** repeats the choice: Left (`D659:0041`) watches it, Right (`D659:0086`) goes to C1 E30, the duel again.
  - ROM run, Right at C0 E10 and no other input: C0 E10, C1 E5–E10, E20, E21, E22–E30, E35, E40. `tools/sim.py` gives the same order. `D659:00C8` (beep, LD 14064–14181, next scene) has no caller in the table.
- Dead code: `D668:0744` (C4 E70/E71 test) and `D668:08CA` (C3 E60/E61 test) have no callers and no far pointers anywhere in the ROM. Only `D668:05BB` (C6 E50) is used (`C44DE`).
- `DA9D:0C5E(n)` LD speed/still effect (n = 1 multispeed … 6). `DA9D:0086/00AB` low tone (`0x15E`). `DA9D:0027` high tone, length 8. `DA9D:0071` returns 2 (no-op). `DA9D:0E6D` trap: plays LD 23581–23649 and 26062–26305, marks C3 E82 cleared (not failed; corrected 2026-10-12), calls `lose_life`, sets `0x100A` and runs `episode_done` → `after_death` (a second `lose_life` when the saved previous record has points). `DA9D:0EF8` beep, enter C1 E10 (start of play).
- `D70D:040E chapter_next`: runs `hellgate` when the per-player Hellgate slot `[p*2+0x1B2A]` equals the current slot (and the devil DIP != not-in-game), then `chapter_advance`.
- `D668:012B game_start(player)`: credits −= coinage; with `chance(85)` Hellgate slot = `rand() % 4 + 4` (after the chapter in slot 4–7); cubes lamp; lives = DIP.

**Data oddities** (found by `tools/sim.py --check`):
- Moves 8 and 35 (mode "any") list Attack/Cube under device 1. `move_match` (`CE55:03CC`) requires the event device to equal the row device, and button events are device `0x10`, so **those rows can never match in the ROM**. `game.json` keeps them as `dead_inputs`. Used by 4 windows (see `--check` warnings).
- C20 E40 windows 957a/b run 0–75 on a 4-frame clip (LD 53335–53339).
- Assets 2, 5, 8, 11 are not in the asset table. C10000 E30/E60/E210/E240 use them and are only timed gaps in the attract loop.
- Weight-0 episodes never chosen by `choose_episodes`: C0 E30, C0 E40 (same LD 352–516 on audio ch1 / ch2), C1 E50, C5 E50. **No route in normal play was found** (2026-10-06):
  - `choose_episodes` gives them 0%;
  - no code holds a constant pointer to their records (`0x7F06`, `0x7F14`, `0x8216`, `0x8C88` in segment `C52E`), nor any `(chapter, episode)` lookup for them;
  - `episode_find` (`C40B:0482`) has no caller;
  - the only generic "flag this record" helper, `C359:05AE`, is never called: no `lcall`, and no stored far pointer to it anywhere in the ROM.

  They are reachable only with level select (DSW2 bit 4), which flags every header. Treat them as unused content (C1 E50, LD 26304–26424, ends one frame before the Time Map stills; C0 E30/E40 are the same intro footage on LD audio ch1 / ch2).

**Verified against the running ROM** (2026-10-07, `tools/romrun.py` + `tools/romdiff.py`). The real program ROM runs headless on Unicorn (x86 16-bit) with faked cabinet hardware:
- 80188 PCB relocated to I/O `0x100` (`C000:0025` writes `0x0001` to `0xFFFE`);
- Timer 1 at 60 Hz (`CMPA 0x8235`, `CON 0xE001` at `DB9B:026D`; INT `0x12` → `D245:0127`);
- a 16450 UART honouring DLAB;
- LD-V4200 replies per `reference/mame/ldv4200hle.cpp:197-245`, delivered one tick late, since `ld_send` (`D30C:0071`) flushes anything received while it is transmitting.

The ROM boots, plays the attract loop in the order `game.json` gives, and starts a game on 1P Start (MAME default DIPs + free play). With no player input, 15/15 scene → death/next transitions match the decoded rules, and **the scene order of the whole game is identical** to `tools/sim.py`.

**Full-game run (perfect inputs).** `tools/romdiff.py --perfect` reads the ROM's current record (`0x37:[p*4+0x0A]`, offset into `C52E:7E42`) and its clip clock (`0x37:0x192A` ÷ 100, the value `input_event` uses). It presses the move `tools/sim.py` would press, mid-window. In 1,800 s of ROM time the real program plays **all the way to the ending**: chapters 0, 1 (1873), 5, 3, 4, 2, 6, 7, 8, 20 (scene order in the run log). **46/46 scene transitions agree** with the decoded rules. Five plays aren't scene-clip starts:
  - LD 27764–28014: Trader, hardcoded in `CEDF`.
  - 23651–24012: Hellgate intro, `D0F0:025E`.
  - 36934–37057: C5 E40's own `replay_from` jump to clip frame 232 (36702 + 232).
  - 47768–48040: high-score entry. `DA9D:064D`, called from `game_over`, compares the score with the top-3 table (`D945:01DD` per entry, segment `0x37` score at `0x101C`). If the score places, it plays `0xBA98–0xBBA8` (`DA9D:074E`). The clip starts one frame after clip 312 (46765–47767) ends.

Harness lessons (all now in `romrun.py`, per `ldv4200hle.cpp`):
- `SM` only sets a stop marker, and `PL`/`MF` without a frame number reply at once. After a hit the ROM sends `184SPMF` (multispeed skip effect, `DA9D:0C5E`) then `PL`.
- The clip clock keeps the previous clip's value for a moment after a scene change.

Rules corrected by this test (old notes above are superseded where they disagree):
- **Mercy life:** the countdown `[p*4+0x150E]` really counts down, by ~30 per second of play (LD frames). It's loaded with 300 at chapter 0/1 entry, so the mercy life only applies to a death within ~10 s of the start. Seen: 298 → 52 → 0.
- **Chapter 1 flow** (`maybe_episode_next` `C40B:004F`): after the opening duel (E20 succeeded, or its 0-point twin E21 finished) play continues at the first chosen header from **E35** (E22–E30 are the Learn path). From then on, a death **replays the same scene** (`[p*2+0x13C8]` set → previous episode returned). E20 death → E21 relies on `[p*2+0x74] = 1`, which `D70D:040E` sets on entering chapter 0.
- **No retry passes:** `D70D:0661 chapter_start` → `D70D:06B7` gives one pass per visit. `D70D:0A84` (some chosen scoring scene of the chapter was played) makes a **revisit queue only the failed scenes**. `D70D:0973 chapter_completed` = a chosen scoring scene (points ≠ 0, not E10) was played and none is currently failed. The Time Map mask uses this, so an era with failures stays on the map.
- Lives seen in RAM (`segment 0x37 + 0x424`): a life is lost for a death in a scoring scene (C1 E20, E35), not for C1 E21 (0 points) or chapter 0 deaths. Cubes go 0 → 1 at the start screen (C1 E5). Both match the decoded rules.

**Source modules (28, not 15).** `__FILE__` + `__DATE__ __TIME__` strings in DGROUP, in link order: attract, config, odds, chorder, episode, asset, input, specobj, window, qinput, action, choice, device, clock, sound, serial, storage, pioneer, display, unsolved, hint, learn, init, chapter, branch, map, account, misc. Build stamps run Jun 04 – Jun 18 1991.
- Code segments in address order: `C2A7 C2BA C301 C359 C3CE C40B C4B6 CE55 CEDF CFD9 D02B D047 D0F0 D17E D245 D2DC D30C D36C D37E D47D D604 D63D D668 D70D D7C0 D843 D945 DA9D` + `DB9B` (C runtime). MS C large model gives one `_TEXT` segment per module in link order.
- Anchors (string references from code, or known functions): config `C301`, specobj `CEDF`, device `D17E`, serial `D30C` (UART), pioneer `D37E`, display `D47D`, account `D945`, misc `DA9D`. From config to display, segment = string index + 1, which gives odds `C359`, chorder `C3CE`, episode `C40B`, asset `C4B6` (holds the clip/asset lookups), input `CE55`, window `CFD9`, qinput `D02B`, action `D047`, choice `D0F0`, clock `D245`, sound `D2DC`, storage `D36C`. `C2A7`/`C2BA` hold attract plus one unnamed module (main?). UNVERIFIED which is which.
- Between display (`D47D`) and account (`D945`) there are **7** segments once `D659` is counted (pass 1 missed it; found as an action-table call target): `D604 D63D D659 D668 D70D D7C0 D843` ↔ unsolved, hint, learn, init, chapter, branch, map in link order. Function contents agree: `D659` sets the learn flag at the C1 E23 Play/Learn choice, `D668:012B` is game_start (init), `D70D` holds chapter_next/chapter_advance, `D7C0` holds episode_done/after_death (branch), `D843:00B9` is time_map. Still inferred, not string-anchored.
- Note: `specobj_play` (`D47D:1D6C`) sits in the display segment, not specobj.
- **Hints** (`D63D:0002`, called by `after_death` `D7C0:06DE`; `after_death` is called by `episode_done` `D70D:0C1D` when a death record ends, `g[0x100A] == 1`).
  - The death record's f6 (+0x0C) is a hint word. The chance is `(w & 0x1F) * 4` when the hard flag `[p*2+0x13C4]` is set, else `((w & 0x3E0) >> 5) * 4`. The special object is `w >> 10`. Object 21 (`0x54xx`) also needs the Trader unused (`D843:0AFE`).
  - With word 0, while lives remain and the scene scores:
    - no cubes → object 0x15 if `chance(25)` and the Trader is unused;
    - else object 0x14 on `chance(25)`, else 0x16 on `chance(25)`.
  - `specobj_play` (`D47D:1D6C`) finds the key in the table at DGROUP `0x117A`, sets `ld_audio(channel)`, then sends `<start>SE <stop>PL` (`D37E:04FA`).
  - Checked in `tools/romrun.py` (no-input game): C1 E35 death 2 (word 0x4F39, hard 1 → 100%) → `specobj_play(19)` → `1AD 5480SE 5594PL`. C1 E20 death 1 (0x4F20, hard 1 → 0%) → nothing.
  - In that run the hard flag was set at C1 E5 (the 5th good scene) and not cleared by later deaths (count 8 ≥ revert 8), as `tools/sim.py` has it.

**Not yet decoded**
- ~~`CBFC0` region 12-byte records~~: this is the asset table (`CB0D6`) read 2 words out of phase.
- ~~`C5C00` region~~: inside the window table `C5AA4–C800F` (pass 2).
- ~~`C9000`/`CB000` 8-byte records~~: inside the input-rule table `C8296–CB0C5` (pass 2).
- ~~episode.c / chapter.c / map.c logic that walks these~~: done (sections above).

## Rule evaluation, frame by frame (2026-10-07, verified by running the ROM)

- **Input** (`CFD9:0082`): on an input at clip frame t (clip clock `0x37:0x192A` / 100), the record's rules are walked in table order. A rule is skipped if its window is not open (variant a/b by the hard flag, `CFD9:0249`) or already cleared. If the move matches (`CE55:03CC`), the action runs with phase 1. The action's return value decides what happens next: **1** clears that window and stops, **2** stops, **0** goes on to the next rule (but the walk stops anyway on a rule with the stop-on-miss bit).
- **Window close** (`CFD9:0316`): every frame, uncleared rules whose window is closing run phase 2 if they are NO-MOVE rules or nibble-6 actions. The same 1/2/0 handling applies.
- **Return values** of the table actions, read from each function:

  | does | function | returns |
  |---|---|---|
  | effect | `DA9D:0027`, `DA9D:00AB`, `DA9D:0C5E` | 1 |
  | effect (beep) | `DA9D:0002`, `DA9D:0086` | 2 |
  | noop | `DA9D:0071` | 2 |
  | episode_end | `D047:03E5` | 2 (scene done when arg = 1) |
  | next_episode | `C40B:07C6` | 1 |
  | replay_from | `C40B:0889` | 1 (play continues at clip frame arg) |
  | cube_rewind | `D047:0679` | 0 (only in death records: Cube during a death) |
  | cubes_grant_start | `DA9D:0F3A` | 1 |
  | start_game | `DA9D:0EF8` | 0 |
  | trap_c3e82 | `DA9D:0E6D` | 2 |
  | hellgate | `D0F0:023A` | 2 |
  | chapter_next_quiet / choice_left / choice_right | `D659:000C` / `D659:0041` / `D659:0086` | 2 / 0 / 2 |
  | attract_cycle | `C2BA:039F` | 2 |

  Built-in actions (`D047:00E6`) return 1 when they score, finish the scene or kill, and 0 otherwise.
- **Cube rewind** (`D047:0679`): the episode is re-entered 3 s before the death. The unit `D245:07D8(3)` is 3 × g[0x100C], and g[0x100C] = 30 frames in segment 0x37. The death time is the frame at which the killing rule fired. A closing window fires at t = close − 1.
  - Verified with `tools/romdiff.py --perfect --die 5,40`: the death fires at clip frame 99 (LD 36801). After Cube, the ROM sends `36801SE 120SP 36711MR 0AD`, which scans back at 2× with the audio muted. It then sends `36711SE 37057SM PL`: the replay starts at frame 9 = 99 − 90.
  - With `--die 5,21` the death is at frame 89, so the restart clamps to frame 0 (LD 35800).
- **Scoring.** Three bands: score > 100000, > 90000, else.
  - Hit (`D047:0761`): points ×10 / ×1 / ×2.
  - Scene bonus (`D047:0875`, after a scene finished without a death, `D7C0:02D0`): ×50 / ×5 / ×10.
  - Chapter bonus (`D047:0996`, when no next scene is left): +50000 / +5000 / +10000. The last one only applies when the chapter-list entry is nonzero (not the intro).
- **Check:** `tools/romdiff.py --perfect` drives the real ROM to the ending. All 46/46 scene transitions and 46/46 per-scene score changes agree with `tools/sim.py`. The press schedule is the same in both: each window's input at the midpoint of the a∩b window. `port/3ds/host` `make parity` shows `engine.c` identical to `sim.py` over 600 games.

## Video source

- Superseded 2026-10-07: the port's video now comes from the laserdisc CHD (`tools/encode_ld.py`; CHD frame p − 1 = VBI picture p, so no mapping). The notes below are about the earlier DVD target.
- Target was the 2001 Digital Leisure DVD, not the arcade CHD. Frame numbers in the ROM are NTSC LD frames. DVD timing will need a mapping (offset plus possible 3:2/field differences per segment). Calibrate by matching a few known scenes, then fit.
- DVD inserts pre-rendered geometric shapes standing in for the cabinet props. Mask or crop them.

## "Hologram" → 3DS stereo

- The cabinet effect is a CRT reflected into a concave mirror (Pepper's-ghost-like). There is no depth data in the footage.
- Footage is actors on black. Two options:
  - A (cheap): luma/chroma key the actor onto one plane at positive parallax-zero or slight pop-out, with a dark background plane behind. Closest to the cabinet look.
  - B (heavier): offline monocular depth (Depth Anything / MiDaS) → pre-baked L/R frames. Doubles video size.
- 3DS top screen: 400×240 per eye. Budget video as MJPEG or pre-decoded frames, and keep New-3DS vs Old-3DS decode limits in mind.

## Open questions (unverified)

- The Sony LDP-1450 command path. MAME has a TODO saying it is not hooked up.
- Whether DVD chapter boundaries line up with LD segment boundaries.
- Exact cabinet mirror geometry and image orientation. The driver sets `ORIENTATION_FLIP_Y`, consistent with mirror viewing.
