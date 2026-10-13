// The bottom screen: the cabinet's one LED line driven by the ROM's message system (ledmsg.c), the
// debug panel (L), the C-stick video settings with their LED feedback, and port help lines.

#include "fe.h"

// Era names per chapter, from the title cards (docs/ROM_FINDINGS.md "Era per chapter").
const char *era_name(int ch)
{
    switch (ch) {
    case 0: return "INTRO";
    case 1: return "1873";
    case 2: return "1998";
    case 3: return "2173";
    case 4: return "ALIEN ERA";
    case 5: return "50000 BC";
    case 6: return "2552";
    case 7: return "1350";
    default: return "";
    }
}

char status_line[64];

// Bottom screen: the cabinet's single 16-character LED line (MAME timetrv.cpp:203-204), driven by the
// ROM's message system (ledmsg.c, after segment D47D) and shown only at the ROM's call sites. The
// line timer runs at 30 Hz (D245:035E); input/timing diagnostics can be put on row 5 with L.

int cur_player;      // 0 / 1 (g[0xC3C])

tt_game *hud_game;   // the game the LED tokens read

u64 trader_deadline; // <WAIT>: the Trader visit's end (seconds left are shown)

static int hud_led_value(void *u, int what, int arg)
{
    (void)u;
    tt_game *g = hud_game;
    switch (what) {
    case LEDV_CUBES: return g ? g->cubes : 0;
    case LEDV_LIVES: return g ? g->lives : 0;
    case LEDV_MERCY_GIVEN: return g ? g->mercy_given : 0;
    case LEDV_PLAYER: return cur_player;
    case LEDV_SCORE: return g ? (int)g->score : 0;
    case LEDV_LAST_SCORE: return (int)last_score[arg & 1];
    case LEDV_CLEAR_LAST_SCORE: last_score[arg & 1] = 0; return 0;
    case LEDV_HOF_SCORE: return (int)hof[2 - arg].score;   // entry 2 = 1st place
    case LEDV_CHAPTER: return last_chapter;
    case LEDV_WAIT_SECONDS: {   // the Trader's time left (CEDF:0719 visit, ~21.6 s)
        u64 now = osGetTime();
        return trader_deadline > now ? (int)((trader_deadline - now + 999) / 1000) : 0;
    }
    case LEDV_LIVES_DIP: return settings.lives;   // the lives DIP
    case LEDV_COINAGE: return 3;     // DSW1 coinage (MAME default 3 coins / credit); free play: no credits
    default: return 0;
    }
}

static const char *hud_led_initials(void *u, int entry)
{
    (void)u;
    return hof[2 - entry].init;
}

static void hud_led_sound(void *u, int id)
{
    (void)u;
    if (hud_game) play_sound(hud_game, id);
}

static u64 hud_ms;          // wall-clock position of the LED's 30 Hz tick

void hud_start(tt_game *g)
{
    hud_game = g;
    led_single_line(1);
    ledm_env env = {NULL, hud_led_value, hud_led_initials, hud_led_sound};
    ledm_init(g, &env);
    hud_ms = osGetTime() * 3;   // in 1/3 ms, so 1/30 s = 100 units
}

void hud_show(int id) { ledm_show(id); }

// Debug panel (toggle with L; off by default, the cabinet shows only its LED line): the 6-row grid with
// score, lives/cubes, the cabinet line, the port status (scene, death/cube prompt), and the last move read
// with its frame ("*" = a move window was open), decode and blit ms of the last frame, longest gap
// between input scans (ms).
bool diag;

int diag_in, diag_frame, diag_open;

u64 diag_dec, diag_blit, diag_gap, diag_last_scan;

// Video settings, tuned live with the C-stick while footage plays (New 3DS/2DS): left/right opacity,
// up/down mirror curve. The new value shows on the LED line for a moment; the file is written once
// the stick has been left alone for 2 s, not on every step (an SD write can stall a frame).
static char vmsg[17];

u64 vmsg_until, vset_dirty_at;

void video_keys(u32 k)
{
    u64 now = osGetTime();
    if (k & (KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT | KEY_CSTICK_UP | KEY_CSTICK_DOWN)) {
        int d_op = ((k & KEY_CSTICK_RIGHT) ? VSET_OPACITY_STEP : 0) - ((k & KEY_CSTICK_LEFT) ? VSET_OPACITY_STEP : 0);
        int d_cu = ((k & KEY_CSTICK_UP) ? 1 : 0) - ((k & KEY_CSTICK_DOWN) ? 1 : 0);
        vset_t *v = vset();
        if (vset_change(d_op, d_cu)) {
            stage_video(v->opacity, v->curve);
            vset_dirty_at = now;
        }
        if (k & (KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT)) snprintf(vmsg, sizeof vmsg, "OPACITY %3d%%", v->opacity);
        if (k & (KEY_CSTICK_UP | KEY_CSTICK_DOWN)) snprintf(vmsg, sizeof vmsg, "MIRROR CURVE %2d", v->curve);
        vmsg_until = now + 1500;
    }
    if (vset_dirty_at && now - vset_dirty_at > 2000) {
        vset_save();
        vset_dirty_at = 0;
    }
}

const char *bottom_hint;   // port help under the LED line (e.g. the Trader's controls), NULL = none

void hud(tt_game *g)
{
    u64 now = osGetTime() * 3;
    for (int n = 0; now >= hud_ms + 100 && n < 30; n++) {
        ledm_tick30();
        hud_ms += 100;
    }
    if (now >= hud_ms + 100) hud_ms = now;   // after a long stall, don't race to catch up
    led_single_line(!diag);
    led_clear();
    led_text(2, osGetTime() < vmsg_until ? vmsg : ledm_line());
    if (!diag && bottom_hint) led_text(5, bottom_hint);
    if (diag) {
        char buf[40];
        static const char *nm[] = {"-",  "U",  "D", "?", "L", "UL", "DL", "?", "R",
                                   "UR", "DR", "?", "?", "?", "?",  "?",  "A"};
        if (g) {
            snprintf(buf, sizeof buf, "SCORE %10lu", (unsigned long)g->score);
            led_text(0, buf);
            snprintf(buf, sizeof buf, "LIVES %d CUBES %d", g->lives, g->cubes);
            if (g->players >= 2) snprintf(buf, sizeof buf, "P%d L%d CUBES %d", cur_player + 1, g->lives, g->cubes);
            led_text(1, buf);
        }
        led_text(3, status_line);
        snprintf(buf, sizeof buf, "%s%c%3d D%2u B%2u G%2u",
                 diag_in == 32       ? "C"
                     : diag_in <= 16 ? nm[diag_in]
                                     : "?",
                 diag_open ? '*' : ' ', diag_frame % 1000, (unsigned)(diag_dec % 100), (unsigned)(diag_blit % 100),
                 (unsigned)(diag_gap % 100));
        led_text(4, buf);
        led_text(5, "L: HIDE DEBUG");
    }
    led_present();
}
