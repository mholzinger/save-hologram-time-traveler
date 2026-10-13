#ifndef TT_FE_H
#define TT_FE_H
/* The 3DS front end's internal interface: what its modules (fe_*.c) share. The public entry points are in
 * frontend.h; the engine contract (the callbacks) is tt_frontend in engine.h. */
#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "audio.h"
#include "data.h"
#include "engine.h"
#include "frontend.h"
#include "led.h"
#include "ledmsg.h"
#include "stage.h"
#include "vset.h"
#include "video.h"

// The picture inside each encoded eye: tools/encode_ld.py puts the 320x240 picture at column 40 + p/2
// (p = --parallax, 6) of the left eye, which is also the start of the one-eye rewind copies.
#define PIC_X0 43

// Waits in the front end count vblanks (60 per second). The ROM's own clock runs at 30 per second
// (D245:07D8(n) = n x g[0x100C], g[0x100C] = 30), and tools/romrun.py ticks are vblanks.
#define VBLANKS_PER_S 60
#define ROM_SECONDS(n) ((n) * VBLANKS_PER_S)   // D245:085A(n): a wait of n seconds

#define MOVE_KEYS \
    (KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN | KEY_CPAD_LEFT | KEY_CPAD_RIGHT | \
     KEY_A | KEY_B | KEY_R | KEY_START)

/* fe_log.c */
void plog_open(void);
void plog_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* fe_screen.c */
extern bool mvd_ok;
void top_show(const u16 *src, int stride, int open, int remaining, int total);
void draw_top(int open, int remaining, int total);

/* fe_hud.c */
extern char status_line[64];
extern int cur_player;
extern tt_game *hud_game;
extern u64 trader_deadline;
extern bool diag;
extern int diag_in, diag_frame, diag_open;
extern u64 diag_dec, diag_blit, diag_gap, diag_last_scan;
extern u64 vmsg_until, vset_dirty_at;
extern const char *bottom_hint;
const char *era_name(int ch);
void hud_start(tt_game *g);
void hud_show(int id);
void video_keys(u32 k);
void hud(tt_game *g);

/* fe_input.c */
extern bool ev_listen;
void quit_check(void);
u32 fe_poll(void);   /* one poll of a wait loop: app alive, keys scanned, SELECT+START quits; keys pressed */
int keys_to_input(u32 k);
int ev_pop(void);
void ev_clear(void);   /* drop queued events (a new scene starts) */
void ev_scan(u32 down, u32 held, bool listen);

/* fe_ld.c */
extern int ep_clock;
extern bool playing_video;
extern int last_ad;
extern u64 pace_ms0;
extern int pace_f0, pace_num, pace_den;
extern s64 ms_slot;
extern int cur_clip;
extern const tt_game *ld_db;
extern int ld_base;
extern bool rewinding;
void pace_start(int frame);
u64 due(int frame);          // when `frame` is due on the pacing clock (osGetTime ms)
bool frame_due(int frame);   // it is (nearly) time to show `frame`
u32 wait_frame(tt_game *g, int frame, u32 keys);
bool show_frame(int frame, int open, int remaining, int total);
int run_frames(tt_game *g, int frames, int listen, int total);
void ld_seek(int target);
void clip_path(char *path, size_t size, const char *name, const char *ext);   // <tt_data>/clips/<name><ext>
bool clip_media(int clip, bool sound);   // open clipNNN's video, and its sound (else silence); false if no video
void open_clip(int clip);
void seek_to(const tt_ep *ep, int t);
void open_named(const char *name, int ch);
int seg_length(const char *name);   /* frames of a named segment (stop - start), 0 if unknown */
void play_named(tt_game *g, const char *name, int ch);   /* open_named + play_to its end */
u32 play_to(tt_game *g, int to, u32 keys);
u32 hold(tt_game *g, int vblanks, u32 keys);
void skip_to(int frame);

/* fe_game.c */
extern int last_chapter;
extern int g_death_t;
void solved(tt_game *g);
void fe_on_episode(tt_game *g, const tt_ep *ep);
void play_sound(tt_game *g, int id);
void fe_fx(tt_game *g, int kind, int arg);
int fe_frame_input(tt_game *g, const tt_ep *ep, int t);
void fe_on_death(tt_game *g, const tt_ep *death);
int fe_use_cube(tt_game *g, const tt_ep *ep);
void fe_hint(tt_game *g, int key);

/* fe_events.c */
int fe_accept_hellgate(tt_game *g);
void fe_hellgate_result(tt_game *g, int k);
void fe_trader(tt_game *g, int automatic);
int fe_map_choice(tt_game *g, const int *opts, int n);

/* fe_hof.c */
typedef struct {   /* one hall of fame entry */
    uint32_t score;
    char init[4];
} hof_entry;
extern uint32_t last_score[2];
extern hof_entry hof[3];
void hof_load(void);
bool hall_of_fame(tt_game *g, uint32_t score, int player);

/* fe_settings.c */
extern tt_settings settings;
extern bool demo_sounds;
void settings_load(void);

/* fe_attract.c */
extern void *blob;
extern int blob_len;
bool load_game(void);

/* fe_session.c */
void frontend_scene_select(void);

#endif
