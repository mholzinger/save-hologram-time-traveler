// Attract mode, the death reel and the Extras menu, and the scene graph they share.

#include "fe.h"

static void *load_file(const char *path, int *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (size <= 0) {
        fclose(f);
        return NULL;
    }
    *len = (int)size;
    fseek(f, 0, SEEK_SET);
    void *buf = malloc((size_t)*len);
    if (buf && fread(buf, 1, (size_t)*len, f) != (size_t)*len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static tt_game game;

void *blob;

int blob_len;

bool load_game(void)
{
    if (blob) return true;
    {
        char tp[64];
        snprintf(tp, sizeof tp, "%s/timetrv.ttg", tt_data);
        blob = load_file(tp, &blob_len);
    }
    if (!blob || tt_load(&game, blob, blob_len)) {
        led_single_line(0);
        led_clear();
        led_text(0, "MISSING DATA");
        led_text(1, "SDMC:/TIMETRV/");
        led_text(2, "TIMETRV.TTG");
        led_present();
        free(blob);
        blob = NULL;
        return false;
    }
    return true;
}

// Attract mode, as the ROM runs it (C2BA:039F attract_cycle, checked over 25 minutes in
// tools/romrun.py): sequence A = C10000 E20 E30 E40 E50 E60 E70 or B = E200 ... E250, where the
// clip-less episodes are black gaps of their window length (90 / 30 frames). After each sequence
// g[0x1A]-- picks the next: B while it is 1..10, A above 10; at 0 it is reset to 20 (A). It starts at
// 20, so A plays 10 times, then B 10 times. The bottom screen shows the hall of fame.
static bool egg_done; // the attract easter egg plays once per attract (C2BA:0019, [0x40])

static int attract_step(const tt_ep *e, bool v)
{
    ld_db = &game;
    if (e->clip != TT_NO_CLIP) ld_seek(tt_clip_start(&game, e->clip));
    if (e->clip != TT_NO_CLIP && v) {
        playing_video = clip_media(e->clip, demo_sounds); // DSW3 Demo Sounds
    } else {
        playing_video = false;
        audio_stop();
    }
    // the record's message (E20 shows 0x0B "HALL OF FAME ..."), else 0x0C "TO VIEW / THE MAGIC /
    // OF HOLOGRAM / PRESS ANY BUTTON" (C2BA:0371)
    if (e->msg) hud_show(e->msg); // E20 shows 0x0B (asset type 3); the ROM's attract shows nothing else
    int frames = e->ld_frames;
    if (!frames && e->n_rules) frames = game.rules[e->first_rule].a_hi; // gap: its window length
    pace_start(0);
    if (!playing_video) top_show(NULL, 800, 0, 0, 0); // a timed gap: black
    for (int t = 0; t <= frames; t++) {
        for (;;) { // wait for frame t on the clock, polling every vblank
            if (!aptMainLoop()) return 1;
            hidScanInput();
            u32 kd = hidKeysDown(), kh = hidKeysHeld();
            // C2BA:0019: holding Attack+Cube and pushing Up or Down plays a hidden clip (the team, or the
            // dedication), once per attract; Attack and Cube alone don't end attract on the cabinet.
            if (!egg_done && (kh & KEY_A) && (kh & (KEY_B | KEY_R)) &&
                (kd & (KEY_DUP | KEY_DDOWN | KEY_CPAD_UP | KEY_CPAD_DOWN))) {
                egg_done = true;
                plog_line("attract easter egg %s", kd & (KEY_DUP | KEY_CPAD_UP) ? "up" : "down");
                play_named(&game, kd & (KEY_DUP | KEY_CPAD_UP) ? "egg_up" : "egg_down", 3);
                audio_stop();
                return 0;
            }
            if (kd &
                ~(KEY_A | KEY_B | KEY_R | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN |
                  KEY_CPAD_LEFT | KEY_CPAD_RIGHT))
                return 1;
            hud(&game);
            if (frame_due(t)) break;
            gspWaitForVBlank();
        }
        if (playing_video && !show_frame(t, 0, 0, 0)) break;
        ep_clock = t; // the LD position for the next search (ld_seek)
    }
    return 0;
}

// ---- Extras (port only, not in the arcade) ----
// Death reel: every death clip in game order, on the cabinet stage, labelled on the LED line with its
// scene and death number ("C3 E51 D1"). For finding footage shot for the cabinet props (a bolt fired
// from a prop at Marshal Gram) and for enjoying the deaths. Right / R next, Left / L previous, Up / Down
// next / previous chapter, A replay, B or START back. Clips advance on their own.
static void death_reel(bool v)
{
    static short reel[512], head[512]; // death record index, its scene header index
    int n = 0, h = -1;
    for (int i = 0; i < game.n_eps && n < 512; i++) {
        if (game.eps[i].kind != TT_KIND_DEATH) {
            h = i;
            continue;
        }
        if (h >= 0 && game.eps[h].kind != TT_KIND_SYSTEM) {
            reel[n] = (short)i;
            head[n++] = (short)h;
        }
    }
    char lbl[48];
    for (int k = 0; k < n;) {
        const tt_ep *d = &game.eps[reel[k]], *e = &game.eps[head[k]];
        if (v)
            open_clip(d->clip);
        else {
            playing_video = false;
            pace_start(0);
        }
        snprintf(lbl, sizeof lbl, "C%d E%d D%d %d/%d", e->chapter, e->episode, d->episode, k + 1, n);
        plog_line("death reel %s clip%03d", lbl, d->clip);
        int frames = d->ld_frames ? d->ld_frames + 15 : 60, step = 1; // +0.5 s of black between clips
        for (int t = 0; t <= frames; t++) {
            u32 key = 0;
            for (;;) {
                if (!aptMainLoop()) return;
                hidScanInput();
                key |= hidKeysDown();
                led_single_line(1);
                led_clear();
                led_text(2, lbl);
                led_present();
                if (key || frame_due(t)) break;
                gspWaitForVBlank();
            }
            if (key & (KEY_B | KEY_START)) {
                audio_stop();
                return;
            }
            if (key & (KEY_DRIGHT | KEY_R)) {
                step = 1;
                break;
            }
            if (key & (KEY_DLEFT | KEY_L)) {
                step = -1;
                break;
            }
            if (key & KEY_A) {
                step = 0;
                break;
            }
            if (key & (KEY_DUP | KEY_DDOWN)) { // to the first death of the next / previous chapter
                int ch = e->chapter, j = k, dir = (key & KEY_DUP) ? 1 : -1;
                while (j + dir >= 0 && j + dir < n && game.eps[head[j]].chapter == ch) j += dir;
                if (dir < 0) {
                    int c2 = game.eps[head[j]].chapter;
                    while (j > 0 && game.eps[head[j - 1]].chapter == c2) j--;
                }
                step = j - k;
                break;
            }
            if (t < d->ld_frames && playing_video)
                show_frame(t, 0, 0, 0);
            else if (t == d->ld_frames) {
                audio_stop();
                top_show(NULL, 800, 0, 0, 0);
            }
        }
        k = (k + step + n) % n;
    }
}

void frontend_extras(void)
{
    static int xsel;
    if (!load_game()) return;
    bool v = video_init();
    mvd_ok = v;
    stage_init();
    audio_init();
    for (;;) {
        if (!aptMainLoop()) break;
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & (KEY_B | KEY_START)) break;
        if (k & (KEY_DUP | KEY_DDOWN)) xsel ^= 1;
        if ((k & KEY_A) && xsel == 0) death_reel(v);
        if ((k & KEY_A) && xsel == 1) {
            audio_exit();
            if (v) video_exit();
            frontend_scene_select();
            v = video_init();
            mvd_ok = v;
            audio_init();
        }
        led_single_line(0);
        led_clear();
        led_text(0, "EXTRAS     B=OK");
        led_text(2, xsel == 0 ? ">DEATH REEL" : " DEATH REEL");
        led_text(3, xsel == 1 ? ">SCENE SELECT" : " SCENE SELECT");
        led_text(5, xsel == 0 ? "EVERY DEATH CLIP" : "PLAY ANY SCENE");
        led_present();
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
    playing_video = false;
    audio_exit();
    if (v) video_exit();
}

void frontend_attract(void)
{
    if (!load_game()) return;
    egg_done = false;
    led_invalidate();
    hud_start(&game);
    bool v = video_init();
    mvd_ok = v;
    stage_init();
    audio_init();
    hof_load();
    static const int seq[2][6] = {{20, 30, 40, 50, 60, 70}, {200, 210, 220, 230, 240, 250}};
    int counter = 20;
    for (bool stop = false; !stop;) {
        const int *eps = seq[counter > 10 ? 0 : 1];
        for (int k = 0; k < 6 && !stop; k++)
            for (int i = 0; i < game.n_eps; i++)
                if (game.eps[i].kind == TT_KIND_SYSTEM && game.eps[i].chapter == 10000 &&
                    game.eps[i].episode == eps[k]) {
                    stop = attract_step(&game.eps[i], v);
                    break;
                }
        if (--counter == 0) counter = 20;
    }
    playing_video = false;
    audio_exit();
    video_exit();
}
