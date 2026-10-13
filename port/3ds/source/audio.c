// Sound on NDSP: channel 0 plays the laserdisc audio (one mono WAV per clip and audio channel), channel 1
// the cabinet's square-wave tone generator (tt_sound steps). See audio.h.
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "audio.h"

static ndspWaveBuf wave;
static s16 *pcm;
static bool ok;
static u32 pcm_rate, pcm_samples;
static ndspWaveBuf chunk[4];   // multispeed: one 1/30 s piece per displayed frame
static bool multispeed;
static ndspWaveBuf tone_wave;
static s16 *tone_pcm;
#define TONE_MAX (32000 * 3)

bool audio_init(void)
{
    ok = R_SUCCEEDED(ndspInit());
    if (!ok) return false;
    ndspSetOutputMode(NDSP_OUTPUT_MONO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetFormat(0, NDSP_FORMAT_MONO_PCM16);
    float mix[12] = {1.0f, 1.0f};
    ndspChnSetMix(0, mix);
    // Channel 1: the cabinet's tone generator (timer 0 square wave, D2DC:0150 / D2DC:0179).
    ndspChnReset(1);
    ndspChnSetInterp(1, NDSP_INTERP_NONE);
    ndspChnSetFormat(1, NDSP_FORMAT_MONO_PCM16);
    ndspChnSetRate(1, 32000.0f);
    float mix1[12] = {0.6f, 0.6f};
    ndspChnSetMix(1, mix1);
    return true;
}

// Play a tone sequence: steps = n x {u16 freq, u16 len} (little-endian). Each step is a square wave of
// freq/2 Hz (timer 0 in alternate-compare mode at 2 MHz, D2DC:0273) lasting len/30 s; a new sound cuts the
// previous one off, as re-programming timer 0 does.
void audio_tone(const u8 *steps, int n)
{
    if (!ok || n <= 0) return;
    ndspChnWaveBufClear(1);
    if (!tone_pcm) tone_pcm = linearAlloc(TONE_MAX * 2);
    if (!tone_pcm) return;
    int pos = 0;
    double lp = 0;
    for (int i = 0; i < n && pos < TONE_MAX; i++) {
        unsigned freq = steps[4 * i] | steps[4 * i + 1] << 8, len = steps[4 * i + 2] | steps[4 * i + 3] << 8;
        if (!freq) break;
        int count = (int)(len * 32000u / 30u);
        double half = 32000.0 / (freq / 2.0) / 2.0, ph = 0;   // samples per half period
        for (int k = 0; k < count && pos < TONE_MAX; k++, pos++) {
            // Port choice (not from the ROM or MAME): the square wave goes through a one-pole low-pass at
            // ~2 kHz, like a small cabinet speaker, so it sounds less harsh; same pitch and timing.
            double x = ((int)(ph / half) & 1) ? -3000.0 : 3000.0;
            lp += 0.325 * (x - lp);   // 1 - exp(-2*pi*2000/32000)
            tone_pcm[pos] = (s16)lp;
            ph += 1.0;
        }
    }
    if (!pos) return;
    DSP_FlushDataCache(tone_pcm, pos * 2);
    memset(&tone_wave, 0, sizeof tone_wave);
    tone_wave.data_vaddr = tone_pcm;
    tone_wave.nsamples = pos;
    ndspChnWaveBufAdd(1, &tone_wave);
}

void audio_stop(void)
{
    if (!ok) return;
    multispeed = false;
    ndspChnWaveBufClear(0);
    if (pcm) linearFree(pcm);
    pcm = NULL;
}

void audio_exit(void)
{
    audio_stop();
    if (ok) ndspChnWaveBufClear(1);
    if (tone_pcm) {
        linearFree(tone_pcm);
        tone_pcm = NULL;
    }
    if (ok) ndspExit();
    ok = false;
}

// Minimal RIFF walk: find "fmt " (rate) and "data".
bool audio_play(const char *path)
{
    audio_stop();
    if (!ok) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    u8 hdr[12];
    u32 rate = 32000, size = 0;
    bool found = false;
    if (fread(hdr, 1, 12, f) == 12 && !memcmp(hdr, "RIFF", 4) && !memcmp(hdr + 8, "WAVE", 4)) {
        u8 ch[8];
        while (fread(ch, 1, 8, f) == 8) {
            u32 n = ch[4] | ch[5] << 8 | ch[6] << 16 | (u32)ch[7] << 24;
            if (!memcmp(ch, "fmt ", 4)) {
                u8 fmt[16];
                if (n < 16 || fread(fmt, 1, 16, f) != 16) break;
                rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16 | (u32)fmt[7] << 24;
                fseek(f, (long)(n - 16 + (n & 1)), SEEK_CUR);
            } else if (!memcmp(ch, "data", 4)) {
                size = n;
                found = true;
                break;
            } else {
                fseek(f, (long)(n + (n & 1)), SEEK_CUR);
            }
        }
    }
    if (!found || !(pcm = linearAlloc(size)) || fread(pcm, 1, size, f) != size) {
        fclose(f);
        audio_stop();
        return false;
    }
    fclose(f);
    DSP_FlushDataCache(pcm, size);
    ndspChnSetRate(0, (float)rate);
    pcm_rate = rate;
    pcm_samples = size / 2;
    memset(&wave, 0, sizeof wave);
    wave.data_vaddr = pcm;
    wave.nsamples = size / 2;
    ndspChnWaveBufAdd(0, &wave);
    // A new clip plays at normal speed: undo a pause left by a speed effect or rewind that the
    // clip change cut short (a death during x3 / x0.5 would otherwise silence every later clip).
    ndspChnSetPaused(0, false);
    return true;
}

// Restart the loaded clip at LD frame `frame` (29.97 fps).
void audio_seek_frame(int frame)
{
    if (!ok || !pcm) return;
    u32 off = (u32)((u64)frame * pcm_rate * 1001 / 30000);
    if (off >= pcm_samples) off = pcm_samples - 1;
    multispeed = false;
    ndspChnWaveBufClear(0);
    memset(&wave, 0, sizeof wave);
    wave.data_vaddr = pcm + off;
    wave.nsamples = pcm_samples - off;
    ndspChnWaveBufAdd(0, &wave);
}

// Laserdisc multispeed (MF/MR at SP != 30): MAME's LD-V4200 does not mute it (ldv4200hle.cpp:652-657,
// squelch only in still/pause/search) and plays, per field, the audio of the track on screen at normal
// pitch (laserdsc.cpp process_track_data). So x3 sounds chopped and x0.5 repeats each frame's sound.
void audio_multispeed(bool on)
{
    if (!ok || multispeed == on) return;
    multispeed = on;
    ndspChnWaveBufClear(0);
    ndspChnSetPaused(0, false);
}

// While in multispeed: queue the 1/30 s of sound belonging to LD frame `frame` (call once per 1/30 s).
void audio_frame_chunk(int frame)
{
    if (!ok || !pcm || !multispeed || frame < 0) return;
    u32 len = pcm_rate * 1001 / 30000;
    u32 off = (u32)((u64)frame * pcm_rate * 1001 / 30000);
    if (off >= pcm_samples) return;
    if (off + len > pcm_samples) len = pcm_samples - off;
    int queued = 0;
    ndspWaveBuf *free_buf = NULL;
    for (int i = 0; i < 4; i++) {
        if (chunk[i].status == NDSP_WBUF_QUEUED || chunk[i].status == NDSP_WBUF_PLAYING)
            queued++;
        else if (!free_buf)
            free_buf = &chunk[i];
    }
    if (!free_buf || queued >= 2) return;   // keep the latency at most 2 frames
    memset(free_buf, 0, sizeof *free_buf);
    free_buf->data_vaddr = pcm + off;
    free_buf->nsamples = len;
    ndspChnWaveBufAdd(0, free_buf);
}

void audio_pause(bool paused)
{
    if (ok) ndspChnSetPaused(0, paused);
}
