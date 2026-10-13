// The engine's callbacks for a scene: frame input, deaths and the Reversal Cube, hints, sounds, speed
// effects and footage segments (tt_frontend in engine.h).

#include "fe.h"

int last_chapter = -1;   // chapter of the episode playing (-1 before the first): <YEAR>, "YOU SOLVED"

// chapter_advance (D70D:0270-02AF): when the chapter just played is completed: the year-done tune
// (D2DC:02E6), "YOU SOLVED THE <YEAR>" (0x3F), then a 3 s wait (D245:07D8(3), wait_input(0x30)).
// chapter_advance (d000_70f1): "YOU SOLVED THE <YEAR>" with the year-done tune when the chapter just
// played is completed. Before the Time Map it goes straight on; on the route into chapter 7 the map
// still for the current mask follows (D843:0A3D) and a 3 s wait that Start ends (D245:07D8(3),
// wait_input(0x30)).
void solved(tt_game *g)
{
    play_sound(g, TT_SND_YEARDONE);
    hud_show(LEDM_SOLVED);
}

static void into_chapter7(tt_game *g)
{
    if (last_chapter >= 1 && last_chapter <= 6 && tt_chapter_completed(g, last_chapter)) solved(g);
    char name[16];
    snprintf(name, sizeof name, "map%04X", tt_map_key(g));
    open_named(name, 0);   // the still (first frame of the mask's map clip); none if the mask has no map
    if (playing_video)
        show_frame(0, 0, 0, 0);
    else
        top_show(NULL, 800, 0, 0, 0);
    plog_line("into chapter 7: map still %s", name);
    for (int v = 0; v < 3 * VBLANKS_PER_S; v++) {   // d000_70f1: 3 s
        if (fe_poll() & KEY_START) break;
        hud(g);
        gspWaitForVBlank();
    }
    playing_video = false;
}

void fe_on_episode(tt_game *g, const tt_ep *ep)
{
    open_clip(ep->clip);
    plog_line("scene C%d E%d clip%03d %s  lives %d cubes %d score %lu", ep->chapter, ep->episode, ep->clip,
              playing_video ? "video" : "NO VIDEO", g->lives, g->cubes, (unsigned long)g->score);
    last_chapter = ep->chapter;
    if (ep->msg) hud_show(ep->msg);   // the record's own message (type-3 asset, C4B6:0158)
    if (*era_name(ep->chapter))
        snprintf(status_line, sizeof status_line, "%-10sSC %d", era_name(ep->chapter), ep->episode);
    else
        snprintf(status_line, sizeof status_line, "CHAPTER %d SC %d", ep->chapter, ep->episode);
    hud(g);
}

// Move the clip to LD frame t: a cube rewind (back into the episode after its death clip) or a
// replay_from jump. MVD has no seek, so decode forward from the start (or from here) without showing.
int g_death_t;

// Window bar for frame t: the longest open window that takes a move (variant b once hardened, CFD9:0249).
static void window_at(tt_game *g, const tt_ep *ep, int t, int *remaining, int *total)
{
    *remaining = *total = 0;
    for (int i = 0; i < ep->n_rules; i++) {
        const tt_rule *r = &g->rules[ep->first_rule + i];
        int lo, hi;
        tt_window(g, r, &lo, &hi);
        if (!r->inputs || t < lo || t > hi || hi - t <= *remaining) continue;
        *remaining = hi - t + 1;
        *total = hi - lo + 1;
    }
}

// ---- Presentation effects from the engine (sounds, laserdisc speed) ----
void play_sound(tt_game *g, int id)
{
    const uint8_t *st;
    int n = tt_sound(g, id, &st);
    if (n) audio_tone(st, n);
}

void fe_fx(tt_game *g, int kind, int arg)
{
    static const char *evn[] = {"game start", "scene done", "death", "cube used", "life lost"};
    if (kind == TT_FX_EVENT && arg >= 0 && arg <= TT_EV_LIFE_LOST && arg != TT_EV_SCENE_DONE)
        plog_line("event: %s  lives %d cubes %d", evn[arg], g->lives, g->cubes);
    if (kind == TT_FX_SEGMENT) plog_line("segment %d", arg);
    if (kind == TT_FX_SOUND) {
        play_sound(g, arg);
        return;
    }
    if (kind == TT_FX_EVENT && arg == TT_EV_INTO_CH7) {
        into_chapter7(g);
        return;
    }
    if (kind == TT_FX_EVENT) {   // the ROM's LED call sites (engine.h TT_EV_*)
        hud_game = g;
        hud_show(arg == TT_EV_DEATH ? LEDM_DEATH : LEDM_STATUS);
        return;
    }
    if (kind == TT_FX_SEGMENT) {
        if (arg == TT_SEG_LEARN_WALK) {
            // D659:0041: beep, then search + play LD 129-245 (D37E:04FA) inside the PLAY/LEARN clip
            // (LD 1-245: C0 E10, C1 E23), then the next scene.
            enum { LEARN_WALK_FROM = 128, LEARN_WALK_TO = 244 };   // clip frames of LD 129-245
            if (cur_clip >= 0) {
                int c = cur_clip;
                if (ep_clock > LEARN_WALK_FROM) { open_clip(c); }
                skip_to(LEARN_WALK_FROM);
                audio_seek_frame(LEARN_WALK_FROM);
                play_to(g, LEARN_WALK_TO, 0);
            }
        } else if (arg == TT_SEG_INTRO_SKIP) {
            play_named(g, "intro_skip", last_ad);
        } else if (arg == TT_SEG_TRAP) {
            // DA9D:0E6D: beep, play LD 23581-23649, then LD 26062-26305 (current audio: C3 E82's, both).
            play_named(g, "trap_a", 3);
            play_named(g, "trap_b", 3);
        }
        return;
    }
    // arg: DA9D:0C5E n (engine.h TT_FX_SPEED): fast x3, slow x1/2, normal; 1-3 warble, 4-6 beep
    int num = 1, den = 1;
    if (arg == 300 || arg == 1 || arg == 4)
        num = 3;
    else if (arg == 50 || arg == 2 || arg == 5)
        den = 2;
    else if (!(arg == 100 || arg == 3 || arg == 6))
        return;
    if (arg >= 1 && arg <= 3)
        play_sound(g, TT_SND_WARBLE);
    else if (arg >= 4 && arg <= 6)
        play_sound(g, TT_SND_BEEP);
    int was_normal = pace_num == 1 && pace_den == 1;
    u64 now = osGetTime();
    pace_ms0 = now;
    pace_f0 = ep_clock;
    pace_num = num;
    pace_den = den;
    bool normal = num == 1 && den == 1;
    ms_slot = -1;
    if (was_normal && !normal) audio_multispeed(true);
    if (!was_normal && normal) audio_seek_frame(ep_clock);   // ends multispeed, normal play from here
}

// The engine's per-frame step: frame t is due on the clock (see pacing above); return the first move
// pressed while waiting for it, then show it.
int fe_frame_input(tt_game *g, const tt_ep *ep, int t)
{
    g_death_t = g->death_t;
    if (t != ep_clock || cur_clip != ep->clip) {
        seek_to(ep, t);
        ev_clear();
    }   // a new scene: no carry-over
    int remaining, total;
    window_at(g, ep, t, &remaining, &total);
    ev_listen = true;
    wait_frame(g, t, MOVE_KEYS);
    ev_listen = false;
    int in = ev_pop();   // one event per frame, oldest first (the next waits one frame)
    if (in) {
        diag_in = in;
        diag_frame = t;
        diag_open = total > 0;
    }
    show_frame(t, total > 0, remaining, total);
    ep_clock = t + 1;
    return in;
}

static int death_frames;   // length of the death clip set up by on_death

void fe_on_death(tt_game *g, const tt_ep *death)
{
    open_clip(death->clip);
    death_frames = death->ld_frames ? death->ld_frames + 1 : 90;   // clip start..stop inclusive; 3 s when unknown
    snprintf(status_line, sizeof status_line, "DEATH");
    plog_line("death %d clip%03d %d frames %s  lives %d cubes %d", death->episode, death->clip, death_frames,
              playing_video ? "video" : "NO VIDEO", g->lives, g->cubes);
    if (g->cubes <= 0) run_frames(g, death_frames, 0, 1);   // no cube to offer: just play it
}

// The Reversal Cube is offered while the death clip plays (CE55:00C9: Cube during a death). Only the
// Cube button counts: while a death plays the ROM sends no joystick event to the rules and drops other
// buttons (CE55:00C9, 01E4-0222), so they must not end the offer. (They did: the offer stopped at the
// first key of any kind, so a direction or A pressed in the moment of dying used up the chance.)
int fe_use_cube(tt_game *g, const tt_ep *ep)
{
    (void)ep;
    snprintf(status_line, sizeof status_line, "DEATH - B/R: use a Reversal Cube");
    bottom_hint = "B:USE CUBE";   // port help: the cabinet has a lit Cube button instead
    rewinding = 0;
    for (int f = 0; f < death_frames && !rewinding; f++) {
        rewinding = wait_frame(g, ep_clock, KEY_B | KEY_R) != 0;
        show_frame(ep_clock, 1, death_frames - f, death_frames);
        ep_clock++;
    }
    bottom_hint = NULL;
    plog_line("cube offer: %s", rewinding ? "USED" : "not used");
    return rewinding;
}

// ---- Hints (D63D:0002 -> specobj_play D47D:1D6C): after a death that was not rewound the ROM may
// play a special object: ld_audio(its channel), then "<start>SE <stop>PL" (checked in tools/romrun.py:
// C1 E35 death 2 -> object 19, "1AD 5480SE 5594PL"). Keys pair up (odd = ch1, even = ch2).
void fe_hint(tt_game *g, int key)
{
    char name[16];
    snprintf(name, sizeof name, "spec%02d", (key | 1) & 0x3F);
    open_named(name, key & 1 ? 1 : 2);
    snprintf(status_line, sizeof status_line, " ");
    play_to(g, 1 << 20, 0);   // to the end of the clip
}
