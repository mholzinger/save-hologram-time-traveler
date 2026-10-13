#ifndef TT_VSET_H
#define TT_VSET_H
#include <stdbool.h>
/* Video settings (port presentation, not arcade logic), kept on the SD card in video.cfg next to the
 * high scores, so they survive restarts and work with the one-file build. */
typedef struct {
    int opacity;   /* 0..100 %: 0 = the picture only adds light (a pure hologram), 100 = the actors are
                      solid and hide what is behind them; black always stays see-through */
    int curve;     /* 0..VSET_CURVE_MAX: 0 = flat mirror, up = the picture bowed away (middle further back) */
} vset_t;
#define VSET_OPACITY_STEP 5
#define VSET_CURVE_MAX 10
#define VSET_DEFAULT_OPACITY 70   /* closer to the arcade (tuned on hardware) */
#define VSET_DEFAULT_CURVE 7
vset_t *vset(void);    /* the settings; loaded on first use (defaults if no file) */
bool vset_change(int d_opacity, int d_curve);   /* step them, clamped; true if a value changed */
void vset_reset(void);                          /* back to the defaults */
void vset_save(void);
#endif
