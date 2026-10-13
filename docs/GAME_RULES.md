# Game rules (cited)

Source key:
- **[DVDM p.N]**: *Hologram Time Traveler* DVD manual, Digital Leisure, ©2000 (`docs/reference/Hologram Time Traveller DVD Manual.pdf`, local only)
- **[PCF]**: PC version `video/HTT/tt/ttfirst.txt`
- **[MAME]**: `reference/mame/timetrv.cpp` DIP definitions (arcade operator settings)

These describe the DVD/PC home versions unless marked [MAME]. Where the arcade differs, the ROM is authoritative.

## Start of game
- 5 lives, 1 Time Reversal Cube at the start of each game. [DVDM p.4]
- Arcade: lives 2/3/4/5 by DIP SW1:3-4, default 3. Max cubes 6 or 36 (SW2:1). Cube cost by SW2:2-4. [MAME]
- First era is always 1873 (Wild West, gun-slinging cowboy). [DVDM p.2] [PCF]
- Home versions offer "Play" or "Learn". Learn = guided tutorial by Kyi-La. [DVDM p.2] Arcade ROM has a `learn.c` module.

## Moves
- Inputs: Left, Right, Up, Down, Fire. [DVDM p.2]
- A window can require one of: a single direction, Fire, a double move (Left/Fire or Right/Fire), or **no move**. [DVDM p.3] [PCF]
- Double move = direction then Fire "very quickly" within the same window. [DVDM p.2-3]
- Scene 1: first move Fire, second move Right/Fire. [DVDM p.2] [PCF]
- DVD shows a yellow diamond when a window is open; a wrong move shows a blue arrow/gun and Gram dies. [DVDM p.2-3]

## Death and progression
- On death you move on to the next scene and return to the failed scene later. [DVDM p.3] [PCF]
- Time Reversal Cube: press Fire/Enter during a death to rewind into the scene. [DVDM p.3]
- Scenes included in a game are chosen at random each new game. [DVDM p.4] (ROM: `chorder.c`, `odds.c`)
- Game over when lives reach 0. [DVDM p.4]

## Time Map, Trader, Hellgate
- After 1873, the Time Map lets the player choose the next era. Completed eras can't be re-entered. [DVDM p.3] (ROM: `map.c`)
- Trader: buy additional cubes, once per Time Map visit. [DVDM p.3]
- Hellgate: optional Devil's slot machine at certain points. Press Fire/Enter to accept. Wins or loses lives. [DVDM p.3]
- Arcade DIP SW2:6-7: "Devil Can Take Lives" / "Devil Never Takes Lives" / "Devil Not In Game". [MAME]

## Coverage claim
- "All of the scenes from the original arcade game are included in this DVD version." [DVDM p.4] Treat this as a claim to verify against `data/clips.csv`.

## DVD video modes
- Main menu → Play → "2D Game" or "3D Game". "Specially encoded to provide both 2D and 3D video". 3D uses the enclosed red/blue glasses. [DVDM p.1]
- Other menu items: Watch (main sequences, no input, excludes the finale), History (period news footage), Coming Soon. [DVDM p.1]
