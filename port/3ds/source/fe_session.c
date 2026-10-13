// A game session: one or two players (a thread each, taking turns at a life lost), level select,
// game over and the hand-over between players.

#include "fe.h"

// D7C0:052D game_over: "PLAYERn / GAME OVER" (0x3D / 0x3E), then the hall of fame (DA9D:064D); the score
// is kept for the attract mode's <ASCORE> line.
// D7C0:052D game_over: message 0x3D / 0x3E, then at once (1 player) the hall of fame (DA9D:064D) and
// D7C0:05F7, which plays LD 48215-48382 with the video on and the audio channel left as it was
// (tools/romdiff.py --random 3 --ld-log: "48215SE1VD 48382PL" right after the last hint clip). The
// LED message runs on over the footage. With 2 players the ROM only waits (D245:085A(2)) instead.
static int games_started; // 2 when this was a 2-player game (players drops to 1 when one is out)

static tt_game games[2];

static volatile int done_p[2];

static void show_msg(tt_game *g, int id, int vblanks);

static void game_over(tt_game *g, int p)
{
    plog_line("GAME OVER player %d  lives %d score %lu", p + 1, g->lives, (unsigned long)g->score);
    last_score[p & 1] = g->score;
    hud_show(p ? LEDM_GAME_OVER_P2 : LEDM_GAME_OVER_P1);
    // Two players and the other one still in (D7C0:052D with g[0x72] >= 2, d000_812d): only the message
    // and a short wait (D245:085A(2)); 038F then hands over and D7C0:068B(1) makes it a 1-player game.
    if (g->players >= 2 && !done_p[1 - p]) {
        audio_stop();
        playing_video = false;
        show_msg(g, p ? LEDM_GAME_OVER_P2 : LEDM_GAME_OVER_P1, ROM_SECONDS(2));
        games[1 - p].players = 1;
        return;
    }
    // The last game over: the hall of fame takes every non-zero score, the higher first (DA9D:064D,
    // d000_b01d), and stops at the first that does not place.
    if (g->players >= 2 || games_started > 1) {
        int hi = last_score[1] > last_score[0];
        for (int k = 0; k < 2; k++) {
            int q = k ? 1 - hi : hi;
            if (!last_score[q] || !hall_of_fame(g, last_score[q], q)) break;
        }
    } else {
        hall_of_fame(g, g->score, p);
    }
    play_named(g, "gameover", last_ad);
    audio_stop();
    playing_video = false;
}

static LightEvent turn_ev[2], all_done;

static void show_msg(tt_game *g, int id, int vblanks)
{
    hud_show(id);
    playing_video = false;
    for (int i = 0; i < vblanks; i++) {
        if (!aptMainLoop()) exit(0);
        draw_top(0, 0, 0);
        hud(g);
        gspWaitForVBlank();
    }
}

// D7C0:04BA: the incoming player's card (the "PLAYER 1/2" goblet), if that player has lives left;
// LD audio channel unchanged. Played at each switch (D7C0:038F) and for player 1 at a 2-player start
// (D668:036F set_player(0)).
static void handover(int p)
{
    if (games[p].lives <= 0) return;
    play_named(&games[p], p ? "handover_p2" : "handover_p1", last_ad);
    audio_stop();
    playing_video = false;
}

// Level select: episode_test_select (c000_4580). The LED shows "C<ch> E<ep>" of the scene on offer; Up steps
// forward and Down back through the scene headers (the ROM: event bits 1 / 2), Attack plays it, and 8 s
// without input (D245:07D8(8)) plays the one shown. Port additions: Left/Right jump a chapter, B ends.
static int fe_pick_episode(tt_game *g, int idx)
{
    char buf[24];
    for (;;) {
        const tt_ep *e = &g->eps[idx];
        snprintf(buf, sizeof buf, "C%d E%d", e->chapter, e->episode);
        snprintf(status_line, sizeof status_line, "LEVEL SELECT: UP/DOWN, A PLAY, B END");
        plog_line("level select offers %s", buf);
        ledm_stop();
        ledm_set_line("                ");
        u32 k = 0;
        for (int v = 0; v < ROM_SECONDS(8) &&
             !(k & (KEY_A | KEY_B | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT | KEY_CPAD_UP | KEY_CPAD_DOWN));
             v++) {
            k = fe_poll();
            char l[17];
            snprintf(l, sizeof l, "%-16.16s", buf);
            ledm_set_line(l);
            led_clear();
            led_text(2, ledm_line());
            led_present();
            top_show(NULL, 800, 0, 0, 0);
            gspWaitForVBlank();
        }
        if (k & KEY_B) return -1;
        if (!k || (k & KEY_A)) return idx; // the timeout plays the one shown
        int dir = (k & (KEY_DUP | KEY_CPAD_UP | KEY_DRIGHT)) ? 1 : -1, ch = e->chapter;
        do {
            idx = (idx + dir + g->n_eps) % g->n_eps;
        } while (!tt_is_header(g, idx) || ((k & (KEY_DLEFT | KEY_DRIGHT)) && g->eps[idx].chapter == ch));
    }
}

static void fe_life_lost(tt_game *g)
{
    int p = g == &games[1];
    if (g->lives <= 0 || done_p[1 - p]) return; // out of lives: tt_run ends; the other one is over
    audio_stop();
    LightEvent_Signal(&turn_ev[1 - p]);
    LightEvent_Wait(&turn_ev[p]);
    cur_player = p;
    hud_game = g;
    hud_show(LEDM_STATUS); // D7C0:038F: the status line, then the card
    handover(p);
}

static void player_thread(void *arg)
{
    int p = (int)(intptr_t)arg;
    LightEvent_Wait(&turn_ev[p]);
    if (done_p[p]) return; // the game was called off (frontend_play could not start both threads)
    cur_player = p;
    hud_game = &games[p];
    hud_show(LEDM_STATUS);
    handover(p); // player 1: set_player(0) at the start; player 2: the first switch (038F)
    tt_run(&games[p]);
    game_over(&games[p], p);
    done_p[p] = 1;
    if (!done_p[1 - p])
        LightEvent_Signal(&turn_ev[1 - p]);
    else
        LightEvent_Signal(&all_done);
}

static bool select_once; // Extras > Scene select: one game in level-select mode

void frontend_scene_select(void)
{
    select_once = true;
    frontend_play(1);
}

int frontend_play(int players)
{
    if (!load_game()) return -1; // the scene graph, loaded once and kept (load_game)
    plog_open();
    plog_line("new game, %d player(s), lives %d, devil %d, difficulty %d", players, settings.lives, settings.devil,
              settings.difficulty);
    tt_frontend fe = {.frame_input = fe_frame_input,
                      .use_cube = fe_use_cube,
                      .accept_hellgate = fe_accept_hellgate,
                      .map_choice = fe_map_choice,
                      .on_episode = fe_on_episode,
                      .on_death = fe_on_death,
                      .trader = fe_trader,
                      .hellgate_result = fe_hellgate_result,
                      .hint = fe_hint,
                      .life_lost = players >= 2 ? fe_life_lost : NULL,
                      .fx = fe_fx,
                      .pick_episode = fe_pick_episode};
    settings_load();
    for (int p = 0; p < players; p++) {
        if (tt_load(&games[p], blob, blob_len)) return -1; // load_game already checked this blob
        tt_new_game(&games[p], (uint32_t)osGetTime() + 7919u * (uint32_t)p, &settings, &fe);
        games[p].players = players;
    }
    consoleClear();
    led_invalidate();
    hud_start(&games[0]);
    hof_load();
    bool v = video_init();
    audio_init();
    mvd_ok = v;
    stage_init();
    if (!v) snprintf(status_line, sizeof status_line, "NO MVD - STAND-IN PICTURE");
    cur_player = 0;
    last_chapter = -1;
    ld_db = &games[0];
    ld_base = -1;
    games_started = players;
    last_score[0] = last_score[1] = 0;
    if (players < 2 && (settings.level_select || select_once)) {
        select_once = false;
        tt_run_select(&games[0]); // no lives lost, so it ends from the picker (B)
    } else if (players < 2) {
        tt_run(&games[0]);
        game_over(&games[0], 0);
    } else {
        s32 prio = 0x30;
        svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
        for (int p = 0; p < 2; p++) {
            LightEvent_Init(&turn_ev[p], RESET_ONESHOT);
            done_p[p] = 0;
        }
        LightEvent_Init(&all_done, RESET_ONESHOT);
        Thread th[2];
        for (int p = 0; p < 2; p++)
            th[p] = threadCreate(player_thread, (void *)(intptr_t)p, 64 * 1024, prio, -2, false);
        if (th[0] && th[1]) {
            LightEvent_Signal(&turn_ev[0]);
            LightEvent_Wait(&all_done);
        } else { // out of memory for the second stack: no game rather than a hang
            plog_line("2-player game not started: thread creation failed");
            done_p[0] = done_p[1] = 1;
            for (int p = 0; p < 2; p++) LightEvent_Signal(&turn_ev[p]);
        }
        for (int p = 0; p < 2; p++)
            if (th[p]) {
                threadJoin(th[p], U64_MAX);
                threadFree(th[p]);
            }
    }
    audio_exit();
    video_exit();
    playing_video = false;
    return 0;
}
