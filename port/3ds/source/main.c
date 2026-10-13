// Time Traveler 3DS: the main menu (a list, Up/Down and A) and app start-up and shut-down.
// Build with EXTRA_CFLAGS=-DTT_STAGE_TEST for the Azahar stage check below (Azahar has no MVD).

#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "audio.h"
#include "data.h"
#include "frontend.h"
#include "video.h"

#ifdef TT_STAGE_TEST
// Emulator check of the cabinet stage (Azahar has no MVD): one real decoded-format frame from
// sdmc:/timetrv/stage_test.bin (800x240 RGB565, red in the low bits, as MVD outputs it; written by
// tools/stage_test_frame.py), drawn on the stage with the window bar at 60%.
#include "stage.h"
#include "led.h"
static void stage_test(void)
{
    led_clear();
    led_text(2, "  5 LIVES LEFT");
    static u16 frame[800 * 240];
    FILE *f = fopen("sdmc:/timetrv/stage_test.bin", "rb");
    bool ok = f && fread(frame, 2, 800 * 240, f) == 800 * 240;
    if (f) fclose(f);
    printf("stage test: frame %s, stage %s\n", ok ? "loaded" : "MISSING", stage_init() ? "ok" : "FAILED");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        stage_picture(ok ? frame : NULL, 800, 43);
        stage_render(0.6f);
        gspWaitForVBlank();
    }
}
#endif

// The main menu: a list chosen with Up/Down and A (D-pad or Circle Pad). With the local art
// (docs/ART.md) it is drawn over menu.bgr under the logo header, with the splash on the top screen;
// without it, the text console.
enum { M_PLAY1, M_PLAY2, M_ATTRACT, M_SETTINGS, M_VIDEO, M_EXTRAS, M_EXIT, M_COUNT };
static const char *const menu_items[M_COUNT] = {
    "Play: 1 player", "Play: 2 players", "Attract mode", "Settings (DIPs)", "Video", "Extras", "Exit",
};

static void draw_menu(int sel)
{
    static uint8_t *menu, *splash;
    static bool tried;
    if (!tried) {
        menu = art_load("menu", 320, 240);
        splash = art_load("splash", 400, 240);
        tried = true;
    }
    if (!menu) {
        consoleClear();
        printf("Time Traveler 3DS (prototype)\n\n");
        for (int i = 0; i < M_COUNT; i++) printf(" %c %s\n", i == sel ? '>' : ' ', menu_items[i]);
        printf("\n Up/Down choose, A select\n\n In game: L  debug panel\n          X  cabinet stage / flat\n");
        return;
    }
    for (int k = 0; k < 2; k++) {   // both top buffers
        art_top(splash);
        art_bottom(menu);
        int y = ART_MENU_TOP;
        for (int i = 0; i < M_COUNT; i++, y += 12) {
            if (i == sel) art_text(40, y, ">", 0xF0, 0x40, 0xA0);   // the logo's magenta
            art_text(56, y, menu_items[i], i == sel ? 0xFF : 0x90, i == sel ? 0xFF : 0x90, i == sel ? 0xFF : 0x90);
        }
        art_text(24, y + 6, "In game: L debug   X stage/flat", 0xA0, 0xA0, 0xA0);
        art_present();
    }
}

// Every way out of the app (the menu's Exit, or SELECT+START anywhere, which calls exit()) shuts the
// services down once: the DSP, the MVD session, the RomFS and the graphics.
static void app_cleanup(void)
{
    static bool done;
    if (done) return;
    done = true;
    audio_exit();
    video_shutdown();
    tt_data_exit();
    gfxExit();
}

int main(void)
{
    gfxInitDefault();
    atexit(app_cleanup);
    gfxSet3D(true);
    osSetSpeedupEnable(true);   // New 3DS: 804 MHz and the extra cache (no effect on Old 3DS)
    consoleInit(GFX_BOTTOM, NULL);
#ifdef TT_STAGE_TEST
    stage_test();
#endif
    tt_data_init();   // RomFS in a one-file build, else the SD card
    art_splash();

    static int sel = M_PLAY1;
    while (aptMainLoop()) {
        draw_menu(sel);
        u32 k = 0;
        while (aptMainLoop()) {
            hidScanInput();
            k = hidKeysDown();
            if (k & (KEY_DUP | KEY_CPAD_UP)) {
                sel = (sel + M_COUNT - 1) % M_COUNT;
                draw_menu(sel);
            }
            if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) {
                sel = (sel + 1) % M_COUNT;
                draw_menu(sel);
            }
            if (k & KEY_A) break;
            gspWaitForVBlank();
        }
        if (!aptMainLoop() || sel == M_EXIT) break;
        switch (sel) {
        case M_PLAY1: frontend_play(1); break;
        case M_PLAY2: frontend_play(2); break;
        case M_ATTRACT: frontend_attract(); break;
        case M_SETTINGS: frontend_settings(); break;
        case M_VIDEO: frontend_video(); break;
        case M_EXTRAS: frontend_extras(); break;
        }
        // The game draws the LED panel single-buffered on the bottom screen; re-bind the menu's text
        // console to the framebuffer now shown.
        consoleInit(GFX_BOTTOM, NULL);
    }

    app_cleanup();
    return 0;
}
