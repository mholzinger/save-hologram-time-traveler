#ifndef TT_ART_H
#define TT_ART_H
#include <stdbool.h>
#include <stdint.h>
/* Optional user-local art (docs/ART.md): raw BGR8 screens from sdmc:/timetrv/art/, made by `make art`.
 * Never shipped; every caller copes with a file being missing. */
uint8_t *art_load(const char *name, int w, int h);   /* malloc'd image, or NULL if missing/short */
void art_top(const uint8_t *img);      /* 400x240 into both eyes; NULL = black */
void art_bottom(const uint8_t *img);   /* 320x240; NULL = black */
/* Text on the bottom screen in the console's 8x8 font, with a 1-px shadow, over whatever is there. */
void art_text(int x, int y, const char *s, uint8_t r, uint8_t g, uint8_t b);
void art_present(void);                /* flush and show what was drawn */
void art_splash(void);                 /* boot splash: 2 s or until a button */
#define ART_MENU_TOP 106               /* first text row under the menu header (tools/art_convert.py) */
#endif
