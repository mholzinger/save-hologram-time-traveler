// Operator settings (the cabinet's DIPs) and the video settings screen, both saved on the SD card.

#include "fe.h"
#include "cfg.h"

// The operator settings (the cabinet's DIPs; frontend_settings, settings.cfg).
tt_settings settings = {3, 6, 0, 3, 2, 0, 1};   // + difficulty medium (DSW3 default 0x0C), 1 coin = 2 cubes

bool demo_sounds = true;   // DSW3 bit 1 (g[0x432]): off mutes the LD audio in attract (clip_play)

// The operator settings (DIPs) on the SD card, plain "name=value" lines like video.cfg.
#define SETTINGS_FILE TT_SAVE_DIR "/settings.cfg"

// One line of settings.cfg; values outside what the DIPs allow are ignored (the default stays).
static void apply(void *user, const char *name, int x)
{
    (void)user;
    if (!strcmp(name, "lives") && x >= 2 && x <= 5) settings.lives = x;
    if (!strcmp(name, "max_cubes") && (x == 6 || x == 40)) settings.max_cubes = x;
    if (!strcmp(name, "devil") && x >= 0 && x <= 2) settings.devil = x;
    if (!strcmp(name, "difficulty") && x >= 0 && x <= 3) settings.difficulty = x;
    if (!strcmp(name, "level_select")) settings.level_select = x != 0;
    if (!strcmp(name, "cube_credits") && (x == 1 || x == 2)) settings.cube_credits = x;
    if (!strcmp(name, "cubes_per_coin") && x >= 1 && x <= 4) settings.cubes_per_coin = x;
    if (!strcmp(name, "demo_sounds")) demo_sounds = x != 0;
}

void settings_load(void)
{
    static bool done;
    if (done) return;
    done = true;
    cfg_read(SETTINGS_FILE, apply, NULL);
}

static void settings_save(void)
{
    FILE *f = cfg_create(SETTINGS_FILE);
    if (!f) return;
    fprintf(f,
            "lives=%d\nmax_cubes=%d\ndevil=%d\ndifficulty=%d\nlevel_select=%d\ncube_credits=%d\n"
            "cubes_per_coin=%d\ndemo_sounds=%d\n",
            settings.lives, settings.max_cubes, settings.devil, settings.difficulty, settings.level_select,
            settings.cube_credits, settings.cubes_per_coin, demo_sounds);
    fclose(f);
}

// Video settings (vset.h), in the settings screen's style, with a live preview on the top screen: a
// chapter-1 clip looping on the cabinet stage, so opacity and curve show as they change. Without the
// MVD decoder or the clip the stage shows empty. Saved on leaving.
#define PREVIEW_CLIP "clip024" // C1: Marshal Gram walking among the bushes and the barrel

void frontend_video(void)
{
    bool v = video_init();
    stage_init();
    char path[64];
    clip_path(path, sizeof path, PREVIEW_CLIP, ".h264");
    bool clip = v && video_open(path);
    vset_t *vs = vset();
    stage_video(vs->opacity, vs->curve);
    int sel = 0, tick = 0;
    char buf[40];
    for (;;) {
        if (!aptMainLoop()) exit(0);
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & (KEY_B | KEY_START)) break;
        if (k & (KEY_DUP | KEY_DDOWN)) sel = (sel + 1 + ((k & KEY_DUP) ? 1 : 0)) % 3;
        int d = (k & KEY_DRIGHT) ? 1 : (k & KEY_DLEFT) ? -1 : 0;
        if (d && sel == 0) vset_change(d * VSET_OPACITY_STEP, 0);
        if (d && sel == 1) vset_change(0, d);
        if ((k & KEY_A) && sel == 2) vset_reset();
        stage_video(vs->opacity, vs->curve);
        led_single_line(0);
        led_clear();
        led_text(0, "VIDEO      B=OK");
        snprintf(buf, sizeof buf, "%cOPACITY  %3d%%", sel == 0 ? '>' : ' ', vs->opacity);
        led_text(2, buf);
        snprintf(buf, sizeof buf, "%cMIRROR CURVE %2d", sel == 1 ? '>' : ' ', vs->curve);
        led_text(3, buf);
        snprintf(buf, sizeof buf, "%cDEFAULTS (A)", sel == 2 ? '>' : ' ');
        led_text(4, buf);
        led_text(5, clip ? "C-STICK IN GAME" : "NO PREVIEW CLIP");
        led_present();
        // The preview: one frame every other vblank (30 fps), looping the clip.
        if (clip && (tick++ & 1) == 0 && !video_next()) {
            video_close();
            clip = video_open(path) && video_next();
        }
        stage_picture(clip ? video_frame() : NULL, 800, PIC_X0);
        stage_render(-1.0f);
        gspWaitForVBlank();
    }
    vset_save();
    if (clip) video_close();
    if (v) video_exit();
}

// The operator settings (the cabinet's DIPs), one per line, Up/Down to pick, Left/Right to change, B saves.
// demo_sounds: DSW3 bit 1 (g[0x432]): off mutes the LD audio in attract (clip_play); on by default
void frontend_settings(void)
{
    static const char *devil[3] = {"TAKES", "NEVER", "NONE"};
    static const char *diff[4] = {"VERY HARD", "HARD", "EASY", "MEDIUM"};
    // Reversal Cube cost (DSW2 0x02, 0x0C): coins per purchase x cubes; cubes 4 = 1 or 2 per coin in turn
    static const int cost[8][2] = {{1, 1}, {1, 2}, {1, 3}, {1, 4}, {2, 1}, {2, 2}, {2, 3}, {2, 4}};
    enum { N_ITEMS = 7 };
    settings_load();
    int sel = 0;
    char buf[N_ITEMS][24];
    for (;;) {
        if (!aptMainLoop()) exit(0);
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & (KEY_B | KEY_START)) {
            settings_save();
            return;
        }
        if (k & KEY_DUP) sel = (sel + N_ITEMS - 1) % N_ITEMS;
        if (k & KEY_DDOWN) sel = (sel + 1) % N_ITEMS;
        int d = (k & KEY_DRIGHT) ? 1 : (k & KEY_DLEFT) ? -1 : 0;
        int c = 0;
        while (c < 7 &&
               !(cost[c][0] == (settings.cube_credits ? settings.cube_credits : 1) &&
                 cost[c][1] == (settings.cubes_per_coin ? settings.cubes_per_coin : 2)))
            c++;
        if (d && sel == 0) settings.difficulty = (settings.difficulty + d + 4) % 4;
        if (d && sel == 1) settings.lives = 2 + (settings.lives - 2 + d + 4) % 4;
        if (d && sel == 2) settings.max_cubes = settings.max_cubes == 6 ? 40 : 6;
        if (d && sel == 3) settings.devil = (settings.devil + d + 3) % 3;
        if (d && sel == 4) {
            c = (c + d + 8) % 8;
            settings.cube_credits = cost[c][0];
            settings.cubes_per_coin = cost[c][1];
        }
        if (d && sel == 5) settings.level_select = !settings.level_select; // DSW2 bit 4
        if (d && sel == 6) demo_sounds = !demo_sounds;
        snprintf(buf[0], 24, "%s", diff[settings.difficulty]);
        snprintf(buf[1], 24, "LIVES %d", settings.lives);
        snprintf(buf[2], 24, "MAX CUBES %d", settings.max_cubes);
        snprintf(buf[3], 24, "DEVIL %s", devil[settings.devil]);
        if (cost[c][1] == 4)
            snprintf(buf[4], 24, "CUBE %dC 1+2", cost[c][0]);
        else
            snprintf(buf[4], 24, "CUBE %dC FOR %d", cost[c][0], cost[c][1]);
        snprintf(buf[5], 24, "LEVEL SEL %s", settings.level_select ? "ON" : "OFF");
        snprintf(buf[6], 24, "DEMO SOUND %s", demo_sounds ? "ON" : "OFF");
        led_single_line(0);
        led_clear();
        led_text(0, "SETTINGS   B=OK");
        int top = sel < 3 ? 0 : sel > N_ITEMS - 3 ? N_ITEMS - 5 : sel - 2; // 5 rows of the list
        for (int r = 0; r < 5; r++) {
            char l[24];
            snprintf(l, sizeof l, "%c%.15s", top + r == sel ? '>' : ' ', buf[top + r]);
            led_text(1 + r, l);
        }
        led_present();
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }
}
