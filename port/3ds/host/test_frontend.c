/* Runs the real 3DS front end (fe_*.c + engine.c + led.c) on the host with simulated time and
 * scripted buttons, logging which footage plays.  ./test_frontend [DECODE_MS] [BLIT_MS] PRESS...
 * PRESS = <ms>:<key>[:<hold ms>], key one of up down left right a b start select x; times are from
 * the start of the game. Example: ./test_frontend 12 6 2000:right 60000:start */
#include <3ds.h>
#include <execinfo.h>
#include <stdlib.h>
#include <string.h>

#include "../source/audio.h"
#include "../source/frontend.h"
#include "../source/video.h"
#include "../source/ledmsg.h"

#undef fopen
static double now_ms, decode_ms = 12, blit_ms = 6;
static struct {
    double t, hold;
    u32 key;
} script[64];
static int nscript;
static u32 held, prev;
static u8 fb_top[2][400 * 240 * 3], fb_bot[320 * 240 * 3];

/* The SD card is a sandbox golden.sh makes fresh for each run (build/host/sdmc: the .ttg only), so the
 * outputs depend on nothing left on this machine (saved settings, high scores, encodes). */
FILE *host_fopen(const char *path, const char *mode)
{
    char p[256];
    if (!strncmp(path, "sdmc:/", 6)) {
        snprintf(p, sizeof p, "../../../build/host/sdmc/%s", path + 6);
        path = p;
    }
    return fopen(path, mode);
}
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t e, u16 *w, u16 *h)
{
    (void)w;
    (void)h;
    return s == GFX_TOP ? fb_top[e] : fb_bot;
}
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t s)
{
    (void)s;
    return GSP_BGR8_OES;
}
void gfxSetDoubleBuffering(gfxScreen_t s, bool on)
{
    (void)s;
    (void)on;
}
void gfxFlushBuffers(void) {}
void gfxSwapBuffers(void) {}
Result GSPGPU_FlushDataCache(const void *p, u32 n)
{
    (void)p;
    (void)n;
    return 0;
}
void gspWaitForVBlank(void)
{
    static char last[17];
    if (getenv("TT_LED") && strcmp(last, ledm_line())) {
        strcpy(last, ledm_line());
        printf("%9.0f ms  LED |%s|\n", now_ms, last);
    }
    double v = 1000.0 / 59.83, n = ((long)(now_ms / v) + 1) * v;
    now_ms = n > now_ms + 0.001 ? n : n + v;
}
bool aptMainLoop(void) { return true; }
static double last_t;
static long same;
void hidScanInput(void)
{
    if (now_ms == last_t) {
        if (++same > 200000) {
            printf("STUCK: time %.0f ms not advancing\n", now_ms);
            void *bt[16];
            backtrace_symbols_fd(bt, backtrace(bt, 16), 1);
            exit(3);
        }
    } else {
        same = 0;
        last_t = now_ms;
    }
    prev = held;
    held = 0;
    for (int i = 0; i < nscript; i++)
        if (now_ms >= script[i].t && now_ms < script[i].t + script[i].hold) held |= script[i].key;
}
u32 hidKeysDown(void) { return held & ~prev; }
u32 hidKeysHeld(void) { return held; }
u64 osGetTime(void) { return (u64)now_ms; }
float osGet3DSliderState(void) { return 0.5f; }
void consoleClear(void) {}
void LightEvent_Init(LightEvent *e, ResetType t)
{
    (void)e;
    (void)t;
}
void LightEvent_Signal(LightEvent *e) { (void)e; }
void LightEvent_Wait(LightEvent *e) { (void)e; }
Result svcGetThreadPriority(s32 *p, Handle h)
{
    (void)h;
    *p = 0x30;
    return 0;
}
Thread threadCreate(ThreadFunc f, void *a, size_t s, int p, int c, bool d)
{
    (void)f;
    (void)a;
    (void)s;
    (void)p;
    (void)c;
    (void)d;
    return NULL;
}
Result threadJoin(Thread t, u64 to)
{
    (void)t;
    (void)to;
    return 0;
}
void threadFree(Thread t) { (void)t; }

/* audio: no-op */
bool audio_init(void) { return true; }
void audio_exit(void) {}
static int a_paused;
bool audio_play(const char *p)
{
    printf("%9.0f ms  audio_play %s%s\n", now_ms, strrchr(p, '/') + 1, a_paused ? " (was paused, now on)" : "");
    a_paused = 0;
    return true;
}
void audio_stop(void) {}
static int ms_chunks;
void audio_seek_frame(int f)
{
    if (ms_chunks) printf("%9.0f ms  audio normal at frame %d after %d multispeed chunks\n", now_ms, f, ms_chunks);
    ms_chunks = 0;
}
bool stage_init(void) { return false; }
const char *tt_data = "sdmc:/timetrv";   /* data.c: the host reads the SD-card paths */
#include "../source/vset.h"
static vset_t vs = {35, 0};   /* vset.c would write build/sdmc/timetrv/video.cfg, the staged card */
vset_t *vset(void) { return &vs; }
bool vset_change(int d_opacity, int d_curve)
{
    vs.opacity += d_opacity;
    vs.curve += d_curve;
    return d_opacity || d_curve;
}
void vset_reset(void) {}
void vset_save(void) {}
void stage_video(int o, int c)
{
    (void)o;
    (void)c;
}
void stage_blank(bool on) { (void)on; }
bool stage_on(void) { return false; }
void stage_toggle(void) {}
void stage_picture(const uint16_t *s, int st, int x0)
{
    (void)s;
    (void)st;
    (void)x0;
}
void stage_render(float b) { (void)b; }
const uint16_t *video_frame(void) { return NULL; }
void audio_multispeed(bool on)
{
    printf("%9.0f ms  audio multispeed %s%s\n", now_ms, on ? "on" : "off", a_paused ? " (paused)" : "");
    if (!on) printf("  %d chunks\n", ms_chunks);
    ms_chunks = 0;
}
void audio_frame_chunk(int f)
{
    (void)f;
    ms_chunks++;
}
void audio_pause(bool p)
{
    a_paused = p;
    printf("%9.0f ms  audio %s\n", now_ms, p ? "muted" : "on");
}
void audio_tone(const unsigned char *st, int n)
{
    printf("%9.0f ms  tone %u Hz x%d\n", now_ms, (st[0] | st[1] << 8) / 2, n);
}

/* video: no footage is needed; each clip's frame count comes from build/host/media_frames.txt
 * ("name frames" per line), which tools/check_media.py --manifest writes from data/ (and checks against
 * the real encodes when they exist). */
static int vframes, vpos;
bool video_init(void) { return true; }
void video_exit(void) {}
static int manifest_frames(const char *name)
{
    FILE *f = fopen("../../../build/host/media_frames.txt", "r");
    char line[64], n[32];
    int frames = -1, c;
    while (f && frames < 0 && fgets(line, sizeof line, f))
        if (sscanf(line, "%31s %d", n, &c) == 2 && !strcmp(n, name)) frames = c;
    if (f) fclose(f);
    return frames;
}
bool video_open(const char *path)
{
    char name[32];
    snprintf(name, sizeof name, "%s", strrchr(path, '/') + 1);
    if (strchr(name, '.')) *strchr(name, '.') = 0;
    int c = manifest_frames(name);
    if (c < 0) {
        printf("%9.0f ms  video_open %s MISSING\n", now_ms, path);
        return false;
    }
    vframes = c;
    vpos = 0;
    printf("%9.0f ms  video_open %s (%d frames)\n", now_ms, strrchr(path, '/') + 1, c);
    return true;
}
bool video_next(void)
{
    if (vpos >= vframes) return false;
    vpos++;
    now_ms += decode_ms;
    return true;
}
void video_blit(void) { now_ms += blit_ms; }
void video_close(void) {}
int video_frame_bytes(void) { return 16; }
bool video_grab(void *d)
{
    memset(d, 0, 16);
    return true;
}
void video_blit_from(const void *s)
{
    (void)s;
    now_ms += blit_ms;
}
int video_eye_bytes(void) { return 16; }
static int eyes_grabbed, eyes_shown;
bool video_grab_eye(void *d)
{
    memset(d, 0, 16);
    eyes_grabbed++;
    return true;
}
void video_blit_eye(const void *s)
{
    (void)s;
    now_ms += blit_ms;
    if (!eyes_shown++) printf("%9.0f ms  rewind: %d frames cached\n", now_ms, eyes_grabbed);
}

static u32 keyname(const char *s)
{
    static const struct {
        const char *n;
        u32 k;
    } m[] = {{"up", KEY_DUP}, {"down", KEY_DDOWN}, {"left", KEY_DLEFT},     {"right", KEY_DRIGHT},
             {"a", KEY_A},    {"b", KEY_B},        {"start", KEY_START},    {"select", KEY_SELECT},
             {"x", KEY_X},    {"y", KEY_Y},        {"cdown", KEY_CPAD_DOWN}};
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++)
        if (!strcmp(s, m[i].n)) return m[i].k;
    fprintf(stderr, "bad key %s\n", s);
    exit(2);
}
static void bye(void) { printf("%9.0f ms  exit\n", now_ms); }

int main(int argc, char **argv)
{
    int i = 1;
    if (i < argc && strchr(argv[i], ':') == NULL) decode_ms = atof(argv[i++]);
    if (i < argc && strchr(argv[i], ':') == NULL) blit_ms = atof(argv[i++]);
    for (; i < argc && nscript < 64; i++) {
        char key[16] = "";
        double t = 0, hold = 120;
        sscanf(argv[i], "%lf:%15[a-z]:%lf", &t, key, &hold);
        script[nscript].t = t;
        script[nscript].hold = hold;
        script[nscript].key = keyname(key);
        printf("press %s at %.0f ms for %.0f ms\n", key, t, hold);
        nscript++;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    atexit(bye);
    frontend_play(1);
    return 0;
}
