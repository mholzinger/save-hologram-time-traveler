/* Host stand-in for the libctru calls fe_*.c / led.c make (test_frontend only). Time is simulated:
 * gspWaitForVBlank advances to the next 1/59.83 s, video decode and blit cost configurable ms. */
#ifndef FAKE_3DS_H
#define FAKE_3DS_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int32_t s32;
typedef int64_t s64;
typedef u32 Handle;
typedef s32 Result;
typedef enum { GFX_TOP, GFX_BOTTOM } gfxScreen_t;
typedef enum { GFX_LEFT, GFX_RIGHT } gfx3dSide_t;
typedef enum { GSP_RGBA8_OES, GSP_BGR8_OES, GSP_RGB565_OES } GSPGPU_FramebufferFormat;
enum {
    KEY_A = 1 << 0,
    KEY_B = 1 << 1,
    KEY_SELECT = 1 << 2,
    KEY_START = 1 << 3,
    KEY_DRIGHT = 1 << 4,
    KEY_DLEFT = 1 << 5,
    KEY_DUP = 1 << 6,
    KEY_DDOWN = 1 << 7,
    KEY_R = 1 << 8,
    KEY_L = 1 << 9,
    KEY_X = 1 << 10,
    KEY_Y = 1 << 11
};
#define KEY_CSTICK_RIGHT (1u << 24)
#define KEY_CSTICK_LEFT (1u << 25)
#define KEY_CSTICK_UP (1u << 26)
#define KEY_CSTICK_DOWN (1u << 27)
#define KEY_CPAD_RIGHT (1u << 28)
#define KEY_CPAD_LEFT (1u << 29)
#define KEY_CPAD_UP (1u << 30)
#define KEY_CPAD_DOWN (1u << 31)
#define KEY_UP (KEY_DUP | KEY_CPAD_UP)
#define KEY_DOWN (KEY_DDOWN | KEY_CPAD_DOWN)
#define KEY_LEFT (KEY_DLEFT | KEY_CPAD_LEFT)
#define KEY_RIGHT (KEY_DRIGHT | KEY_CPAD_RIGHT)
#define CUR_THREAD_HANDLE 0xFFFF8000
#define U64_MAX UINT64_MAX
typedef struct {
    int v;
} LightEvent;
typedef enum { RESET_ONESHOT, RESET_STICKY } ResetType;
typedef void *Thread;
typedef void (*ThreadFunc)(void *);
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t e, u16 *w, u16 *h);
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t s);
void gfxSetDoubleBuffering(gfxScreen_t s, bool on);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
Result GSPGPU_FlushDataCache(const void *p, u32 n);
void gspWaitForVBlank(void);
bool aptMainLoop(void);
void hidScanInput(void);
u32 hidKeysDown(void);
u32 hidKeysHeld(void);
u64 osGetTime(void);
float osGet3DSliderState(void);
void consoleClear(void);
void LightEvent_Init(LightEvent *e, ResetType t);
void LightEvent_Signal(LightEvent *e);
void LightEvent_Wait(LightEvent *e);
Result svcGetThreadPriority(s32 *p, Handle h);
Thread threadCreate(ThreadFunc f, void *arg, size_t stack, int prio, int core, bool detached);
Result threadJoin(Thread t, u64 timeout);
void threadFree(Thread t);
/* sdmc:/ paths map to build/sdmc/ */
FILE *host_fopen(const char *path, const char *mode);
#define fopen host_fopen
#endif
