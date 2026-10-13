// MVD playback. Pattern follows libctru's mvd.h documentation: NAL units with the 3-byte
// 00 00 01 prefix, in linear memory, fed to mvdstdProcessVideoFrame; a frame is rendered with
// mvdstdRenderVideoFrame when it returns MVD_STATUS_FRAMEREADY. Runs on the New 3DS.
#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "video.h"

#define VW 800
#define VH 240
#define TOP_W 400
#define TOP_H 240

static MVDSTD_Config config;
static u8 *inbuf, *outbuf;
static u32 inlen, pos;
static bool ready, have_frame;

static volatile int init_result = 1;   // 1 = pending

static void init_thread(void *arg)
{
    (void)arg;
    init_result = R_SUCCEEDED(mvdstdInit(MVDMODE_VIDEOPROCESSING, MVD_INPUT_H264, MVD_OUTPUT_RGB565,
                                         MVD_DEFAULT_WORKBUF_SIZE, NULL))
        ? 0
        : -1;
}

bool video_init(void)
{
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    if (!n3ds) return false;
    // mvdstdInit gets its service handle with srvGetServiceHandle, which blocks until the service
    // is registered. Azahar has no mvd:STD (log: "called service=mvd:STD delayed") and doesn't
    // implement srv IsServiceRegistered either, so probe on a helper thread and give up after 1 s.
    // On a timeout the helper thread stays blocked; it is never touched again.
    static bool probed;
    if (!probed) {
        probed = true;
        Thread t = threadCreate(init_thread, NULL, 16 * 1024, 0x30, -2, false);
        if (!t) return false;
        if (R_FAILED(threadJoin(t, 1000000000ULL))) {
            init_result = -1;
            return false;
        }
        threadFree(t);
    } else if (!ready) {
        return false;
    }
    if (init_result != 0) return false;
    if (!outbuf) outbuf = linearAlloc(VW * VH * 2);
    if (!outbuf) {
        mvdstdExit();
        return false;
    }
    mvdstdGenerateDefaultConfig(&config, VW, VH, VW, VH, NULL, (u32 *)outbuf, (u32 *)outbuf);
    ready = true;
    return true;
}

void video_exit(void)
{
    video_close();   // the MVD session and output buffer stay open for the next game
}

void video_shutdown(void)   // at app exit: close the MVD session too
{
    video_close();
    if (ready) mvdstdExit();
    if (outbuf) {
        linearFree(outbuf);
        outbuf = NULL;
    }
    ready = false;
}

bool video_open(const char *path)
{
    video_close();
    if (!ready) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    if (size <= 0) {
        fclose(f);
        return false;
    }
    inlen = (u32)size;
    fseek(f, 0, SEEK_SET);
    inbuf = linearAlloc(inlen);
    if (!inbuf || fread(inbuf, 1, inlen, f) != inlen) {
        fclose(f);
        video_close();
        return false;
    }
    fclose(f);
    GSPGPU_FlushDataCache(inbuf, inlen);
    pos = 0;
    have_frame = false;
    return true;
}

void video_close(void)
{
    if (inbuf) linearFree(inbuf);
    inbuf = NULL;
    inlen = pos = 0;
    have_frame = false;
}

// Next NAL unit [start, end) beginning at a 00 00 01 prefix (a 4-byte prefix's leading 00 is skipped).
static bool next_nal(u32 *start, u32 *end)
{
    u32 i = pos;
    while (i + 3 <= inlen && !(inbuf[i] == 0 && inbuf[i + 1] == 0 && inbuf[i + 2] == 1)) i++;
    if (i + 3 > inlen) return false;
    u32 j = i + 3;
    while (j + 3 <= inlen &&
           !(inbuf[j] == 0 && inbuf[j + 1] == 0 &&
             (inbuf[j + 2] == 1 || (inbuf[j + 2] == 0 && j + 3 < inlen && inbuf[j + 3] == 1))))
        j++;
    if (j + 3 > inlen) j = inlen;
    *start = i;
    *end = j;
    pos = j;
    return true;
}

bool video_next(void)
{
    u32 s, e;
    while (inbuf && next_nal(&s, &e)) {
        Result r = mvdstdProcessVideoFrame(inbuf + s, e - s, 0, NULL);
        if (!MVD_CHECKNALUPROC_SUCCESS(r)) return false;
        if (r == MVD_STATUS_FRAMEREADY) {
            mvdstdRenderVideoFrame(&config, true);
            GSPGPU_InvalidateDataCache(outbuf, VW * VH * 2);
            have_frame = true;
            return true;
        }
    }
    return false;
}

// Top framebuffers are BGR8 and column-major (240-px columns, origin bottom-left).
static void blit(const u16 *src)
{
    for (int eye = 0; eye < 2; eye++) {
        u8 *fb = gfxGetFramebuffer(GFX_TOP, eye ? GFX_RIGHT : GFX_LEFT, NULL, NULL);
        for (int x = 0; x < TOP_W; x++) {
            const u16 *col = src + eye * TOP_W + x;
            u8 *p = fb + x * TOP_H * 3;
            for (int y = TOP_H - 1; y >= 0; y--, p += 3) {
                // MVD's RGB565 output has red in the low 5 bits on hardware: a blue sign on the disc
                // showed red on the New 3DS (2026-10-11 photo vs tools/encode_ld.py output).
                u16 v = col[y * VW];
                p[0] = (u8)((v >> 11) << 3);           // B
                p[1] = (u8)(((v >> 5) & 0x3F) << 2);   // G
                p[2] = (u8)((v & 0x1F) << 3);          // R
            }
        }
    }
}

const uint16_t *video_frame(void) { return have_frame ? (const uint16_t *)outbuf : NULL; }

int video_frame_bytes(void) { return VW * VH * 2; }

bool video_grab(void *dst)
{
    if (!have_frame) return false;
    memcpy(dst, outbuf, VW * VH * 2);
    return true;
}

void video_blit_from(const void *src) { blit((const u16 *)src); }

// One eye (the left half, 400x240 RGB565) of the last frame, and showing such a copy in both eyes (flat):
// used for the cube rewind, which plays cached frames backwards.
int video_eye_bytes(void) { return TOP_W * VH * 2; }

bool video_grab_eye(void *dst)
{
    if (!have_frame) return false;
    for (int y = 0; y < VH; y++) memcpy((u16 *)dst + y * TOP_W, (const u16 *)outbuf + y * VW, TOP_W * 2);
    return true;
}

void video_blit_eye(const void *src)
{
    const u16 *s = src;
    for (int eye = 0; eye < 2; eye++) {
        u8 *fb = gfxGetFramebuffer(GFX_TOP, eye ? GFX_RIGHT : GFX_LEFT, NULL, NULL);
        for (int x = 0; x < TOP_W; x++) {
            u8 *p = fb + x * TOP_H * 3;
            for (int y = TOP_H - 1; y >= 0; y--, p += 3) {
                u16 v = s[y * TOP_W + x];
                p[0] = (u8)((v >> 11) << 3);
                p[1] = (u8)(((v >> 5) & 0x3F) << 2);
                p[2] = (u8)((v & 0x1F) << 3);
            }
        }
    }
}
