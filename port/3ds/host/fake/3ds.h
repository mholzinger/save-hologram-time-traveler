/* Host stand-in for the bits of libctru that led.c uses (render tests only). */
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef enum { GFX_TOP, GFX_BOTTOM } gfxScreen_t;
typedef enum { GFX_LEFT, GFX_RIGHT } gfx3dSide_t;
u8 *gfxGetFramebuffer(gfxScreen_t s, gfx3dSide_t e, u16 *w, u16 *h);
typedef enum { GSP_RGBA8_OES, GSP_BGR8_OES, GSP_RGB565_OES } GSPGPU_FramebufferFormat;
GSPGPU_FramebufferFormat gfxGetScreenFormat(gfxScreen_t s);
#include <stdbool.h>
typedef int32_t Result;
void gfxSetDoubleBuffering(gfxScreen_t s, bool on);
Result GSPGPU_FlushDataCache(const void *p, u32 n);
