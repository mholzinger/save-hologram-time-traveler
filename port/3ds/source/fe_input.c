// Input as the cabinet makes it: one event per change of the stick, every button press its own event
// (D0F0:0A0A), queued in order for the engine's per-frame step; and SELECT+START to quit.

#include "fe.h"

// The 3DS Start button is the cabinet's 1P Start (C1 E5-E7 "press start" windows, device 0x20);
// the game is left with SELECT + START held.
void quit_check(void)
{
    if ((hidKeysHeld() & (KEY_SELECT | KEY_START)) == (KEY_SELECT | KEY_START)) {
        if (vset_dirty_at) vset_save();   // a video setting changed in the last 2 s
        exit(0);
    }
}

// One poll of a wait loop: leave when the app is closing, scan the keys, quit on SELECT+START; returns the
// keys newly pressed. (Menus that must not quit on SELECT+START poll by hand.)
u32 fe_poll(void)
{
    if (!aptMainLoop()) exit(0);
    hidScanInput();
    quit_check();
    return hidKeysDown();
}

int keys_to_input(u32 k)
{
    if (k & (KEY_DUP | KEY_CPAD_UP)) return TT_IN_UP;
    if (k & (KEY_DDOWN | KEY_CPAD_DOWN)) return TT_IN_DOWN;
    if (k & (KEY_DLEFT | KEY_CPAD_LEFT)) return TT_IN_LEFT;
    if (k & (KEY_DRIGHT | KEY_CPAD_RIGHT)) return TT_IN_RIGHT;
    if (k & KEY_A) return TT_IN_ATTACK;
    if (k & (KEY_B | KEY_R)) return TT_IN_CUBE;
    if (k & KEY_START) return TT_IN_START1;
    return 0;
}

// ---- Input events, as the cabinet makes them (D0F0:0A0A input_poll_joy_coin): every time the stick's
// position changes to a non-neutral one, one joystick event carrying the whole position (U/D/L/R bits,
// so a diagonal is two bits); every button press is its own event. The rules see them one at a time,
// in order (CE55:00C9), and a move matches when its bits overlap (CE55:03CC mode 1). The queue matters:
// ORing a frame's presses and keeping one by priority (Up before Down) would read a quick Down tap on the
// Circle Pad that springs back past centre into Up in the same 33 ms as Up, a failed duck.
#define EVQ 8

static int evq[EVQ], evq_n;

static int joy_prev;   // stick position at the last scan (D-pad and Circle Pad together)

bool ev_listen; // set while the engine's per-frame step waits (frame_input)

static void ev_push(int e)
{
    if (evq_n < EVQ) evq[evq_n++] = e;
}

void ev_clear(void) { evq_n = 0; }

int ev_pop(void)
{
    if (!evq_n) return 0;
    int e = evq[0];
    memmove(evq, evq + 1, (size_t)--evq_n * sizeof evq[0]);
    return e;
}

// One scan: queue the events of this scan when `listen`; the stick position is tracked always, so a
// direction held from before a window opens is not an event, as on the cabinet.
void ev_scan(u32 down, u32 held, bool listen)
{
    int joy = (held & (KEY_DUP | KEY_CPAD_UP) ? TT_IN_UP : 0) | (held & (KEY_DDOWN | KEY_CPAD_DOWN) ? TT_IN_DOWN : 0) |
        (held & (KEY_DLEFT | KEY_CPAD_LEFT) ? TT_IN_LEFT : 0) |
        (held & (KEY_DRIGHT | KEY_CPAD_RIGHT) ? TT_IN_RIGHT : 0);
    if (listen && joy != joy_prev && joy) ev_push(joy);
    joy_prev = joy;
    if (!listen) return;
    if (down & KEY_A) ev_push(TT_IN_ATTACK);
    if (down & (KEY_B | KEY_R)) ev_push(TT_IN_CUBE);
    // Start buttons (device 0x20) carry both bits as they are held (D0F0:09EF): Y is the cabinet's 2P Start,
    // so START+Y together is 1P+2P Start (skips the intro in chapter 0, input_event c000_e619).
    if (down & (KEY_START | KEY_Y)) ev_push((held & KEY_START ? TT_IN_START1 : 0) | (held & KEY_Y ? TT_IN_START2 : 0));
}
