/* Time Traveler game engine. See engine.h. Keep in step with tools/sim.py (parity test:
 * port/3ds/host/Makefile `make parity`). Every random draw happens in the same order as sim.py. */
#include "engine.h"

#include <string.h>

#define MAX_QUEUE 64

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }

/* ---- Loading the packed scene graph (.ttg) ------------------------------------------------------------------- */

/* A read position in the blob. Every read checks `has` first, so a truncated or damaged file (e.g. a
 * half-finished upload) is rejected instead of read past its end. */
typedef struct {
    const uint8_t *p, *end;
} cursor;

static int has(const cursor *c, int n) { return n >= 0 && c->end - c->p >= n; }

/* Steps over n NUL-terminated strings; 0 if one runs off the end. */
static int skip_strings(cursor *c, int n)
{
    for (int i = 0; i < n; i++) {
        while (c->p < c->end && *c->p) c->p++;
        if (!has(c, 1)) return 0;
        c->p++;
    }
    return 1;
}

/* The optional sections after the Time Map (older packs end before them): {u16 count, count entries}. Each
 * entry is walked to find the next section; *start points at the first entry, NULL if the section is absent. */
enum { SEC_MESSAGES, SEC_SOUNDS, SEC_STRINGS };
static int optional_section(cursor *c, int kind, const uint8_t **start, int *count)
{
    *start = NULL;
    *count = 0;
    if (!has(c, 2)) return 1;
    int n = rd16(c->p);
    c->p += 2;
    *start = c->p;
    *count = n;
    for (int i = 0; i < n; i++) {
        if (kind == SEC_MESSAGES) { /* {u16 id, u8 mode, u8 pad, u16 time, text\0} */
            if (!has(c, 6)) return 0;
            c->p += 6;
            if (!skip_strings(c, 1)) return 0;
        } else if (kind == SEC_SOUNDS) { /* {u8 id, u8 steps, steps x {u16 freq, u16 len}} */
            if (!has(c, 2) || !has(c, 2 + 4 * c->p[1])) return 0;
            c->p += 2 + 4 * c->p[1];
        } else if (!skip_strings(c, 2)) { /* {key\0 value\0} */
            return 0;
        }
    }
    return 1;
}

/* Loads the packed scene graph (tools/pack_game.py has the layout). Returns 0, or -1 not a TTG7 pack, -2 a
 * table too big or a record's rules outside the rule table, -3 truncated, -4 a bad Time Map.
 * The tables live in static storage: every tt_game loaded from the same blob shares them (both players
 * of a 2-player game do), and loading a different blob replaces them for all. */
int tt_load(tt_game *g, const void *blob, int len)
{
    static tt_ep eps[512];
    static tt_rule rules[2048];
    static tt_subst subst[16];
    static tt_clip clips[512];
    static tt_tmap tmap[128];
    cursor c = {blob, (const uint8_t *)blob + (len > 0 ? len : 0)};
    const uint8_t *p;

    /* Header: magic, table sizes, the 5x5 weights */
    if (!has(&c, 37)) return -3;
    if (memcmp(c.p, "TTG7", 4)) return -1; /* TTG7: full LED table + strings */
    int ne = rd16(c.p + 4), nr = rd16(c.p + 6), ns = rd16(c.p + 8), nc = rd16(c.p + 10);
    if (ne > 512 || nr > 2048 || ns > 16 || nc > 512) return -2;
    memcpy(g->weights, c.p + 12, 25);
    c.p += 37;

    /* Fixed-size records: episodes (20 bytes), rules (22), substitutions (14), clips (12) */
    if (!has(&c, ne * 20 + nr * 22 + ns * 14 + nc * 12)) return -3;
    for (int i = 0; i < ne; i++, c.p += 20) {
        p = c.p;
        eps[i] = (tt_ep){rd16(p),     rd16(p + 2),  p[4],         p[5],         p[6],         p[7],
                         rd16(p + 8), rd16(p + 10), rd16(p + 12), rd16(p + 14), rd16(p + 16), rd16(p + 18)};
    }
    for (int i = 0; i < nr; i++, c.p += 22) {
        p = c.p;
        rules[i] = (tt_rule){rd16(p), rd16(p + 2), rd16(p + 4), rd16(p + 6), rd16(p + 8), rd16(p + 10), p[12],
                             p[13],   p[14],       p[15],       p[16],       p[17],       rd16(p + 18), rd16(p + 20)};
    }
    for (int i = 0; i < ns; i++, c.p += 14) {
        p = c.p;
        subst[i] =
            (tt_subst){rd16(p), rd16(p + 2), p[4], p[5], {{rd16(p + 6), rd16(p + 8)}, {rd16(p + 10), rd16(p + 12)}}};
    }
    for (int i = 0; i < nc; i++, c.p += 12) {
        p = c.p;
        clips[i] = (tt_clip){rd16(p), rd16(p + 2), rd32(p + 4), rd16(p + 8), p[10], p[11]};
    }
    for (int i = 0; i < ne; i++) /* each record's rules lie inside the table (play_record keeps 256 per record) */
        if (eps[i].first_rule + eps[i].n_rules > nr || eps[i].n_rules > 256) return -2;

    /* The clips' source names (one per tt_clip.source; unused since the laserdisc became the source) */
    int ntitles = 0;
    for (int i = 0; i < nc; i++)
        if (clips[i].source + 1 > ntitles) ntitles = clips[i].source + 1;
    if (!skip_strings(&c, ntitles)) return -3;

    /* Time Map: {u16 key, u8 n, u8 pad, 4 x {s16 option, u16 still}} */
    if (!has(&c, 2)) return -3;
    int nt = rd16(c.p);
    c.p += 2;
    if (nt > 128) return -4;
    if (!has(&c, nt * 20)) return -3;
    for (int i = 0; i < nt; i++, c.p += 20) {
        p = c.p;
        tmap[i] = (tt_tmap){rd16(p), p[2], {0}, {0}};
        if (tmap[i].n > 4) return -4;
        for (int k = 0; k < 4; k++) {
            tmap[i].opt[k] = (int8_t)rd16(p + 4 + 4 * k);
            tmap[i].still[k] = rd16(p + 6 + 4 * k);
        }
    }

    /* LED messages, sounds, LED token strings */
    if (!optional_section(&c, SEC_MESSAGES, &g->msgs, &g->n_msgs) ||
        !optional_section(&c, SEC_SOUNDS, &g->sounds, &g->n_sounds) ||
        !optional_section(&c, SEC_STRINGS, &g->strings, &g->n_strings))
        return -3;

    g->tmap = tmap;
    g->n_tmap = nt;
    g->eps = eps;
    g->n_eps = ne;
    g->rules = rules;
    g->n_rules = nr;
    g->subst = subst;
    g->n_subst = ns;
    g->clips = clips;
    g->n_clips = nc;
    return 0;
}

/* ---- Random numbers and episode lookup ----------------------------------------------------------------------- */

/* xorshift32, same as tools/sim.py XorShift */
uint32_t tt_rand(tt_game *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g->rng = x;
}
int tt_randrange(tt_game *g, int n) { return (int)(tt_rand(g) % (uint32_t)n); }
static int chance(tt_game *g, int p) { return tt_randrange(g, 100) < p; }

static int find(const tt_game *g, int ch, int ep)
{
    for (int i = 0; i < g->n_eps; i++)
        if (g->eps[i].kind != TT_KIND_DEATH && g->eps[i].chapter == ch && g->eps[i].episode == ep) return i;
    return -1;
}

/* chosen[] write that ignores a missing (chapter, episode): find() returns -1 then. */
static void set_chosen(tt_game *g, int idx, uint8_t v)
{
    if (idx >= 0 && idx < (int)sizeof g->chosen) g->chosen[idx] = v;
}

static int next_header(const tt_game *g, int i)
{
    for (i++; i < g->n_eps; i++)
        if (g->eps[i].kind != TT_KIND_DEATH) return i;
    return -1;
}

/* ---- Per-game selection (C359:0034 game_choose) -------------------------------------------------------------- */

/* A run of pick-one scenes starting at header i (it ends at a scene flagged as a new run): one of them is
 * drawn by weight; a bonus one only counts in the bonus chapter. Returns the pick; *last = the run's last. */
static int pick_one(tt_game *g, int i, int *last)
{
    int run[32], n = 0;
    run[n++] = *last = i;
    for (int k = next_header(g, i); k >= 0 && n < 32; k = next_header(g, k)) {
        const tt_ep *x = &g->eps[k];
        if ((x->kind != TT_KIND_PICK_ONE && x->kind != TT_KIND_PICK_ONE_BONUS) || (x->flags & 1)) break;
        run[n++] = *last = k;
    }
    int r = tt_randrange(g, 100), acc = 0;
    for (int k = 0; k < n; k++) {
        const tt_ep *x = &g->eps[run[k]];
        acc += x->weight;
        if (r < acc && (x->kind == TT_KIND_PICK_ONE || x->chapter == g->bonus)) return run[k];
    }
    int pick = run[n - 1];   /* SIM: last plain pick_one of the run */
    for (int k = 0; k < n; k++)
        if (g->eps[run[k]].kind == TT_KIND_PICK_ONE) pick = run[k];
    return pick;
}

/* Half the games swap in a rare scene: each substitution group outside the bonus chapter gets a 3% draw in
 * turn until one hits; its scene is chosen and the scenes it replaces are dropped. */
static void rare_substitution(tt_game *g)
{
    int groups[16], ng = 0;
    for (int k = 0; k < g->n_subst; k++)
        if (g->subst[k].in_ch != g->bonus) groups[ng++] = k;
    int hit = -1;
    while (ng && hit < 0)
        for (int k = 0; k < ng; k++)
            if (chance(g, 3)) {
                hit = groups[k];
                break;
            }
    if (hit >= 0) {
        const tt_subst *s = &g->subst[hit];
        set_chosen(g, find(g, s->in_ch, s->in_ep), 1);
        for (int k = 0; k < s->n_out; k++) set_chosen(g, find(g, s->out[k][0], s->out[k][1]), 0);
    }
}

/* C359:0034 game_choose (+ D668:012B Hellgate slot): which scenes this game plays, the bonus chapter and
 * the Hellgate slot. Every draw is in sim.py's order (parity). */
static void choose(tt_game *g)
{
    g->bonus = 2 + tt_randrange(g, 4);
    memset(g->chosen, 0, sizeof g->chosen);
    for (int i = (g->eps[0].kind == TT_KIND_DEATH ? next_header(g, 0) : 0); i >= 0; i = next_header(g, i)) {
        const tt_ep *e = &g->eps[i];
        if (e->kind == TT_KIND_SYSTEM) {
            g->chosen[i] = 1;
        } else if (e->kind == TT_KIND_CHANCE) {
            if (chance(g, e->weight)) g->chosen[i] = 1;
        } else if (e->kind == TT_KIND_CHANCE_BONUS) {
            if (e->chapter == g->bonus && chance(g, e->weight)) g->chosen[i] = 1;
        } else if (e->kind == TT_KIND_PICK_ONE || e->kind == TT_KIND_PICK_ONE_BONUS) {
            g->chosen[pick_one(g, i, &i)] = 1; /* i moves to the end of the run */
        }
    }
    if (chance(g, 50)) rare_substitution(g);
    static const int slots[4] = {4, 5, 6, 7};
    g->hellgate_slot = chance(g, 85) ? slots[tt_randrange(g, 4)] : -1;
}

void tt_new_game(tt_game *g, uint32_t seed, const tt_settings *s, const tt_frontend *fe)
{
    g->set = *s;
    g->fe = *fe;
    g->rng = (uint32_t)(seed * 2654435761u + 0x9E3779B9u);
    if (!g->rng) g->rng = 1;
    g->lives = s->lives;
    g->cubes = 0;
    g->score = 0;   /* first cube from C1 E5 (DA9D:0F3A) */
    g->good_eps = 0;
    g->hard = 0;
    g->death_t = 0;
    g->players = 1;
    g->learn = 1;
    g->mercy_eps = 0;
    g->mercy_ok = 1;
    g->mercy_frames = 0;
    g->mercy_given = 0;
    g->prev_pts = g->cur_pts = 0;
    g->played = 0;
    g->played_hash = 0;
    memset(g->status, 0, sizeof g->status);
    choose(g);
}

/* ---- Scoring and difficulty (D047) --------------------------------------------------------------------------- */

/* Difficulty (C2BA:079D): g[0x1C] = revert threshold, g[0x2C] = good episodes until variant-b windows.
 * DA9D:0DE5 counts good episodes and sets the flag at == g[0x2C]; episode_fail_to_death clears it while
 * the count is < g[0x1C]; chapter_next clears it at chapters 0 and 1. window_is_open (CFD9:0249) uses
 * +2/+4 (variant a) or +6/+8 (variant b) by that flag. */
static const int diff_revert[4] = {5, 6, 11, 8}, diff_harden[4] = {3, 4, 7, 5};

/* Score bands: > 100000, > 90000, else (D047:0761, D047:0875, D047:0996). */
static int band(const tt_game *g) { return g->score > 100000 ? 2 : g->score > 90000 ? 1 : 0; }

/* D047:0761 score_add: a hit */
static void add_points(tt_game *g, int pts)
{
    static const int mult[3] = {2, 1, 10};
    g->score += (uint32_t)(pts * mult[band(g)]);
}

/* D047:0875: a scene finished without a death */
static void add_scene_bonus(tt_game *g, int pts)
{
    static const int mult[3] = {10, 5, 50};
    g->score += (uint32_t)(pts * mult[band(g)]);
}

/* D047:0996: no scene left in the chapter */
static void add_chapter_bonus(tt_game *g, int ch)
{
    int b = band(g);
    if (b == 2)
        g->score += 50000;
    else if (b == 1)
        g->score += 5000;
    else if (ch != 0)
        g->score += 10000;
}

/* ---- Hellgate (D0F0:025E) ------------------------------------------------------------------------------------ */

/* D0F0:025E hellgate */
static void hellgate(tt_game *g)
{
    g->hellgate_offer = 1;
    if (!g->fe.accept_hellgate(g)) return;
    int r = tt_randrange(g, 100);
    static const int lim_take[6] = {5, 45, 55, 60, 90, 100}, lim_never[6] = {5, 30, 35, 60, 90, 100};
    const int *lim = g->set.devil == 0 ? lim_take : lim_never;
    int k = 0;
    while (r >= lim[k]) k++;
    if (k == 1)
        g->lives++;
    else if (k == 2)
        g->cubes += 2;   /* cubes_add (c000_fc7f) has no cap */
    long s = (long)g->score;
    if (k <= 2)
        s += 25000;
    else if (g->set.devil == 0) {
        s -= 25000;
        if (k <= 4)
            g->lives--;
        else {
            g->lives = 0;
            g->mercy_ok = 0;
        }
    }
    g->score = (uint32_t)(s < 0 ? 0 : s);
    if (g->fe.hellgate_result) g->fe.hellgate_result(g, k);
    /* second offer after a jackpot with the devil allowed: accepting it always loses everything */
    if (k == 0 && g->set.devil == 0) {
        g->hellgate_offer = 2;
        if (g->fe.accept_hellgate(g)) {
            g->lives = 0;
            g->mercy_ok = 0;
            g->score = g->score > 25000 ? g->score - 25000 : 0;
            if (g->fe.hellgate_result) g->fe.hellgate_result(g, 5);
        }
    }
}

/* ---- One record, frame by frame (CFD9: windows and rule actions) --------------------------------------------- */

enum { R_NONE = -1, R_DONE, R_REWIND, R_JUMP, R_FAIL, R_DEATH, R_REPLAY, R_SKIP };

/* One phase of a rule's action (tools/sim.py Game.act). Returns the action's code (0/1/2); an outcome
 * that ends the record goes in *res (R_NONE otherwise), with *arg for a death or replay. */
static int act(tt_game *g, const tt_ep *ep, int op, int ret, int a, int *res, int *arg)
{
    *res = R_NONE;
    switch (op) {
    case TT_OP_SCORE: add_points(g, ep->points); break;
    case TT_OP_DONE: *res = R_DONE; break;
    case TT_OP_DEATH:
        *res = R_DEATH;
        *arg = a;
        break;
    case TT_OP_JUMP:
        *res = R_JUMP;
        g->jump_ep = a;
        break;
    case TT_OP_FAIL:
        if (g->fe.fx) g->fe.fx(g, TT_FX_SEGMENT, TT_SEG_TRAP);
        *res = R_FAIL;
        break;
    case TT_OP_REWIND: *res = R_REWIND; break;
    case TT_OP_REPLAY:
        *res = R_REPLAY;
        *arg = a;
        break;
    case TT_OP_HELLGATE: hellgate(g); break;
    case TT_OP_CUBES:
        if (a == 0 || g->cubes == 0) g->cubes = 1;
        break;
    case TT_OP_SKIP:
        g->learn = 0;
        *res = R_SKIP;
        break;
    case TT_OP_SPEED:
        if (g->fe.fx) g->fe.fx(g, TT_FX_SPEED, a);
        break;
    case TT_OP_SOUND:
        if (g->fe.fx) g->fe.fx(g, TT_FX_SOUND, a);
        break;
    case TT_OP_LEARN:
        g->learn = 1;
        if (g->fe.fx) g->fe.fx(g, TT_FX_SEGMENT, TT_SEG_LEARN_WALK);
        *res = R_DONE;
        break;
    }
    return ret;
}

int tt_rule_good(const tt_rule *r) { return (r->flags & TT_RF_GOOD) != 0; }

/* window_is_open (CFD9:0249): variant b once the player is hardened (g->hard), else variant a. */
void tt_window(const tt_game *g, const tt_rule *r, int *lo, int *hi)
{
    *lo = g->hard ? r->b_lo : r->a_lo;
    *hi = g->hard ? r->b_hi : r->a_hi;
}

typedef struct {
    int frame, input;
} press;

/* order[] = the record's rules sorted by (opening frame, window), stable: the order windows are asked in. */
static void sort_by_opening(const tt_rule *r0, int n, int *order)
{
    for (int i = 0; i < n; i++) order[i] = i;
    for (int i = 1; i < n; i++)   /* insertion sort */
        for (int j = i; j > 0; j--) {
            const tt_rule *a = &r0[order[j - 1]], *b = &r0[order[j]];
            if (a->a_lo < b->a_lo || (a->a_lo == b->a_lo && a->window <= b->window)) break;
            int t = order[j];
            order[j] = order[j - 1];
            order[j - 1] = t;
        }
}

/* The frame to press in a window's rules: the middle of where variants a and b are both open (so the press
 * lands whichever the difficulty picks), or its start if that span is empty. */
static int press_frame(const tt_rule *const *grp, int ng)
{
    int min_a = 1 << 30, min_b = 1 << 30, max_a = 0, max_b = 0;
    for (int k = 0; k < ng; k++) {
        if (grp[k]->a_lo < min_a) min_a = grp[k]->a_lo;
        if (grp[k]->b_lo < min_b) min_b = grp[k]->b_lo;
        if (grp[k]->a_hi > max_a) max_a = grp[k]->a_hi;
        if (grp[k]->b_hi > max_b) max_b = grp[k]->b_hi;
    }
    int lo = min_a > min_b ? min_a : min_b, hi = max_a < max_b ? max_a : max_b;
    return (lo + hi) / 2 > lo ? (lo + hi) / 2 : lo;
}

/* Scripted presses for a record (choose_input front ends): each window, in opening order, is asked once with
 * all its rules; a chosen input is pressed at press_frame, moved on a frame while another press holds it. */
static int schedule(tt_game *g, const tt_ep *ep, press *out, int max)
{
    const tt_rule *r0 = &g->rules[ep->first_rule];
    int n = ep->n_rules, order[256], np = 0;
    sort_by_opening(r0, n, order);
    int done_w[256], nw = 0;
    for (int i = 0; i < n; i++) {
        int w = r0[order[i]].window, seen = 0;
        for (int k = 0; k < nw; k++)
            if (done_w[k] == w) seen = 1;
        if (seen) continue;
        done_w[nw++] = w;
        const tt_rule *grp[64];
        int ng = 0;
        for (int k = i; k < n && ng < 64; k++)
            if (r0[order[k]].window == w) grp[ng++] = &r0[order[k]];
        int x = g->fe.choose_input(g, ep, grp, ng);
        if (!x || np >= max) continue;
        int f = press_frame(grp, ng);
        for (int k = 0; k < np; k++)
            if (out[k].frame == f) {
                f++;
                k = -1;
            }
        out[np++] = (press){f, x};
    }
    return np;
}

/* A record's length in LD frames: its clip, or (no clip: an attract gap) the end of its last window. */
static int record_length(const tt_game *g, const tt_ep *ep)
{
    int len = ep->ld_frames;
    if (!len)
        for (int i = 0; i < ep->n_rules; i++)
            if (g->rules[ep->first_rule + i].a_hi + 1 > len) len = g->rules[ep->first_rule + i].a_hi + 1;
    return len;
}

static int in_list(const uint16_t *a, int n, int v)
{
    for (int i = 0; i < n; i++)
        if (a[i] == v) return 1;
    return 0;
}

/* One record frame by frame, as the ROM evaluates it (tools/sim.py Game.play_record).
 * Input at frame t: CFD9:0082 walks the rules in table order; each open, uncleared rule whose move
 * matches runs phase 1. Each frame CFD9:0316 lets closing, uncleared rules that may fire on close run
 * phase 2. Returns R_DONE, R_DEATH (*death), R_JUMP, R_FAIL or R_REWIND. */
static int play_record(tt_game *g, const tt_ep *ep, int start, int *death)
{
    const tt_rule *r0 = &g->rules[ep->first_rule];
    int n = ep->n_rules, len = record_length(g, ep);
    press sched[128];
    int np = g->fe.frame_input ? 0 : schedule(g, ep, sched, 128);
    uint16_t cleared[256];
    int nc = 0;
    for (int t = start; t <= len; t++) {
        int x = 0;
        if (g->fe.frame_input)
            x = g->fe.frame_input(g, ep, t);
        else
            for (int k = 0; k < np; k++)
                if (sched[k].frame == t) x = sched[k].input;
        /* input_event (c000_e619): a Start event with 1P and 2P Start both down while the slot holds chapter 0
         * plays LD 248-350 and goes to chapter_next (CE55:0138); the learn flag is left as it is. */
        if ((x & (TT_IN_START1 | TT_IN_START2)) == (TT_IN_START1 | TT_IN_START2) && ep->chapter == 0) {
            if (g->fe.fx) g->fe.fx(g, TT_FX_SEGMENT, TT_SEG_INTRO_SKIP);
            g->death_t = t;
            return R_SKIP;
        }
        for (int phase = 1; phase <= 2; phase++) {
            if (phase == 1 && !x) continue;
            for (int i = 0; i < n; i++) {
                const tt_rule *r = &r0[i];
                int lo, hi, ret, res, arg = 0, hit = 0;
                tt_window(g, r, &lo, &hi);
                if (in_list(cleared, nc, r->window)) continue;
                if (phase == 1) {
                    if (t < lo || t > hi) continue;
                    hit = (r->inputs & x) != 0;
                    if (hit && (r->flags & TT_RF_BEEP) && g->fe.fx) g->fe.fx(g, TT_FX_SOUND, TT_SND_BEEP);
                    ret = hit ? act(g, ep, r->hit_op, r->hit_ret, r->hit_arg, &res, &arg) : (res = R_NONE, 0);
                } else {
                    if (hi > t + 1 || hi <= start || !(r->flags & TT_RF_CLOSES)) continue;
                    ret = act(g, ep, r->expire_op, r->expire_ret, r->expire_arg, &res, &arg);
                }
                if (res == R_REPLAY) return play_record(g, ep, arg, death);
                if (res == R_DEATH && g->fe.fx) {   /* death(n): message 0x8D (D047:0626), death tune (D047:053A) */
                    g->fe.fx(g, TT_FX_EVENT, TT_EV_DEATH);
                    g->fe.fx(g, TT_FX_SOUND, TT_SND_DEATH);
                }
                if (res != R_NONE) {
                    g->death_t = t;
                    *death = arg;
                    return res;
                }
                if (ret == 1 || (phase == 2 && ret == 2)) {
                    if (nc < 256) cleared[nc++] = r->window;
                    break;
                }
                if (ret == 2) break;
                if (phase == 1 && (r->flags & TT_RF_STOP_ON_MISS)) break;
            }
        }
    }
    return R_DONE;
}

/* ---- An episode with its deaths, cubes and lives (D047, D7C0) ------------------------------------------------ */

static int idx_to_header_index(const tt_game *g, int idx)
{
    int n = 0;
    for (int i = 0; i < idx; i++)
        if (g->eps[i].kind != TT_KIND_DEATH) n++;
    return n;
}

enum { E_DONE, E_FAILED, E_JUMP, E_SKIP, E_TRAP };

/* The mercy life (D7C0:0616 / D7C0:038F) is still on offer: the countdown [p*4+0x150E], loaded with 300 at
 * chapter 0/1 entry, has not run out (counted here as LD frames played). */
#define MERCY_FRAMES 300
static int mercy_open(const tt_game *g) { return g->mercy_ok && g->mercy_eps <= 6 && g->mercy_frames < MERCY_FRAMES; }

/* After a life is lost: with none left, the mercy life if it is still on offer (LED "EXTRA LIFE"). */
static void mercy_life(tt_game *g)
{
    if (g->lives < 1 && mercy_open(g)) {
        g->lives++;
        g->mercy_given = 1;
    }
}

/* episode_enter (C40B:08A8) saves the previous record at [p*4+0x1582]; after_death reads its f6. */
static void enter_rec(tt_game *g, int pts)
{
    g->prev_pts = g->cur_pts;
    g->cur_pts = pts;
}

/* D63D:0002 hint, from after_death: see tools/sim.py Game.hint. */
static void hint(tt_game *g, const tt_ep *ep, const tt_ep *death)
{
    unsigned v = death->points;   /* the death record's f6 is the hint word */
    int unused = g->trader_flag != 2, key = 0;
    if (!v) {
        if (g->lives > 0 && ep->points) {
            if (g->cubes < 1) {
                if (chance(g, 25) && unused) key = 0x15;
            } else if (chance(g, 25))
                key = 0x14;
            else if (chance(g, 25))
                key = 0x16;
        }
    } else {
        int p = g->hard ? (int)(v & 0x1F) << 2 : (int)(v & 0x3E0) >> 3;
        if (chance(g, p) && (((v >> 8) & 0xFC) != 0x54 || unused)) key = (int)(v >> 10);
    }
    if (key && g->fe.hint) g->fe.hint(g, key);
}

/* The C3 E82 trap (DA9D:0E6D, d000_b83d): the scene is marked cleared, lose_life, the death flag 0x100A, then
 * episode_done -> after_death (d000_82a5): lose_life again when the record saved as previous has a non-zero
 * f6, the hint with this record, mercy, and in 2P the switch (038F). */
static int trap(tt_game *g, const tt_ep *ep, int idx)
{
    g->status[idx] = 1;
    int second = g->prev_pts != 0;
    if (ep->chapter != 0) g->lives--; /* lose_life (DA9D:04AE) */
    if (second && ep->chapter != 0) g->lives--;
    hint(g, ep, ep);
    mercy_life(g);
    if (g->fe.fx) g->fe.fx(g, TT_FX_EVENT, TT_EV_LIFE_LOST);
    if (second && g->fe.life_lost) g->fe.life_lost(g);
    return E_TRAP;
}

/* episode_fail_to_death: enter death record `death` of the episode at idx (its sub-records follow it); returns
 * its index, -1 if absent. */
static int enter_death(tt_game *g, int idx, int death)
{
    for (int k = idx + 1; k < g->n_eps && g->eps[k].kind == TT_KIND_DEATH; k++)
        if (g->eps[k].episode == death) {
            g->mercy_frames += g->eps[k].ld_frames;
            enter_rec(g, g->eps[k].points);
            if (g->fe.on_death) g->fe.on_death(g, &g->eps[k]);
            return k;
        }
    return -1;
}

/* after_death (D7C0:06A5) once the death was not rewound: a scoring scene costs a life, then the hint, the
 * mercy life and in 2P the hand-over. */
static int scene_failed(tt_game *g, const tt_ep *ep, int idx, int dk)
{
    g->status[idx] = 2;
    int lost = ep->points && ep->chapter != 0;
    if (lost) g->lives--;
    if (dk >= 0) hint(g, ep, &g->eps[dk]);
    mercy_life(g);
    if (lost && g->fe.fx) g->fe.fx(g, TT_FX_EVENT, TT_EV_LIFE_LOST);
    if (lost && g->fe.life_lost) g->fe.life_lost(g);
    return E_FAILED;
}

/* One episode until it is done, failed, left by a jump or skip, or trapped. A death offers a cube (rewind
 * and play on), else costs the scene. */
static int play_episode(tt_game *g, int idx)
{
    const tt_ep *ep = &g->eps[idx];
    g->played++;
    g->played_hash = (int)((uint32_t)g->played_hash * 31u + (uint32_t)idx_to_header_index(g, idx) + 1u);
    if (g->fe.on_episode) g->fe.on_episode(g, ep);
    enter_rec(g, ep->points);
    int len = record_length(g, ep);
    for (int start = 0;;) {
        int death = 0, res = play_record(g, ep, start, &death);
        /* the mercy countdown [p*4+0x150E] runs at 30 a second of play (romdiff --watch 0x150E): count the
         * frames actually played, not the whole clip up front */
        int upto = res == R_DONE ? len : g->death_t;
        if (upto > start) g->mercy_frames += upto - start;
        if (res == R_FAIL) return trap(g, ep, idx);
        if (res == R_JUMP) return E_JUMP;
        if (res == R_SKIP) return E_SKIP;
        if (res == R_DONE && ++g->good_eps == diff_harden[g->set.difficulty & 3]) g->hard = 1;
        if (res != R_DEATH) return E_DONE;
        if (g->good_eps < diff_revert[g->set.difficulty & 3]) g->hard = 0;
        int dk = enter_death(g, idx, death);
        if (g->cubes > 0 && g->fe.use_cube(g, ep)) {
            g->cubes--;
            if (g->fe.fx) g->fe.fx(g, TT_FX_EVENT, TT_EV_CUBE_USED);
            /* D047:0679: re-enter 3 s (3 x 30 frames, g[0x100C]) before the death */
            start = g->death_t > 90 ? g->death_t - 90 : 0;
            enter_rec(g, ep->points); /* D047:0679 re-enters the saved scene */
            continue;
        }
        return scene_failed(g, ep, idx, dk);
    }
}

/* ---- A chapter: its scene queue (D70D) ----------------------------------------------------------------------- */

/* D668:05BB: C6 E50 is offered when E22 was chosen, E30 or E40 chosen, none failed, chance(80). */
static int c6e50_allowed(tt_game *g)
{
    int e22 = find(g, 6, 22), e30 = find(g, 6, 30), e40 = find(g, 6, 40);
    if (e22 < 0 || e30 < 0 || e40 < 0) return 0;
    if (!g->chosen[e22] || !(g->chosen[e30] || g->chosen[e40])) return 0;
    if (g->status[e22] == 2 || g->status[e30] == 2 || g->status[e40] == 2) return 0;
    return chance(g, 80);
}

/* Scoring scenes of a chapter: chosen headers with points, not E10 (D70D:0973 / D70D:0A84). */
static int is_scoring(const tt_game *g, int i, int ch)
{
    const tt_ep *e = &g->eps[i];
    return e->kind != TT_KIND_DEATH && e->kind != TT_KIND_SYSTEM && e->chapter == ch && g->chosen[i] &&
        e->episode != 10 && e->points;
}

/* D70D:0A84: a chosen scoring scene of ch has been played -> revisit (failed scenes only). */
static int revisiting(const tt_game *g, int ch)
{
    for (int i = 0; i < g->n_eps; i++)
        if (is_scoring(g, i, ch) && g->status[i]) return 1;
    return 0;
}

/* D70D:0973: a chosen scoring scene was played and none is currently failed. */
static int chapter_completed(const tt_game *g, int ch)
{
    int played = 0;
    for (int i = 0; i < g->n_eps; i++)
        if (is_scoring(g, i, ch)) {
            if (g->status[i] == 2) return 0;
            if (g->status[i]) played = 1;
        }
    return played;
}

/* The chapter's chosen scenes in table order (D70D:0661 -> D70D:06B7); a revisit queues only failed ones. */
static int chapter_queue(const tt_game *g, int ch, int *queue)
{
    int nq = 0;
    for (int i = 0; i < g->n_eps && nq < MAX_QUEUE; i++) {
        const tt_ep *e = &g->eps[i];
        if (e->kind != TT_KIND_DEATH && e->kind != TT_KIND_SYSTEM && e->chapter == ch && g->chosen[i]) queue[nq++] = i;
    }
    if (revisiting(g, ch)) {
        int k = 0;
        for (int i = 0; i < nq; i++)
            if (g->status[queue[i]] == 2) queue[k++] = queue[i];
        nq = k;
    }
    return nq;
}

/* episode_done for a scene finished without a death: marked done, the scene bonus, and the mercy count. */
static void scene_done(tt_game *g, int ch, int e)
{
    g->status[e] = 1;
    /* episode_done (D70D:0C7A-0D60): C1 E30 done also marks E20 and E21, E21 marks E20 */
    if (ch == 1 && (g->eps[e].episode == 21 || g->eps[e].episode == 30)) {
        int a20 = find(g, 1, 20), a21 = find(g, 1, 21);
        if (a20 >= 0) g->status[a20] = 1;
        if (g->eps[e].episode == 30 && a21 >= 0) g->status[a21] = 1;
    }
    g->mercy_eps++;
    add_scene_bonus(g, g->eps[e].points);
    if (g->fe.fx) g->fe.fx(g, TT_FX_EVENT, TT_EV_SCENE_DONE);
}

/* One visit to chapter ch: one pass over its queue. Failed scenes wait for a later visit, except in chapter 1
 * after the opening duel, where a death replays the scene. The chapter bonus needs the last scene good. */
static void chapter(tt_game *g, int ch)
{
    if (ch == 0 || ch == 1) {
        g->mercy_eps = 0;
        g->mercy_frames = 0;
    }
    if (ch == 0) g->learn = 1; /* D70D:040E */
    if (ch == 6 && c6e50_allowed(g)) set_chosen(g, find(g, 6, 50), 1);
    int queue[MAX_QUEUE], nq = chapter_queue(g, ch, queue);
    /* Chapter 1 flow (maybe_episode_next C40B:004F), see tools/sim.py Game.chapter */
    int duel_done = 0, retries = 0, e35 = ch == 1 ? find(g, 1, 35) : -1;
    int last_good = 0, queue_started = nq > 0;
    for (int pass = 0; nq && g->lives > 0 && pass < 1; pass++) { /* failures return on a later visit */
        int next[MAX_QUEUE], nn = 0;
        for (int i = 0; i < nq;) {
            int e = queue[i];
            if (g->status[e] == 1) {
                i++;
                continue;
            }
            int res = play_episode(g, e);
            int failed = res == E_FAILED;
            /* episode_done (d000_7c08): with lives left, the mercy flag [p*2+0x34] is cleared once mercy is
             * no longer available (D7C0:0616: countdown out, more than 6 scenes, or used), so the LED says
             * "LAST LIFE" again instead of "EXTRA LIFE". */
            if (g->lives >= 1 && !mercy_open(g)) g->mercy_given = 0;
            last_good = 0;
            if (res == E_SKIP) {
                g->status[e] = 1;
                return;
            } /* chapter_next: rest of the chapter skipped */
            if (res == E_TRAP) {
                if (g->lives <= 0) return;
            } else if (failed) {
                g->status[e] = 2;
                if (g->lives <= 0) return;
            } else if (res == E_JUMP) {
                g->status[e] = 1;
                int t = find(g, 1, g->jump_ep), k;
                for (k = 0; k < nq && queue[k] != t; k++);
                i = k < nq ? k : i + 1;
                continue;
            } else {
                scene_done(g, ch, e);
                last_good = 1;
            }
            if (e35 >= 0) {
                int ep = g->eps[e].episode;
                /* two players: E20 either way goes on to E35 (C40B:004F, D70D:0917(1, 0x23) when g[0x72] >= 2) */
                /* E21 dying goes on to E35 only with the learn flag (C40B:0262); else into E22-E30 */
                if (!duel_done && ((ep == 20 && (!failed || g->players >= 2)) || (ep == 21 && (!failed || g->learn)))) {
                    duel_done = 1;
                    int k;
                    for (k = 0; k < nq && queue[k] < e35; k++);
                    i = k;
                    continue;
                }
                if (duel_done && failed && retries < 50) {
                    retries++;
                    continue;
                } /* replay the scene */
            }
            if (failed) next[nn++] = e;
            i++;
        }
        memcpy(queue, next, sizeof(int) * (size_t)nn);
        nq = nn;
    }
    if (queue_started && last_good && g->lives > 0) add_chapter_bonus(g, ch);
}

/* ---- Lookups for the front end: LED messages, clips, strings, sounds ----------------------------------------- */

const char *tt_message(const tt_game *g, int id, int line, int *time)
{
    const uint8_t *p = g->msgs;
    for (int i = 0; p && i < g->n_msgs; i++) {
        const char *text = (const char *)p + 6;
        if (rd16(p) == id && line-- == 0) {
            if (time) *time = rd16(p + 4);
            return text;
        }
        p = (const uint8_t *)text + strlen(text) + 1;
    }
    return NULL;
}

int tt_chapter_completed(const tt_game *g, int ch) { return chapter_completed(g, ch); }

/* Level select (DSW2 bit 4, g[0x1926]). maybe_episode_next (c000_40ff) asks episode_test_select (c000_4580)
 * for every next scene; lose_life (DA9D:04AE) takes no life; every header is chosen (choose_episodes
 * c000_3646). The picker skips death sub-records (10003, 2000); the system records (chapter 10000, attract
 * only) are left out here. */
int tt_is_header(const tt_game *g, int idx)
{
    return idx >= 0 && idx < g->n_eps && g->eps[idx].kind != TT_KIND_DEATH && g->eps[idx].kind != TT_KIND_SYSTEM;
}

void tt_run_select(tt_game *g)
{
    for (int i = 0; i < g->n_eps; i++)
        if (tt_is_header(g, i)) set_chosen(g, i, 1);
    if (g->fe.fx) g->fe.fx(g, TT_FX_EVENT, TT_EV_GAME_START);
    int i = 0;
    while (i < g->n_eps && !tt_is_header(g, i)) i++;
    while (g->fe.pick_episode && (i = g->fe.pick_episode(g, i)) >= 0) {
        int lives = g->lives;
        play_episode(g, i);
        g->lives = lives; /* lose_life does nothing with level select on */
        do i = (i + 1) % g->n_eps;
        while (!tt_is_header(g, i));
    }
}

int tt_clip_start(const tt_game *g, int clip)
{
    for (int i = 0; i < g->n_clips; i++)
        if (g->clips[i].clip == clip) return (int)g->clips[i].ld_start;
    return -1;
}

const char *tt_string(const tt_game *g, const char *key)
{
    const char *p = (const char *)g->strings;
    for (int i = 0; p && i < g->n_strings; i++) {
        const char *v = p + strlen(p) + 1;
        if (!strcmp(p, key)) return v;
        p = v + strlen(v) + 1;
    }
    return "";
}

int tt_sound(const tt_game *g, int id, const uint8_t **steps)
{
    const uint8_t *p = g->sounds;
    for (int i = 0; p && i < g->n_sounds; i++) {
        if (p[0] == id) {
            *steps = p + 2;
            return p[1];
        }
        p += 2 + 4 * p[1];
    }
    return 0;
}

/* ---- Trader, Time Map and the chapter list (CEDF, D843, C3CE) ------------------------------------------------ */

int tt_buy_cubes(tt_game *g)
{
    /* trader_buy (c000_fbe5): coin n of this visit; cubes_add has no cap, so a coin at full stock still buys */
    int per = g->set.cubes_per_coin ? g->set.cubes_per_coin : 2,
        credits = g->set.cube_credits ? g->set.cube_credits : 1;
    int n = ++g->trader_coin;
    if (per == 4)
        g->cubes += (n & 1) ? 1 : 2;
    else if (n % credits == 0)
        g->cubes += per;
    return g->cubes >= g->set.max_cubes;
}

static void trader(tt_game *g, int automatic)
{
    g->trader_coin = 0;
    if (g->fe.trader) g->fe.trader(g, automatic);
}

/* D843:0127-0241: before the map's cursor loop; cur = [p*2+0x3A], the slot just played. */
static void map_entry(tt_game *g, int cur)
{
    if (cur == 5 && g->slot_ch[5] >= 0 && chapter_completed(g, g->slot_ch[5]) && g->trader_flag != 2 &&
        g->cubes < g->set.max_cubes) {
        trader(g, 1); /* D843:01B3 trader(1) */
        g->trader_flag = 2;
    }
    if (g->trader_flag != 2) g->trader_flag = g->cubes < g->set.max_cubes ? 0 : 1;
}

/* The Time Map's key (D843:0A3D): the bonus chapter's bit (bonus + 8) | a bit per completed chapter 1-19.
 * Chapters past 7 are only played after the last map, so on the map itself only bits 1-7 can be set. */
static uint16_t map_key(const tt_game *g)
{
    unsigned key = 1u << (g->bonus + 8);
    for (int c = 1; c < 20; c++)
        if (chapter_completed(g, c)) key |= 1u << c;
    return (uint16_t)key;
}

unsigned tt_map_key(const tt_game *g) { return map_key(g); }

/* The Time Map's options for its key (D843:0B33, data/time_map.json); chapter -1 is the Trader, offered only
 * while trader_flag is 0 (D843:098C). Sets g->map_rec, the table entry (-1: none, the fallback is used). */
static int map_options(tt_game *g, const int *remaining, int nrem, int *opts)
{
    uint16_t key = map_key(g);
    int n = 0, real = 0;
    g->map_rec = -1;
    for (int i = 0; i < g->n_tmap; i++)
        if (g->tmap[i].key == key && g->map_rec < 0) g->map_rec = i;
    for (int i = 0; i < g->n_tmap; i++)
        if (g->tmap[i].key == key)
            for (int k = 0; k < g->tmap[i].n; k++) {
                int c = g->tmap[i].opt[k];
                if (c >= 0 || (c == -1 && g->trader_flag == 0)) {
                    opts[n++] = c;
                    real += c >= 0;
                }
            }
    if (!real) { /* SIM: mask not in the table */
        g->map_fallbacks++;
        g->map_rec = -1;
        n = 0;
        for (int k = 0; k < nrem; k++) opts[n++] = remaining[k];
        if (!n) opts[n++] = 6;
    }
    return n;
}

/* Chapter list (C3CE:0055): slot 1 = chapter 0, 2 = 1, 3-7 = Time Map, 8 = 7, 9 = 8, 10 = 20.
 * chapter_advance (D70D:0021) moves on only when the chapter just played is completed; otherwise the
 * slot is played again (slots 2-7 through the Time Map, 8-9 the same chapter). Chapter 0 always moves
 * on; after chapter 20 the game is over. See tools/sim.py Game.run. */
void tt_run(tt_game *g)
{
    static const int fixed[11] = {10000, 0, 1, -1, -1, -1, -1, -1, 7, 8, 20};
    int remaining[4] = {2, 3, 4, 5}, nrem = 4;
    g->map_fallbacks = 0;
    g->trader_flag = 0; /* D668:01A7 */
    for (int i = 0; i < 11; i++) g->slot_ch[i] = -1;
    if (g->fe.fx) g->fe.fx(g, TT_FX_EVENT, TT_EV_GAME_START);
    int slot = 1, cur = 0;
    for (int guard = 0; guard < 200 && g->lives > 0 && slot <= 10; guard++) {
        int ch;
        if (fixed[slot] >= 0) { /* an incomplete chapter 1 replays at once (chapter_advance d000_70f1) */
            ch = fixed[slot];
            if (ch == 7 && g->fe.fx) g->fe.fx(g, TT_FX_EVENT, TT_EV_INTO_CH7);
        } else { /* Time Map (D843:00B9), options from D843:0B33 */
            map_entry(g, cur);
            for (;;) {
                int opts[4], n = map_options(g, remaining, nrem, opts); /* tt_tmap holds at most 4 */
                ch = opts[g->fe.map_choice(g, opts, n)];
                if (ch != -1) break;
                trader(g, 0); /* D843:05F1, then the map carries on */
                if (g->trader_flag != 2) g->trader_flag = 1;
            }
            for (int k = 0; k < nrem; k++)
                if (remaining[k] == ch) {
                    for (; k < nrem - 1; k++) remaining[k] = remaining[k + 1];
                    nrem--;
                    break;
                }
        }
        g->slot_ch[slot] = ch;
        chapter(g, ch);
        /* chapter_next (D70D:040E, d000_74de) clears the hard flag when the chapter just played is 0 or 1;
         * confirmed with romdiff --watch 0x13C4: set at C1 E35, cleared at the end of C1, never set again. */
        if (ch == 0 || ch == 1) g->hard = 0;
        if (slot == g->hellgate_slot && g->set.devil != 2 && g->lives > 0) {
            g->hellgate_slot = -1; /* hellgate (d000_115e) zeroes the slot on entry: offered once a game */
            hellgate(g);
        }
        cur = slot;
        if (ch == 20) break;
        if (ch == 0 || chapter_completed(g, ch)) slot++;
    }
}
