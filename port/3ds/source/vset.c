// See vset.h. The file is plain text, one "name=value" per line, so it can be edited by hand.
#include <string.h>
#include "cfg.h"
#include "data.h"
#include "vset.h"

#define VSET_FILE TT_SAVE_DIR "/video.cfg"

static vset_t v = {VSET_DEFAULT_OPACITY, VSET_DEFAULT_CURVE};
static int loaded;

static int clamp(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

static void apply(void *user, const char *name, int value)
{
    (void)user;
    if (!strcmp(name, "opacity")) v.opacity = clamp(value, 0, 100);
    if (!strcmp(name, "curve")) v.curve = clamp(value, 0, VSET_CURVE_MAX);
}

vset_t *vset(void)
{
    if (!loaded) {
        loaded = 1;
        cfg_read(VSET_FILE, apply, NULL);
    }
    return &v;
}

bool vset_change(int d_opacity, int d_curve)
{
    vset_t *s = vset(), old = *s;
    s->opacity = clamp(s->opacity + d_opacity, 0, 100);
    s->curve = clamp(s->curve + d_curve, 0, VSET_CURVE_MAX);
    return s->opacity != old.opacity || s->curve != old.curve;
}

void vset_reset(void)
{
    vset();   // load first, so a later lazy load cannot overwrite the reset
    v.opacity = VSET_DEFAULT_OPACITY;
    v.curve = VSET_DEFAULT_CURVE;
}

void vset_save(void)
{
    FILE *f = cfg_create(VSET_FILE);
    if (!f) return;
    fprintf(f, "opacity=%d\ncurve=%d\n", v.opacity, v.curve);
    fclose(f);
}
