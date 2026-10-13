#ifndef TT_STAGE_H
#define TT_STAGE_H
#include <stdbool.h>
#include <stdint.h>
/* The cabinet stage on the top screen (citro3d, real stereo): a glossy black oval floor with a magenta
 * rim and a dark back wall, the laserdisc picture standing on it as a flat additive "hologram" plane (the
 * footage carries its own filmed floor reflection). Layout from photos of a restored cabinet (docs/TASKS.md "Cabinet stage").
 * Port presentation, not arcade logic: X toggles it in game; off = the flat full-screen picture. */
bool stage_init(void);      /* C3D, shader, textures; false if it could not start */
bool stage_on(void);        /* initialised and enabled */
void stage_toggle(void);
/* Apply the video settings (vset.h): opacity 0..100 %, curve 0..VSET_CURVE_MAX. */
void stage_video(int opacity, int curve);
/* LD video off during a search: the picture is hidden, the stage lighting stays. */
void stage_blank(bool on);
/* The picture: 320x240 MVD RGB565 (red in the low bits) at column x0 of rows `stride` pixels wide;
 * NULL = no picture (the LD video is off). */
void stage_picture(const uint16_t *src, int stride, int x0);
/* Draw both eyes and swap. bar >= 0: the move-window bar (fraction of the window left). */
void stage_render(float bar);
#endif
