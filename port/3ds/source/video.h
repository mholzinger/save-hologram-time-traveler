#ifndef TT_VIDEO_H
#define TT_VIDEO_H
#include <stdbool.h>
#include <stdint.h>
/* MVD (New 3DS hardware H.264) playback of one clip: Annex-B stream, 800x240 side-by-side L|R
 * (tools/encode_ld.py). Runs on the New 3DS. */
bool video_init(void);                 /* mvdstdInit; false on Old 3DS or failure */
void video_exit(void);                 /* closes the clip; the decoder stays open for the next game */
void video_shutdown(void);             /* at app exit: also closes the MVD session */
bool video_open(const char *path);     /* loads the whole stream into linear memory */
bool video_next(void);                 /* decodes NAL units until one frame is rendered */
void video_close(void);
const uint16_t *video_frame(void);   /* the last decoded 800x240 RGB565 frame, NULL if none */
int video_frame_bytes(void);           /* size of one decoded 800x240 RGB565 frame */
bool video_grab(void *dst);            /* copy the last decoded frame (for screens that step backwards) */
void video_blit_from(const void *src); /* show a grabbed frame */
int video_eye_bytes(void);             /* one eye of a frame, 400x240 RGB565 */
bool video_grab_eye(void *dst);        /* copy the left eye of the last frame */
void video_blit_eye(const void *src);  /* show a one-eye copy in both eyes */
#endif
