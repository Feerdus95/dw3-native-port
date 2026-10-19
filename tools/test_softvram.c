/* The 1x VRAM drawn when read (platform/gpu_draw.c's queue) has to come out
   as drawing every primitive at once did: the same random frames (double
   buffered, covered by fills and opaque rectangles, drawing into a page that
   is then sampled, LoadImage and MoveImage over the queue, the mask bits)
   are drawn once with everything drawn after each packet and once queued,
   and the VRAM compared. Run from build/:
   i686-w64-mingw32-clang -I<SDL>/include -I../platform ../tools/test_softvram.c ../platform/gpu_draw.c ../platform/gpu.c -L<SDL>/lib -lSDL3 -o test_softvram.exe
   ./test_softvram.exe */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

int ResetGraph(int mode);
int LoadImage(RECT *r, uint32_t *p);
int MoveImage(RECT *r, int x, int y);
void gpu_gp0(const uint32_t *p, int n);

int g_depth, g_log;
uint16_t g_vram_dirty[8];
void gl_sync(int force) {}
void gl_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {}
void gl_vram_dirty(int x, int y, int w, int h) {}
void gl_shadows_resolve(void) {}
void gl_depth_clear(void) {}
void gl_refresh(int x, int y, int w, int h) {}
void gl_copy(int sx, int sy, int w, int h, int dx, int dy) {}
void gl_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) {}
void gl_depth_save(void) {}
void gl_depth_models(void) {}
void gl_image(const struct SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2) {}
void gl_battle_fog(int r, int g, int b) {}
void native_effect_draw(unsigned key, float x, float y, float xs, int cx1, int cy1, int cx2, int cy2) {}
int gte_precise(const uint32_t *word, float *x, float *y, float *z) { return 0; }
int gte_widescreen(void) { return 0; }
int gte_in_battle(void) { return 0; }
void perf_begin(int s) {}
void perf_end(int s) {}
static double s_drawn; /* the primitives drawn into g_vram (perf PF_SOFT_N) */
void perf_gpu(int s, double ms) { s_drawn += s == PF_SOFT_N ? ms : 0; }
const StageFx *stagefx_current(float *bright) { return NULL; }
void stagefx_stage_drawn(int polygons) {}
void stagefx_draw(int ox, int oy, int x1, int y1, int x2, int y2) {}
int controls_glyph(int tx, int ty, int depth, int u, int v, int w, int h) { return -1; }
int controls_game_icon(int bit, struct SDL_Surface **img, unsigned **tex) { return 0; }

static uint32_t s_seed;
static int rnd(int n) {
    s_seed = s_seed * 1103515245u + 12345u;
    return (int)(s_seed >> 8) % n;
}
static int s_eager; /* the reference: everything drawn after each packet */

static void gp0(const uint32_t *p, int n) {
    gpu_gp0(p, n);
    if (s_eager) {
        soft_all();
    }
}

static void env(int tx, int ty, int depth, int semi, int x1, int y1, int x2, int y2, int mask) {
    uint32_t p[6] = {0xE1000000u | tx >> 6 | (ty >> 8) << 4 | semi << 5 | depth << 7 | 1u << 9,
                     0xE3000000u | x1 | y1 << 10, 0xE4000000u | x2 | y2 << 10, 0xE5000000u | x1 | y1 << 11,
                     0xE6000000u | mask, 0xE2000000u};
    int i;

    for (i = 0; i < 6; i++) {
        gp0(&p[i], 1);
    }
}

static uint32_t xy(int w, int h) { return (uint32_t)(rnd(w + 40) - 20) | (uint32_t)(rnd(h + 40) - 20) << 16; }
static uint32_t col(void) { return (uint32_t)rnd(0x1000000); }

/* A random primitive in a w x h area, textured ones from the page at 640, 0 (4 bpp, CLUT at 0, 240: in the
   frame buffer's 64x64 block, as the battle has them) */
static void prim(int w, int h) {
    uint32_t p[12], tp = 640 >> 6 | 1u << 9, clut = 240u << 6;

    switch (rnd(6)) {
    case 0: /* textured triangle */
        p[0] = 0x24000000u | col(), p[1] = xy(w, h), p[2] = (uint32_t)rnd(65536) | clut << 16, p[3] = xy(w, h);
        p[4] = (uint32_t)rnd(65536) | tp << 16, p[5] = xy(w, h), p[6] = (uint32_t)rnd(65536);
        gp0(p, 7);
        break;
    case 1: /* gouraud quad */
        p[0] = 0x38000000u | col(), p[1] = xy(w, h), p[2] = col(), p[3] = xy(w, h), p[4] = col(), p[5] = xy(w, h), p[6] = col(), p[7] = xy(w, h);
        gp0(p, 8);
        break;
    case 2: /* semi-transparent triangle */
        p[0] = 0x22000000u | col(), p[1] = xy(w, h), p[2] = xy(w, h), p[3] = xy(w, h);
        gp0(p, 4);
        break;
    case 3: /* textured semi-transparent rectangle */
        p[0] = 0x66000000u | col(), p[1] = xy(w, h), p[2] = (uint32_t)rnd(65536) | clut << 16, p[3] = (uint32_t)(rnd(64) | rnd(64) << 16);
        gp0(p, 4);
        break;
    case 4: /* gouraud line */
        p[0] = 0x50000000u | col(), p[1] = xy(w, h), p[2] = col(), p[3] = xy(w, h);
        gp0(p, 4);
        break;
    default: /* opaque rectangle */
        p[0] = 0x60000000u | col(), p[1] = xy(w, h), p[2] = (uint32_t)(rnd(80) | rnd(80) << 16);
        gp0(p, 3);
        break;
    }
}

static void run(uint16_t *out) {
    static uint16_t data[256 * 256];
    RECT r;
    int f, i;

    s_seed = 1;
    memset(g_vram, 0, VRAM_W * VRAM_H * 2);
    soft_discard();
    for (i = 0; i < 256 * 256; i++) {
        data[i] = (uint16_t)rnd(65536);
    }
    r = (RECT){640, 0, 64, 256}, LoadImage(&r, (uint32_t *)data);
    r = (RECT){0, 240, 16, 1}, LoadImage(&r, (uint32_t *)data);
    for (f = 0; f < 40; f++) {
        int x0 = f % 2 * 320;
        uint32_t p[3];

        env(640, 0, 0, rnd(4), x0, 0, x0 + 319, 239, f % 7 == 3 ? 3 : 0);
        if (f % 3 == 0) { /* the frame's background: a fill, or an opaque rectangle */
            p[0] = 0x02000000u | col(), p[1] = (uint32_t)x0, p[2] = 320u | 240u << 16;
            gp0(p, 3);
        } else {
            p[0] = 0x60000000u | col(), p[1] = 0, p[2] = 320u | 240u << 16;
            gp0(p, 3);
        }
        for (i = 0; i < 60; i++) {
            prim(320, 240);
        }
        /* drawn into 768, 256 and then sampled there as a 15-bit page */
        env(640, 0, 0, 0, 768, 256, 831, 319, 0);
        for (i = 0; i < 8; i++) {
            prim(64, 64);
        }
        env(768, 256, 2, 1, x0, 0, x0 + 319, 239, 0);
        p[0] = 0x64000000u | col(), p[1] = xy(320, 240), p[2] = 0; /* u, v 0: what was just drawn */
        {
            uint32_t q[4] = {p[0], p[1], p[2], 64u | 64u << 16};
            gp0(q, 4);
        }
        { /* a face's part, page to page, and the cursor's bar, page to this buffer (DR_MOVE) */
            uint32_t m[4] = {0x80000000u, 640u | 16u << 16, 704u | 16u << 16, 16u | 16u << 16};
            uint32_t b[4] = {0x80000000u, 640u | 32u << 16, (uint32_t)(x0 + 100) | 100u << 16, 32u | 8u << 16};

            gp0(m, 4);
            gp0(b, 4);
        }
        /* each case on frames of its own, the last ones left uncovered at the end */
        if (f % 6 == 1) { /* a move out of this buffer, to where nothing reads */
            r = (RECT){x0 + 10, 20, 64, 64}, MoveImage(&r, 896, 64);
            if (s_eager) soft_all();
        }
        if (f % 6 == 2) { /* the page cleared while the sprite that read it is still queued */
            p[0] = 0x02000000u | col(), p[1] = 768u | 256u << 16, p[2] = 64u | 64u << 16;
            gp0(p, 3);
        }
        if (f % 6 == 3) { /* a load over part of what this frame drew */
            r = (RECT){x0 + 40, 50, 100, 100}, LoadImage(&r, (uint32_t *)data);
            if (s_eager) soft_all();
        }
    }
    soft_all();
    memcpy(out, g_vram, VRAM_W * VRAM_H * 2);
}

int main(int argc, char **argv) {
    static uint16_t eager[VRAM_W * VRAM_H], queued[VRAM_W * VRAM_H];
    int i, diff = 0;
    double all;

    setvbuf(stdout, NULL, _IONBF, 0);
    ResetGraph(0);
    s_eager = 1, run(eager);
    printf("drawn at once: %.0f primitives\n", s_drawn);
    all = s_drawn, s_drawn = 0;
    s_eager = 0, run(queued);
    printf("queued: %.0f drawn\n", s_drawn);
    for (i = 0; i < VRAM_W * VRAM_H; i++) {
        diff += eager[i] != queued[i];
    }
    printf("%d pixels differ\n", diff);
    assert(diff == 0);
    /* the covered frames dropped, not drawn (a CLUT in the frame buffer's
       block kept them all once); this test reads back far more than a frame does */
    assert(s_drawn > 0 && s_drawn < all * 0.85);
    return 0;
}
