#ifndef TT_DATA_H
#define TT_DATA_H
/* Where the game data is read from: the RomFS packed into a one-file build (`make release`) when it
 * holds the game, else the SD card (`make deploy`). Saves (high scores) always go to the SD card. */
extern const char *tt_data;   /* "romfs:/timetrv" or "sdmc:/timetrv", no trailing slash */
void tt_data_init(void);
void tt_data_exit(void);      /* unmounts the RomFS if it was mounted */
#define TT_SAVE_DIR "sdmc:/timetrv"
#endif
