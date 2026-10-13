/* The cabinet's LED message system, after the ROM's display module (segment D47D).
 * See ledmsg.h. Portable C (host-tested against the ROM's LED output: port/3ds/host/test_ledmsg.c). */
#include "ledmsg.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    int id, mode, time;
    const char *text;
} rec;

static rec recs[256];
static int nrecs;
static const tt_game *G;
static ledm_env env;

/* D47D state: current record (0x8E), active (0x13C2), line buffer (0x192E) and its output position
 * (0xFF6), mode (0x1580), line timer (0x18, 1/30 s), LED RAM (ports 0x1180/0x1200). */
static int cur, active, pos, mode, timer, phase;
static char buf[64], out[17];

static int val(int what, int arg) { return env.value ? env.value(env.user, what, arg) : 0; }
static const char *str(const char *key) { return tt_string(G, key); }

void ledm_init(const tt_game *g, const ledm_env *e)
{
    G = g;
    env = *e;
    nrecs = 0;
    const uint8_t *p = g->msgs;
    for (int i = 0; p && i < g->n_msgs && nrecs < 256; i++) {
        const char *text = (const char *)p + 6;
        recs[nrecs++] = (rec){p[0] | p[1] << 8, p[2], p[4] | p[5] << 8, text};
        p = (const uint8_t *)text + strlen(text) + 1;
    }
    cur = active = pos = mode = timer = phase = 0;
    memset(out, ' ', 16);
    out[16] = 0;
    buf[0] = 0;
}

#define TOK_MAX 48   /* a token's value buffer (token(), the line builder) */

/* D47D:0294 / D47D:0F7C: the value of one <TOKEN>. */
static void right_align(char *dst, int width, long v)   /* digits end at dst[width - 1] */
{
    char d[16];
    snprintf(d, sizeof d, "%ld", v);
    int n = (int)strlen(d);
    if (n <= width) memcpy(dst + width - n, d, (size_t)n + 1);
}

static void lives_text(char *o)   /* D47D:0F7C */
{
    int lives = val(LEDV_LIVES, 0);
    o[0] = 0;
    if (lives >= 2)
        snprintf(o, TOK_MAX, "%d%s", lives, str("lives_left"));
    else if (lives == 1)
        snprintf(o, TOK_MAX, "%s", val(LEDV_MERCY_GIVEN, 0) == 1 ? str("extra_life") : str("last_life"));
}

static void counted(char *o, int n, const char *one, const char *many)
{
    snprintf(o, TOK_MAX, "%d%s", n, n == 1 ? str(one) : str(many));
}

static void token(const char *t, char *o)
{
    o[0] = 0;
    if (!strcmp(t, "H")) {
        snprintf(o, TOK_MAX, "%s", str("h"));
        return;
    }
    if (!strcmp(t, "SCORE")) {
        snprintf(o, TOK_MAX, "%s", str("score_pad"));
        right_align(o, 7, (long)val(LEDV_SCORE, val(LEDV_PLAYER, 0)));
        return;
    }
    static const char *const hs[3] = {"HSCORE1", "HSCORE2", "HSCORE3"}, *const pl[3] = {"first", "second", "third"};
    for (int i = 0; i < 3; i++)
        if (!strcmp(t, hs[i])) {   /* entry 2 is 1st place (D945:012A / D945:01DD) */
            snprintf(o, TOK_MAX, "%s%s%s", str(pl[i]), env.initials ? env.initials(env.user, 2 - i) : "   ",
                     str("hs_pad"));
            right_align(o, 16, (long)val(LEDV_HOF_SCORE, 2 - i));
            return;
        }
    /* ASCORE1/ESCORE1/ASCORE2/ESCORE2: the last game's score once (then cleared), else a hall-of-fame line */
    if (!strcmp(t, "ASCORE1") || !strcmp(t, "ESCORE1") || !strcmp(t, "ASCORE2") || !strcmp(t, "ESCORE2")) {
        int p = t[6] == '2';
        long sc = val(LEDV_LAST_SCORE, p);
        if (!sc) {
            token(!strcmp(t, "ASCORE2") ? "HSCORE2" : "HSCORE1", o);
            return;
        }
        snprintf(o, TOK_MAX, "%s", p ? str("player2") : str("player1"));
        right_align(o, 16, sc);
        val(LEDV_CLEAR_LAST_SCORE, p);
        return;
    }
    if (!strcmp(t, "PLAYER")) {
        snprintf(o, TOK_MAX, "%d", val(LEDV_PLAYER, 0) + 1);
        return;
    }
    if (!strcmp(t, "BULLETS") || !strcmp(t, "LIVES") || !strcmp(t, "INPGLOBAL")) {
        /* INPGLOBAL names the player's active special object (CEDF:0619: type 3 orb, 4 cube); none is
         * active in play (ROM run: the status loop shows the lives line with a cube held), so lives. */
        lives_text(o);
        return;
    }
    if (!strcmp(t, "WAIT")) {
        snprintf(o, TOK_MAX, "%d", val(LEDV_WAIT_SECONDS, 0));
        return;
    }
    if (!strcmp(t, "CREDIT_LIVES")) {
        counted(o, val(LEDV_LIVES_DIP, 0), "life", "lives");
        return;
    }
    if (!strcmp(t, "CREDITS")) {
        int c = val(LEDV_COINAGE, 0);
        counted(o, c ? val(LEDV_CREDITS, 0) / c : 0, "credit", "credits");
        return;
    }
    if (!strcmp(t, "CREDIT_COINS")) {
        counted(o, val(LEDV_COINAGE, 0), "coin", "coins");
        return;
    }
    if (!strcmp(t, "CREDIT_MORE_COINS")) {
        int c = val(LEDV_COINAGE, 0), cr = val(LEDV_CREDITS, 0);
        counted(o, c - (c ? cr % c : 0), "coin", "coins");
        return;
    }
    if (!strcmp(t, "INSERTED_COINS")) {
        counted(o, val(LEDV_CREDITS, 0), "coin", "coins");
        return;
    }
    if (!strcmp(t, "CUBES")) {
        counted(o, val(LEDV_CUBES, 0), "cube", "cubes");
        return;
    }
    if (!strcmp(t, "CONTINUE_COINS")) {
        counted(o, val(LEDV_CONTINUE_COINS, 0), "coin", "coins");
        return;
    }
    if (!strcmp(t, "CONTINUE_LIVES")) {
        counted(o, val(LEDV_CONTINUE_LIVES, 0), "life", "lives");
        return;
    }
    if (!strcmp(t, "SOUND")) {
        if (env.sound) env.sound(env.user, TT_SND_ALARM);
        return;
    }        /* D2DC:01FD */
    if (!strcmp(t, "BEEP")) {
        if (env.sound) env.sound(env.user, TT_SND_BEEP);
        return;
    }          /* DA9D:0002 */
    if (!strcmp(t, "SSOUND")) {
        if (env.sound) env.sound(env.user, TT_SND_ALARM_SHORT);
        return;
    } /* D2DC:0254 */
    if (!strcmp(t, "WARP")) {
        if (env.sound) env.sound(env.user, TT_SND_WARP);
        return;
    }          /* D2DC:02CA */
    if (!strcmp(t, "YEARDONE")) {
        if (env.sound) env.sound(env.user, TT_SND_YEARDONE);
        return;
    }  /* D2DC:02E6 */
    if (!strcmp(t, "YEAR")) {   /* D70D:0000: era name of the chapter in the current slot */
        int ch = val(LEDV_CHAPTER, 0);
        if (ch >= 0 && ch <= 6) {
            char k[8];
            snprintf(k, sizeof k, "era%d", ch);
            snprintf(o, TOK_MAX, "%s", str(k));
        }
        return;
    }
}

/* D47D:01A5 / D47D:1646: expand the record's text into the line buffer, padded to 16 with spaces. */
static void line(int i)
{
    const char *s = recs[i].text;
    int n = 0;
    while (*s && n < (int)sizeof buf - 1) {
        if (*s == '<' && strchr(s, '>')) {
            char t[24], v[TOK_MAX];
            const char *e = strchr(s, '>');
            snprintf(t, sizeof t, "%.*s", (int)(e - s - 1), s + 1);
            token(t, v);
            for (const char *q = v; *q && n < (int)sizeof buf - 1;) buf[n++] = *q++;
            s = e + 1;
        } else {
            buf[n++] = *s++;
        }
    }
    while (n < 16) buf[n++] = ' ';
    buf[16] = 0;   /* the LED shows 16 characters */
    pos = 0;
    mode = recs[i].mode;
    timer = recs[i].time;
}

/* D47D:00F3 show(id): the first record with that id (D47D:009A). */
void ledm_show(int id)
{
    for (int i = 0; i < nrecs; i++)
        if (recs[i].id == id && recs[i].text[0]) {
            active = 1;
            cur = i;
            line(i);
            return;
        }
}

/* D47D:107B: the record after the current line. */
static void next(void)
{
    int id = recs[cur].id;
    if (++cur >= nrecs) {
        active = 0;
        return;
    }
    const rec *r = &recs[cur];
    if (r->id == 0 && r->mode == 2 && r->time == 0)
        ledm_show(id);        /* restart */
    else if (r->id == 0 && r->mode == 3 && r->time != 0)
        ledm_show(id);   /* no special object: restart */
    else if (r->id == 0 && r->mode == 1) {                                /* end: D47D:1237, blank line */
        active = 0;
        memcpy(out, str("blank16"), 16);
        pos = 16;
    } else
        line(cur);
}

/* D47D:1280, every other 30 Hz tick: type one character (mode 1) or show the line (mode 2). */
static void tick15(void)
{
    if (pos < 16 && buf[pos]) {
        if (mode == 1) {
            out[pos] = buf[pos];
            pos++;
        } else if (mode == 2) {
            memcpy(out, buf, 16);
            pos = 16;
        }
    }
    if (mode != 8 && active && timer == 0) next();
}

/* The timer ISR's odd ticks (D245:035E-0395): line timer at 30 Hz, display every other one. */
void ledm_tick30(void)
{
    if (timer > 0) timer--;
    if (!(phase++ & 1)) tick15();
}

const char *ledm_line(void) { return out; }
void ledm_set_line(const char *s) { memcpy(out, s, 16); }
void ledm_stop(void)
{
    active = 0;
    pos = 16;
}   /* D47D:13B3/14AB write the LED directly; the message stops */
