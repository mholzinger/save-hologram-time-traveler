# Local art (splash, menu, icon)

The 3DS build can show official Time Traveler art: a splash at boot, the main menu on the bottom screen under the logo, and the homebrew icon. **The art is user-local and never shipped or committed.** It is Sega and Digital Leisure material. Only the code and the scripts that convert it are in git. A clean clone builds and runs without any art: the splash falls back to a black screen with the title, the menu falls back to the text console, and the icon falls back to libctru's placeholder.

## Steps

1. **Fetch the sources** into `docs/reference/art/` (git-ignored):
   ```
   bash docs/reference/art/fetch_art.sh
   ```
   `fetch_art.sh` and `cut_art.py` live in that ignored folder with the art. They are not in git, so keep your own copy.
2. **Cut and convert:**
   ```
   make art
   ```
   This runs `docs/reference/art/cut_art.py`, which crops the sources into `build/art/*.png`. It then runs `tools/art_convert.py`, which writes the screens the port loads to `build/art/sdmc/timetrv/art/`. Both scripts need Pillow in the repo venv: `.venv/bin/pip install pillow`.
3. **Build and deploy:**
   ```
   make deploy
   ```
   `make stage` copies `build/art/sdmc/timetrv/art/` to `build/sdmc/timetrv/art/` when it exists, and the deploy sends it to the card. The 3DS build uses `build/art/icon_48.png` as the app icon when it exists.

## What the port loads

These files are read from `sdmc:/timetrv/art/` (`port/3ds/source/art.c`). Each is raw BGR8 in the framebuffer's memory order, like `map.bgr`.

| File | Size | Made from | Shown |
|---|---|---|---|
| `splash.bgr` | 400x240 | `splash_top_400x240.png` | top screen at boot (2 s or a button) and behind the main menu |
| `splash_bottom.bgr` | 320x240 | `menu_bottom_320x240.png` | bottom screen at boot |
| `menu.bgr` | 320x240 | `menu_bottom` dimmed, with `logo_lockup` scaled to 320 wide as a header | main menu |

`banner_256x128.png`, `icon_24.png` and the `char_*` renders are cut but not used yet. They are for a future `.cia` build (bannertool) and other screens (docs/TASKS.md Phase 5).

## Rules

- Never `git add` anything under `build/` or `docs/reference/`. Both are in `.gitignore`.
- Never commit image files. `git status` should show no `.png`, `.jpg` or `.bgr` files.
