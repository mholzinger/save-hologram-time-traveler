// Optional user-local art (see art.h, docs/ART.md), from the data root (data.h). Images are BGR8 in
// the framebuffer's memory order (tools/art_convert.py), so they are copied as they are; the bottom
// screen may be in the text console's RGB565 mode, so that case converts.
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "art.h"
#include "data.h"

#define TOP_W 400
#define BOT_W 320
#define SCR_H 240

uint8_t *art_load(const char *name, int w, int h)
{
    char path[64];
    snprintf(path, sizeof path, "%s/art/%s.bgr", tt_data, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    size_t n = (size_t)w * h * 3;
    uint8_t *img = malloc(n);
    bool ok = img && fread(img, 1, n, f) == n;
    fclose(f);
    if (!ok) {
        free(img);
        return NULL;
    }
    return img;
}

void art_top(const uint8_t *img)
{
    if (gfxGetScreenFormat(GFX_TOP) != GSP_BGR8_OES) return;   // the port leaves the top screen in BGR8
    for (int e = 0; e < 2; e++) {
        u8 *fb = gfxGetFramebuffer(GFX_TOP, e ? GFX_RIGHT : GFX_LEFT, NULL, NULL);
        if (img)
            memcpy(fb, img, TOP_W * SCR_H * 3);
        else
            memset(fb, 0, TOP_W * SCR_H * 3);
    }
}

static bool bottom_565(void) { return gfxGetScreenFormat(GFX_BOTTOM) == GSP_RGB565_OES; }

void art_bottom(const uint8_t *img)
{
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    if (!bottom_565()) {
        if (img)
            memcpy(fb, img, BOT_W * SCR_H * 3);
        else
            memset(fb, 0, BOT_W * SCR_H * 3);
        return;
    }
    u16 *d = (u16 *)fb;
    for (int i = 0; i < BOT_W * SCR_H; i++) {
        if (!img) {
            d[i] = 0;
            continue;
        }
        const u8 *p = img + i * 3;   // b, g, r
        d[i] = (u16)((p[2] >> 3) << 11 | (p[1] >> 2) << 5 | p[0] >> 3);
    }
}

static void bottom_px(u8 *fb, bool c565, int x, int y, u8 r, u8 g, u8 b)
{
    if (x < 0 || x >= BOT_W || y < 0 || y >= SCR_H) return;
    int i = x * SCR_H + (SCR_H - 1 - y);   // column-major, origin bottom-left
    if (c565) {
        ((u16 *)fb)[i] = (u16)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
    } else {
        u8 *p = fb + i * 3;
        p[0] = b;
        p[1] = g;
        p[2] = r;
    }
}

void art_text(int x, int y, const char *s, uint8_t r, uint8_t g, uint8_t b)
{
    const ConsoleFont *font = &consoleGetDefault()->font;   // libctru's 8x8 font, MSB = left pixel
    u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
    bool c565 = bottom_565();
    for (int pass = 0; pass < 2; pass++) {   // shadow, then the text
        int ox = pass ? 0 : 1, oy = pass ? 0 : 1;
        u8 cr = pass ? r : 0, cg = pass ? g : 0, cb = pass ? b : 0;
        for (int i = 0; s[i]; i++) {
            int c = (u8)s[i] - font->asciiOffset;
            if (c < 0 || c >= font->numChars) continue;
            const u8 *gl = font->gfx + 8 * c;
            for (int row = 0; row < 8; row++)
                for (int col = 0; col < 8; col++)
                    if (gl[row] & (0x80 >> col)) bottom_px(fb, c565, x + i * 8 + col + ox, y + row + oy, cr, cg, cb);
        }
    }
}

void art_present(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

void art_splash(void)
{
    uint8_t *top = art_load("splash", TOP_W, SCR_H), *bot = art_load("splash_bottom", BOT_W, SCR_H);
    // Both buffers of the (double-buffered) top screen, so the picture holds whichever is shown.
    for (int k = 0; k < 2; k++) {
        art_top(top);
        art_bottom(bot);
        if (!bot) art_text(160 - 8 * 8, 116, "TIME TRAVELER 3D", 0xE0, 0xE0, 0xE0);   // no art: the title on black
        art_present();
    }
    u64 end = osGetTime() + 2000;
    while (aptMainLoop() && osGetTime() < end) {
        hidScanInput();
        if (hidKeysDown()) break;
        gspWaitForVBlank();
    }
    free(top);
    free(bot);
}
