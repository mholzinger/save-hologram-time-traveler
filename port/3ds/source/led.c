// 14-segment LED panel on the bottom screen.
// Glyph table and segment layout from MAME src/mame/sega/timetrv.cpp (timetrv_state::led_w,
// reference/mame/timetrv.cpp:99-170), BSD-3-Clause, copyright-holders as in that file.
// Segment bits (MAME comment): 0 top, 1 upper right, 2 lower right, 3 bottom, 4 lower left,
// 5 upper left, 6 middle left, 7 middle right, 8 upper centre, 9 lower centre,
// 10 lower-left diagonal, 11 upper-left diagonal, 12 upper-right diagonal,
// 13 lower-right diagonal, 14 decimal point. Glyph index = character code & 0x3F.
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#include "led.h"

#define BW 320
#define BH 240
#define CW 20   // character cell
#define CH 40

static const u16 glyphs[0x40] = {
    0x01bb, 0x00f7, 0x038f, 0x0039, 0x030f, 0x0079, 0x0071, 0x00bd, 0x00f6, 0x0309, 0x001e, 0x3070, 0x0038,
    0x1836, 0x2836, 0x003f, 0x00f3, 0x203f, 0x20f3, 0x00ed, 0x0301, 0x003e, 0x1430, 0x2436, 0x3c00, 0x1a00,
    0x1409, 0x3080, 0x2800, 0x0c40, 0x1403, 0x0008, 0x0000, 0x4100, 0x0022, 0x03ce, 0x03ed, 0x3ce4, 0x3c0d,
    0x0100, 0x3000, 0x0c00, 0x3fc0, 0x03c0, 0x0400, 0x00c0, 0x4000, 0x1400, 0x143f, 0x0300, 0x00db, 0x00cf,
    0x00e6, 0x2069, 0x00fd, 0x0007, 0x00ff, 0x00ef, 0x4000, 0x0400, 0x0408, 0x00c8, 0x2008, 0x0283,
};

// Segment end points in a 14x30 glyph box: {x0, y0, x1, y1} (y down).
static const s8 seg[15][4] = {
    {1, 0, 13, 0},  {14, 1, 14, 14}, {14, 16, 14, 29}, {1, 30, 13, 30}, {0, 16, 0, 29},
    {0, 1, 0, 14},  {1, 15, 6, 15},  {8, 15, 13, 15},  {7, 1, 7, 14},   {7, 16, 7, 29},
    {6, 16, 1, 29}, {1, 1, 6, 14},   {13, 1, 8, 14},   {8, 16, 13, 29}, {15, 30, 15, 30},
};

static char rows[6][17];
static int bpp = 3;   // bytes per pixel of the bottom framebuffer: 3 (BGR8) or 2 (RGB565, set by consoleInit)

static void px(u8 *fb, int x, int y, u8 g)
{
    if (x < 0 || x >= BW || y < 0 || y >= BH) return;
    int i = (x * BH) + (BH - 1 - y);              // column-major, origin bottom-left
    if (bpp == 2) {
        ((u16 *)fb)[i] = (u16)((g >> 2) << 5);   // RGB565: green only
    } else {
        u8 *p = fb + i * 3;                       // BGR8
        p[0] = 0;
        p[1] = g;
        p[2] = 0;
    }
}

static void line(u8 *fb, int x0, int y0, int x1, int y1, u8 g)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        px(fb, x0, y0, g);
        px(fb, x0 + 1, y0, g);   // 2 px wide
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}

void led_clear(void)
{
    memset(rows, ' ', sizeof rows);
    for (int r = 0; r < 6; r++) rows[r][16] = 0;
}

void led_text(int row, const char *s)
{
    if (row < 0 || row > 5) return;
    memset(rows[row], ' ', 16);
    for (int i = 0; i < 16 && s[i]; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        rows[row][i] = c;
    }
}

const char *led_line(void) { return rows[2]; }

void led_segments(char c, void (*f)(void *u, int x0, int y0, int x1, int y1, int lit), void *u)
{
    u16 gl = glyphs[(u8)c & 0x3F];
    for (int k = 0; k < 15; k++) f(u, seg[k][0], seg[k][1], seg[k][2], seg[k][3], gl >> k & 1);
}

static bool led_dirty, one_line;
void led_invalidate(void) { led_dirty = true; }
void led_single_line(int on)
{
    if (one_line != !!on) led_dirty = true;
    one_line = !!on;
}

void led_present(void)
{
    // Redrawing all 96 characters is costly, so the panel is single-buffered and only redrawn when
    // its text changes (or the framebuffer moved, e.g. after the menu's console).
    static char shown[6][17];
    static u8 *shown_fb;
    static int shown_bpp;
    static bool single;
    if (!single) {
        gfxSetDoubleBuffering(GFX_BOTTOM, false);
        single = true;
    }
    // The menu's text console switches the bottom screen to RGB565; draw in whatever is active.
    bpp = gfxGetScreenFormat(GFX_BOTTOM) == GSP_RGB565_OES ? 2 : 3;
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!led_dirty && fb == shown_fb && bpp == shown_bpp && !memcmp(shown, rows, sizeof shown)) return;
    led_dirty = false;
    memcpy(shown, rows, sizeof shown);
    shown_fb = fb;
    shown_bpp = bpp;
    memset(fb, 0, BW * BH * bpp);
    // In game the cabinet has one 16-character line (MAME timetrv.cpp:203-204): row 2 is it, drawn in
    // the middle of the screen, and other rows only when they hold text (port diagnostics, row 5).
    // Port screens (settings, errors) use the full 6-row grid.
    for (int r = 0; r < 6; r++) {
        bool blank = true;
        for (int i = 0; i < 16; i++)
            if (rows[r][i] != ' ') blank = false;
        if (one_line && ((r != 2 && blank) || r == 1 || r == 3)) continue;   // 1 and 3 overlap the centered line
        for (int i = 0; i < 16; i++) {
            u16 gl = glyphs[(u8)rows[r][i] & 0x3F];
            int ox = i * CW + 3, oy = one_line && r == 2 ? (BH - 30) / 2 : r * CH + 5;
            for (int k = 0; k < 15; k++) {
                u8 g = (gl >> k & 1) ? 0xFF : 0x14;   // unlit segments glow faintly, like the panel
                line(fb, ox + seg[k][0], oy + seg[k][1], ox + seg[k][2], oy + seg[k][3], g);
            }
        }
    }
    GSPGPU_FlushDataCache(fb, BW * BH * bpp);
}
