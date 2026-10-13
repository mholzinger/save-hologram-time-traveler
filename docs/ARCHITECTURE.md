# Architecture

How the game gets from the arcade ROM and laserdisc to a 3DS, and where each piece lives. Facts about the
ROM itself (addresses, decoded functions) are in `docs/ROM_FINDINGS.md`; this page is about the code.

## The idea

The port is not an emulator. The arcade's program ROM is mined once for its scene graph: which clip each
scene plays, its input windows, what each input does (score, death, jump, rewind), and the rules around
them (lives, cubes, Time Map, Trader, Hellgate). That data drives a small native engine. The footage
comes from the laserdisc image, encoded once for the 3DS hardware decoder.

## Data flow

```
rom/tt061891.bin ──tools/extract_tables.py──> data/*.json  (checked in: derived facts, no media)
                                              build/led_messages.json  (ROM wording: not in git)
data/*.json + build/led_messages.json ──tools/pack_game.py──> build/timetrv.ttg  (binary scene graph)
rom/timetrv.chd ──tools/encode_ld.py──> build/enc_ld/*.h264 + *.wav  (one per clip and named segment)
build/timetrv.ttg + build/enc_ld + build/art ──make stage──> build/sdmc/  (the SD card layout)
build/sdmc/timetrv ──make release──> build/release/timetrv3ds.3dsx  (one file, data in RomFS)
```

`make game` runs all of it. Nothing under `rom/`, `build/` or `video/` is committed.

## The pieces

| Piece | Where | Does |
|---|---|---|
| Table extraction | `tools/extract_tables.py` | One `read_<table>()` per ROM table, hash-checked ROM; writes `data/` |
| Scene graph | `data/game.json` | Every scene with its rules and media, plus the game constants |
| Reference model | `tools/sim.py` | The rules in Python, read from `data/game.json` |
| Pack | `tools/pack_game.py` | `game.json` to the `.ttg` binary; the layout is in its docstring and `engine.h` |
| Engine | `port/3ds/source/engine.c` | Portable C99: plays a whole game through front-end callbacks |
| Front end | `port/3ds/source/fe_*.c` | The 3DS side of those callbacks: video, sound, input, HUD, menus |
| Presentation | `stage.c`, `video.c`, `audio.c`, `led.c`, `ledmsg.c`, `art.c` | Cabinet stage (citro3d), MVD video, NDSP sound, LED panel |
| ROM harness | `tools/romrun.py`, `tools/romdiff.py` | Runs the real ROM on Unicorn and checks it against `sim.py` |

## Engine and front end

`engine.c` has no 3DS code, so it builds and is tested on any machine. It walks the scene graph and calls
the front end through `tt_frontend` (`engine.h`):

- `on_episode`, `on_death`: a scene or death clip is about to play.
- `frame_input`: called once per laserdisc frame of the record; returns the input pressed. The front end
  plays the frame, so the engine's clock is the video's clock.
- `use_cube`, `accept_hellgate`, `map_choice`, `trader`, `pick_episode`: the player's decisions.
- `fx`, `hint`, `hellgate_result`, `life_lost`: presentation (sounds, laserdisc speed, LED messages).

Every callback may block for as long as its footage runs. `tt_run()` returns when the game is over.

Two players: each player's game runs `tt_run()` on its own thread (`fe_session.c`), and only one runs at a
time. A lost life hands over to the other thread (the ROM's switch at `D7C0:038F`).

The front end is split by job, all sharing `fe.h`:

| Module | Job |
|---|---|
| `fe_session.c` | A game from start to game over, two-player hand-over, level select |
| `fe_game.c` | The engine callbacks for scenes, deaths, cubes, hints, effects |
| `fe_events.c` | Hellgate, Trader, Time Map |
| `fe_ld.c` | The laserdisc model: pacing, seeks and their blanking, named segments |
| `fe_input.c` | Buttons to cabinet inputs, the input event queue, quit |
| `fe_hud.c`, `fe_screen.c` | LED panel and bottom screen, the flat top-screen picture |
| `fe_attract.c`, `fe_hof.c`, `fe_settings.c`, `fe_log.c` | Attract mode, hall of fame, settings, play log |

## How correctness is checked

`make test` runs all of it, and none of it needs a 3DS:

1. `sim.py --check`: the scene graph is complete (every death a rule names exists, and so on).
2. Parity: `engine.c` and `sim.py` play 900 games (perfect, no input, random) and must agree on every
   score, life and scene. Both draw random numbers in the same order, so they stay in step.
3. Golden outputs (`port/3ds/host/golden.sh`): the real front end runs on the host with scripted presses,
   plus the 2-player, LED message and LED glyph tests; each output must match `port/3ds/host/expected/`.
4. ROM differential (`tools/romdiff.py`): the real ROM runs on Unicorn with faked inputs, and every scene
   change and score it produces must match what the rules say.

The chain is: the ROM is the authority, `sim.py` is checked against the ROM, and `engine.c` is checked
against `sim.py`.

## Conventions

- ROM addresses are real-mode `SEG:OFF` (`D37E:03AB`). Code that follows a ROM routine cites its address
  in a comment, so it can be checked in Ghidra.
- `SIM:` in a comment marks a modelling choice, not a ROM fact. `UNVERIFIED` marks a fact without a source.
- `engine.c` and `sim.py` keep the same function names, order and section headers. A change to one is
  made in the other in the same commit.
