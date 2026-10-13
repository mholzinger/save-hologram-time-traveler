/* The cabinet's 16-character LED line, driven like the ROM's display module (segment D47D):
 * messages from the table C52E:0008 (packed in the .ttg), <TOKEN> expansion (D47D:0294), mode 1 typed one
 * character per display tick, mode 2 shown at once, line timer in 1/30 s, control records {0,1} end,
 * {0,2} restart, {0,3,t} special-object branch. docs/ROM_FINDINGS.md "LED messages". */
#ifndef TT_LEDMSG_H
#define TT_LEDMSG_H
#include "engine.h"

enum {
    LEDV_CUBES,
    LEDV_LIVES,
    LEDV_MERCY_GIVEN,
    LEDV_PLAYER,
    LEDV_SCORE,
    LEDV_LAST_SCORE,
    LEDV_CLEAR_LAST_SCORE,
    LEDV_HOF_SCORE,
    LEDV_CHAPTER,
    LEDV_WAIT_SECONDS,
    LEDV_LIVES_DIP,
    LEDV_COINAGE,
    LEDV_CREDITS,
    LEDV_CONTINUE_COINS,
    LEDV_CONTINUE_LIVES
};

typedef struct {
    void *user;
    int (*value)(void *user, int what, int arg);           /* LEDV_*; arg = player or hall-of-fame entry */
    const char *(*initials)(void *user, int entry);         /* hall of fame entry 2 = 1st place */
    void (*sound)(void *user, int id);                      /* TT_SND_* from sound tokens */
} ledm_env;

void ledm_init(const tt_game *g, const ledm_env *env);
/* The LED messages the front end shows by id (records of the ROM's message table C52E:0008). The comments
 * say what each is for; the wording is the ROM's, read from the pack (build/led_messages.json, never in
 * git). Every port shows the same ones at the same call sites. */
enum {
    LEDM_ATTRACT_HOF = 0x0B,      /* attract: the hall of fame, top 3 */
    LEDM_ATTRACT_PROMPT = 0x0C,   /* attract: invites a button press */
    LEDM_STATUS = 0x0E,           /* the status loop: cubes, score, lives */
    LEDM_HELLGATE_OFFER = 0x17,   /* Hellgate offer: pull the joystick to play */
    LEDM_HG_JACKPOT = 0x19,       /* Hellgate: jackpot, a free game */
    LEDM_HG_CUBES = 0x1A,         /* Hellgate: won 2 cubes */
    LEDM_HG_LIFE = 0x1B,          /* Hellgate: won a life */
    LEDM_HG_LOST_LIFE = 0x1C,     /* Hellgate: lost a life */
    LEDM_HG_TAKES_ALL = 0x1E,     /* Hellgate: the devil takes all lives */
    LEDM_HG_BETTER_LUCK = 0x123,  /* Hellgate: lost, with the devil never taking lives */
    LEDM_PLAYER = 0x37,           /* the player's number */
    LEDM_GAME_OVER_P1 = 0x3D,     /* player 1's game over */
    LEDM_GAME_OVER_P2 = 0x3E,     /* player 2's game over */
    LEDM_SOLVED = 0x3F,           /* an era solved, with its name */
    LEDM_TRADER = 0x41,           /* Trader: cubes and seconds left */
    LEDM_PICK_ERA = 0x44,         /* Time Map: pick an era */
    LEDM_DEATH = 0x8D,            /* the death loop: status and score */
};
void ledm_show(int id);         /* D47D:00F3 */
void ledm_tick30(void);         /* call at 30 Hz */
const char *ledm_line(void);    /* the 16 characters shown */
void ledm_stop(void);
void ledm_set_line(const char *s16);   /* what the LED shows already (tests) */
#endif
