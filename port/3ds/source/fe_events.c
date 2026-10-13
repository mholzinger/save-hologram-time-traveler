// The game's set pieces between scenes: Hellgate (D0F0:025E), the Trader (CEDF:0719) and the Time Map
// (D843:00B9), each on the ROM's own footage and timings.

#include "fe.h"

// ---- Hellgate (D0F0:025E). Footage per docs/ROM_FINDINGS.md "Lives, cubes, Trader, Hellgate".
// The offer plays LD 23651-23980; after 127 clock units (D245:081F(0x7F), 30 per second) the ROM
// waits up to 301 units for an input event and accepts only joystick DOWN (event code 2, the
// slot-machine lever; D0F0:02B2-02FA). Input before that window is flushed. Checked with
// tools/romdiff.py --hellgate: Down 5 s in starts the roll at once.
int fe_accept_hellgate(tt_game *g)
{
    bool second = g->hellgate_offer == 2;
    snprintf(status_line, sizeof status_line, second ? "AGAIN? PULL DOWN" : "PULL DOWN TO DEAL");
    hud_show(LEDM_HELLGATE_OFFER);   // "TO PLAY / PULL JOYSTICK" (D0F0:0290)
    open_named(second ? "hg_offer2" : "hg_offer", 1);
    // hellgate (d000_115e): wait 0x7F (second offer 0x75) clock units, then wait_input(0x11, 301 / 0xAA):
    // the first joystick or button event decides, and only Down (bits 2) accepts; anything else declines.
    int last = seg_length(second ? "hg_offer2" : "hg_offer");
    const int open_at = second ? 117 : 127, close_at = open_at + (second ? 170 : 301);   // clock units = LD frames
    const u32 any = KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN | KEY_CPAD_LEFT |
        KEY_CPAD_RIGHT | KEY_A | KEY_B | KEY_R;
    for (int f = 0; f < close_at; f++) {
        u32 k = wait_frame(g, f, any);
        if (f <= last) show_frame(f, 0, 0, 0);
        if (k && f >= open_at) return (k & (KEY_DDOWN | KEY_CPAD_DOWN)) && keys_to_input(k) == TT_IN_DOWN;
    }
    return 0;
}

void fe_hellgate_result(tt_game *g, int k)
{
    plog_line("hellgate result %d", k);
    static const char *clip[6] = {"hg_jackpot", "hg_life", "hg_cubes", "hg_lose1", "hg_lose2", "hg_devil"};
    static const char *msg[6] = {"JACKPOT",        "EXTRA LIFE",     "TWO CUBES",
                                 "THE DEVIL WINS", "THE DEVIL WINS", "DEVIL TAKES ALL"};
    snprintf(status_line, sizeof status_line, "%s", msg[k]);
    // D0F0:03A9 0x19 jackpot, 040F 0x1B life, 046D 0x1A cubes, 04D1/05AB 0x1C lost a life,
    // 066B/0820 0x1E "HELLGATE" (all lives), 054B/0625/0727 0x123 when the devil never takes lives
    static const int ids[6] = {LEDM_HG_JACKPOT,   LEDM_HG_LIFE,      LEDM_HG_CUBES,
                               LEDM_HG_LOST_LIFE, LEDM_HG_LOST_LIFE, LEDM_HG_TAKES_ALL};
    if (k != 5) hud_show(k >= 3 && g->set.devil != 0 ? LEDM_HG_BETTER_LUCK : ids[k]);   // k 5: after its first part
    play_sound(g, TT_SND_ALARM);                    // D2DC:01FD at every result (D0F0:03A0-0817)
    open_named(clip[k], 1);
    if (k == 5) {
        // the devil takes all: LD 25164-25323, then the message, then 25325-25516; no after-clip and no
        // status line (d000_115e returns) unless it was the second offer, which ends on 0x0E
        play_to(g, 25323 - 25164, 0);
        hud_show(g->set.devil != 0 ? LEDM_HG_BETTER_LUCK : LEDM_HG_TAKES_ALL);
        play_to(g, seg_length(clip[k]), 0);
        if (g->hellgate_offer == 2) hud_show(LEDM_STATUS);
        return;
    }
    play_to(g, seg_length(clip[k]), 0);
    play_named(g, "hg_after", k <= 2 ? 1 : 2);   // ch1 won, ch2 lost
    if (!(k == 0 && g->set.devil == 0))
        hud_show(LEDM_STATUS); // a jackpot with the devil allowed goes on to the second offer
}

// ---- Trader (CEDF:0719). Timeline measured on the ROM (tools/romdiff.py --trader): intro LD
// 27764-27959 (audio ch2 when the map runs it on its own, ch1 when picked), the still 28039 for
// 244 ticks, then a muted forward/backward loop 27961-28014 three times (~21.6 s in all), then back
// to the map. Each coin buys cubes (tt_buy_cubes, CEDF:0D80) and plays 28053-28188 (ch1; ch2 once
// full); a full stock ends the visit. Port: SELECT inserts a (free) coin, X leaves early.
#define TRADER_VISIT_MS 21600   // the visit's length (~1296 ticks)
#define TRADER_STILL_VBLANKS 244   // the price still before the loop

void fe_trader(tt_game *g, int automatic)
{
    // The cabinet takes coins in its slot (Attack does nothing here); on the 3DS, A or SELECT is a coin.
    const u32 keys = KEY_SELECT | KEY_A | KEY_X, coin = KEY_SELECT | KEY_A;
    snprintf(status_line, sizeof status_line, "A/SELECT=COIN X=LEAVE");
    bottom_hint = "A:COIN  X:LEAVE";
    hud_show(LEDM_TRADER); // "<CUBES>   <WAIT>" (CEDF:07B4)
    trader_deadline = osGetTime() + TRADER_VISIT_MS;
    open_named("trader_intro", automatic ? 2 : 1);
    u32 k = play_to(g, seg_length("trader_intro"), keys);
    for (int stage = 0, loops = 0;;) {
        if (k & KEY_X) break;
        if (k & coin) {
            int full = tt_buy_cubes(g);
            open_named("trader_buy", full ? 2 : 1);
            k = play_to(g, seg_length("trader_buy"), full ? KEY_X : keys);
            if (full) break;
            continue;
        }
        if (stage == 0) { // the price still for the DIP (specobj_CEDF_0EC0)
            stage = 1;
            int cr = settings.cube_credits ? settings.cube_credits : 1,
                cu = settings.cubes_per_coin ? settings.cubes_per_coin : 2;
            int still = cu == 4 ? 26902 : (cr == 1 && cu == 2) ? 28039 : (cu == 3) ? 28049 : 28029;
            if (still == 28049 || still == 26902) {
                open_named(still == 28049 ? "price_28049" : "price_26902", 0);
            } else {
                open_named("trader_wait", 0);
                skip_to(still - 27961);
            }
            k = hold(g, TRADER_STILL_VBLANKS, keys);
            continue;
        }
        if (loops++ >= 3) break;
        open_named("trader_loop", 0); // palindrome: forward then backward
        k = play_to(g, 2 * (seg_length("trader_loop") + 1) - 1, keys); // the frames forward, then back
    }
    bottom_hint = NULL;
}

// ---- Time Map (D843:00B9) on the laserdisc map art: each option is a 5-frame still
// (data/time_map.json). Measured on the ROM (tools/romdiff.py --map-keys): it opens on option 1;
// any joystick direction steps to the next option, bouncing at the ends; stepping on plays the
// highlight forward (PL), stepping back plays it backwards at 2x (120SP MR); Attack picks; with no
// input it picks the current option after 1566 ticks (1437 after the last move).
#define MAP_TIMEOUT 1566        // vblanks without input before the current option is taken
#define MAP_TIMEOUT_MOVED 1437  // the same after a move

int fe_map_choice(tt_game *g, const int *opts, int n)
{
    const tt_tmap *r = g->map_rec >= 0 ? &g->tmap[g->map_rec] : NULL;
    int still[4], lo = 1 << 30, hi = 0;
    for (int i = 0; i < n; i++) {
        still[i] = 0;
        for (int k = 0; r && k < r->n; k++)
            if (r->opt[k] == opts[i]) still[i] = r->still[k];
        if (still[i] && still[i] < lo) lo = still[i];
        if (still[i] + 4 > hi) hi = still[i] + 4;
    }
    // decode the map's frames once so the highlight can move both ways
    enum { MAP_FRAMES = 24 };
    u8 *cache[MAP_FRAMES] = {0};   // about 9 MB, freed when the map closes
    int nf = 0;
    if (r && lo <= hi && hi - lo < MAP_FRAMES) {
        char name[16];
        snprintf(name, sizeof name, "map%04X", r->key);
        open_named(name, 0);
        for (nf = 0; nf <= hi - lo && video_next(); nf++) {
            if (!cache[nf]) cache[nf] = malloc((size_t)video_frame_bytes());
            if (!cache[nf] || !video_grab(cache[nf])) break;
        }
    }
    audio_stop();
    // "YOU SOLVED THE <YEAR>" (D70D:0289) after a completed era, then "TO PICK TIME ERA / PUSH BUTTON"
    // (D843:07AC).
    if (last_chapter >= 1 && last_chapter <= 6 && tt_chapter_completed(g, last_chapter))
        solved(g); // no pause before the map
    hud_show(LEDM_PICK_ERA); // "TO PICK TIME ERA / PUSH BUTTON" (D843:07AC)
    int sel = 0, dir = 1, pos = still[0] - lo, timeout = MAP_TIMEOUT;
    for (;;) {
        snprintf(status_line, sizeof status_line, "%s", opts[sel] < 0 ? "TRADER" : era_name(opts[sel]));
        u32 k = fe_poll();
        if (pos >= 0 && pos < nf) {
            top_show((const u16 *)cache[pos], 800, 0, 0, 0);
        } else {
            draw_top(0, 0, 0);
        }
        hud(g);
        gspWaitForVBlank();
        if ((k & KEY_A) || --timeout <= 0) {
            playing_video = false;
            for (int i = 0; i < MAP_FRAMES; i++) free(cache[i]);
            return sel;
        }
        if (n > 1 && keys_to_input(k) & (TT_IN_UP | TT_IN_DOWN | TT_IN_LEFT | TT_IN_RIGHT)) {
            if (sel + dir < 0 || sel + dir >= n) dir = -dir;
            sel += dir;
            int target = dir > 0 ? still[sel] - lo : still[sel] + 4 - lo;
            for (int step = dir > 0 ? 1 : -2; pos != target;) { // forward 1 frame / 2 vblanks, back 2 / vblank
                pos += step;
                if ((step > 0 && pos > target) || (step < 0 && pos < target)) pos = target;
                for (int f = 0; f < (step > 0 ? 2 : 1); f++) {
                    if (pos >= 0 && pos < nf) top_show((const u16 *)cache[pos], 800, 0, 0, 0);
                    gspWaitForVBlank();
                }
            }
            timeout = MAP_TIMEOUT_MOVED;
        }
    }
}
