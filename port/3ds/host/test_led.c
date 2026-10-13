/* Renders the LED panel (led.c) to a PPM so the 14-segment glyphs can be checked on the host. */
#include <stdio.h>
#include "3ds.h"
#include "../source/led.h"

static u8 fb[320 * 240 * 3];
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t s)
{
    (void)s;
    return GSP_BGR8_OES;
}
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t e, u16 *w, u16 *h)
{
    (void)s;
    (void)e;
    (void)w;
    (void)h;
    return fb;
}
void gfxSetDoubleBuffering(gfxScreen_t s, bool on)
{
    (void)s;
    (void)on;
}
Result GSPGPU_FlushDataCache(const void *p, u32 n)
{
    (void)p;
    (void)n;
    return 0;
}

int main(int argc, char **argv)
{
    led_clear();
    led_single_line(0);   /* settings-style grid */
    led_text(0, "SCORE     123450");
    led_text(1, "LIVES 3  CUBES 1");
    led_text(3, "CHAPTER 1 EP 20");
    led_text(4, "ABCDEFGHIJKLMNOP");
    led_text(5, "QRSTUVWXYZ 0-9?!");
    led_present();
    FILE *f = fopen(argc > 1 ? argv[1] : "led.ppm", "wb");
    fprintf(f, "P6 320 240 255\n");
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            u8 *p = fb + ((x * 240) + (239 - y)) * 3;
            u8 rgb[3] = {p[2], p[1], p[0]};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return 0;
}
