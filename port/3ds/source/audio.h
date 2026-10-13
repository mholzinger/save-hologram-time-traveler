#ifndef TT_AUDIO_H
#define TT_AUDIO_H
#include <stdbool.h>
/* NDSP playback of one clip's mono PCM16 WAV (tools/encode_ld.py, 32 kHz). Needs the user's
 * own DSP firmware dump at sdmc:/3ds/dspfirm.cdc. Runs on the New 3DS. */
bool audio_init(void);
void audio_exit(void);
bool audio_play(const char *wav_path);
void audio_stop(void);
void audio_seek_frame(int ld_frame);   /* restart the loaded clip at that LD frame */
void audio_pause(bool paused);
void audio_multispeed(bool on);         /* LD fast/slow play: sound follows the frame on screen */
void audio_frame_chunk(int ld_frame);   /* multispeed: queue that frame's 1/30 s of sound */
void audio_tone(const unsigned char *steps, int n);   /* tone generator: n x {u16 freq, u16 len}, see audio.c */
#endif
