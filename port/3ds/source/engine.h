/* Time Traveler game engine: walks the packed scene graph (tools/pack_game.py -> *.ttg).
 * Portable C99, no libctru, so it builds and is tested on the host (port/3ds/host/).
 * Rules mirror tools/sim.py, which mirrors docs/ROM_FINDINGS.md. */
#ifndef TT_ENGINE_H
#define TT_ENGINE_H

#include <stdint.h>

enum {
    TT_IN_UP = 1,
    TT_IN_DOWN = 2,
    TT_IN_LEFT = 4,
    TT_IN_RIGHT = 8,
    TT_IN_ATTACK = 16,
    TT_IN_CUBE = 32,
    TT_IN_START1 = 64,
    TT_IN_START2 = 128
};
enum { TT_KIND_SYSTEM, TT_KIND_PICK_ONE, TT_KIND_PICK_ONE_BONUS, TT_KIND_CHANCE, TT_KIND_CHANCE_BONUS, TT_KIND_DEATH };
enum { TT_MODE_NONE, TT_MODE_SINGLE, TT_MODE_ANY, TT_MODE_SEQUENCE };
enum {
    TT_OP_NONE,
    TT_OP_SCORE,
    TT_OP_DONE,
    TT_OP_DEATH,
    TT_OP_JUMP,
    TT_OP_FAIL,
    TT_OP_REWIND,
    TT_OP_REPLAY,
    TT_OP_HELLGATE,
    TT_OP_CUBES,
    TT_OP_SKIP,
    TT_OP_LEARN,
    TT_OP_SPEED,
    TT_OP_SOUND
};   /* SKIP: chapter_next, learn = 0 (D659:000C);
                                                                     LEARN: scene done, learn = 1 (D659:0041) */
enum { TT_RF_STOP_ON_MISS = 1, TT_RF_CLOSES = 2, TT_RF_GOOD = 4, TT_RF_BEEP = 8 };
/* Sound ids (data/sounds.json via tools/pack_game.py) and effect kinds for tt_frontend.fx. */
enum {
    TT_SND_BEEP,
    TT_SND_BEEP_LONG,
    TT_SND_BEEP_SHORT,
    TT_SND_LOW,
    TT_SND_LOW_LONG,
    TT_SND_WARBLE,
    TT_SND_DEATH,
    TT_SND_ALARM,
    TT_SND_ALARM_SHORT,
    TT_SND_WARP,
    TT_SND_YEARDONE
};
enum { TT_FX_SOUND, TT_FX_SPEED, TT_FX_SEGMENT, TT_FX_EVENT };
/* EVENT: points where the ROM shows an LED message: game start (D668:0102, 0x0E), scene done (D70D:0E3F,
 * 0x0E), death (D047:0626, 0x8D), cube used (CE55:023A, 0x0E), life lost (DA9D:0370, 0x0E). */
enum {
    TT_EV_GAME_START,
    TT_EV_SCENE_DONE,
    TT_EV_DEATH,
    TT_EV_CUBE_USED,
    TT_EV_LIFE_LOST,
    TT_EV_INTO_CH7   /* chapter_advance's route into chapter 7 (d000_70f1): map still, 3 s */
};
/* SEGMENT: footage the action plays itself before going on (D37E:04FA search + play to), arg TT_SEG_* */
enum {
    TT_SEG_LEARN_WALK, /* D659:0041: LD 129-245 */
    TT_SEG_TRAP,       /* DA9D:0E6D: LD 23581-23649 + 26062-26305 */
    TT_SEG_INTRO_SKIP  /* CE55:0138: LD 248-350 */
};
/* SPEED arg = DA9D:0C5E n: 300/1/4 fast, 50/2/5 slow, 100/3/6 normal */

#define TT_NO_CLIP 0xFFFF /* tt_ep.clip of an episode without footage (tools/pack_game.py) */

typedef struct {
    uint16_t chapter, episode;
    uint8_t kind, weight, flags, n_deaths;
    uint16_t points, clip, first_rule, n_rules, ld_frames, msg;
} tt_ep; /* 20 bytes; msg = LED message id */
/* One input rule. Phase 1 (matching input) and phase 2 (window closing) each run one op and return
 * the action's code: 1 = clear this window and stop, 2 = stop, 0 = go on (CFD9:0082, CFD9:0316). */
typedef struct {
    uint16_t window, a_lo, a_hi, b_lo, b_hi, inputs;
    uint8_t mode, flags, hit_op, hit_ret, expire_op, expire_ret;
    uint16_t hit_arg, expire_arg;
} tt_rule; /* 22 bytes */
typedef struct {
    uint16_t in_ch, in_ep;
    uint8_t n_out, pad;
    uint16_t out[2][2];
} tt_subst;
typedef struct {
    uint16_t clip, source;
    uint32_t ld_start;
    uint16_t ld_frames;
    uint8_t audio, confident;
} tt_clip; /* LD clip table */

typedef struct {
    uint16_t key;
    uint8_t n;
    int8_t opt[4];
    uint16_t still[4];
} tt_tmap; /* Time Map (D843:0B33); opt -1 = Trader */

typedef struct tt_game tt_game;

/* Callbacks the front end supplies. All may use tt_rand() for randomness. */
typedef struct {
    /* Real time: called once per LD frame t of the record being played (t jumps on a replay or a cube
     * rewind). Return the TT_IN_* bit pressed during that frame, or 0. When NULL, choose_input is used. */
    int (*frame_input)(tt_game *g, const tt_ep *ep, int t);
    /* Scripted: one window's rules (same window id), n >= 1, asked for every window when a record
     * starts. Return a TT_IN_* bit, or 0. The press lands mid-window (tools/sim.py Game.schedule). */
    int (*choose_input)(tt_game *g, const tt_ep *ep, const tt_rule *const *rules, int n);
    int (*use_cube)(tt_game *g, const tt_ep *ep); /* called only when cubes > 0 */
    int (*accept_hellgate)(tt_game *g); /* g->hellgate_offer: 1 first offer, 2 after a jackpot */
    int (*map_choice)(tt_game *g, const int *remaining, int n); /* returns an index into remaining */
    void (*on_episode)(tt_game *g, const tt_ep *ep); /* episode about to play (optional) */
    void (*on_death)(tt_game *g, const tt_ep *death); /* death record about to play, before use_cube (optional) */
    /* Trader (CEDF:0719): automatic = 1 when the map runs it on its own (slot 5), 0 when picked on the map.
     * The front end plays the footage and calls tt_buy_cubes() once per coin. Optional. */
    void (*trader)(tt_game *g, int automatic);
    /* Hellgate roll result (D0F0:025E): 0 jackpot, 1 +life, 2 +2 cubes, 3/4 lose a life, 5 devil takes all.
     * Called after the engine applied it. Optional. */
    void (*hellgate_result)(tt_game *g, int outcome);
    /* Hint (D63D:0002): play special object `key` (data/specobj_clips.json, 1..28). Optional. */
    void (*hint)(tt_game *g, int key);
    /* A death cost a life (after the hint and the mercy check). Two players: the ROM hands over to the
     * other player here (after_death -> D7C0:038F switch_player). Optional. */
    void (*life_lost)(tt_game *g);
    /* Presentation effects, no game state: TT_FX_SOUND (TT_SND_*) and TT_FX_SPEED (laserdisc speed). Optional. */
    void (*fx)(tt_game *g, int kind, int arg);
    /* Level select (DSW2 bit 4): episode_test_select (c000_4580) shows the scene after `idx` ("C<ch> E<ep>")
     * and lets the player step through the scene headers; return the index to play, -1 to end. */
    int (*pick_episode)(tt_game *g, int idx);
} tt_frontend;

/* devil: 0 takes lives, 1 never, 2 absent. difficulty = DSW3 field 0..3 (0 very difficult, 1 difficult,
 * 2 easy, 3 medium; C2BA:079D). */
/* Reversal Cube cost (DSW2): every cube_credits coins (g[0x88], 0 = 1) buy cubes_per_coin cubes (g[0x1928], 0 = 2);
 * cubes_per_coin 4 alternates 1 and 2 per coin (trader_buy c000_fbe5). */
typedef struct {
    int lives, max_cubes, devil, difficulty, cubes_per_coin, level_select, cube_credits;
} tt_settings;

struct tt_game {
    /* data */
    const tt_ep *eps;
    int n_eps;
    const tt_rule *rules;
    int n_rules;
    const tt_subst *subst;
    int n_subst;
    const tt_clip *clips;
    int n_clips;
    const tt_tmap *tmap;
    int n_tmap;
    uint8_t weights[5][5];
    /* state */
    tt_settings set;
    tt_frontend fe;
    uint32_t rng;
    int lives, cubes, bonus, hellgate_slot;
    uint32_t score;
    uint8_t chosen[512], status[512]; /* status: 0 none, 1 done, 2 failed */
    int played, played_hash;
    int good_eps, hard; /* [p*2+0x42E] successful episodes; [p*2+0x13C4] window variant b */
    int death_t; /* clip frame of the last death (cube rewind restarts 90 frames before) */
    int mercy_eps, mercy_ok; /* [p*2+0x1E], [p*2+0x1594] (D7C0:0616) */
    int mercy_given; /* [p*2+0x34]: the last life is the mercy one (LED "EXTRA LIFE") */
    int prev_pts, cur_pts; /* f6 of the record episode_enter saved as previous ([p*4+0x1582]) and of the current one */
    int mercy_frames; /* LD frames since chapter 0/1 entry ([p*4+0x150E] counts 300 down) */
    int map_fallbacks;
    const uint8_t *msgs;
    int n_msgs;
    const uint8_t *sounds;
    int n_sounds;
    const uint8_t *strings;
    int n_strings; /* {key\0 value\0}: LED token strings, eraN, hofN */
    /* {u8 id, u8 steps, steps x {u16 freq, u16 len}} */ /* LED lines: {u16 id, u8 mode, u8 pad, u16 time, text\0} */
    int learn; /* [p*2+0x74]: 1 from chapter 0 entry; 0 once the intro is skipped */
    int jump_ep; /* chapter-1 episode a jump action enters (D659:0086 E30, DA9D:0EF8 E10) */
    int players; /* g[0x72]: 1, or 2 for a two-player game (set before tt_run) */
    int map_rec; /* index into tmap of the map shown by map_choice, -1 = fallback */
    int hellgate_offer; /* 1 or 2 while accept_hellgate runs */
    int trader_flag; /* [p*2+0x1B24]: 0 on the map, 1 not this visit, 2 used for good */
    int trader_coin; /* coins inserted this Trader visit (trader_coins c000_fb70 counts from 1) */
    int slot_ch[11]; /* chapter played in each chapter-list slot */
};

int tt_load(tt_game *g, const void *blob, int len); /* 0 on success */
void tt_new_game(tt_game *g, uint32_t seed, const tt_settings *s, const tt_frontend *fe);
void tt_run(tt_game *g); /* plays a whole game through the callbacks */
void tt_run_select(tt_game *g); /* level select: scenes chosen with fe.pick_episode */
int tt_is_header(const tt_game *g, int idx); /* a scene header the level-select picker can offer */
uint32_t tt_rand(tt_game *g);
int tt_randrange(tt_game *g, int n);
int tt_buy_cubes(tt_game *g); /* one coin at the Trader; returns 1 when cubes are full */
/* Line `line` of LED message `id` (C52E:0008 via the pack), or NULL; *time = its display time in 1/30 s. */
const char *tt_message(const tt_game *g, int id, int line, int *time);
int tt_chapter_completed(const tt_game *g, int ch); /* D70D:0973 */
int tt_clip_start(const tt_game *g, int clip); /* LD frame the clip starts at, -1 if unknown */
unsigned tt_map_key(const tt_game *g); /* D843:0A3D: bonus bit | completed chapters 1-19 */
/* Steps of sound `id`: *steps points at n {u16 freq, u16 len} pairs (little-endian); returns n, 0 if absent.
 * freq gives a square wave of freq/2 Hz (D2DC:0273), len is in 1/30 s. */
int tt_sound(const tt_game *g, int id, const uint8_t **steps);
const char *tt_string(const tt_game *g, const char *key); /* "" when absent */
int tt_rule_good(const tt_rule *r); /* the perfect policy would press this rule's input */
void tt_window(const tt_game *g, const tt_rule *r, int *lo, int *hi); /* frames the rule's window is open */

#endif
