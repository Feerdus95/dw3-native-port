/* The renderer: what the platform layer asks of the graphics backend. platform/gl/ (OpenGL 3.3) and, in the 64-bit
   build, platform/d3d12/ (Direct3D 12) implement these same functions; gfx.c calls the one picked at start (gfx.h), and
   nothing else of the port changes. The rest of the port
   (gpu.c, gpu_draw.c, stagefx.c, effects.c, postcards.c, video.c, state.c) calls only what is declared here, and
   the tests link doubles for it (tools/test_*.c). The drawing is render_scale times finer than VRAM; the GTE keeps
   each vertex's fraction for it (gte.c). Included by platform.h. */
#ifndef RENDERER_H
#define RENDERER_H

#include <stdint.h>

struct SDL_Window;
struct SDL_Surface;

/* The window's flags for this backend, with its attributes set (video.c creates the window with them, before gfx_init) */
uint64_t gfx_window_flags(void);
int gfx_fallback(void); /* gfx_init failed: OpenGL next, on a new window; 0 when it was OpenGL (gfx.c) */
const char *gfx_name(void); /* the renderer running: "D3D12" or "OpenGL" */
/* The drawing on the graphics card (gl.c), render_scale times finer than
   VRAM; the GTE keeps each vertex's fraction for it (gte.c) */
extern int g_res_scale;  /* render_scale = 1-8 */
typedef struct {
    float x, y, u, v;  /* VRAM pixels (fractions kept), texels */
    uint8_t r, g, b, a;
    uint16_t clut_x, clut_y, tx, ty;
    uint16_t umin, vmin, umax, vmax;
    uint16_t twmx, twmy, twox, twoy; /* texture window: mask, offset */
    uint32_t mode;     /* gl.c's MAIN_FS */
    float z;           /* the GTE's view depth, 0 for none: drawn in order, not by the depth buffer */
    float w;           /* the same, for the textures in perspective, 0 for none */
    float mx, my;      /* DW3_UPSCALE_DATA=motion: where it was last frame minus where it is, in VRAM pixels, 0 for unknown */
} GVtx;
extern uint16_t g_vram_dirty[8]; /* 64x64 blocks the CPU wrote, a bit each */
int gfx_init(struct SDL_Window *window);
void gfx_sync(int force);         /* follows render_scale; force: VRAM was replaced */
int gfx_scale(void);              /* the render scale in use, at least 1 */
void gfx_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi);
void gfx_vram_dirty(int x, int y, int w, int h);
void gfx_shadows_resolve(void); /* an ordering table drawn: its battle shadows onto the copy */
void gfx_depth_clear(void); /* a new ordering table: its 3D drawn over what came before */
void gfx_refresh(int x, int y, int w, int h); /* the copy from VRAM, after the CPU wrote it */
void gfx_copy(int sx, int sy, int w, int h, int dx, int dy);
void gfx_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw);
/* an RGBA image over the copy, from (x0, y0) to (x1, y1), blended by its
   alpha (or added, ADD) inside the drawing area; its texture made in *tex on first use */
typedef struct {
    float x, y, z, u, v; /* VRAM pixels, view depth, texels 0-1 */
    uint8_t r, g, b, a;
} PropVtx;
/* The props drawn from tables (gl.c's PINST_VS): the camera as gte_camera_project has it, the clock and wind */
typedef struct {
    float row[3][4];         /* view = row . (x, y, z, 1) */
    float h, ofx, ofy, wide; /* focal length, screen offset, 0.75 on the wide screen else 1 */
    float t, bright, ox, oy; /* the frame clock, the stage's brightness, the drawing offset */
    float wind[2];           /* the billboards' */
} PCam;
typedef struct {
    float pos[3];                  /* the ground point, y up */
    float w, h, sway, rot, fixed;  /* size, sway, turn (degrees), 1: flat in the world */
    uint8_t c[4];                  /* tint, fog */
} PInst;
typedef struct {
    float center[3], radius, top, fall, wind, above, size[2], floor, tint[3], fog;
    int count, leaf;
} PPart;
/* The 3D models from tables too: a mesh kept on the card (8 floats a vertex: x y z u v nx ny nz), and per instance
   its place, scale, turn, sway, fog and tint (gl.c's PINST_VS, mode 3) */
typedef struct {
    float pos[3], scale;     /* the foot, y up; the size factor */
    float rot, sway, fog, pad; /* turn (degrees), wind sway, fog share 0-1 */
    float tint[4];           /* 0-1 */
} PModel;
typedef struct {
    unsigned vao, vbo;
    int n; /* vertices, a multiple of 3 */
} GMesh;
void gfx_mesh_make(GMesh *g, const float *v, int n); /* g->n stays 0 when it can't */
void gfx_props_model(const struct SDL_Surface *img, unsigned *tex, const GMesh *g, float miny, float h, unsigned inst, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
unsigned gfx_inst_make(const PInst *v, int n);
unsigned gfx_buf_make(const void *p, unsigned long bytes);
void gfx_inst_free(unsigned b);
void gfx_props_inst(const struct SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
void gfx_props_part(const struct SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
void gfx_depth_save(void); /* the stage's depth, kept for the props (gl.c) */
void gfx_depth_models(void); /* the fighters' depth, kept for the props: the grass under a fighter is trodden down */
void gfx_props(const struct SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo);
void gfx_image(const struct SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2);
int gfx_image_prepare(const struct SDL_Surface *img, unsigned *tex);
void gfx_present(const struct SDL_Surface *menu, struct SDL_Surface **shot);
void gfx_battle_fog(int r, int g, int b); /* a battle's sky drawn, its horizon's colour (gl.c) */

#endif
