/* renderer.h's functions: the renderer picked at start (native.ini's renderer), Direct3D 12 or OpenGL, and the
   options every renderer reads. The 32-bit build has OpenGL only. */
#include <SDL3/SDL.h>
#include "platform.h"
#include "gfx.h"

int g_renderer;  /* renderer = auto (Direct3D 12 where it can be made, else OpenGL), d3d12, gl */
int g_res_scale = 1; /* native.ini, the menu */
int g_depth = 1;
int g_true_color = 1, g_perspective = 1, g_smooth_3d = 1; /* true_color, perspective, smooth_3d, the menu */
int g_sharpen, g_brightness, g_gamma, g_saturation; /* sharpen 0 to 10; the others -10 to 10, 0 as the game's */
int g_scanlines, g_integer_scale; /* scanlines 0 to 10; integer_scale = 1: the picture a whole multiple of 240 rows */
int g_smaa, g_fsr, g_up_mode = 1; /* smaa; fsr: 1 FSR 1, 2 FSR 3.1, 3 DLSS; g_up_mode: 0 DLAA, 1 Quality, 2 Balanced, 3 Performance */
int g_soft_shadows;
int g_vsync = 1; /* vsync, the menu */
int g_battle_shadows = 1; /* battle_shadows, the menu (native.h) */
int g_battle_sky;         /* battle_sky, the menu (native.h) */
uint16_t g_vram_dirty[8]; /* 64x64 blocks of VRAM written since they were uploaded */

static const struct Gfx *s_gfx = &GFX_GL;
static int s_picked;

/* The first call (video.c, before the window): the renderer native.ini asks for, if this machine has it; after a
   gfx_fallback, OpenGL */
uint64_t gfx_window_flags(void) {
#ifdef _WIN64
    if (!s_picked && g_renderer != 2 && dx_available()) {
        s_gfx = &GFX_D3D12;
    }
#endif
    s_picked = 1;
    LOG("renderer: %s", s_gfx->name);
    return s_gfx->window_flags();
}

/* gfx_init failed: OpenGL instead, on a new window; 0 when it was OpenGL already */
int gfx_fallback(void) {
    if (s_gfx == &GFX_GL) {
        return 0;
    }
    s_gfx = &GFX_GL;
    return 1;
}

const char *gfx_name(void) {
    return s_gfx->name;
}

int gfx_init(SDL_Window *window) {
    return s_gfx->init(window);
}

void gfx_sync(int force) {
    s_gfx->sync(force);
}

int gfx_scale(void) {
    return s_gfx->scale();
}

int gfx_scale_tied(void) {
    return s_gfx->scale_tied();
}

int gfx_dlss_status(void) {
    return s_gfx->dlss_status();
}

int gfx_fsr3_status(void) {
    return s_gfx->fsr3_status();
}

void gfx_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {
    s_gfx->triangles(v, n, x1, y1, x2, y2, semi);
}

void gfx_vram_dirty(int x, int y, int w, int h) {
    s_gfx->vram_dirty(x, y, w, h);
}

void gfx_shadows_resolve(void) {
    s_gfx->shadows_resolve();
}

void gfx_depth_clear(void) {
    s_gfx->depth_clear();
}

void gfx_refresh(int x, int y, int w, int h) {
    s_gfx->refresh(x, y, w, h);
}

void gfx_copy(int sx, int sy, int w, int h, int dx, int dy) {
    s_gfx->copy(sx, sy, w, h, dx, dy);
}

void gfx_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) {
    s_gfx->copy_to(sx, sy, w, h, dx, dy, dw);
}

void gfx_mesh_make(GMesh *g, const float *v, int n) {
    s_gfx->mesh_make(g, v, n);
}

void gfx_props_model(const SDL_Surface *img, unsigned *tex, const GMesh *g, float miny, float h, unsigned inst, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    s_gfx->props_model(img, tex, g, miny, h, inst, first, n, cam, x1, y1, x2, y2, fogo);
}

unsigned gfx_inst_make(const PInst *v, int n) {
    return s_gfx->inst_make(v, n);
}

unsigned gfx_buf_make(const void *p, unsigned long bytes) {
    return s_gfx->buf_make(p, bytes);
}

void gfx_inst_free(unsigned b) {
    s_gfx->inst_free(b);
}

void gfx_props_inst(const SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    s_gfx->props_inst(img, tex, b, first, n, cam, x1, y1, x2, y2, fogo);
}

void gfx_props_part(const SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    s_gfx->props_part(img, tex, p, cam, x1, y1, x2, y2, fogo);
}

void gfx_depth_save(void) {
    s_gfx->depth_save();
}

void gfx_depth_models(void) {
    s_gfx->depth_models();
}

void gfx_props(const SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo) {
    s_gfx->props(img, tex, v, n, x1, y1, x2, y2, fogo);
}

void gfx_image(const SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2) {
    s_gfx->image(img, tex, x0, y0, x1, y1, add, cx1, cy1, cx2, cy2);
}

int gfx_image_prepare(const SDL_Surface *img, unsigned *tex) {
    return s_gfx->image_prepare(img, tex);
}

void gfx_present(const SDL_Surface *menu, SDL_Surface **shot) {
    s_gfx->present(menu, shot);
}

void gfx_battle_fog(int r, int g, int b) {
    s_gfx->battle_fog(r, g, b);
}
