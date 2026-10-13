// The hall of fame (DA9D:064D): the top-3 table on the SD card and the initials entry.

#include "fe.h"

uint32_t last_score[2];   // <ASCORE1/2>: the last game's score, shown once in attract

hof_entry hof[3];    // [0] = 1st place

// ---- Hall of fame (DA9D:064D, called from game_over). The ROM compares the score with a top-3
// table (D945:01DD), and if it places: plays LD 47768-48040 on audio ch2, scrolls the messages below
// (DGROUP 0x16AF-0x172B) and takes three initials. Each letter starts at 'A'; joystick Up (event
// bits 1 = ~IN0 & 0xF, D0F0:0A0A) steps it +1, Down (2) -1, through 26 letters (DA9D:0ADF/0B13);
// the letter blinks, one step per wait_input timeout of 15 clock units = 0.5 s (D17E:039E, 30 units
// per second); Attack confirms (DA9D:0B62). The score goes into the table before the initials, as
// "TEN" (D945:0232 + D945:017C, DGROUP 0x16AB); the first confirm makes it "AAA" (0x173A) and each
// confirm stores its letter (D945:01AC). 12 steps without input (DA9D:0A0C) or Start/coin (event 0x20
// or 4) end the entry and keep what is stored. "FUK" is stored as "YOU" (DA9D:0BC8, 0x1742).
// The port keeps the table on the SD card.
void hof_load(void)
{
    // The cabinet's defaults (account init: initials at ROM DC955, scores D2229/D224E/D2273), packed as hofN.
    for (int i = 0; i < 3; i++) {
        char k[8], in[4] = "AAA";
        unsigned long sc = 0;
        snprintf(k, sizeof k, "hof%d", i);
        if (hud_game) sscanf(tt_string(hud_game, k), "%3s %lu", in, &sc);
        hof[i] = (hof_entry){(uint32_t)sc, ""};
        memcpy(hof[i].init, in, 4);
    }
    FILE *f = fopen(TT_SAVE_DIR "/hiscore.bin", "rb");
    if (!f) return;
    hof_entry t[3];
    if (fread(t, sizeof t, 1, f) == 1) memcpy(hof, t, sizeof hof);
    fclose(f);
    for (int i = 0; i < 3; i++) hof[i].init[3] = 0;
}

static void hof_save(void)
{
    mkdir(TT_SAVE_DIR, 0777); // a one-file build may find no folder on the card
    FILE *f = fopen(TT_SAVE_DIR "/hiscore.bin", "wb");
    if (!f) return;
    fwrite(hof, sizeof hof, 1, f);
    fclose(f);
}

// One vblank of the hall of fame: the clip keeps playing on its clock while the LED is written directly
// (D47D:14AB / D47D:13B3 bypass the message system).
static void hof_vblank(tt_game *g)
{
    fe_poll();
    if (playing_video && frame_due(ep_clock)) {
        show_frame(ep_clock, 0, 0, 0);
        ep_clock++;
    }
    led_clear();
    led_text(2, ledm_line());
    led_present();
    (void)g;
    gspWaitForVBlank();
}

static void hof_wait(tt_game *g, int units)
{
    for (int i = 0; i < units * 2; i++) hof_vblank(g);
} // 1/30 s units

// D47D:14AB: type the string over the line one character per 1/30 s, padding with spaces to 16.
static void led_type(tt_game *g, const char *str)
{
    char line[17];
    memcpy(line, ledm_line(), 17);
    for (int i = 0; i < 16; i++) {
        line[i] = i < (int)strlen(str) ? str[i] : ' ';
        ledm_set_line(line);
        hof_wait(g, 1);
    }
}

bool hall_of_fame(tt_game *g, uint32_t score, int player) // false: the score does not place
{
    hof_load();
    int rank = -1;
    for (int i = 2; i >= 0; i--)
        if (score > hof[i].score) rank = i;
    if (rank < 0) return false;
    static const char *place[3] = {"1ST", "2ND", "3RD"};
    char place_line[24];
    snprintf(place_line, sizeof place_line, "%s PLACE.", place[rank]);
    const char *msg[] = {"CONGRATULATIONS", player ? "PLAYER2" : "PLAYER1",
                         "WELCOME TO THE",  "HOLOGRAM",
                         "HALL OF FAME.",   "PLEASE ENTER",
                         "YOUR INITIALS",   "FOR ACHIEVING",
                         place_line};
    ledm_stop();
    open_named("hiscore", 2); // ld_audio(2), LD 47768-48040 (DA9D:0736-0756)
    for (int i = 2; i > rank; i--) hof[i] = hof[i - 1]; // in the table at once, as "TEN"
    hof[rank].score = score;
    memcpy(hof[rank].init, "TEN", 4);
    hof_save();
    ledm_set_line("                "); // D47D:1237
    play_sound(g, TT_SND_ALARM); // DA9D:07EA
    for (unsigned m = 0; m < sizeof msg / sizeof msg[0]; m++) {
        led_type(g, msg[m]);
        hof_wait(g, 15); // [bp-0x30] = 15 (DA9D:0656)
    }
    // Initials: "<place> AAA  <score>" typed (DA9D:0966-09C7); letters at 4..6 blink, one step per 15 units.
    char line[40];
    snprintf(line, sizeof line, "%s AAA  %lu", place[rank], (unsigned long)score);
    led_type(g, line);
    char init[4] = "AAA";
    bool confirmed = false;
    for (int pos = 0; pos < 3; pos++) {
        int letter = 0, idle = 0, step = 0;
        for (;;) {
            u32 k = 0;
            for (int v = 0; v < 30 && !k; v++) { // wait_input(.., 15 units)
                hof_vblank(g);
                k = hidKeysDown();
            }
            int in = keys_to_input(k);
            if ((!in && ++idle >= 12) || in == TT_IN_START1) {
                playing_video = false;
                return true;
            } // keeps the entry
            if (in) idle = 0;
            if (in == TT_IN_UP) letter = (letter + 1) % 26;
            if (in == TT_IN_DOWN) letter = (letter + 25) % 26;
            init[pos] = (char)('A' + letter);
            line[4 + pos] = init[pos];
            if (in & TT_IN_ATTACK) {
                if (!confirmed) {
                    memcpy(hof[rank].init, "AAA", 4);
                    confirmed = true;
                }
                hof[rank].init[pos] = init[pos];
                hof_save();
                char l[17];
                snprintf(l, sizeof l, "%-16.16s", line);
                ledm_set_line(l);
                break;
            }
            char l[17];
            snprintf(l, sizeof l, "%-16.16s", line);
            if (!in && (++step & 1)) l[4 + pos] = ' '; // blink
            ledm_set_line(l);
        }
    }
    if (!strcmp(init, "FUK")) strcpy(init, "YOU");
    memcpy(hof[rank].init, init, 4);
    hof_save();
    playing_video = false;
    return true;
}
