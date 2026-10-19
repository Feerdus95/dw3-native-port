/* The GPU, in software: GP0 packets (what the game's primitives already are)
   rasterised into the 1024x512 16-bit VRAM, as the PSX GPU draws them; the
   same primitives go to the graphics card (gl.c), which draws what the
   screen shows, finer. Reference: the PSX GPU as documented in psx-spx
   (nocash). */
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <SDL3/SDL_stdinc.h>
#include "platform.h"

/* Drawing state (GP0 E1-E6) */
typedef struct {
    int tx, ty;        /* texture page base, in pixels */
    int semi;          /* semi-transparency mode 0-3 */
    int depth;         /* 0: 4 bpp, 1: 8 bpp, 2: 15 bpp */
    int dither;
    int tw_mask_x, tw_mask_y, tw_off_x, tw_off_y; /* texture window, in pixels */
    int x1, y1, x2, y2; /* drawing area, inclusive */
    int ox, oy;        /* drawing offset */
    int set_mask, check_mask;
} DrawState;
SAVED static DrawState g;

/* --- pixels --- */

static int blend(int back, int front, int mode) {
    int r, gg, b;
    int br = back & 31, bg = back >> 5 & 31, bb = back >> 10 & 31;
    int fr = front & 31, fg = front >> 5 & 31, fb = front >> 10 & 31;

    switch (mode) {
    case 0: r = (br + fr) >> 1; gg = (bg + fg) >> 1; b = (bb + fb) >> 1; break;
    case 1: r = br + fr; gg = bg + fg; b = bb + fb; break;
    case 2: r = br - fr; gg = bg - fg; b = bb - fb; break;
    default: r = br + (fr >> 2); gg = bg + (fg >> 2); b = bb + (fb >> 2); break;
    }
    r = r < 0 ? 0 : r > 31 ? 31 : r;
    gg = gg < 0 ? 0 : gg > 31 ? 31 : gg;
    b = b < 0 ? 0 : b > 31 ? 31 : b;
    return r | gg << 5 | b << 10;
}

/* Writes a pixel; semi: whether this one blends (bit 15 of the texel, or an untextured semi primitive) */
static void plot(int x, int y, int c, int semi) {
    uint16_t *p;

    if (x < g.x1 || x > g.x2 || y < g.y1 || y > g.y2) {
        return;
    }
    p = &g_vram[(y & 511) * VRAM_W + (x & 1023)];
    g_vram_dirty[(y & 511) >> 6] |= (uint16_t)(1u << ((x & 1023) >> 6));
    if (g.check_mask && (*p & 0x8000)) {
        return;
    }
    if (semi) {
        c = blend(*p, c, g.semi) | (c & 0x8000);
    }
    *p = (uint16_t)(c | (g.set_mask ? 0x8000 : 0));
}

/* The texel at (u, v) of the current page with the given CLUT; 0 = transparent */
static int texel(int u, int v, int clut) {
    int cx = (clut & 0x3F) << 4, cy = clut >> 6 & 0x1FF;
    uint16_t w;

    u &= 255;
    v &= 255;
    u = (u & ~g.tw_mask_x) | (g.tw_off_x & g.tw_mask_x);
    v = (v & ~g.tw_mask_y) | (g.tw_off_y & g.tw_mask_y);
    switch (g.depth) {
    case 0:
        w = g_vram[((g.ty + v) & 511) * VRAM_W + ((g.tx + u / 4) & 1023)];
        return g_vram[cy * VRAM_W + ((cx + (w >> (u % 4 * 4) & 15)) & 1023)];
    case 1:
        w = g_vram[((g.ty + v) & 511) * VRAM_W + ((g.tx + u / 2) & 1023)];
        return g_vram[cy * VRAM_W + ((cx + (w >> (u % 2 * 8) & 255)) & 1023)];
    default:
        return g_vram[((g.ty + v) & 511) * VRAM_W + ((g.tx + u) & 1023)];
    }
}

/* The GPU's 4x4 dither, added to 8-bit colors before they drop to 5 bits */
static const int8_t DITHER[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
int g_dither_on = 1; /* native.ini: dither = 0 turns it off */

static int to5(int v, int d) {
    v += d;
    return v < 0 ? 0 : v > 255 ? 31 : v >> 3;
}

/* Texel times vertex color (128 = 1.0) at 8 bits, dithered by d, saturated */
static int modulate(int t, int r, int gg, int b, int d) {
    return to5((t & 31) * r >> 4, d) | to5((t >> 5 & 31) * gg >> 4, d) << 5 | to5((t >> 10 & 31) * b >> 4, d) << 10 | (t & 0x8000);
}

static int rgb15(int r, int gg, int b, int d) {
    return to5(r, d) | to5(gg, d) << 5 | to5(b, d) << 10;
}

/* --- state --- */

static void set_tpage(uint32_t w) {
    g.tx = (w & 15) * 64;
    g.ty = (w >> 4 & 1) * 256;
    g.semi = w >> 5 & 3;
    g.depth = w >> 7 & 3;
    if (g.depth == 3) {
        g.depth = 2;
    }
}

static void gp0_env(uint32_t w) {
    switch (w >> 24) {
    case 0xE1: set_tpage(w); g.dither = w >> 9 & 1; break;
    case 0xE2:
        g.tw_mask_x = (w & 31) * 8;
        g.tw_mask_y = (w >> 5 & 31) * 8;
        g.tw_off_x = (w >> 10 & 31) * 8;
        g.tw_off_y = (w >> 15 & 31) * 8;
        break;
    case 0xE3: g.x1 = w & 1023; g.y1 = w >> 10 & 511; break;
    case 0xE4: g.x2 = w & 1023; g.y2 = w >> 10 & 511; break;
    case 0xE5:
        g.ox = (int)(w << 21) >> 21;
        g.oy = (int)(w << 10) >> 21;
        break;
    case 0xE6: g.set_mask = w & 1; g.check_mask = w >> 1 & 1; break;
    }
}

/* --- triangles --- */

static int sx(uint32_t w) { return ((int)(w << 21) >> 21); }
static int sy(uint32_t w) { return ((int)(w << 5) >> 21); }

typedef struct {
    int x, y, r, g, b, u, v;
} Vtx;

static int64_t edge(const Vtx *a, const Vtx *b, int x, int y) {
    return (int64_t)(b->x - a->x) * (y - a->y) - (int64_t)(b->y - a->y) * (x - a->x);
}

/* A triangle: flags bit 0 gouraud, 1 textured, 2 raw texture, 3 semi */
static void triangle(Vtx a, Vtx b, Vtx c, int flags, int clut) {
    int minx, maxx, miny, maxy, x, y;
    int64_t area = edge(&a, &b, c.x, c.y);
    Vtx t;

    if (area == 0) {
        return;
    }
    if (area < 0) { /* make it counter-clockwise in this orientation */
        t = b; b = c; c = t;
        area = -area;
    }
    minx = a.x < b.x ? a.x : b.x; if (c.x < minx) minx = c.x;
    maxx = a.x > b.x ? a.x : b.x; if (c.x > maxx) maxx = c.x;
    miny = a.y < b.y ? a.y : b.y; if (c.y < miny) miny = c.y;
    maxy = a.y > b.y ? a.y : b.y; if (c.y > maxy) maxy = c.y;
    if (maxx - minx >= 1024 || maxy - miny >= 512) {
        return; /* the GPU drops polygons this large */
    }
    if (minx < g.x1) minx = g.x1;
    if (miny < g.y1) miny = g.y1;
    if (maxx > g.x2 + 1) maxx = g.x2 + 1;
    if (maxy > g.y2 + 1) maxy = g.y2 + 1;
    for (y = miny; y < maxy; y++) {
        for (x = minx; x < maxx; x++) {
            /* pixel centres, top-left rule: right and bottom edges are left out */
            int64_t w0 = edge(&b, &c, x, y), w1 = edge(&c, &a, x, y), w2 = edge(&a, &b, x, y);
            int r, gg, bb, col, semi, d;

            if (w0 < 0 || w1 < 0 || w2 < 0) {
                continue;
            }
            if ((w0 == 0 && !((c.y < b.y) || (c.y == b.y && c.x > b.x))) ||
                (w1 == 0 && !((a.y < c.y) || (a.y == c.y && a.x > c.x))) ||
                (w2 == 0 && !((b.y < a.y) || (b.y == a.y && b.x > a.x)))) {
                continue;
            }
            if (flags & 1) {
                r = (int)((w0 * a.r + w1 * b.r + w2 * c.r) / area);
                gg = (int)((w0 * a.g + w1 * b.g + w2 * c.g) / area);
                bb = (int)((w0 * a.b + w1 * b.b + w2 * c.b) / area);
            } else {
                r = a.r; gg = a.g; bb = a.b;
            }
            /* dithered: shaded or texture-blended polygons, while E1 asks for it */
            d = g.dither && g_dither_on && (flags & 1 || (flags & 6) == 2) ? DITHER[y & 3][x & 3] : 0;
            semi = flags & 8;
            if (flags & 2) {
                int u = (int)((w0 * a.u + w1 * b.u + w2 * c.u) / area);
                int v = (int)((w0 * a.v + w1 * b.v + w2 * c.v) / area);
                int tex = texel(u, v, clut);

                if (tex == 0) {
                    continue;
                }
                semi = semi && (tex & 0x8000);
                col = (flags & 4) ? tex : modulate(tex, r, gg, bb, d);
            } else {
                col = rgb15(r, gg, bb, d);
            }
            plot(x, y, col, semi);
        }
    }
}

/* --- the card's copy --- */

static int too_big(const Vtx *a, const Vtx *b, const Vtx *c) {
    int minx = a->x < b->x ? a->x : b->x, maxx = a->x > b->x ? a->x : b->x;
    int miny = a->y < b->y ? a->y : b->y, maxy = a->y > b->y ? a->y : b->y;

    if (c->x < minx) minx = c->x;
    if (c->x > maxx) maxx = c->x;
    if (c->y < miny) miny = c->y;
    if (c->y > maxy) maxy = c->y;
    return maxx - minx >= 1024 || maxy - miny >= 512;
}

/* Widescreen battles' menus (DrawOTag): the x the card draws a packet at,
   x' = a + (x - a) * s, keeping a menu's shape on the 16:9 picture */
#define WIDE_PARTNER_LAYER 0x1009 /* fightstg/partner_view.c's, the partner's picture */
#define WIDE_MENUS_LAYER 0x1005 /* fightstg/types.h's BATTLE_LAYER_MENUS: the HUD, the menus, the messages */
static float s_map_a, s_map_s = 1, s_map_d; /* x to a + (x - a) * s + d on the card (widescreen) */
static float s_clip_a, s_clip_s = 1;          /* and the drawing area's x to a + (x - a) * s */
int g_draw_layer;

/* The drawing area for the card, as s_clip_* place it */
#define CLIPX(x) ((int)(s_clip_a + ((x) - s_clip_a) * s_clip_s + 0.5f))
#define CLIP CLIPX(g.x1), g.y1, CLIPX(g.x2), g.y2

void native_wide_side(int edge) {
    if (gte_widescreen()) {
        s_map_a = (float)edge, s_map_s = 0.75f; /* the frame buffers start at x 0 */
    }
}

/* A vertex for the card with the drawing state; flags as triangle()'s */
static GVtx gvtx(float x, float y, int r, int gg, int b, int u, int v, int clut, int flags, int dither) {
    GVtx o;

    o.x = s_map_a + (x - s_map_a) * s_map_s + s_map_d;
    o.y = y;
    o.u = (float)u;
    o.v = (float)v;
    o.r = (uint8_t)r;
    o.g = (uint8_t)gg;
    o.b = (uint8_t)b;
    o.a = 0;
    o.clut_x = (uint16_t)((clut & 0x3F) << 4);
    o.clut_y = (uint16_t)(clut >> 6 & 0x1FF);
    o.tx = (uint16_t)g.tx;
    o.ty = (uint16_t)g.ty;
    o.umin = o.vmin = 0;
    o.umax = o.vmax = 255;
    o.twmx = (uint16_t)g.tw_mask_x;
    o.twmy = (uint16_t)g.tw_mask_y;
    o.twox = (uint16_t)g.tw_off_x;
    o.twoy = (uint16_t)g.tw_off_y;
    o.z = 0;
    o.w = 0;
    o.mode = (uint32_t)((flags & 2) | (flags & 6) | (flags & 8) | (flags & 512) | (flags & 1024) | (flags & 2048) | g.depth << 4 | (dither ? 64 : 0) | (g.set_mask ? 256 : 0));
    return o;
}

/* Three vertices, their texture coordinates kept between theirs (the card
   samples finer: past the edge it would reach the next texel) */
static void gl_tri(GVtx a, GVtx b, GVtx c) {
    GVtx t[3] = {a, b, c};
    int i, umin = 255, vmin = 255, umax = 0, vmax = 0;

    for (i = 0; i < 3; i++) {
        umin = t[i].u < umin ? (int)t[i].u : umin;
        vmin = t[i].v < vmin ? (int)t[i].v : vmin;
        umax = t[i].u > umax ? (int)t[i].u : umax;
        vmax = t[i].v > vmax ? (int)t[i].v : vmax;
    }
    for (i = 0; i < 3; i++) {
        t[i].umin = (uint16_t)umin, t[i].vmin = (uint16_t)vmin, t[i].umax = (uint16_t)umax, t[i].vmax = (uint16_t)vmax;
    }
    gl_triangles(t, 3, CLIP, g.semi);
}

/* A rectangle for the card: the texels from (u0, v0), one per VRAM pixel */
static void gl_rect(int x, int y, int w, int h, int r, int gg, int b, int flags, int u0, int v0, int clut) {
    GVtx q[6];
    int i;

    for (i = 0; i < 6; i++) {
        static const int CX[6] = {0, 1, 0, 1, 1, 0}, CY[6] = {0, 0, 1, 0, 1, 1};

        q[i] = gvtx((float)(x + CX[i] * w), (float)(y + CY[i] * h), r, gg, b, u0 + CX[i] * w, v0 + CY[i] * h, clut, flags, 0);
        q[i].umin = (uint16_t)u0, q[i].vmin = (uint16_t)v0;
        q[i].umax = (uint16_t)(u0 + w - 1), q[i].vmax = (uint16_t)(v0 + h - 1);
    }
    gl_triangles(q, 6, CLIP, flags & 512 ? 4 : g.semi); /* a soft shadow darkens by multiplying */
}

/* A vertex word where the card puts it: with the GTE's fraction, when it
   made that word; s_far: how many were a stage's painted horizon's
   (gte_precise_far) */
static int s_far;

static void precise(const uint32_t *w, float *x, float *y, float *z) {
    /* a battle's menus are 2D: a point there that matched a 3D one by its
       value took that one's fraction, and the enemy's HP bar wobbled */
    if ((gte_in_battle() && g_draw_layer == WIDE_MENUS_LAYER) || !gte_precise(w, x, y, z)) {
        *z = 0;
        *x = (float)sx(*w);
        *y = (float)sy(*w);
    }
    if (*z < 0) {
        *z = -*z;
        s_far++;
    }
    *x += (float)g.ox;
    *y += (float)g.oy;
}

/* --- rectangles and lines --- */

/* A rectangle into the 1x VRAM */
static void rect_px(int x, int y, int w, int h, int col, int textured, int raw, int semi, int u0, int v0, int clut, int soft) {
    int i, j;
    int r = col & 255, gg = col >> 8 & 255, b = col >> 16 & 255;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int c, s = semi;

            if (soft && textured) { /* the card's ellipse (gl.c), for the copy a battle's transition shows */
                float dx = (i + 0.5f - w * 0.484f) / (w * 0.42f), dy = (j + 0.5f - h * 0.469f) / (h * 0.4f);
                float d = sqrtf(dx * dx + dy * dy), k = (d - 0.6f) / 0.4f;
                int tex = texel(u0 + (int)(w * 0.484f), v0 + (int)(h * 0.469f), clut);

                if (d >= 1.0f || tex == 0) {
                    continue;
                }
                k = k <= 0 ? 1.0f : 1.0f - k * k * (3.0f - 2.0f * k);
                c = raw ? tex : modulate(tex, r, gg, b, 0);
                c = ((int)((c & 31) * k + 0.5f)) | ((int)((c >> 5 & 31) * k + 0.5f)) << 5 | ((int)((c >> 10 & 31) * k + 0.5f)) << 10;
                plot(x + i, y + j, c, 1);
                continue;
            }
            if (textured) {
                int tex = texel(u0 + i, v0 + j, clut);

                if (tex == 0) {
                    continue;
                }
                s = semi && (tex & 0x8000);
                c = raw ? tex : modulate(tex, r, gg, b, 0);
            } else {
                c = rgb15(r, gg, b, 0);
            }
            plot(x + i, y + j, c, s);
        }
    }
}

/* A line's pixels: into the 1x VRAM (px), or to the card */
static void line_walk(int x0, int y0, int c0, int x1, int y1, int c1, int gouraud, int semi, int px) {
    int dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    int i;

    if (n >= 1024) {
        return;
    }
    for (i = 0; i <= n; i++) {
        int x = n ? x0 + dx * i / n : x0, y = n ? y0 + dy * i / n : y0;
        int c = c0, d = gouraud && g.dither && g_dither_on ? DITHER[y & 3][x & 3] : 0;

        if (gouraud && n) {
            int r = (c0 & 255) + ((c1 & 255) - (c0 & 255)) * i / n;
            int gg = (c0 >> 8 & 255) + ((c1 >> 8 & 255) - (c0 >> 8 & 255)) * i / n;
            int b = (c0 >> 16 & 255) + ((c1 >> 16 & 255) - (c0 >> 16 & 255)) * i / n;
            c = r | gg << 8 | b << 16;
        }
        if (px) {
            plot(x, y, rgb15(c & 255, c >> 8 & 255, c >> 16 & 255, d), semi);
        } else {
            gl_rect(x, y, 1, 1, c & 255, c >> 8 & 255, c >> 16 & 255, semi ? 8 : 0, 0, 0, 0);
        }
    }
}

/* --- the 1x VRAM, drawn when read ---
   What the primitives draw into g_vram is only read back when the game
   samples it as a texture, moves it (MoveImage), or a state is saved: most
   of a frame's drawing is covered by the next one's background first. So the
   primitives are queued with their drawing state and drawn into g_vram, in
   order, only when something reads where they draw; those an opaque fill or
   rectangle covers before that are dropped. The 64x64 blocks they write and
   read are kept as masks, so a check is O(1) unless it touches them. */
enum { SOFT_TRI, SOFT_RECT, SOFT_LINE, SOFT_COPY };
typedef struct {
    int kind;
    int box[4];     /* what it writes, inclusive, in the drawing area */
    int read[2][4]; /* the texture page and CLUT it reads; x1 > x2: none */
    DrawState g;
    union {
        struct { Vtx a, b, c; int flags, clut; } tri;
        struct { int x, y, w, h, col, textured, raw, semi, u0, v0, clut, soft; } rect;
        struct { int x0, y0, c0, x1, y1, c1, gouraud, semi; } line;
        struct { int sx, sy, dx, dy, w, h; } copy;
    } u;
} Soft;

/* A VRAM to VRAM copy (GP0 80), wrapping at the edges */
static void copy_px(int sx0, int sy0, int dx, int dy, int w, int h) {
    static uint16_t *tmp;
    int i, j;

    if (tmp == NULL && (tmp = malloc(VRAM_W * VRAM_H * 2)) == NULL) {
        return;
    }
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            tmp[j * w + i] = g_vram[((sy0 + j) & 511) * VRAM_W + ((sx0 + i) & 1023)];
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            g_vram[((dy + j) & 511) * VRAM_W + ((dx + i) & 1023)] = tmp[j * w + i];
    gl_vram_dirty(dx, dy, w, h);
}

#define SOFT_MAX 16384 /* about four frames; when full, drawn */
static Soft *s_soft;
static int s_nsoft;
/* Where the queue writes or reads: 64x64 blocks, and 8x8 cells for when a
   block is shared (the CLUTs under the frame buffers) */
typedef struct {
    uint16_t block[8];
    uint8_t cell[64][16];
} Cells;
static Cells s_soft_w, s_soft_r;

static void mark(Cells *m, int x1, int y1, int x2, int y2) {
    int r, c;

    x1 = SDL_max(x1, 0), y1 = SDL_max(y1, 0), x2 = SDL_min(x2, VRAM_W - 1), y2 = SDL_min(y2, VRAM_H - 1);
    for (r = y1 >> 6; r <= y2 >> 6; r++) {
        for (c = x1 >> 6; c <= x2 >> 6; c++) {
            m->block[r] |= (uint16_t)(1u << c);
        }
    }
    for (r = y1 >> 3; r <= y2 >> 3; r++) {
        for (c = x1 >> 3; c <= x2 >> 3; c++) {
            m->cell[r][c >> 3] |= (uint8_t)(1u << (c & 7));
        }
    }
}

static int meets(const Cells *m, int x1, int y1, int x2, int y2) {
    int r, c, hit = 0;

    x1 = SDL_max(x1, 0), y1 = SDL_max(y1, 0), x2 = SDL_min(x2, VRAM_W - 1), y2 = SDL_min(y2, VRAM_H - 1);
    for (r = y1 >> 6; r <= y2 >> 6 && !hit; r++) {
        for (c = x1 >> 6; c <= x2 >> 6 && !hit; c++) {
            hit = m->block[r] >> c & 1;
        }
    }
    for (r = y1 >> 3; r <= y2 >> 3 && hit; r++) {
        for (c = x1 >> 3; c <= x2 >> 3; c++) {
            if (m->cell[r][c >> 3] >> (c & 7) & 1) {
                return 1;
            }
        }
    }
    return 0;
}

static void soft_mark(const Soft *e) {
    int k;

    mark(&s_soft_w, e->box[0], e->box[1], e->box[2], e->box[3]);
    for (k = 0; k < 2; k++) {
        if (e->read[k][0] <= e->read[k][2]) {
            mark(&s_soft_r, e->read[k][0], e->read[k][1], e->read[k][2], e->read[k][3]);
        }
    }
}

static void soft_masks(void) {
    int i;

    SDL_memset(&s_soft_w, 0, sizeof s_soft_w);
    SDL_memset(&s_soft_r, 0, sizeof s_soft_r);
    for (i = 0; i < s_nsoft; i++) {
        soft_mark(&s_soft[i]);
    }
}

/* Why the queue is being drawn, for the meter's csv (perf_note) */
static const char *s_why = "?";
static int s_why_r[4];

static void why(const char *what, int x, int y, int w, int h) {
    s_why = what, s_why_r[0] = x, s_why_r[1] = y, s_why_r[2] = w, s_why_r[3] = h;
}

/* One queued primitive into g_vram, with its drawing state (left in g) */
static void soft_run(const Soft *e) {
    g = e->g;
    if (e->kind == SOFT_TRI) {
        triangle(e->u.tri.a, e->u.tri.b, e->u.tri.c, e->u.tri.flags, e->u.tri.clut);
    } else if (e->kind == SOFT_RECT) {
        rect_px(e->u.rect.x, e->u.rect.y, e->u.rect.w, e->u.rect.h, e->u.rect.col, e->u.rect.textured, e->u.rect.raw,
                e->u.rect.semi, e->u.rect.u0, e->u.rect.v0, e->u.rect.clut, e->u.rect.soft);
    } else if (e->kind == SOFT_LINE) {
        line_walk(e->u.line.x0, e->u.line.y0, e->u.line.c0, e->u.line.x1, e->u.line.y1, e->u.line.c1, e->u.line.gouraud,
                  e->u.line.semi, 1);
    } else {
        copy_px(e->u.copy.sx, e->u.copy.sy, e->u.copy.dx, e->u.copy.dy, e->u.copy.w, e->u.copy.h);
    }
}

/* Everything queued, into g_vram */
void soft_all(void) {
    DrawState saved = g;
    int i;

    if (s_nsoft == 0) {
        return;
    }
    if (perf_on() && s_nsoft >= 64) {
        perf_note("soft: %d drawn for %s %d,%d %dx%d", s_nsoft, s_why, s_why_r[0], s_why_r[1], s_why_r[2], s_why_r[3]);
    }
    s_why = "?";
    perf_begin(PF_SOFT);
    for (i = 0; i < s_nsoft; i++) {
        soft_run(&s_soft[i]);
    }
    perf_gpu(PF_SOFT_N, s_nsoft);
    perf_end(PF_SOFT);
    g = saved;
    soft_discard();
}

/* A loaded state replaced g_vram: what was queued is gone with the old one */
void soft_discard(void) {
    s_nsoft = 0;
    SDL_memset(&s_soft_w, 0, sizeof s_soft_w);
    SDL_memset(&s_soft_r, 0, sizeof s_soft_r);
}

/* What soft_resolve is for: x1-y2 is about to be read; written from outside
   the queue (a fill, LoadImage, MoveImage); or covered by an opaque primitive
   queued next */
enum { RESOLVE_READ, RESOLVE_WRITE, RESOLVE_COVER };

static int rects_meet(const int *a, int x1, int y1, int x2, int y2) {
    return a[0] <= a[2] && a[0] <= x2 && a[2] >= x1 && a[1] <= y2 && a[3] >= y1;
}

/* Only what x1-y2 depends on: the queued primitives it needs drawn (a read:
   those drawing there; a write: those reading there or drawing partly
   there) and, back through the queue, all they depend on (earlier ones
   drawing where they read or draw) or that depend on them going first
   (earlier ones reading where they draw), are drawn now, in order; for a
   cover they are only kept. Then a write or cover drops the rest that draws
   only inside it. What is left stays queued, in order. One pass, backwards. */
static void soft_resolve(int x1, int y1, int x2, int y2, int mode) {
    static Cells uw, urw; /* where the needed ones draw; draw or read */
    static uint8_t *need;
    int i, k, n = 0, drawn = 0, dropped = 0;
    DrawState saved = g;

    if (need == NULL && (need = SDL_malloc(SOFT_MAX)) == NULL) {
        soft_all();
        return;
    }
    SDL_memset(&uw, 0, sizeof uw);
    SDL_memset(&urw, 0, sizeof urw);
    for (i = s_nsoft - 1; i >= 0; i--) {
        const Soft *e = &s_soft[i];
        const int *b = e->box;
        int inside = b[0] >= x1 && b[1] >= y1 && b[2] <= x2 && b[3] <= y2, over = rects_meet(b, x1, y1, x2, y2);
        int reads = rects_meet(e->read[0], x1, y1, x2, y2) || rects_meet(e->read[1], x1, y1, x2, y2);

        need[i] = mode == RESOLVE_READ ? over : reads || (mode == RESOLVE_WRITE && over && !inside);
        if (!need[i] && meets(&urw, b[0], b[1], b[2], b[3])) {
            need[i] = 1;
        }
        for (k = 0; k < 2 && !need[i]; k++) {
            need[i] = e->read[k][0] <= e->read[k][2] && meets(&uw, e->read[k][0], e->read[k][1], e->read[k][2], e->read[k][3]);
        }
        if (need[i]) {
            mark(&uw, b[0], b[1], b[2], b[3]);
            mark(&urw, b[0], b[1], b[2], b[3]);
            for (k = 0; k < 2; k++) {
                if (e->read[k][0] <= e->read[k][2]) {
                    mark(&urw, e->read[k][0], e->read[k][1], e->read[k][2], e->read[k][3]);
                }
            }
        }
    }
    if (mode != RESOLVE_COVER) {
        perf_begin(PF_SOFT);
        for (i = 0; i < s_nsoft; i++) {
            if (need[i]) {
                soft_run(&s_soft[i]);
                drawn++;
            }
        }
        perf_end(PF_SOFT);
        perf_gpu(PF_SOFT_N, drawn);
        g = saved;
    }
    for (i = 0; i < s_nsoft; i++) {
        const int *b = s_soft[i].box;
        int inside = b[0] >= x1 && b[1] >= y1 && b[2] <= x2 && b[3] <= y2;

        if (mode == RESOLVE_COVER ? need[i] || !inside : !need[i] && (mode == RESOLVE_READ || !inside)) {
            s_soft[n++] = s_soft[i];
        } else if (!need[i]) {
            dropped++;
        }
    }
    if (perf_on() && drawn >= 64) {
        perf_note("soft: %d drawn, %d dropped, %d left, for %s %d,%d-%d,%d", drawn, dropped, n,
                  mode == RESOLVE_READ ? "a read of" : mode == RESOLVE_WRITE ? "a write of" : "a cover of", x1, y1, x2, y2);
    }
    s_nsoft = n;
    soft_masks();
}

void soft_read(int x, int y, int w, int h) {
    if (w > 0 && h > 0 && s_nsoft > 0 && meets(&s_soft_w, x, y, x + w - 1, y + h - 1)) {
        soft_resolve(x, y, SDL_min(x + w, VRAM_W) - 1, SDL_min(y + h, VRAM_H) - 1, RESOLVE_READ);
    }
}

/* Something outside the queue (a fill, LoadImage, MoveImage) is about to
   write x, y, w, h */
void soft_write(int x, int y, int w, int h) {
    int x2 = SDL_min(x + w, VRAM_W) - 1, y2 = SDL_min(y + h, VRAM_H) - 1;

    if (w > 0 && h > 0 && s_nsoft > 0 && (meets(&s_soft_w, x, y, x2, y2) || meets(&s_soft_r, x, y, x2, y2))) {
        soft_resolve(x, y, x2, y2, RESOLVE_WRITE);
    }
}

static void soft_push(Soft *e, int opaque);

/* Queues E (its kind and arguments set) with the drawing state, writing
   x1-y2 (clipped here to the drawing area); OPAQUE: it covers that box */
static void soft_queue(Soft *e, int x1, int y1, int x2, int y2, int textured, int clut, int opaque) {
    int w = 64 << SDL_min(g.depth, 2);

    e->box[0] = SDL_max(x1, g.x1), e->box[1] = SDL_max(y1, g.y1);
    e->box[2] = SDL_min(x2, g.x2), e->box[3] = SDL_min(y2, g.y2);
    if (e->box[0] > e->box[2] || e->box[1] > e->box[3]) {
        return; /* nothing of it in the drawing area */
    }
    e->read[0][0] = e->read[1][0] = 1, e->read[0][2] = e->read[1][2] = 0;
    if (textured) {
        /* the page (wrapping past x 1023: all across) and the CLUT row */
        int cx = (clut & 0x3F) << 4, cy = clut >> 6 & 0x1FF, cw = g.depth == 0 ? 16 : 256;

        e->read[0][0] = g.tx + w > VRAM_W ? 0 : g.tx, e->read[0][1] = g.ty;
        e->read[0][2] = g.tx + w > VRAM_W ? VRAM_W - 1 : g.tx + w - 1, e->read[0][3] = SDL_min(g.ty + 255, VRAM_H - 1);
        if (g.depth < 2) {
            e->read[1][0] = cx + cw > VRAM_W ? 0 : cx, e->read[1][1] = cy;
            e->read[1][2] = cx + cw > VRAM_W ? VRAM_W - 1 : cx + cw - 1, e->read[1][3] = cy;
        }
    }
    soft_push(e, opaque);
}

/* Queues E, its box and reads set, with the drawing state; OPAQUE: it covers its box */
static void soft_push(Soft *e, int opaque) {
    if (opaque && !g.check_mask && s_nsoft > 0 && meets(&s_soft_w, e->box[0], e->box[1], e->box[2], e->box[3])) {
        soft_resolve(e->box[0], e->box[1], e->box[2], e->box[3], RESOLVE_COVER);
    }
    if (s_soft == NULL && (s_soft = SDL_malloc(SOFT_MAX * sizeof(Soft))) == NULL) {
        return;
    }
    if (s_nsoft == SOFT_MAX) {
        why("a full queue", 0, 0, 0, 0), soft_all();
    }
    e->g = g;
    s_soft[s_nsoft++] = *e;
    soft_mark(e);
}

static void soft_tri(Vtx a, Vtx b, Vtx c, int flags, int clut) {
    Soft e;

    e.kind = SOFT_TRI;
    e.u.tri.a = a, e.u.tri.b = b, e.u.tri.c = c, e.u.tri.flags = flags, e.u.tri.clut = clut;
    soft_queue(&e, SDL_min(SDL_min(a.x, b.x), c.x), SDL_min(SDL_min(a.y, b.y), c.y), SDL_max(SDL_max(a.x, b.x), c.x),
               SDL_max(SDL_max(a.y, b.y), c.y), flags & 2, clut, 0);
}

static void rect(int x, int y, int w, int h, int col, int textured, int raw, int semi, int u0, int v0, int clut, int soft) {
    int bit;
    struct SDL_Surface *img;
    unsigned *tex;
    Soft e;

    /* a face button in the game's text, as the icons chosen (controls.c):
       square, centred where the glyph was */
    if (w > 0 && h > 0 && textured && (bit = controls_glyph(g.tx, g.ty, g.depth, u0, v0, w, h)) >= 0 &&
        controls_game_icon(bit, &img, &tex)) {
        float c = s_map_a + (x + w / 2.0f - s_map_a) * s_map_s + s_map_d;

        gl_image(img, tex, c - h / 2.0f, (float)y, c + h / 2.0f, (float)(y + h), 0, CLIP);
    } else if (w > 0 && h > 0) {
        gl_rect(x, y, w, h, col & 255, col >> 8 & 255, col >> 16 & 255,
                (textured ? 2 : 0) | (textured && raw ? 4 : 0) | (semi ? 8 : 0) | (soft ? 512 : 0), u0, v0, clut);
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    e.kind = SOFT_RECT;
    e.u.rect.x = x, e.u.rect.y = y, e.u.rect.w = w, e.u.rect.h = h, e.u.rect.col = col, e.u.rect.textured = textured;
    e.u.rect.raw = raw, e.u.rect.semi = semi, e.u.rect.u0 = u0, e.u.rect.v0 = v0, e.u.rect.clut = clut, e.u.rect.soft = soft;
    soft_queue(&e, x, y, x + w - 1, y + h - 1, textured, clut, !textured && !semi);
}

static void line(int x0, int y0, int c0, int x1, int y1, int c1, int gouraud, int semi) {
    Soft e;

    line_walk(x0, y0, c0, x1, y1, c1, gouraud, semi, 0);
    e.kind = SOFT_LINE;
    e.u.line.x0 = x0, e.u.line.y0 = y0, e.u.line.c0 = c0, e.u.line.x1 = x1, e.u.line.y1 = y1, e.u.line.c1 = c1;
    e.u.line.gouraud = gouraud, e.u.line.semi = semi;
    soft_queue(&e, SDL_min(x0, x1), SDL_min(y0, y1), SDL_max(x0, x1), SDL_max(y0, y1), 0, 0, 0);
}

/* --- packets --- */

/* One GP0 packet of n words; returns how many it used */
void gpu_gp0(const uint32_t *p, int n) {
    uint32_t cmd = p[0] >> 24;

    if (cmd >= 0x20 && cmd < 0x40) {
        int gouraud = cmd & 0x10, quad = cmd & 8, tex = cmd & 4, semi = cmd & 2, raw = cmd & 1;
        int nv = quad ? 4 : 3, i, k = 0, clut = 0;
        Vtx v[4];
        float fx[4], fy[4], fz[4];
        uint32_t color = p[k++] & 0xFFFFFF;

        s_far = 0;
        for (i = 0; i < nv; i++) {
            if (i > 0 && gouraud) {
                color = p[k++] & 0xFFFFFF;
            }
            v[i].r = color & 255;
            v[i].g = color >> 8 & 255;
            v[i].b = color >> 16 & 255;
            v[i].x = sx(p[k]) + g.ox;
            v[i].y = sy(p[k]) + g.oy;
            precise(&p[k], &fx[i], &fy[i], &fz[i]);
            k++;
            if (tex) {
                v[i].u = p[k] & 255;
                v[i].v = p[k] >> 8 & 255;
                if (i == 0) {
                    clut = p[k] >> 16;
                } else if (i == 1) {
                    set_tpage(p[k] >> 16);
                }
                k++;
            } else {
                v[i].u = v[i].v = 0;
            }
        }
        i = (gouraud ? 1 : 0) | (tex ? 2 : 0) | (raw ? 4 : 0) | (semi ? 8 : 0);
        if (!tex && semi) {
            i |= 8;
        }
        soft_tri(v[0], v[1], v[2], i, clut);
        if (quad) {
            soft_tri(v[1], v[2], v[3], i, clut);
        }
        {
            int dither = g.dither && g_dither_on && (gouraud || (tex && !raw));
            GVtx gv[4];

            /* by the depth buffer when the GTE gave every point its depth */
            int known = fz[0] > 0 && fz[1] > 0 && fz[2] > 0 && (!quad || fz[3] > 0), deep = g_depth && known;

            for (k = 0; k < nv; k++) {
                gv[k] = gvtx(fx[k], fy[k], v[k].r, v[k].g, v[k].b, v[k].u, v[k].v, clut, i | (s_far == nv ? 2048 : 0), dither);
                gv[k].z = deep ? fz[k] : 0;
                gv[k].w = known ? fz[k] : 0;
            }
            if (!too_big(&v[0], &v[1], &v[2])) {
                gl_tri(gv[0], gv[1], gv[2]);
            }
            if (quad && !too_big(&v[1], &v[2], &v[3])) {
                gl_tri(gv[1], gv[2], gv[3]);
            }
        }
    } else if (cmd == 0x1C) {
        /* a battle shadow's triangle (native.h): the card alone, by the depth
           buffer, where every point has its depth */
        GVtx gv[3];
        int k;
        float fx, fy, fz;

        for (k = 0; k < 3; k++) {
            precise(&p[1 + k], &fx, &fy, &fz);
            if (fz <= 0 || !g_depth) {
                return;
            }
            gv[k] = gvtx(fx, fy, 0, 0, 0, 0, 0, 0, 8 | 1024, 0);
            gv[k].z = gv[k].w = fz;
        }
        gl_triangles(gv, 3, CLIP, 5);
    } else if (cmd == 0x1D) { /* an effect's own image (native.h): the card alone */
        native_effect_draw(p[0] & 0xFFFFFF, s_map_a + (sx(p[1]) + g.ox - s_map_a) * s_map_s, (float)(sy(p[1]) + g.oy), s_map_s,
                           CLIP);
    } else if (cmd == 0x1E) {
        /* a drawing area's background (gpu.c's SetDrawEnv), a TILE; in a
           battle, the screen's, by the stage's profile (stagefx.c), also a
           sky on the card: darker toward the top (or its sky_top), the
           stage's colour at the horizon */
        int x = sx(p[1]) + g.ox, y = sy(p[1]) + g.oy, w = p[2] & 0x3FF, h = p[2] >> 16 & 0x1FF, k;
        int c[3] = {p[0] & 255, p[0] >> 8 & 255, p[0] >> 16 & 255};
        float bright;
        const StageFx *fx = w >= 256 && h >= 200 ? stagefx_current(&bright) : NULL;

        rect(x, y, w, h, p[0] & 0xFFFFFF, 0, 0, 0, 0, 0, 0, 0);
        if (fx != NULL && fx->sky) {
            static const int CX[6] = {0, 1, 0, 1, 1, 0}, CY[6] = {0, 0, 1, 0, 1, 1};
            GVtx q[6];
            int top[3], i;

            for (i = 0; i < 3; i++) {
                top[i] = fx->sky_top[i] >= 0 ? (int)(fx->sky_top[i] * bright) : c[i] * 45 / 100;
            }
            for (k = 0; k < 6; k++) {
                const int *t = CY[k] ? c : top;

                q[k] = gvtx((float)(x + CX[k] * w), (float)(y + CY[k] * h), t[0], t[1], t[2], 0, 0, 0, 0, g_dither_on);
            }
            gl_triangles(q, 6, CLIP, g.semi);
        }
        if (fx != NULL) {
            gl_battle_fog(c[0], c[1], c[2]);
        }
    } else if (cmd >= 0x60 && cmd < 0x80) {
        int size = cmd >> 3 & 3, tex = cmd & 4, semi = cmd & 2, raw = cmd & 1;
        int k = 1, x = sx(p[1]) + g.ox, y = sy(p[1]) + g.oy, u = 0, vv = 0, clut = 0, w, h, soft = 0;

        k++;
        if (tex) {
            u = p[k] & 255;
            vv = p[k] >> 8 & 255;
            clut = p[k] >> 16;
            k++;
        }
        if (size == 0) {
            w = p[k] & 0x3FF;
            h = p[k] >> 16 & 0x1FF;
            soft = tex && semi && p[k] >> 31; /* NATIVE_SOFT_SHADOW (native.h), for the card */
        } else {
            w = h = size == 1 ? 1 : size == 2 ? 8 : 16;
        }
        rect(x, y, w, h, p[0] & 0xFFFFFF, tex, raw, semi, u, vv, clut, soft);
    } else if (cmd >= 0x40 && cmd < 0x60) {
        int gouraud = cmd & 0x10, poly = cmd & 8, semi = cmd & 2, k = 0;
        int c0 = p[k++] & 0xFFFFFF, x0 = sx(p[k]) + g.ox, y0 = sy(p[k]) + g.oy, c1, x1, y1;

        k++;
        for (;;) {
            if (k >= n || (poly && (p[k] & 0xF000F000) == 0x50005000)) {
                break;
            }
            c1 = gouraud ? (int)(p[k++] & 0xFFFFFF) : c0;
            if (k >= n) {
                break;
            }
            x1 = sx(p[k]) + g.ox;
            y1 = sy(p[k]) + g.oy;
            k++;
            line(x0, y0, c0, x1, y1, c1, gouraud, semi);
            x0 = x1; y0 = y1; c0 = c1;
            if (!poly) {
                break;
            }
        }
    } else if (cmd == 0x02) { /* fill, no clip or offset; x in steps of 16 */
        int x = p[1] & 0x3F0, y = p[1] >> 16 & 0x1FF, w = ((p[2] & 0x3FF) + 15) & ~15, h = p[2] >> 16 & 0x1FF, i, j;
        int c = rgb15(p[0] & 255, p[0] >> 8 & 255, p[0] >> 16 & 255, 0);

        if (x + w > VRAM_W || y + h > VRAM_H) {
            why("a wrapping fill", x, y, w, h), soft_all(); /* wraps: it covers what it covers */
        } else {
            soft_write(x, y, w, h);
        }
        for (j = 0; j < h; j++) {
            for (i = 0; i < w; i++) {
                g_vram[((y + j) & 511) * VRAM_W + ((x + i) & 1023)] = (uint16_t)c;
            }
        }
        gl_vram_dirty(x, y, w, h);
        gl_refresh(x, y, w, h);
    } else if (cmd >= 0x80 && cmd < 0xA0) { /* VRAM to VRAM */
        int sx0 = p[1] & 1023, sy0 = p[1] >> 16 & 511, dx = p[2] & 1023, dy = p[2] >> 16 & 511;
        int w = p[3] & 0xFFFF, h = p[3] >> 16;

        if (w == 0) w = 0x400;
        if (h == 0) h = 0x200;
        /* the faces' parts (page to page, nothing queued there): at once;
           the cursor's bar (onto the screen the queue draws): queued after it */
        if (sx0 + w > VRAM_W || sy0 + h > VRAM_H || dx + w > VRAM_W || dy + h > VRAM_H) {
            why("a wrapping copy", dx, dy, w, h), soft_all(); /* wraps */
            copy_px(sx0, sy0, dx, dy, w, h);
        } else if (s_nsoft == 0 || !(meets(&s_soft_w, sx0, sy0, sx0 + w - 1, sy0 + h - 1) ||
                                     meets(&s_soft_w, dx, dy, dx + w - 1, dy + h - 1) || meets(&s_soft_r, dx, dy, dx + w - 1, dy + h - 1))) {
            copy_px(sx0, sy0, dx, dy, w, h);
        } else {
            Soft e;

            e.kind = SOFT_COPY;
            e.u.copy.sx = sx0, e.u.copy.sy = sy0, e.u.copy.dx = dx, e.u.copy.dy = dy, e.u.copy.w = w, e.u.copy.h = h;
            e.box[0] = dx, e.box[1] = dy, e.box[2] = dx + w - 1, e.box[3] = dy + h - 1;
            e.read[0][0] = sx0, e.read[0][1] = sy0, e.read[0][2] = sx0 + w - 1, e.read[0][3] = sy0 + h - 1;
            e.read[1][0] = 1, e.read[1][2] = 0;
            soft_push(&e, 0);
        }
        gl_copy_to(sx0, sy0, w, h, s_map_a + (dx - s_map_a) * s_map_s, dy, w * s_map_s); /* a menu's highlight, placed with it */
    } else if (cmd >= 0xE1 && cmd <= 0xE6) {
        gp0_env(p[0]);
    } else if (cmd > 0x02) {
        LOGC(LOG_GPU, "gpu: GP0 %02X not drawn", cmd);
    }
    /* 0x00 nop, 0x01 cache flush, 0x1F irq: nothing to do */
}

/* A packet of n words can hold several commands (DR_ENV, DR_MOVE): split them */
static int cmd_words(const uint32_t *p) {
    uint32_t cmd = p[0] >> 24;

    if (cmd >= 0x20 && cmd < 0x40) {
        int nv = (cmd & 8) ? 4 : 3;
        return 1 + nv * (1 + ((cmd & 4) ? 1 : 0)) + ((cmd & 0x10) ? nv - 1 : 0);
    }
    if (cmd >= 0x60 && cmd < 0x80) return 2 + ((cmd & 4) ? 1 : 0) + (((cmd >> 3 & 3) == 0) ? 1 : 0);
    if (cmd == 0x02) return 3;
    if (cmd == 0x1C) return 4; /* a battle shadow's triangle (native.h) */
    if (cmd == 0x1D) return 2; /* an effect's own image (native.h) */
    if (cmd == 0x1E) return 3; /* a drawing area's background (gpu.c) */
    if (cmd >= 0x80 && cmd < 0xA0) return 4;
    return 1;
}

void gpu_packet(const uint32_t *p, int n) {
    while (n > 0) {
        uint32_t cmd = p[0] >> 24;
        int k = (cmd >= 0x40 && cmd < 0x60) ? n : cmd_words(p); /* lines take the rest */

        if (k > n) {
            k = n;
        }
        gpu_gp0(p, k);
        p += k;
        n -= k;
    }
}

/* --- widescreen battles' menus --- */

#define WIDE_MAX 4096
#define WMIN(a, b) ((a) < (b) ? (a) : (b))
#define WMAX(a, b) ((a) > (b) ? (a) : (b))
static struct {
    int box[4]; /* x0, y0, x1, y1 on screen, from the drawing area's corner */
    int root;   /* its menu: union-find */
    float a, s;
} s_wide[WIDE_MAX];
static int s_wide_n;

static int wide_root(int i) {
    while (s_wide[i].root != i) {
        i = s_wide[i].root = s_wide[s_wide[i].root].root;
    }
    return i;
}

/* A packet's box on screen (its polygons and rectangles), 0 if it has none;
   *ox, *x1 and *y1 follow its offset and drawing area */
static int packet_box(const uint32_t *p, int n, int *ox, int *oy, int *x1, int *y1, int box[4]) {
    int found = 0;

    while (n > 0) {
        uint32_t cmd = p[0] >> 24;
        int k = (cmd >= 0x40 && cmd < 0x60) ? n : cmd_words(p), i, xs[4], ys[4], nv = 0;

        if (k > n) {
            k = n;
        }
        cmd = cmd == 0x1E ? 0x60 : cmd; /* a background: a TILE */
        if (cmd >= 0x20 && cmd < 0x40) {
            int gouraud = cmd & 0x10, tex = cmd & 4, w = 1;

            nv = (cmd & 8) ? 4 : 3;
            for (i = 0; i < nv; i++) {
                xs[i] = sx(p[w]) + *ox - *x1;
                ys[i] = sy(p[w]) + *oy - *y1;
                w += 1 + (tex ? 1 : 0) + (gouraud && i < nv - 1 ? 1 : 0);
            }
        } else if (cmd >= 0x60 && cmd < 0x80) {
            int size = cmd >> 3 & 3, wh = 2 + ((cmd & 4) ? 1 : 0);
            int w = size == 0 ? (int)(p[wh] & 0x3FF) : size == 1 ? 1 : size == 2 ? 8 : 16;
            int h = size == 0 ? (int)(p[wh] >> 16 & 0x1FF) : w;

            xs[0] = sx(p[1]) + *ox - *x1, ys[0] = sy(p[1]) + *oy - *y1;
            xs[1] = xs[0] + w, ys[1] = ys[0] + h;
            nv = 2;
        } else if (cmd >= 0x80 && cmd < 0xA0) { /* a copy within VRAM: where it goes (the cursor's bar) */
            xs[0] = (int)(p[2] & 1023) - *x1, ys[0] = (int)(p[2] >> 16 & 511) - *y1;
            xs[1] = xs[0] + (int)(p[3] & 0xFFFF), ys[1] = ys[0] + (int)(p[3] >> 16);
            nv = ys[0] >= 0 && ys[0] < 240 ? 2 : 0; /* the other buffer's copy goes with the first's */
        } else if (cmd == 0xE3) {
            *x1 = p[0] & 1023, *y1 = p[0] >> 10 & 511;
        } else if (cmd == 0xE5) {
            *ox = (int)(p[0] << 21) >> 21, *oy = (int)(p[0] << 10) >> 21;
        }
        for (i = 0; i < nv; i++) {
            if (!found) {
                box[0] = box[2] = xs[i], box[1] = box[3] = ys[i];
                found = 1;
            }
            box[0] = xs[i] < box[0] ? xs[i] : box[0];
            box[1] = ys[i] < box[1] ? ys[i] : box[1];
            box[2] = xs[i] > box[2] ? xs[i] : box[2];
            box[3] = ys[i] > box[3] ? ys[i] : box[3];
        }
        p += k;
        n -= k;
    }
    return found;
}

/* Each packet of the menus' table its place on the 16:9 picture: packets
   that touch are one menu; a menu on the left keeps its shape against the
   left edge, one on the right against the right edge; the HUD at the top,
   the partner's picture and what spans the screen (the messages) stay
   stretched with the picture */
static void wide_plan(uint32_t *p) {
    int ox = g.ox, oy = g.oy, x1 = g.x1, y1 = g.y1, guard = 0, i, j, r;
    static int menu[WIDE_MAX][4];

    s_wide_n = 0;
    while (p != NULL && guard++ < 1000000 && s_wide_n < WIDE_MAX) {
        uint32_t tag = *p;
        int n = tag >> 24;

        if (n) {
            if (!packet_box(p + 1, n, &ox, &oy, &x1, &y1, s_wide[s_wide_n].box)) {
                s_wide[s_wide_n].box[0] = 1 << 20; /* none: touches nothing */
                s_wide[s_wide_n].box[2] = -(1 << 20);
            }
            s_wide[s_wide_n].root = s_wide_n;
            s_wide_n++;
        }
        if ((tag & 0xFFFFFF) == 0xFFFFFF) {
            break;
        }
        p = (uint32_t *)(uintptr_t)(tag & 0xFFFFFF);
    }
    /* ponytail: O(n^2) over the menus' packets, a few hundred a frame */
    for (i = 0; i < s_wide_n; i++) {
        for (j = i + 1; j < s_wide_n; j++) {
            int *a = s_wide[i].box, *b = s_wide[j].box;

            if (a[3] < 0x4A || b[3] < 0x4A) {
                continue; /* the HP panels at the top join no menu: a menu opening under them took them along */
            }
            if (a[0] <= b[2] + 2 && b[0] <= a[2] + 2 && a[1] <= b[3] + 2 && b[1] <= a[3] + 2) {
                s_wide[wide_root(i)].root = wide_root(j);
            }
        }
    }
    for (i = 0; i < s_wide_n; i++) {
        menu[i][0] = 1 << 20, menu[i][1] = 1 << 20, menu[i][2] = -(1 << 20), menu[i][3] = -(1 << 20);
    }
    for (i = 0; i < s_wide_n; i++) {
        int *b = s_wide[i].box;

        r = wide_root(i);
        if (b[0] <= b[2]) {
            menu[r][0] = WMIN(menu[r][0], b[0]), menu[r][1] = WMIN(menu[r][1], b[1]);
            menu[r][2] = WMAX(menu[r][2], b[2]), menu[r][3] = WMAX(menu[r][3], b[3]);
        }
    }
    /* each keeps its shape (3/4 across the 16:9 picture): a menu against
       the side its middle is on, as the HP panels at the top each by its
       own place (a gap opens between them), the partner's picture and its
       frame on the right with the gauge; the messages, across the screen,
       in its middle */
    for (i = 0; i < s_wide_n; i++) {
        int *m = menu[wide_root(i)], *b = s_wide[i].box;
        int hud = b[3] < 0x4A || m[3] < 0x4A;                             /* the HP panels, at the top */
        int wide = m[2] - m[0] > 200;                                      /* the messages */
        int mid = hud ? (b[0] + b[2]) / 2 : (m[0] + m[2]) / 2;

        if (m[0] > m[2] && !hud) {
            s_wide[i].a = 0, s_wide[i].s = 1;
        } else {
            s_wide[i].a = (float)(x1 + (wide && !hud ? 160 : mid < 160 ? 0 : 320)), s_wide[i].s = 0.75f;
        }
    }
}

/* Walks an ordering table: each tag is {next address: 24, words: 8}, then the words */
void DrawOTag(uint32_t *p) {
    int guard = 0, wide = gte_widescreen() && g_draw_layer == WIDE_MENUS_LAYER, k = 0, drawn = 0;

    perf_begin(PF_DRAW);
    gl_sync(0);
    gl_depth_clear();
    if (wide) {
        wide_plan(p);
    }
    if (gte_widescreen() && g_draw_layer == WIDE_PARTNER_LAYER) {
        /* the partner's picture (fightstg/partner_view.c: x 0xD0-0x133, its
           middle 0x102), by its frame on the right edge: the GTE already
           drew its 3D 3/4 as wide about that middle, so it only moves, and
           its area narrows about the edge (the frame buffers start at x 0) */
        s_map_d = (320 - 0x102) * 0.25f;
        s_clip_a = 320, s_clip_s = 0.75f;
    }
    while (p != NULL && guard++ < 1000000) {
        uint32_t tag = *p;
        int n = tag >> 24;

        if (n) {
            if (wide && k < s_wide_n) {
                s_map_a = s_wide[k].a, s_map_s = s_wide[k].s;
                k++;
            }
            gpu_packet(p + 1, n);
            drawn++;
        }
        if ((tag & 0xFFFFFF) == 0xFFFFFF) {
            break;
        }
        p = (uint32_t *)(uintptr_t)(tag & 0xFFFFFF);
    }
    s_map_a = 0, s_map_s = 1, s_map_d = 0;
    s_clip_a = 0, s_clip_s = 1;
    gl_shadows_resolve();
    if (gte_in_battle() && g_draw_layer == 0x1002) { /* BATTLE_LAYER_STAGE: its depth, for the props (stagefx.c) */
        stagefx_stage_drawn(drawn);
        gl_depth_save();
    }
    if (gte_in_battle() && g_draw_layer == 0x1004) { /* fightstg/types.h's BATTLE_LAYER_MODELS */
        perf_begin(PF_PROPS);
        gl_depth_models();
        stagefx_draw(g.ox, g.oy, CLIP);
        perf_end(PF_PROPS);
    }
    perf_end(PF_DRAW);
}
