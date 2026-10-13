// Flat top-screen drawing: the picture as the full screen (the stage off), the stand-in actor plane
// without MVD, and the move-window bar.

#include "fe.h"

#define TOP_W 400

#define TOP_H 240

bool mvd_ok = true;     // false: no H.264 decoder, draw the stand-in picture instead of footage

static inline void put_px(u8 *fb, int x, int y, u8 r, u8 g, u8 b)
{
    if (x < 0 || x >= TOP_W || y < 0 || y >= TOP_H) return;
    u8 *p = fb + ((x * TOP_H) + (TOP_H - 1 - y)) * 3;
    p[0] = b;
    p[1] = g;
    p[2] = r;
}

static void fill_rect(u8 *fb, int x0, int y0, int w, int h, u8 r, u8 g, u8 b)
{
    for (int x = x0; x < x0 + w; x++)
        for (int y = y0; y < y0 + h; y++) put_px(fb, x, y, r, g, b);
}

// The move-window bar: a port aid (not in the arcade), mixed faintly over the picture.
#define BAR_ALPHA 77   // of 256, about 30 %

static void bar_rect(u8 *fb, int x0, int y0, int w, int h)
{
    for (int x = x0; x < x0 + w; x++)
        for (int y = y0; y < y0 + h; y++) {
            if (x < 0 || x >= TOP_W || y < 0 || y >= TOP_H) continue;
            u8 *p = fb + ((x * TOP_H) + (TOP_H - 1 - y)) * 3;   // BGR8, as put_px
            static const u8 c[3] = {0x20, 0xE0, 0xF0};
            for (int k = 0; k < 3; k++) p[k] = (u8)((p[k] * (256 - BAR_ALPHA) + c[k] * BAR_ALPHA) >> 8);
        }
}

// Show one picture on the top screen and swap: `src` is MVD RGB565 rows `stride` pixels wide (800 = a
// full side-by-side frame, 400 = a one-eye copy), NULL = no footage. The window bar (open/remaining/
// total in frames) is drawn over it. With the cabinet stage on (X in game) the picture floats on the
// stage (stage.c); else the flat full-screen picture.
void top_show(const u16 *src, int stride, int open, int remaining, int total)
{
    if (stage_on()) {
        stage_picture(src, stride, PIC_X0);
        stage_render(open && total > 0 ? (float)remaining / total : -1.0f);
        return;
    }
    int d = (int)(osGet3DSliderState() * 10.0f);
    if (src) {   // real footage; the window bar is drawn over it
        if (stride == 400)
            video_blit_eye(src);
        else
            video_blit_from(src);
        if (open && total > 0)
            for (int eye = 0; eye < 2; eye++)
                bar_rect(gfxGetFramebuffer(GFX_TOP, eye ? GFX_RIGHT : GFX_LEFT, NULL, NULL), 100 + (eye ? -d : d), 228,
                         200 * remaining / total, 4);
    } else {
        // No footage: black, as the cabinet's monitor with the LD video off ("0VD"). The stand-in actor
        // plane is only for consoles without the MVD decoder.
        for (int eye = 0; eye < 2; eye++) {
            u8 *fb = gfxGetFramebuffer(GFX_TOP, eye ? GFX_RIGHT : GFX_LEFT, NULL, NULL);
            memset(fb, 0x00, TOP_W * TOP_H * 3);                     // the void
            int px = eye ? -d : d;
            if (!mvd_ok) fill_rect(fb, 150 + px, 60, 100, 120, 0xC0, 0xA0, 0x70);  // actor plane
            if (open && total > 0) bar_rect(fb, 100 + px, 200, 200 * remaining / total, 8);
        }
    }
    gfxFlushBuffers();
    gfxSwapBuffers();
}

void draw_top(int open, int remaining, int total)
{
    top_show(playing_video ? video_frame() : NULL, 800, open, remaining, total);
}
