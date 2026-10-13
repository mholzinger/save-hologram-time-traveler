#ifndef TT_LED_H
#define TT_LED_H
/* Bottom-screen stand-in for the cabinet's 16-character 14-segment LED panel. */
void led_clear(void);
void led_text(int row, const char *s);   /* row 0..5, up to 16 characters, '@'..'_' and ' '..'?' */
void led_present(void);   /* redraws only when the text changed */
void led_single_line(int on); /* 1: only row 2, centered (the cabinet's one line); 0: 6-row grid */
void led_invalidate(void); /* force the next led_present to redraw (screen used by something else) */
/* For the cabinet stage's in-scene panel (stage.c): the cabinet's one line (row 2), and the 15 segments of
 * one character as end points in a 14x30 box (y down), each with whether it is lit. */
const char *led_line(void);
void led_segments(char c, void (*seg)(void *u, int x0, int y0, int x1, int y1, int lit), void *u);
#endif
