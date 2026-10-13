// The cabinet stage (see stage.h). citro3d, both eyes rendered from a stereo projection, so the floor
// and wall have real depth and the picture is a flat plane standing on the floor, like the cabinet's
// hologram. The picture is added to the scene (additive blend): black is the LD video's "nothing", so
// it lets the stage show through, as the mirror image does in the cabinet.
#include <3ds.h>
#include <citro3d.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "led.h"
#include "stage.h"
#include "vset.h"
#include "stage_shbin.h"

#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define PIC_W 320
#define PIC_H 240
#define TEX_W 512
#define TEX_H 256

// World units: the picture is 6 x 4.5 (4:3) at z = 0, facing the camera. The footage was shot on the
// cabinet's glossy floor: the actors' feet are at about row 140 of 240 and their filmed reflection is
// below that (median of 29 sampled clips; tools/encode_ld.py mid frames). So that row sits on the stage
// floor (y = 0) and the filmed reflection shows in the floor; the stage adds no reflection of its own.
#define PW 6.0f
#define PH 4.5f
#define FLOOR_ROW 140.0f
#define PIC_TOP (PH * FLOOR_ROW / PIC_H)
#define PIC_Z 1.6f         // the image stands over the middle of the floor, in front of the props
#define CAM_Y 5.2f         // about 30 degrees down onto the floor, as the player sees the cabinet (photos)
#define CAM_Z 9.5f
#define LOOK_Y 1.0f
// The glossy black floor, seen from above: the back is one circular arc across the whole width (about
// the top third of a circle), whose ends turn through rounded corners into a straight front edge
// (photos of a restored cabinet).
#define FLOOR_W 4.9f       // half width where the arc ends
#define FLOOR_ARC 60.0f    // half angle of the arc, degrees (120 degrees = a third of the circle)
#define FLOOR_BACK (-2.1f) // z of the back of the arc
#define FLOOR_RC 1.6f      // corner radius
#define WALL_Z (-3.8f)
#define STAGE_FOV 34.0f    // vertical field of view, degrees
// The whole view sits this far up the screen (fraction of the half height), so the bottom of a curved
// picture (the Time Map's labels) stays on screen (checked on hardware). A shift of the projection, so
// the camera angle and the stereo depth are unchanged.
#define VIEW_SHIFT 0.2f
#define BAR_ALPHA 0.3f     // the move-window bar is a port aid, not in the arcade: kept faint
#define IOD_MAX 1.1f       // eye separation at full 3D slider (world units)
#define ARC_SEG 32         // back curve
#define CORNER_SEG1 2      // each front corner: from the arc to the side
#define CORNER_SEG2 6      // and from the side to the front edge
// The floor reaches FLOOR_SIDE further toward the player than the corners alone: straight sides
// between the corner halves move only the front edge, to just under the bottom of the screen, so the
// lower part of the picture (drawn only on the floor, e.g. the Time Map's labels) is not cut off by the
// surround (checked on hardware). The back curve and the props stay where they are.
#define FLOOR_SIDE 1.2f
#define FLOOR_PTS (ARC_SEG + 1 + 2 * (CORNER_SEG1 + CORNER_SEG2 + 2))
#define FLOOR_TRIS FLOOR_PTS   // fan around the centre, closed

typedef struct {
    float p[3];
    float t[2];
    float c[4];
} vtx;

#define PROP_MAX 600       // vertices for all props
#define DISC_SEG 24

#define PIC_STRIPS 16     // the picture's vertical strips (the mirror curve, below)
enum {
    V_WALL = 0,
    V_RIM = 6,
    V_FLOOR = 12,
    V_PIC = V_FLOOR + FLOOR_TRIS * 3,
    V_REFL = V_PIC + PIC_STRIPS * 6,
    V_BAR = V_REFL + PIC_STRIPS * 6,
    V_PROPS = V_BAR + 6,
    V_PREFL = V_PROPS + PROP_MAX,
    V_LED = V_PREFL + PROP_MAX,
    V_LEDR = V_LED + 6,
    V_COUNT = V_LEDR + 6
};
static int prop_n;   // vertices used in the props block (and its mirror image in V_PREFL)
#define PROPS 6
static int prop_first[PROPS], prop_cnt[PROPS], props;
static u32 prop_fleck[PROPS];   // each prop's fleck colour, 0xRRGGBB
#define SPECK_W 128        // the props' paint spatter texture, repeating
#define SPECK_SCALE 0.45f  // repeats per world unit
#define REFL_K 0.38f       // brightness of the props' reflection in the glossy floor (photos 19, 21)

// The cabinet surround is lit only between clips; while the video plays it sits almost black (as on a
// running cabinet). It fades between the two so a cut does not flash.
#define RIM_DIM 0.15f      // rim brightness while a picture is on
#define PROP_DIM 0.82f     // props a little dimmer while a picture is on (tuned on hardware)
#define RIM_STEP 0.06f     // per rendered frame: about half a second for a full fade
// The picture is added to the scene, and its bright parts also cover what is behind it by the opacity
// setting (vset.h): behind = behind * (1 - opacity * min(1, 4 * picture green)). Green stands in for
// brightness; x4 makes any clearly lit pixel count fully, so at 100 % the actors are solid while black
// stays see-through.
static float pic_cover = 0.70f;
// The mirror's curve (vset.h): the picture is PIC_STRIPS vertical strips; its middle stays at PIC_Z and
// its edges come forward by up to CURVE_DEPTH (bowed away from the player, side to side only, as in
// videos of the arcade).
#define CURVE_DEPTH 1.8f
static int pic_curve = -1;   // the curve step the picture geometry was built for

// The green 14-segment LED line, floating behind the floor (photos 19, 21; reference
// video: its line also shows mirrored in the glossy floor). The cabinet's one 16-character line is drawn
// into a texture whenever it changes.
// The glyphs are drawn at half the bottom screen's size (cell 10 texels, 1 texel strokes) so the texture
// is about as large as the panel on screen; at full size it shrank 3:1 and the segments aliased away.
#define LED_TW 256
#define LED_TH 32
#define LED_CW 10
#define LED_Z (-3.58f)     // just in front of the back wall; no housing drawn (on the cabinet only the text shows)
// Larger than the cabinet's (about 1.4 units half width there): at 400x240 the true size leaves about
// 6 pixels a character, too small to read.
#define LED_HW 2.2f        // half width of the 16 characters
#define LED_Y0 1.70f     // lowered with VIEW_SHIFT so the line stays on screen
#define LED_Y1 2.58f
#define LED_REFL 0.5f      // brightness of its reflection
static C3D_Tex led_tex;
static char led_shown[17] = "?";

static bool ready, enabled = true, has_pic, blank;
static float lit = 1.0f;   // 1 = no picture (stage lit), 0 = picture on; fades between
static C3D_RenderTarget *target[2];
static DVLB_s *dvlb;
static shaderProgram_s prog;
static int u_proj, u_mv;
static vtx *vbo;
static C3D_Tex tex[2], speck;
static int cur;
static u32 ox[PIC_W], oy[PIC_H];   // texel offsets in the 8x8-tiled (Morton) texture layout

// Texel offset in the GPU's tiled texture layout: 8x8 tiles stored left to right then bottom to top, and
// inside a tile the bits of x and y interleave (x bit 0, y bit 0, x bit 1, ...). The offset is the sum of
// an x part and a y part, so callers may also add tile_ofs(x, 0, w) + tile_ofs(0, y, w).
static u32 tile_ofs(int x, int y, int w)
{
    return (u32)(((y >> 3) * (w / 8) + (x >> 3)) * 64 +
                 ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3)));
}

static void quad(vtx *v, const float a[3], const float b[3], const float c[3], const float d[3], float u0, float v0,
                 float u1, float v1, float r, float g, float bl)
{
    // a = bottom left, b = bottom right, c = top right, d = top left
    const float *p[6] = {a, b, c, c, d, a};
    const float uv[6][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u1, v1}, {u0, v1}, {u0, v0}};
    for (int i = 0; i < 6; i++) {
        memcpy(v[i].p, p[i], sizeof v[i].p);
        v[i].t[0] = uv[i][0];
        v[i].t[1] = uv[i][1];
        v[i].c[0] = r;
        v[i].c[1] = g;
        v[i].c[2] = bl;
        v[i].c[3] = 1.0f;
    }
}

// The six painted props around the floor (placement and lit colours from photos
// of a restored cabinet). Each is a convex solid built in its own
// frame, turned, set down so its lowest point touches the floor, and flat shaded by one fixed light.
typedef struct {
    float p[3];
} v3;
static v3 tri_buf[PROP_MAX];
static int tri_n;

static void ptri(v3 a, v3 b, v3 c)
{
    if (tri_n + 3 <= PROP_MAX) {
        tri_buf[tri_n++] = a;
        tri_buf[tri_n++] = b;
        tri_buf[tri_n++] = c;
    }
}
static void pquad(v3 a, v3 b, v3 c, v3 d)
{
    ptri(a, b, c);
    ptri(c, d, a);
}

static void box_tris(float w, float h, float d)
{
    float x = w / 2, z = d / 2;
    v3 c[8] = {{{-x, 0, z}},  {{x, 0, z}},  {{x, h, z}},  {{-x, h, z}},
               {{-x, 0, -z}}, {{x, 0, -z}}, {{x, h, -z}}, {{-x, h, -z}}};
    pquad(c[0], c[1], c[2], c[3]);
    pquad(c[5], c[4], c[7], c[6]);
    pquad(c[4], c[0], c[3], c[7]);
    pquad(c[1], c[5], c[6], c[2]);
    pquad(c[3], c[2], c[6], c[7]);
    pquad(c[4], c[5], c[1], c[0]);
}

static void wedge_tris(float l, float b,
                       float t)   // flat triangle facing +z, t thick: point l out along +x from a base b tall
{
    float z = t / 2;
    v3 f[3] = {{{0, -b / 2, z}}, {{l, 0, z}}, {{0, b / 2, z}}},
       k[3] = {{{0, -b / 2, -z}}, {{l, 0, -z}}, {{0, b / 2, -z}}};
    ptri(f[0], f[1], f[2]);
    ptri(k[2], k[1], k[0]);
    for (int i = 0; i < 3; i++) pquad(f[i], k[i], k[(i + 1) % 3], f[(i + 1) % 3]);
}

static void disc_tris(float r, float t, float cut)   // disc facing +z, sawn flat `cut` below its centre
{
    float from = -asinf(cut / r);              // right end of the flat (angle from +x)
    float to = (float)M_PI + asinf(cut / r);  // left end
    v3 f[DISC_SEG + 1], k[DISC_SEG + 1];
    for (int i = 0; i <= DISC_SEG; i++) {
        float a = from + (to - from) * i / DISC_SEG;
        f[i] = (v3){{r * cosf(a), r * sinf(a), t / 2}};
        k[i] = (v3){{r * cosf(a), r * sinf(a), -t / 2}};
    }
    v3 fc = {{0, 0, t / 2}}, kc = {{0, 0, -t / 2}};
    for (int i = 0; i < DISC_SEG; i++) {
        ptri(fc, f[i], f[i + 1]);
        ptri(kc, k[i + 1], k[i]);
        pquad(f[i], k[i], k[i + 1], f[i + 1]);
    }
    ptri(fc, f[DISC_SEG], f[0]);
    ptri(kc, k[0], k[DISC_SEG]);
    pquad(f[DISC_SEG], k[DISC_SEG], k[0], f[0]);
}

// Turn (roll about z, then pitch about x, then yaw about y, degrees), rest on the floor at (x, z), shade.
static void place(float x, float z, float yaw, float pitch, float roll, float r, float g, float b, u32 fleck)
{
    if (props < PROPS) {
        prop_first[props] = prop_n;
        prop_fleck[props] = fleck;
    }
    float cr = cosf(roll * (float)M_PI / 180), sr = sinf(roll * (float)M_PI / 180);
    float cp = cosf(pitch * (float)M_PI / 180), sp = sinf(pitch * (float)M_PI / 180);
    float cy = cosf(yaw * (float)M_PI / 180), sy = sinf(yaw * (float)M_PI / 180);
    float lo = 1e9f, cen[3] = {0, 0, 0};
    for (int i = 0; i < tri_n; i++) {
        float *q = tri_buf[i].p;
        float x1 = q[0] * cr - q[1] * sr, y1 = q[0] * sr + q[1] * cr, z1 = q[2];
        float y2 = y1 * cp - z1 * sp, z2 = y1 * sp + z1 * cp;
        q[0] = x1 * cy + z2 * sy;
        q[1] = y2;
        q[2] = -x1 * sy + z2 * cy;
        if (q[1] < lo) lo = q[1];
    }
    for (int i = 0; i < tri_n; i++) {
        tri_buf[i].p[0] += x;
        tri_buf[i].p[1] -= lo;
        tri_buf[i].p[2] += z;
        for (int k = 0; k < 3; k++) cen[k] += tri_buf[i].p[k] / tri_n;
    }
    const float L[3] = {-0.25f, 0.55f, 0.80f};   // the cabinet light: mostly from the front, above, a little left
    for (int i = 0; i + 2 < tri_n && prop_n + 3 <= PROP_MAX; i += 3) {
        float *a = tri_buf[i].p, *bb = tri_buf[i + 1].p, *c = tri_buf[i + 2].p;
        float e1[3] = {bb[0] - a[0], bb[1] - a[1], bb[2] - a[2]}, e2[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len <= 0) continue;
        float out = 0;   // solids are convex: the normal points away from the centre
        for (int k = 0; k < 3; k++) out += n[k] * ((a[k] + bb[k] + c[k]) / 3 - cen[k]);
        int ord[3] = {0, 1, 2};
        if (out < 0) {   // wind every face counter-clockwise seen from outside, so back faces can be culled
            len = -len;
            ord[1] = 2;
            ord[2] = 1;
        }
        float d = (n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / len;
        float sh = 0.55f + 0.45f * (d > 0 ? d : 0);
        // The spatter is projected along the face's main axis (box mapping).
        float ax = fabsf(n[0]), ay = fabsf(n[1]), az = fabsf(n[2]);
        int su = ax >= ay && ax >= az ? 2 : 0, sv = ay >= ax && ay >= az ? 2 : 1;
        for (int k = 0; k < 3; k++) {
            const float *q = tri_buf[i + ord[k]].p;
            vtx *v = vbo + V_PROPS + prop_n, *m = vbo + V_PREFL + prop_n;
            prop_n++;
            memcpy(v->p, q, sizeof v->p);
            v->t[0] = q[su] * SPECK_SCALE + r;   // offset per prop so neighbours do not repeat the pattern
            v->t[1] = q[sv] * SPECK_SCALE + g;
            v->c[0] = r * sh;
            v->c[1] = g * sh;
            v->c[2] = b * sh;
            v->c[3] = 1.0f;
            *m = *v;   // the reflection: mirrored in the floor (y = 0)
            m->p[1] = -q[1];
        }
    }
    if (props < PROPS) prop_cnt[props] = prop_n - prop_first[props];
    props++;
    tri_n = 0;
}

// Where a prop meets the floor's curved back edge at x (cabinet photos: each prop's base sits on the
// edge and follows it, 2026-10-12): the yaw that turns its front along the edge, and the point to place
// it so its front bottom edge is just inside the floor (so the reflection starts at its base). d = the
// prop's depth; m = where along its own x axis the part on the edge is centred (0 for a box).
#define EDGE_IN 0.05f
#define DISC_BEHIND 0.25f   // how far the disc stands behind the right triangle's plane
static void on_edge(float x, float d, float m, float *px, float *pz, float *yaw)
{
    float al = FLOOR_ARC * (float)M_PI / 180, R = FLOOR_W / sinf(al), z0 = FLOOR_BACK + R;
    float w = sqrtf(R * R - x * x), z = z0 - w, sl = x / w;   // the arc and its slope dz/dx
    float a = atanf(-sl), L = sqrtf(1 + sl * sl);
    float nx = -sl / L, nz = 1 / L;                          // into the floor
    *yaw = a * 180 / (float)M_PI;
    *px = x - m * cosf(a) + nx * (EDGE_IN - d / 2);
    *pz = z + m * sinf(a) + nz * (EDGE_IN - d / 2);
}

static void build_props(void)
{
    prop_n = props = 0;
    float x, z, y;
    // Colours as the cabinet light shows them (a video of a running cabinet): saturated, with one
    // fleck colour each. Every base is on the floor's back edge (on_edge).
    // The two "prisms" are flat triangular cut-outs, lying with their long lower edge flat on the
    // floor edge, point toward the centre: rolled by atan(0.5 / 1.1) = 24.4 degrees; that edge then runs
    // from x = -0.21 to 1.00 along the cut-out, centred at 0.40.
    wedge_tris(1.1f, 1.0f, 0.3f);
    on_edge(-4.5f, 0.3f, 0.40f, &x, &z, &y);
    place(x, z, y, 0, -24.4f, 0.60f, 0.95f, 0.05f, 0xE0281A); // front left: lime triangle, red flecks
    box_tris(1.1f, 1.1f, 0.35f);
    on_edge(-3.5f, 0.35f, 0, &x, &z, &y);
    place(x, z, y, 0, 0, 0.10f, 0.12f, 0.42f, 0x50E040); // back left: blue slab, green flecks
    box_tris(2.0f, 0.6f, 0.45f);
    on_edge(0.0f, 0.45f, 0, &x, &z, &y);
    place(x, z, y, 0, 0, 0.72f, 0.98f, 0.08f, 0xE0381A); // back centre: lime slab, red flecks
    // The red bar where the footage needs it: in C3 E51 (clip155, frame 297) a bolt is fired from it at
    // Marshal Gram; at x = 2.0 the bolt's first frame is on the bar's middle (projected, curve 0-7).
    box_tris(0.55f, 1.25f, 0.35f);
    on_edge(2.0f, 0.35f, 0, &x, &z, &y);
    place(x, z, y, 0, 0, 1.00f, 0.16f, 0.05f, 0xF0D030); // back right: red bar, yellow flecks
    // The disc as in the reference: about 1.06 across, sawn low (0.84 r below the centre), muted lavender.
    // It stands just behind the right triangle, facing the same way (cabinet photos): the triangle's
    // yaw, and DISC_BEHIND further out than the edge.
    float ty;
    on_edge(4.5f, 0.3f, -0.40f, &x, &z, &ty);
    disc_tris(0.53f, 0.3f, 0.45f);
    on_edge(4.0f, 0.3f + 2 * DISC_BEHIND, 0, &x, &z, &y);
    place(x, z, ty, 0, 0, 0.55f, 0.48f, 0.68f, 0xE8D8E8); // right: lavender half-disc, pale flecks
    wedge_tris(1.1f, 1.0f, 0.3f);
    on_edge(4.5f, 0.3f, -0.40f, &x, &z, &y);
    place(x, z, y, 0, 204.4f, 0.05f, 0.72f, 0.12f, 0xE0281A); // front right: green triangle (mirror)
}

static void led_seg(void *u, int x0, int y0, int x1, int y1, int on)
{
    // A segment of the 14x30 glyph box, halved, placed at the character cell's corner `u` (x, y).
    const int *o = u;
    u16 *d = led_tex.data, c = on ? (u16)(9 << 11 | 60 << 5 | 9) : (u16)(5 << 5); // lit green; unlit glows faintly
    x0 = o[0] + x0 / 2;
    x1 = o[0] + x1 / 2;
    y0 = o[1] + y0 / 2;
    y1 = o[1] + y1 / 2;
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        int y = LED_TH - 1 - y0; // texture rows run bottom to top (see the picture upload)
        if (x0 >= 0 && x0 < LED_TW && y >= 0 && y < LED_TH) d[tile_ofs(x0, y, LED_TW)] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}

static void led_update(void) // redraw the panel texture when the line changed
{
    const char *s = led_line();
    if (!strncmp(s, led_shown, 16)) return;
    strncpy(led_shown, s, 16);
    memset(led_tex.data, 0, LED_TW * LED_TH * 2);
    for (int i = 0; i < 16; i++) {
        int at[2] = {i * LED_CW + 1, (LED_TH - 16) / 2};
        led_segments(s[i] ? s[i] : ' ', led_seg, at);
        if (!s[i]) break;
    }
    GSPGPU_FlushDataCache(led_tex.data, LED_TW * LED_TH * 2);
}

// The picture, upright (the camera looks across the stage), in two parts split at the filmed floor
// line: above it the actors, below it their filmed reflection, which is drawn only on the floor. Image
// row r is at v = r / TEX_H (see the upload). Each part is PIC_STRIPS strips following the curve.
static void build_picture(int curve)
{
    float u1 = (float)PIC_W / TEX_W, vt = 0.0f, vb = (float)PIC_H / TEX_H;
    float vf = FLOOR_ROW / TEX_H, yb = PIC_TOP - PH, d = CURVE_DEPTH * curve / VSET_CURVE_MAX;
    for (int i = 0; i < PIC_STRIPS; i++) {
        float s0 = (float)i / PIC_STRIPS, s1 = (float)(i + 1) / PIC_STRIPS;
        float x0 = -PW / 2 + PW * s0, x1 = -PW / 2 + PW * s1;
        float z0 = PIC_Z + d * (2 * s0 - 1) * (2 * s0 - 1), z1 = PIC_Z + d * (2 * s1 - 1) * (2 * s1 - 1);
        quad(vbo + V_PIC + 6 * i, (float[]){x0, 0, z0}, (float[]){x1, 0, z1}, (float[]){x1, PIC_TOP, z1},
             (float[]){x0, PIC_TOP, z0}, u1 * s0, vf, u1 * s1, vt, 1, 1, 1);
        quad(vbo + V_REFL + 6 * i, (float[]){x0, yb, z0}, (float[]){x1, yb, z1}, (float[]){x1, 0, z1},
             (float[]){x0, 0, z0}, u1 * s0, vb, u1 * s1, vf, 1, 1, 1);
    }
    pic_curve = curve;
}

void stage_blank(bool on) { blank = on; }

void stage_video(int opacity, int curve)
{
    pic_cover = opacity / 100.0f;
    if (!ready || curve == pic_curve) return;
    build_picture(curve);
    GSPGPU_FlushDataCache(vbo + V_PIC, sizeof(vtx) * PIC_STRIPS * 12); // V_PIC and V_REFL
}

static void build_geometry(void)
{
    // Back wall and the magenta rim around the floor (cabinet photos).
    quad(vbo + V_WALL, (float[]){-14, 0, WALL_Z}, (float[]){14, 0, WALL_Z}, (float[]){14, 9, WALL_Z},
         (float[]){-14, 9, WALL_Z}, 0, 0, 0, 0, 0.035f, 0.025f, 0.045f); // near black, faint purple (reference video)
    // The rim: magenta, brighter at the front where the cabinet light falls (photos).
    // The magenta surround: the whole stage floor to the back wall, bright at the front and darker
    // behind, where the cabinet light falls off (photos).
    quad(vbo + V_RIM, (float[]){-16, 0, 6}, (float[]){16, 0, 6}, (float[]){16, 0, WALL_Z}, (float[]){-16, 0, WALL_Z}, 0,
         0, 0, 0, 0.85f, 0.05f, 0.22f); // magenta leaning red (tuned on hardware)
    for (int k = 2; k <= 4; k++) {
        vbo[V_RIM + k].c[0] = 0.32f;
        vbo[V_RIM + k].c[1] = 0.02f;
        vbo[V_RIM + k].c[2] = 0.08f;
    }
    // The floor outline: the arc from its right end over the back to its left end, then the left
    // corner, the front edge and the right corner. Each corner turns from the arc's direction to the
    // front edge's, so the outline has no kinks.
    float pts[FLOOR_PTS][2];
    int n = 0;
    float al = FLOOR_ARC * (float)M_PI / 180, R = FLOOR_W / sinf(al), z0 = FLOOR_BACK + R;
    for (int i = 0; i <= ARC_SEG; i++) {
        float a = al - 2 * al * i / ARC_SEG;
        pts[n][0] = R * sinf(a);
        pts[n++][1] = z0 - R * cosf(a);
    }
    float zend = z0 - R * cosf(al), front = 0;
    for (int c = 0; c < 2; c++) { // left corner (mirror), then right corner (outward normal -30 .. 90 deg)
        float sx = c ? 1.0f : -1.0f, n0 = -((float)M_PI / 2 - al);
        float cx = sx * (FLOOR_W - FLOOR_RC * cosf(n0)), cz = zend - FLOOR_RC * sinf(n0);
        front = cz + FLOOR_SIDE + FLOOR_RC;
        // Outward normal n0 .. 0 (arc to side), then 0 .. 90 degrees (side to front) FLOOR_SIDE nearer:
        // the two points at 0 degrees make the straight side.
        float ang[CORNER_SEG1 + CORNER_SEG2 + 2], dz[CORNER_SEG1 + CORNER_SEG2 + 2];
        int m = 0;
        for (int i = 0; i <= CORNER_SEG1; i++, m++) {
            ang[m] = n0 - n0 * i / CORNER_SEG1;
            dz[m] = 0;
        }
        for (int i = 0; i <= CORNER_SEG2; i++, m++) {
            ang[m] = ((float)M_PI / 2) * i / CORNER_SEG2;
            dz[m] = FLOOR_SIDE;
        }
        for (int i = 0; i < m; i++) {
            int k = c ? m - 1 - i : i; // keep the outline in order
            pts[n][0] = cx + sx * FLOOR_RC * cosf(ang[k]);
            pts[n++][1] = cz + dz[k] + FLOOR_RC * sinf(ang[k]);
        }
    }
    float mz = (FLOOR_BACK + front) / 2;
    for (int i = 0; i < FLOOR_TRIS; i++) {
        vtx *v = vbo + V_FLOOR + 3 * i;
        const float *a = pts[i], *b = pts[(i + 1) % FLOOR_PTS];
        float tri[3][3] = {{0, 0, mz}, {a[0], 0, a[1]}, {b[0], 0, b[1]}};
        for (int k = 0; k < 3; k++) {
            memcpy(v[k].p, tri[k], sizeof v[k].p);
            v[k].t[0] = v[k].t[1] = 0;
            v[k].c[0] = 0.01f;
            v[k].c[1] = 0.01f;
            v[k].c[2] = 0.015f;
            v[k].c[3] = 1.0f;
        }
    }
    build_picture(vset()->curve);
    pic_cover = vset()->opacity / 100.0f;
    build_props();
    // The LED line (texture top is v = 0, as for the picture) and its mirror image in the floor.
    float lu = 16.0f * LED_CW / LED_TW;
    quad(vbo + V_LED, (float[]){-LED_HW, LED_Y0, LED_Z}, (float[]){LED_HW, LED_Y0, LED_Z},
         (float[]){LED_HW, LED_Y1, LED_Z}, (float[]){-LED_HW, LED_Y1, LED_Z}, 0, 1, lu, 0, 1, 1, 1);
    quad(vbo + V_LEDR, (float[]){-LED_HW, -LED_Y0, LED_Z}, (float[]){LED_HW, -LED_Y0, LED_Z},
         (float[]){LED_HW, -LED_Y1, LED_Z}, (float[]){-LED_HW, -LED_Y1, LED_Z}, 0, 1, lu, 0, LED_REFL, LED_REFL,
         LED_REFL);
}

bool stage_init(void)
{
    // Tried once: if any step fails the stage stays off (the flat picture is used) rather than being set
    // up again over a half-initialised citro3d.
    static bool tried;
    if (ready || tried) return ready;
    tried = true;
    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) return false;
    for (int e = 0; e < 2; e++) {
        target[e] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        if (!target[e]) return false;
        C3D_RenderTargetSetOutput(target[e], GFX_TOP, e ? GFX_RIGHT : GFX_LEFT, DISPLAY_TRANSFER_FLAGS);
    }
    dvlb = DVLB_ParseFile((u32 *)stage_shbin, stage_shbin_size);
    if (!dvlb) return false;
    shaderProgramInit(&prog);
    shaderProgramSetVsh(&prog, &dvlb->DVLE[0]);
    u_proj = shaderInstanceGetUniformLocation(prog.vertexShader, "projection");
    u_mv = shaderInstanceGetUniformLocation(prog.vertexShader, "modelView");
    vbo = linearAlloc(sizeof(vtx) * V_COUNT);
    if (!vbo) return false;
    memset(vbo, 0, sizeof(vtx) * V_COUNT);
    build_geometry();
    GSPGPU_FlushDataCache(vbo, sizeof(vtx) * V_COUNT);
    for (int i = 0; i < 2; i++) {
        if (!C3D_TexInit(&tex[i], TEX_W, TEX_H, GPU_RGB565)) return false;
        memset(tex[i].data, 0, TEX_W * TEX_H * 2);
        GSPGPU_FlushDataCache(tex[i].data, TEX_W * TEX_H * 2);
        C3D_TexSetFilter(&tex[i], GPU_LINEAR, GPU_LINEAR);
        C3D_TexSetWrap(&tex[i], GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    }
    // The paint spatter on the props (photos 19, 21): sparse small flecks of a few colours on a clear
    // ground. Alpha marks a fleck; the colour comes from the prop. A fixed seed keeps it the same every run.
    if (!C3D_TexInit(&speck, SPECK_W, SPECK_W, GPU_RGBA8)) return false;
    memset(speck.data, 0, SPECK_W * SPECK_W * 4);
    u32 seed = 0x1991u, *sp = speck.data;
    for (int i = 0; i < 260; i++) {
        seed = seed * 1103515245u + 12345u;
        int fx = (int)(seed >> 9) % SPECK_W;
        seed = seed * 1103515245u + 12345u;
        int fy = (int)(seed >> 9) % SPECK_W;
        seed = seed * 1103515245u + 12345u;
        u32 col = 0xFFFFFFFFu; // a fleck; its colour is the prop's
        int sz = (seed >> 20) % 4 == 0 ? 2 : 1; // a few larger drops
        for (int dy = 0; dy < sz; dy++)
            for (int dx = 0; dx < sz; dx++) {
                int x = (fx + dx) % SPECK_W, y = (fy + dy) % SPECK_W;
                sp[tile_ofs(x, y, SPECK_W)] = col;
            }
    }
    GSPGPU_FlushDataCache(speck.data, SPECK_W * SPECK_W * 4);
    C3D_TexSetFilter(&speck, GPU_LINEAR, GPU_NEAREST);
    C3D_TexSetWrap(&speck, GPU_REPEAT, GPU_REPEAT);
    for (int x = 0; x < PIC_W; x++) // the picture upload adds a column part and a row part (tile_ofs)
        ox[x] = tile_ofs(x, 0, TEX_W);
    for (int r = 0; r < PIC_H; r++) {
        // Image row r goes in tile row TEX_H - 1 - r, and tile row k samples at v = 1 - (k + 0.5) / TEX_H,
        // so image row r is at v = (r + 0.5) / TEX_H: the image top is v = 0. (Taking the top as v = 1
        // showed the picture upside down on the New 3DS and in Azahar alike, 2026-10-11.)
        int y = TEX_H - 1 - r;
        oy[r] = tile_ofs(0, y, TEX_W);
    }
    if (!C3D_TexInit(&led_tex, LED_TW, LED_TH, GPU_RGB565)) return false;
    C3D_TexSetFilter(&led_tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&led_tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    led_shown[0] = '?'; // force the first draw
    ready = true;
    return true;
}

bool stage_on(void) { return ready && enabled; }
void stage_toggle(void) { enabled = !enabled; }

void stage_picture(const uint16_t *src, int stride, int x0)
{
    if (!ready) return;
    if (!src) {
        has_pic = false;
        return;
    }
    // The other texture: the GPU may still be drawing the previous frame from tex[cur].
    int n = cur ^ 1;
    u16 *d = tex[n].data;
    for (int r = 0; r < PIC_H; r++) {
        const u16 *row = src + r * stride + x0;
        u16 *base = d + oy[r];
        for (int x = 0; x < PIC_W; x++) {
            u16 v = row[x]; // MVD output: red in the low 5 bits (video.c); GPU_RGB565 wants it high
            base[ox[x]] = (u16)((v >> 11) | (v & 0x07E0) | (v << 11));
        }
    }
    GSPGPU_FlushDataCache(d, TEX_W * TEX_H * 2);
    cur = n;
    has_pic = true;
}

static void env_color(void)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
}

static void env_texture(void)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
}

// The picture: colour as env_texture; alpha = how much of the scene behind it the picture covers (see
// pic_cover and the blend in draw_scene): stage 0 takes the green x4 (clamped to 1), stage 1 scales it
// by the opacity.
static void env_picture(void)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
    C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_G, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    C3D_TexEnvScale(env, C3D_Alpha, GPU_TEVSCALE_4);
    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_PREVIOUS, 0, 0);
    C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_CONSTANT, 0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
    C3D_TexEnvColor(env, (u32)(pic_cover * 255.0f + 0.5f) << 24);
}

static void env_dim(float k) // vertex colour scaled by k
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_CONSTANT, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    u32 c = (u32)(k * 255.0f + 0.5f);
    C3D_TexEnvColor(env, 0xFF000000u | c << 16 | c << 8 | c);
}

// Props: the fleck colour over the shaded paint where the spatter texture has a fleck, then scaled by k
// (1 for the props, REFL_K for their reflection).
static void env_props(float k, u32 fleck)
{
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_PRIMARY_COLOR, GPU_TEXTURE0);
    C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
    // TEV constants are 0xAABBGGRR
    C3D_TexEnvColor(env, 0xFF000000u | (fleck & 0xFF) << 16 | (fleck & 0xFF00) | (fleck >> 16 & 0xFF));
    C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvSrc(env, C3D_Alpha, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    env = C3D_GetTexEnv(1);
    C3D_TexEnvInit(env);
    C3D_TexEnvSrc(env, C3D_Both, GPU_PREVIOUS, GPU_CONSTANT, 0);
    C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
    u32 c = (u32)(k * 255.0f + 0.5f);
    C3D_TexEnvColor(env, 0xFF000000u | c << 16 | c << 8 | c);
}

static void env_reset1(void) { C3D_TexEnvInit(C3D_GetTexEnv(1)); } // stage 1 passes the previous through

static void opaque(void) { C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO); }

static void draw_scene(float iod, bool bar)
{
    C3D_Mtx proj, mv;
    Mtx_PerspStereoTilt(&proj, C3D_AngleFromDegrees(STAGE_FOV), C3D_AspectRatioTop, 0.5f, 60.0f, iod,
                        sqrtf((CAM_Z - PIC_Z) * (CAM_Z - PIC_Z) + CAM_Y * CAM_Y), false); // image at screen depth
    C3D_Mtx shift; // clip-space translation: the tilted top screen's vertical is clip x
    Mtx_Identity(&shift);
    shift.r[0].w = VIEW_SHIFT;
    Mtx_Multiply(&proj, &shift, &proj);
    Mtx_LookAt(&mv, FVec3_New(0, CAM_Y, CAM_Z), FVec3_New(0, LOOK_Y, 0), FVec3_New(0, 1, 0), false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &mv);

    // Drawn back to front (nothing intersects yet), so the depth test always passes; GPU_WRITE_ALL keeps
    // the depth/stencil buffer writable for the floor's stencil mark.
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0x00);
    C3D_CullFace(GPU_CULL_NONE);
    env_color();
    opaque();
    C3D_DrawArrays(GPU_TRIANGLES, V_WALL, 6);
    env_dim(RIM_DIM + (1 - RIM_DIM) * lit);
    C3D_DrawArrays(GPU_TRIANGLES, V_RIM, 6);
    env_color();
    C3D_StencilTest(true, GPU_ALWAYS, 1, 0xFF, 0xFF); // the floor marks the stencil
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
    C3D_DrawArrays(GPU_TRIANGLES, V_FLOOR, FLOOR_TRIS * 3);
    // The props' reflection, only on the glossy floor. It lies under the floor, so it cannot be depth
    // tested against it; each prop is convex and they do not overlap, so culling the faces turned away
    // (mirroring reverses the winding) is enough.
    C3D_StencilTest(true, GPU_EQUAL, 1, 0xFF, 0x00);
    C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
    // Back to front, as nothing here is depth tested: the LED line, then the props.
    C3D_CullFace(GPU_CULL_FRONT_CCW);
    for (int i = 0; i < PROPS; i++) {
        if (i == 0) {
            C3D_CullFace(GPU_CULL_NONE);
            C3D_TexBind(0, &led_tex);
            env_reset1();
            env_texture();
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
            C3D_DrawArrays(GPU_TRIANGLES, V_LEDR, 6);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
            C3D_CullFace(GPU_CULL_FRONT_CCW);
        }
        C3D_TexBind(0, &speck);
        env_props(REFL_K * (PROP_DIM + (1 - PROP_DIM) * lit), prop_fleck[i]);
        C3D_DrawArrays(GPU_TRIANGLES, V_PREFL + prop_first[i], prop_cnt[i]);
    }
    // The props are the only solids that hide each other: depth tested (cleared to 0, nearer is greater).
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0x00);
    C3D_CullFace(GPU_CULL_BACK_CCW);
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_ALL);
    for (int i = 0; i < PROPS; i++) {
        env_props(PROP_DIM + (1 - PROP_DIM) * lit, prop_fleck[i]);
        C3D_DrawArrays(GPU_TRIANGLES, V_PROPS + prop_first[i], prop_cnt[i]);
    }
    C3D_CullFace(GPU_CULL_NONE);
    env_reset1();
    // The LED line, added onto the wall; the props in front hide them.
    C3D_DepthTest(true, GPU_GREATER, GPU_WRITE_COLOR);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ONE, GPU_ONE);
    C3D_TexBind(0, &led_tex);
    env_texture();
    C3D_DrawArrays(GPU_TRIANGLES, V_LED, 6);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    C3D_DepthTest(true, GPU_ALWAYS, GPU_WRITE_ALL);
    if (has_pic && !blank) {
        C3D_TexBind(0, &tex[cur]);
        env_picture();
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE);
        C3D_StencilTest(true, GPU_EQUAL, 1, 0xFF, 0x00); // the filmed reflection only on the glossy floor
        C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
        C3D_DrawArrays(GPU_TRIANGLES, V_REFL, PIC_STRIPS * 6);
        C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0x00);
        C3D_DrawArrays(GPU_TRIANGLES, V_PIC, PIC_STRIPS * 6);
        env_reset1();
    }
    C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0x00);
    if (bar) { // screen-space overlay, same place in both eyes
        Mtx_OrthoTilt(&proj, 0, 400, 240, 0, -1, 1, true);
        Mtx_Identity(&mv);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_proj, &proj);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, u_mv, &mv);
        env_color();
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ZERO);
        C3D_DrawArrays(GPU_TRIANGLES, V_BAR, 6);
        opaque();
    }
}

void stage_render(float bar)
{
    if (!ready) return;
    float iod = osGet3DSliderState() * IOD_MAX;
    led_update();
    float want = has_pic ? 0.0f : 1.0f;
    lit = lit < want ? fminf(lit + RIM_STEP, want) : fmaxf(lit - RIM_STEP, want);
    C3D_FrameBegin(0); // waits for the GPU; pacing is the caller's (wait_frame)
    C3D_BindProgram(&prog);
    C3D_AttrInfo *ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(ai, 2, GPU_FLOAT, 4);
    C3D_BufInfo *bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, vbo, sizeof(vtx), 3, 0x210);
    if (bar >= 0) { // the move-window bar, as the flat screen draws it (fe_screen.c draw_top)
        if (bar > 1) bar = 1;
        float w = 200 * bar;
        quad(vbo + V_BAR, (float[]){100, 232, 0}, (float[]){100 + w, 232, 0}, (float[]){100 + w, 228, 0},
             (float[]){100, 228, 0}, 0, 0, 0, 0, 0.94f, 0.88f, 0.13f);
        for (int i = 0; i < 6; i++) vbo[V_BAR + i].c[3] = BAR_ALPHA;
        GSPGPU_FlushDataCache(vbo + V_BAR, sizeof(vtx) * 6);
    }
    for (int e = 0; e < 2; e++) {
        if (e == 1 && iod <= 0) break; // 3D off: the left image is the only one shown
        C3D_RenderTargetClear(target[e], C3D_CLEAR_ALL, 0x000000FF, 0);
        C3D_FrameDrawOn(target[e]);
        draw_scene(e ? iod : -iod, bar >= 0);
    }
    C3D_FrameEnd(0);
}
