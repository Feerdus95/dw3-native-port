/* The renderers behind renderer.h: each backend (platform/gl/, platform/d3d12/) fills one table, gfx.c picks one at
   start and its gfx_* call through it. Included by the backends and gfx.c only. */
#ifndef GFX_H
#define GFX_H

#include "renderer.h"

struct Gfx {
    const char *name;
    uint64_t (*window_flags)(void);
    int (*init)(struct SDL_Window *window);
    void (*sync)(int force);
    int (*scale)(void);
    int (*scale_tied)(void);
    int (*dlss_status)(void);
    int (*fsr3_status)(void);
    void (*triangles)(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi);
    void (*vram_dirty)(int x, int y, int w, int h);
    void (*shadows_resolve)(void);
    void (*depth_clear)(void);
    void (*refresh)(int x, int y, int w, int h);
    void (*copy)(int sx, int sy, int w, int h, int dx, int dy);
    void (*copy_to)(int sx, int sy, int w, int h, float dx, int dy, float dw);
    void (*mesh_make)(GMesh *g, const float *v, int n);
    void (*props_model)(const struct SDL_Surface *img, unsigned *tex, const GMesh *g, float miny, float h, unsigned inst, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
    unsigned (*inst_make)(const PInst *v, int n);
    unsigned (*buf_make)(const void *p, unsigned long bytes);
    void (*inst_free)(unsigned b);
    void (*props_inst)(const struct SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
    void (*props_part)(const struct SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
    void (*depth_save)(void);
    void (*depth_models)(void);
    void (*props)(const struct SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo);
    void (*image)(const struct SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2);
    int (*image_prepare)(const struct SDL_Surface *img, unsigned *tex);
    void (*present)(const struct SDL_Surface *menu, struct SDL_Surface **shot);
    void (*battle_fog)(int r, int g, int b);
};

/* A backend's functions, prefix P (gl, dx), declared and put in its table */
#define GFX_DECLARE(P)                                                                                                    \
    uint64_t P##_window_flags(void);                                                                                      \
    int P##_init(struct SDL_Window *window);                                                                              \
    void P##_sync(int force);                                                                                             \
    int P##_scale(void);                                                                                                  \
    int P##_scale_tied(void);                                                                                             \
    int P##_dlss_status(void);                                                                                            \
    int P##_fsr3_status(void);                                                                                            \
    void P##_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi);                                   \
    void P##_vram_dirty(int x, int y, int w, int h);                                                                      \
    void P##_shadows_resolve(void);                                                                                       \
    void P##_depth_clear(void);                                                                                           \
    void P##_refresh(int x, int y, int w, int h);                                                                         \
    void P##_copy(int sx, int sy, int w, int h, int dx, int dy);                                                          \
    void P##_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw);                                           \
    void P##_mesh_make(GMesh *g, const float *v, int n);                                                                  \
    void P##_props_model(const struct SDL_Surface *img, unsigned *tex, const GMesh *g, float miny, float h, unsigned inst, \
                         int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);           \
    unsigned P##_inst_make(const PInst *v, int n);                                                                        \
    unsigned P##_buf_make(const void *p, unsigned long bytes);                                                            \
    void P##_inst_free(unsigned b);                                                                                       \
    void P##_props_inst(const struct SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam,      \
                        int x1, int y1, int x2, int y2, const float *fogo);                                               \
    void P##_props_part(const struct SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1,    \
                        int x2, int y2, const float *fogo);                                                               \
    void P##_depth_save(void);                                                                                            \
    void P##_depth_models(void);                                                                                          \
    void P##_props(const struct SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, \
                   const float *fogo);                                                                                    \
    void P##_image(const struct SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add,         \
                   int cx1, int cy1, int cx2, int cy2);                                                                   \
    int P##_image_prepare(const struct SDL_Surface *img, unsigned *tex);                                                  \
    void P##_present(const struct SDL_Surface *menu, struct SDL_Surface **shot);                                          \
    void P##_battle_fog(int r, int g, int b);

#define GFX_TABLE(P, NAME)                                                                                                \
    {.name = NAME, .window_flags = P##_window_flags, .init = P##_init, .sync = P##_sync, .scale = P##_scale,             \
     .scale_tied = P##_scale_tied, .dlss_status = P##_dlss_status, .fsr3_status = P##_fsr3_status,                      \
     .triangles = P##_triangles, .vram_dirty = P##_vram_dirty, .shadows_resolve = P##_shadows_resolve,                  \
     .depth_clear = P##_depth_clear, .refresh = P##_refresh, .copy = P##_copy, .copy_to = P##_copy_to,                  \
     .mesh_make = P##_mesh_make, .props_model = P##_props_model, .inst_make = P##_inst_make, .buf_make = P##_buf_make,  \
     .inst_free = P##_inst_free, .props_inst = P##_props_inst, .props_part = P##_props_part,                            \
     .depth_save = P##_depth_save, .depth_models = P##_depth_models, .props = P##_props, .image = P##_image,            \
     .image_prepare = P##_image_prepare, .present = P##_present, .battle_fog = P##_battle_fog}

extern const struct Gfx GFX_GL, GFX_D3D12;
int dx_available(void); /* a Direct3D 12 device can be made (platform/d3d12/) */

#endif
