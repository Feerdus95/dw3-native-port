/* The GPU, in software: GP0 packets (what the game's primitives already are)
   rasterised into the 1024x512 16-bit VRAM, as the PSX GPU draws them; the
   same primitives go to the graphics card (gl.c), which draws what the
   screen shows, finer. Reference: the PSX GPU as documented in psx-spx
   (nocash). */
#include <stdlib.h>
#include <string.h>
#include "platform.h"

/* Drawing state (GP0 E1-E6) */
SAVED static struct {
    int tx, ty;        /* texture page base, in pixels */
    int semi;          /* semi-transparency mode 0-3 */
    int depth;         /* 0: 4 bpp, 1: 8 bpp, 2: 15 bpp */
    int dither;
    int tw_mask_x, tw_mask_y, tw_off_x, tw_off_y; /* texture window, in pixels */
    int x1, y1, x2, y2; /* drawing area, inclusive */
    int ox, oy;        /* drawing offset */
    int set_mask, check_mask;
} g;

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

/* A vertex for the card with the drawing state; flags as triangle()'s */
static GVtx gvtx(float x, float y, int r, int gg, int b, int u, int v, int clut, int flags, int dither) {
    GVtx o;

    o.x = x;
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
    o.mode = (uint32_t)((flags & 2) | (flags & 6) | (flags & 8) | (flags & 512) | g.depth << 4 | (dither ? 64 : 0) | (g.set_mask ? 256 : 0));
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
    gl_triangles(t, 3, g.x1, g.y1, g.x2, g.y2, g.semi);
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
    gl_triangles(q, 6, g.x1, g.y1, g.x2, g.y2, flags & 512 ? 4 : g.semi); /* a soft shadow darkens by multiplying */
}

/* A vertex word where the card puts it: with the GTE's fraction, when it
   made that word */
static void precise(const uint32_t *w, float *x, float *y, float *z) {
    if (!gte_precise(w, x, y, z)) {
        *z = 0;
        *x = (float)sx(*w);
        *y = (float)sy(*w);
    }
    *x += (float)g.ox;
    *y += (float)g.oy;
}

/* --- rectangles and lines --- */

static void rect(int x, int y, int w, int h, int col, int textured, int raw, int semi, int u0, int v0, int clut, int soft) {
    int i, j;
    int r = col & 255, gg = col >> 8 & 255, b = col >> 16 & 255;

    if (w > 0 && h > 0) {
        gl_rect(x, y, w, h, r, gg, b, (textured ? 2 : 0) | (textured && raw ? 4 : 0) | (semi ? 8 : 0) | (soft ? 512 : 0), u0, v0, clut);
    }

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            int c, s = semi;

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

static void line(int x0, int y0, int c0, int x1, int y1, int c1, int gouraud, int semi) {
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
        plot(x, y, rgb15(c & 255, c >> 8 & 255, c >> 16 & 255, d), semi);
        gl_rect(x, y, 1, 1, c & 255, c >> 8 & 255, c >> 16 & 255, semi ? 8 : 0, 0, 0, 0);
    }
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
        triangle(v[0], v[1], v[2], i, clut);
        if (quad) {
            triangle(v[1], v[2], v[3], i, clut);
        }
        {
            int dither = g.dither && g_dither_on && (gouraud || (tex && !raw));
            GVtx gv[4];

            /* by the depth buffer when the GTE gave every point its depth */
            int known = fz[0] > 0 && fz[1] > 0 && fz[2] > 0 && (!quad || fz[3] > 0), deep = g_depth && known;

            for (k = 0; k < nv; k++) {
                gv[k] = gvtx(fx[k], fy[k], v[k].r, v[k].g, v[k].b, v[k].u, v[k].v, clut, i, dither);
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

        for (j = 0; j < h; j++) {
            for (i = 0; i < w; i++) {
                g_vram[((y + j) & 511) * VRAM_W + ((x + i) & 1023)] = (uint16_t)c;
            }
        }
        gl_vram_dirty(x, y, w, h);
        gl_refresh(x, y, w, h);
    } else if (cmd >= 0x80 && cmd < 0xA0) { /* VRAM to VRAM */
        int sx0 = p[1] & 1023, sy0 = p[1] >> 16 & 511, dx = p[2] & 1023, dy = p[2] >> 16 & 511;
        int w = p[3] & 0xFFFF, h = p[3] >> 16, i, j;
        static uint16_t *tmp;

        if (tmp == NULL) tmp = malloc(VRAM_W * VRAM_H * 2);

        if (w == 0) w = 0x400;
        if (h == 0) h = 0x200;
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                tmp[j * w + i] = g_vram[((sy0 + j) & 511) * VRAM_W + ((sx0 + i) & 1023)];
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                g_vram[((dy + j) & 511) * VRAM_W + ((dx + i) & 1023)] = tmp[j * w + i];
        gl_vram_dirty(dx, dy, w, h);
        gl_copy(sx0, sy0, w, h, dx, dy);
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

/* Walks an ordering table: each tag is {next address: 24, words: 8}, then the words */
void DrawOTag(uint32_t *p) {
    int guard = 0;

    gl_sync(0);
    gl_depth_clear();
    while (p != NULL && guard++ < 1000000) {
        uint32_t tag = *p;
        int n = tag >> 24;

        if (n) {
            gpu_packet(p + 1, n);
        }
        if ((tag & 0xFFFFFF) == 0xFFFFFF) {
            break;
        }
        p = (uint32_t *)(uintptr_t)(tag & 0xFFFFFF);
    }
}
