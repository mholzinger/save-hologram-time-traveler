// The laserdisc model: the open clip and its clock, frame pacing on the wall clock, speed effects,
// searches (with seek blanking) and the named footage segments the ROM plays from fixed frames.

#include "fe.h"

int ep_clock;          // LD frames elapsed in the current episode

bool playing_video;    // current episode has a decoded clip (else the screen is black)

int last_ad = 3;        // LD audio channel(s) last selected ("<n>AD"), for footage that keeps it

// ---- Frame pacing. Footage runs on the wall clock, the clock its audio plays by: frame n of the
// open clip is due at pace_t0 + n * 1001/30 ms. Waiting polls input every vblank; when decoding falls
// behind there is no wait, and frames more than 2 late are decoded but not drawn, so picture and
// sound stay together. (Counting 2 vblanks per frame let the picture drift behind the audio on
// hardware: ~4 s in the chapter-1 tutorial.)
// The laserdisc speed effects (DA9D:0C5E) change the rate the clip clock runs at (D245:029E adds
// g[0x6C] = 300 / 50 / 100 per 1/30 s): pacing is piecewise, rebased at each change.
u64 pace_ms0;

int pace_f0, pace_num = 1, pace_den = 1;

s64 ms_slot = -1;   // last 1/30 s slot that got its multispeed audio chunk

void pace_start(int frame)
{
    pace_ms0 = osGetTime();
    pace_f0 = frame;
    pace_num = pace_den = 1;
    ms_slot = -1;
}

u64 due(int frame)
{
    s64 d = (s64)(frame - pace_f0) * 1001 * pace_den / (30 * pace_num);
    return (u64)((s64)pace_ms0 + d);
}

bool frame_due(int frame) { return osGetTime() + 8 >= due(frame); }   // 8 ms early: about half a vblank

// Wait until `frame` is due; returns the keys in `keys` pressed meanwhile.
u32 wait_frame(tt_game *g, int frame, u32 keys)
{
    u32 got = 0;
    for (;;) {
        u32 k = fe_poll();
        if (k & KEY_L) diag = !diag;
        if (k & KEY_X) stage_toggle();   // cabinet stage / flat picture
        video_keys(k);
        u64 now = osGetTime();
        if (diag_last_scan && now - diag_last_scan > diag_gap) diag_gap = now - diag_last_scan;
        diag_last_scan = now;
        got |= k & keys;
        ev_scan(k, hidKeysHeld(), ev_listen);
        hud(g);
        if (pace_num != pace_den) {   // multispeed: the sound of the frame on screen, every 1/30 s
            s64 slot = (s64)(now - pace_ms0) * 30 / 1001;
            if (slot != ms_slot) {
                ms_slot = slot;
                audio_frame_chunk(frame > 0 ? frame - 1 : 0);
            }
        }
        if (frame_due(frame)) return got;
        gspWaitForVBlank();
    }
}

// Decode the next frame and show it (window bar: open/remaining/total in frames) unless running late.
bool show_frame(int frame, int open, int remaining, int total)
{
    if (!playing_video) {
        draw_top(open, remaining, total);
        return true;
    }
    u64 t0 = osGetTime();
    if (!video_next()) return false;
    u64 t1 = osGetTime();
    diag_dec = t1 - t0;
    if (t1 < due(frame + 2)) {
        draw_top(open, remaining, total);
        diag_blit = osGetTime() - t1;
    }
    return true;
}

// Play `frames` LD frames of the open clip. Returns the first move seen when `listen`, else 0.
int run_frames(tt_game *g, int frames, int listen, int total)
{
    for (int f = 0; f < frames; f++) {
        int in = keys_to_input(wait_frame(g, ep_clock, listen ? MOVE_KEYS : 0));
        show_frame(ep_clock, listen, frames - f, total);
        ep_clock++;
        if (in) return in;
    }
    return 0;
}

int cur_clip = -1;   // clip open in the video/audio players

// ---- Seek blanking. Every clip and segment start is a laserdisc search (ld_search_play_to D37E:03AB,
// ld_play_to d000_3cda). MAME's LD-V4200 (ldv4200hle.cpp:634-638) turns the video off for a search more
// than 100 frames away and halves the distance every field until it lands (868-874); the ROM also sends
// 0VD itself (D37E:07C7). The real player's seek time is in no source here (UNVERIFIED), so MAME's model
// is used: the picture is black for as many fields as the halving takes (about 0.1-0.25 s).
const tt_game *ld_db;   // a loaded game: its LD clip table (tt_clip_start)

int ld_base = -1;       // LD frame of frame 0 of the open footage, -1 = unknown

void ld_seek(int target)
{
    int from = ld_base >= 0 ? ld_base + ep_clock : -1;
    ld_base = target;
    if (from < 0 || target < 0) return;
    int d = abs(target - from), n = 0;
    if (d <= 100) return;
    while (d > 2) {
        d -= d / 2;
        n++;
    }
    audio_stop();
    plog_line("seek %d -> %d: %d fields black", from, target, n);
#ifdef TT_HOST
    printf("          seek %d -> %d: %d fields black\n", from, target, n);
#endif
    stage_blank(true);
    for (int i = 0; i < n; i++) {
        if (!aptMainLoop()) exit(0);
        if (stage_on())
            stage_render(-1.0f);   // the picture hidden, the stage lit as it was
        else
            top_show(NULL, 800, 0, 0, 0);
        gspWaitForVBlank();
    }
    stage_blank(false);
}

// The footage the ROM plays from fixed laserdisc frames (encoded under these names by tools/encode_ld.py
// EXTRA, same ranges). Maps and hints are not listed: their position is unknown here (-1).
static const struct {
    const char *name;
    int start, stop;
} segs[] = {
    {"trader_intro", 27764, 27959}, {"trader_wait", 27961, 28039}, {"trader_buy", 28053, 28188},
    {"trader_loop", 27961, 28014},  {"hg_offer", 23651, 23980},    {"hg_offer2", 23982, 24184},
    {"hg_jackpot", 24187, 24337},   {"hg_cubes", 24340, 24501},    {"hg_life", 24505, 24667},
    {"hg_after", 24669, 24838},     {"hg_lose1", 24841, 24999},    {"hg_lose2", 25003, 25163},
    {"hg_devil", 25164, 25516},     {"hiscore", 47768, 48040},     {"gameover", 48215, 48382},
    {"trap_a", 23581, 23649},       {"trap_b", 26062, 26305},      {"handover_p1", 15330, 15450},
    {"handover_p2", 15612, 15732},  {"intro_skip", 248, 350},      {"egg_up", 53469, 53618},
    {"egg_down", 53646, 53884},     {"price_28049", 28049, 28053}, {"price_26902", 26902, 26906},
};

static int seg_find(const char *name)
{
    for (unsigned i = 0; i < sizeof segs / sizeof segs[0]; i++)
        if (!strcmp(segs[i].name, name)) return (int)i;
    return -1;
}
static int named_start(const char *name)
{
    int i = seg_find(name);
    return i < 0 ? -1 : segs[i].start;
}
int seg_length(const char *name)
{
    int i = seg_find(name);
    return i < 0 ? 0 : segs[i].stop - segs[i].start;
}

// Open a named segment and play it to its end (LD audio channel ch, 0 = silent).
void play_named(tt_game *g, const char *name, int ch)
{
    open_named(name, ch);
    play_to(g, seg_length(name), 0);
}

bool rewinding;      // a cube was just used: the next seek is the rewind

void clip_path(char *path, size_t size, const char *name, const char *ext)
{
    snprintf(path, size, "%s/clips/%s%s", tt_data, name, ext);
}

bool clip_media(int clip, bool sound)
{
    char name[16], path[64];
    snprintf(name, sizeof name, "clip%03d", clip);
    clip_path(path, sizeof path, name, ".h264");
    if (!video_open(path)) return false;
    clip_path(path, sizeof path, name, ".wav");
    if (sound)
        audio_play(path);
    else
        audio_stop();
    return true;
}

void open_clip(int clip)
{
    ld_seek(ld_db && clip != TT_NO_CLIP ? tt_clip_start(ld_db, clip) : -1);
    playing_video = clip != TT_NO_CLIP && clip_media(clip, true);
    if (!playing_video) audio_stop();
    last_ad = 3;   // most clips play both channels (all death clips do: data/clips.json audio_mask)
    cur_clip = clip;
    ep_clock = 0;
    pace_start(0);
}

void seek_to(const tt_ep *ep, int t)
{
    if (cur_clip != ep->clip || t < ep_clock) open_clip(ep->clip);
    if (playing_video)
        while (ep_clock < t && video_next()) ep_clock++;
    ep_clock = t;
    if (rewinding) {
        // The ROM searches to the death frame and scans back to t at speed 120 (x2) with the audio
        // muted ("<death>SE 120SP <t>MR 0AD", tools/romdiff.py --die). MVD only decodes forward, so:
        // decode on to the death frame keeping every 2nd frame (one eye), show those newest first at
        // 30 fps (= x2 backwards), and re-decode the clip up to t in the gaps.
        rewinding = false;
        audio_pause(true);
        snprintf(status_line, sizeof status_line, "REWIND");
        enum { REWIND_FRAMES = 96 };
        u8 *cache[REWIND_FRAMES] = {0};   // about 18 MB, freed after the rewind
        int n = 0;
        if (playing_video)
            for (int f = t; f < g_death_t && n < REWIND_FRAMES && video_next(); f++)
                if (!((f - t) & 1)) {
                    if (!cache[n]) cache[n] = malloc((size_t)video_eye_bytes());
                    if (!cache[n] || !video_grab_eye(cache[n])) break;
                    n++;
                }
        if (n) {
            open_clip(ep->clip);
            audio_pause(true);
            u64 next = osGetTime();
            for (int k = n - 1; k >= 0; k--) {
                if (!aptMainLoop()) exit(0);
                top_show((const u16 *)cache[k], 400, 0, 0, 0);
                next += 1001 / 30;
                while (osGetTime() < next) {
                    if (playing_video && ep_clock < t && video_next())
                        ep_clock++;
                    else
                        gspWaitForVBlank();
                }
            }
            if (playing_video)
                while (ep_clock < t && video_next()) ep_clock++;
            ep_clock = t;
        }
        for (int i = 0; i < REWIND_FRAMES; i++) free(cache[i]);
        audio_pause(false);
    }
    audio_seek_frame(t);
    pace_start(t);
}

// ---- footage the ROM plays from hardcoded frames (tools/encode_ld.py EXTRA and mapKKKK) ----
void open_named(const char *name, int ch)   // ch: LD audio channel 1/2, 0 = silent
{
    ld_seek(named_start(name));
    char path[64], wav[16];
    clip_path(path, sizeof path, name, ".h264");
    playing_video = video_open(path);
    plog_line("footage %s %s", name, playing_video ? "video" : "NO VIDEO");
    if (ch) last_ad = ch;
    if (ch && playing_video) {
        snprintf(wav, sizeof wav, "_%d.wav", ch);
        clip_path(path, sizeof path, name, wav);
        audio_play(path);
    } else {
        audio_stop();
    }
    cur_clip = -1;
    ep_clock = 0;
    pace_start(0);
}

// Play the open footage on to frame `to` (or its end), on the clock; stops early on a key in `keys`.
u32 play_to(tt_game *g, int to, u32 keys)
{
    for (; ep_clock <= to; ep_clock++) {
        u32 k = wait_frame(g, ep_clock, keys);
        if (!show_frame(ep_clock, 0, 0, 0)) return k;
        if (k) {
            ep_clock++;
            return k;
        }
    }
    return 0;
}

// Hold the current picture for `vblanks`; returns the first key in `keys`.
u32 hold(tt_game *g, int vblanks, u32 keys)
{
    for (int i = 0; i < vblanks; i++) {
        u32 k = fe_poll();
        hud(g);
        if (k & keys) return k & keys;
        gspWaitForVBlank();
    }
    return 0;
}

void skip_to(int frame)   // decode without showing
{
    while (playing_video && ep_clock < frame && video_next()) ep_clock++;
    pace_start(ep_clock);
}
