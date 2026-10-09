/* libgpu/libgs, the part milestone 1 needs: VRAM, transfers and the display
   area. Drawing (DrawOTag and the primitives) is milestone 2. */
#include <stdlib.h>
#include <string.h>
#include "platform.h"

uint16_t *g_vram;
SAVED DISPENV g_disp;
SAVED int g_disp_on;

SAVED static int s_video_mode;

int ResetGraph(int mode) {
    if (g_vram == NULL) {
        g_vram = calloc(VRAM_W * VRAM_H, 2);
    }
    return 0;
}

int SetGraphDebug(int level) {
    (void)level;
    return 0;
}

long SetVideoMode(long mode) {
    long old = s_video_mode;

    s_video_mode = (int)mode;
    return old;
}

void SetDispMask(int mask) {
    g_disp_on = mask;
}

DISPENV *PutDispEnv(DISPENV *env) {
    g_disp = *env;
    return env;
}

int DrawSync(int mode) {
    (void)mode;
    return 0;
}


/* A rect of VRAM, clipped */
static void fill(const RECT *r, uint16_t c) {
    int x, y;

    for (y = r->y; y < r->y + r->h && y < VRAM_H; y++) {
        for (x = r->x; x < r->x + r->w && x < VRAM_W; x++) {
            g_vram[y * VRAM_W + x] = c;
        }
    }
}

int ClearImage(RECT *r, uint8_t red, uint8_t green, uint8_t blue) {
    fill(r, (uint16_t)(red >> 3 | (green >> 3) << 5 | (blue >> 3) << 10));
    return 0;
}

int ClearImage2(RECT *r, uint8_t red, uint8_t green, uint8_t blue) {
    return ClearImage(r, red, green, blue);
}

int LoadImage(RECT *r, uint32_t *p) {
    const uint16_t *src = (const uint16_t *)p;
    int y;


    for (y = 0; y < r->h && r->y + y < VRAM_H; y++) {
        memcpy(&g_vram[(r->y + y) * VRAM_W + r->x], src + y * r->w, (size_t)r->w * 2);
    }
    return 0;
}

int MoveImage(RECT *r, int x, int y) {
    static uint16_t *tmp;
    int j;

    if (tmp == NULL) {
        tmp = malloc(VRAM_W * VRAM_H * 2);
    }

    for (j = 0; j < r->h; j++) {
        memcpy(&tmp[j * r->w], &g_vram[(r->y + j) * VRAM_W + r->x], (size_t)r->w * 2);
    }
    for (j = 0; j < r->h; j++) {
        memcpy(&g_vram[(y + j) * VRAM_W + x], &tmp[j * r->w], (size_t)r->w * 2);
    }
    return 0;
}

void GsInitGraph(uint16_t w, uint16_t h, uint16_t intmode, uint16_t dith, uint16_t vrammode) {
    (void)w, (void)h, (void)intmode, (void)dith, (void)vrammode;
}

/* A TIM after its ID word: flags, then the CLUT block if bit 3, then the
   pixel block; each block is {bytes, y << 16 | x, h << 16 | w, data} */
void GsGetTimInfo(uint32_t *im, GsIMAGE *tim) {
    uint32_t flags = *im++;

    tim->pmode = flags;
    if (flags & 8) {
        tim->cx = (int16_t)(im[1] & 0xFFFF);
        tim->cy = (int16_t)(im[1] >> 16);
        tim->cw = (uint16_t)(im[2] & 0xFFFF);
        tim->ch = (uint16_t)(im[2] >> 16);
        tim->clut = im + 3;
        im += im[0] / 4;
    }
    tim->px = (int16_t)(im[1] & 0xFFFF);
    tim->py = (int16_t)(im[1] >> 16);
    tim->pw = (uint16_t)(im[2] & 0xFFFF);
    tim->ph = (uint16_t)(im[2] >> 16);
    tim->pixel = im + 3;
}

DISPENV *SetDefDispEnv(DISPENV *env, int x, int y, int w, int h) {
    env->disp.x = (int16_t)x;
    env->disp.y = (int16_t)y;
    env->disp.w = (int16_t)w;
    env->disp.h = (int16_t)h;
    env->screen.x = env->screen.y = env->screen.w = env->screen.h = 0;
    env->isinter = env->isrgb24 = env->pad0 = env->pad1 = 0;
    return env;
}

/* --- libgpu helpers that build packets in the game's memory --- */

typedef struct {
    RECT clip;
    int16_t ofs[2];
    RECT tw;
    uint16_t tpage;
    uint8_t dtd, dfe, isbg, r0, g0, b0;
    uint32_t dr_env[16];
} DRAWENV;

#define TAG_END 0x00FFFFFF
#define ADDR(p) ((uint32_t)(uintptr_t)(p) & 0xFFFFFF)

/* ot[0] -> ot[1] -> ... -> end */
uint32_t *ClearOTag(uint32_t *ot, int n) {
    int i;

    for (i = 0; i < n - 1; i++) {
        ot[i] = ADDR(&ot[i + 1]);
    }
    ot[n - 1] = TAG_END;
    return ot;
}

/* ot[n-1] -> ... -> ot[0] -> end: DrawOTag starts from the last */
uint32_t *ClearOTagR(uint32_t *ot, int n) {
    int i;

    for (i = n - 1; i > 0; i--) {
        ot[i] = ADDR(&ot[i - 1]);
    }
    ot[0] = TAG_END;
    return ot;
}

uint16_t GetTPage(int tp, int abr, int x, int y) {
    return (uint16_t)((tp & 3) << 7 | (abr & 3) << 5 | (y & 0x100) >> 4 | (x & 0x3FF) >> 6 | (y & 0x200) << 2);
}

uint16_t GetClut(int x, int y) {
    return (uint16_t)(y << 6 | (x >> 4 & 0x3F));
}

static uint32_t draw_mode(int dfe, int dtd, int tpage) {
    return 0xE1000000u | (dtd ? 0x200 : 0) | (dfe ? 0x400 : 0) | (tpage & 0x9FF);
}

/* {tag, code}: tag length in the top byte, the address bits kept */
void SetDrawTPage(uint32_t *p, int dfe, int dtd, int tpage) {
    p[0] = (p[0] & 0xFFFFFF) | 1u << 24;
    p[1] = draw_mode(dfe, dtd, tpage);
}

void SetDrawMove(uint32_t *p, RECT *r, int x, int y) {
    p[0] = (p[0] & 0xFFFFFF) | (r->w && r->h ? 5u : 0u) << 24;
    p[1] = 0x01000000;
    p[2] = 0x80000000;
    p[3] = (uint32_t)(r->y << 16 | (r->x & 0xFFFF));
    p[4] = (uint32_t)(y << 16 | (x & 0xFFFF));
    p[5] = (uint32_t)(r->h << 16 | (r->w & 0xFFFF));
}

void SetSprt(uint32_t *p) {
    p[0] = (p[0] & 0xFFFFFF) | 4u << 24;
    ((uint8_t *)p)[7] = 0x64;
}

void SetSemiTrans(uint32_t *p, int abe) {
    if (abe) {
        ((uint8_t *)p)[7] |= 2;
    } else {
        ((uint8_t *)p)[7] &= ~2;
    }
}

DRAWENV *SetDefDrawEnv(DRAWENV *env, int x, int y, int w, int h) {
    memset(env, 0, sizeof *env);
    env->clip.x = (int16_t)x;
    env->clip.y = (int16_t)y;
    env->clip.w = (int16_t)w;
    env->clip.h = (int16_t)h;
    env->ofs[0] = (int16_t)x;
    env->ofs[1] = (int16_t)y;
    env->tpage = GetTPage(0, 0, 640, 0);
    env->dtd = 1;
    return env;
}

/* The DRAWENV as packets: area, offset, mode, window, mask, and the clear */
void SetDrawEnv(uint32_t *dr, DRAWENV *env) {
    int n = 1;
    RECT c = env->clip;
    int tww = env->tw.w ? ((~(env->tw.w - 1)) >> 3) & 31 : 0, twh = env->tw.h ? ((~(env->tw.h - 1)) >> 3) & 31 : 0;

    /* as libgpu, the area is kept inside VRAM: a layer whose clip shrinks to
       nothing or runs off the edge must not wrap around to the far side */
    c.x = c.x < 0 ? 0 : c.x > VRAM_W - 1 ? VRAM_W - 1 : c.x;
    c.y = c.y < 0 ? 0 : c.y > VRAM_H - 1 ? VRAM_H - 1 : c.y;
    c.w = c.w < 0 ? 0 : c.w > VRAM_W - c.x ? VRAM_W - c.x : c.w;
    c.h = c.h < 0 ? 0 : c.h > VRAM_H - c.y ? VRAM_H - c.y : c.h;
    dr[n++] = 0xE3000000u | (c.y & 0x3FF) << 10 | (c.x & 0x3FF);
    dr[n++] = 0xE4000000u | ((c.y + (c.h ? c.h : 1) - 1) & 0x3FF) << 10 | ((c.x + (c.w ? c.w : 1) - 1) & 0x3FF);
    dr[n++] = 0xE5000000u | (env->ofs[1] & 0x7FF) << 11 | (env->ofs[0] & 0x7FF);
    dr[n++] = draw_mode(env->dfe, env->dtd, env->tpage);
    dr[n++] = 0xE2000000u | ((env->tw.y >> 3) & 31) << 15 | ((env->tw.x >> 3) & 31) << 10 | twh << 5 | tww;
    dr[n++] = 0xE6000000u;
    if (env->isbg && c.w > 0 && c.h > 0) {
        /* a TILE, clipped to the area like libgpu's: a fill (GP0 02) ignores
           the area and its 16-pixel rounding reached past the clip */
        dr[n++] = 0x60000000u | env->b0 << 16 | env->g0 << 8 | env->r0;
        dr[n++] = (uint32_t)((c.y - env->ofs[1]) << 16 | ((c.x - env->ofs[0]) & 0xFFFF));
        dr[n++] = (uint32_t)(c.h << 16 | (c.w & 0xFFFF));
    }
    dr[0] = (dr[0] & 0xFFFFFF) | (uint32_t)(n - 1) << 24;
}

/* Drawing is immediate here: the GPU is always idle */
int IsIdleGPU(int max) {
    return 0;
}

uint32_t *BreakDraw(void) {
    return NULL;
}

void DrawOTag(uint32_t *p);

int ContinueDraw(uint32_t *insaddr, uint32_t *contaddr) {
    DrawOTag(insaddr);
    return 0;
}
