/* The OpenGL 3.3 renderer's own: what its files share. The GPU's drawing on the graphics card, at render_scale times
   the PSX's resolution. gpu_draw.c still draws every primitive into VRAM in software, at 1x: that VRAM is what the
   game reads back and what the textures come from (uploaded here as an integer texture, CLUTs looked up in the
   shader). The card draws the same primitives, finer, into its own copy of VRAM, which is what the screen shows. */
#ifndef GL_BACKEND_H
#define GL_BACKEND_H

#define RECT PSX_RECT /* windows.h, through SDL_opengl.h, has its own */
#include "../platform.h"
#include "../gfx.h"
#undef RECT
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

GFX_DECLARE(gl) /* renderer.h's, gl_* (gfx.c calls them through GFX_GL) */

/* --- the functions, through SDL (opengl32 only exports 1.1) --- */
#define GL_FUNCS(X)                                                                                                  \
    X(glGetError) X(glGetIntegerv) X(glColorMask) X(glEnable) X(glDisable) X(glViewport) X(glScissor) X(glClear) X(glClearColor)   \
    X(glGenTextures) X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexSubImage2D) X(glTexParameteri)     \
    X(glPixelStorei) X(glReadPixels) X(glGetTexImage) X(glDrawArrays) X(glActiveTexture) X(glBlendFuncSeparate) X(glBlendEquation)   \
    X(glBlendColor) X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog)  \
    X(glCreateProgram) X(glAttachShader) X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glUseProgram) \
    X(glGetUniformLocation) X(glUniform1i) X(glUniform1f) X(glUniform2f) X(glUniform4f) X(glUniform2i) X(glGenVertexArrays) X(glBindVertexArray)  \
    X(glGenBuffers) X(glBindBuffer) X(glBufferData) X(glEnableVertexAttribArray) X(glVertexAttribPointer)           \
    X(glVertexAttribIPointer) X(glGenFramebuffers) X(glDeleteFramebuffers) X(glBindFramebuffer)                     \
    X(glFramebufferTexture2D) X(glCheckFramebufferStatus) X(glBlitFramebuffer)                                  \
    X(glGenRenderbuffers) X(glDeleteRenderbuffers) X(glBindRenderbuffer) X(glRenderbufferStorage)                \
    X(glFramebufferRenderbuffer) X(glDepthFunc) X(glDepthMask) X(glClearDepth) X(glClearStencil)                  \
    X(glStencilFunc) X(glStencilOp) X(glPolygonOffset) X(glGenerateMipmap) X(glDrawArraysInstanced)              \
    X(glVertexAttribDivisor) X(glDeleteBuffers) X(glGenQueries) X(glBeginQuery) X(glEndQuery)      \
    X(glGetQueryObjectiv) X(glGetQueryObjectui64v)

#define X(f) extern __typeof__(f) *p_##f;
GL_FUNCS(X)
#undef X
#define GL(f) p_##f

#define SMAA_HEAD(vs, ps)                                                                                    \
    "#version 330 core\nuniform vec4 rt;\n#define SMAA_RT_METRICS rt\n#define SMAA_GLSL_3\n#define SMAA_PRESET_HIGH\n" \
    "#define SMAA_INCLUDE_VS " #vs "\n#define SMAA_INCLUDE_PS " #ps "\n"
/* A fighter treads the grass under it: a blade whose foot is this close (the game's units, in depth) to the fighter's
   surface is flattened, fully by 40% of it */
#define PROP_CLEAR 600.0f
/* The card's own times (the battle's performance meter, perf.c): a timer query around each batch and each props draw,
   read back a few frames later so nothing waits for them */
#define QSLOTS 4
#define QMAX 600
#define BATCH 6144

struct GlState {
    SDL_Window *window;
    GLuint main, blit, post, vao, vbo, quad_vao, quad_vbo;
    GLint pass_loc, scale_loc, size_loc, true_loc, persp_loc, smooth_loc, fog_loc, fogz_loc, jit_loc;
    int data_depth, data_jitter, data_view, data_selftest; /* selftest: a known depth written in, to check the pass */
    GLuint lin, lin_tex, lin_fbo;
    float jit_px[2];
    unsigned jit_n;
    GLuint vram_tex; /* the 1x VRAM, R16UI */
    GLuint fbo, fbo_tex; /* the copy, render_scale times bigger */
    GLuint fbo_depth; /* its depth buffer, for the 3D models (g_depth) */
    GLuint smaa, easu, area_tex, search_tex; /* their programs (0: not on this card) and SMAA's tables */
    GLuint pic_tex[3], pic_fbo[3], up_tex, up_fbo; /* the display area copied and SMAA's two; FSR's output */
    int pic_w, pic_h, up_w, up_h, frame_3d; /* GLS.frame_3d: the frame has 3D in it */
    int deep; /* the batch is by the depth buffer */
    float bg[3]; /* the battle's background colour this frame, 5 bits; GLS.fog_on: drawn */
    float fog[3], fog_amount, fog_from; /* the fog shown: colour (5 bits), at the far side, where it starts */
    int fog_on, fog_shown;
    float zfar, zframe; /* the farthest opaque 3D: smoothed, and this frame's */
    GLuint tmp_fbo, tmp_tex; /* for a VRAM to VRAM copy */
    GLuint mask_fbo, mask_tex, blur_fbo, blur_tex; /* battle shadows: where they fall, blurred */
    GLuint soft;
    GLuint prop, prop_vao, prop_vbo, sd_tex, sd_fbo; /* props; the stage's depth saved */
    GLuint pinst, inst_vao, part_vao; /* the props from tables (PINST_VS) */
    int sd_valid;
    int sd_org[2], fd_org[2]; /* the VRAM corner of the buffer each saved depth was drawn in */
    GLuint fd_tex, fd_fbo; /* the fighters' depth, kept for the props */
    int fd_valid;
    GLint soft_dir_loc, soft_str_loc;
    int mask_box[4]; /* what the mask holds, in VRAM pixels */
    GLuint movie_tex, menu_tex;
    int scale, max_scale;
    GVtx *batch; /* malloc'd: the image has to stay under 16 MB (main.c) */
    int count;
    int semi; /* the batch's semi-transparency mode, -1: none in it yet */
    int clip[4]; /* the drawing area the batch was made with */
};
extern struct GlState GLS;

extern const char *MAIN_VS;
extern const char *MAIN_FS;
extern const char *BLIT_VS;
extern const char *BLIT_FS;
extern const char *PROP_VS;
extern const char *PROP_FS;
extern const char *PINST_VS;
extern const char *SOFT_FS;
void gl_query_begin(int kind);
void gl_query_end(void);
void gl_query_frame(void);
GLuint gl_program_of(const char *const *vs, const char *const *fs, int n);
GLuint gl_program(const char *vs, const char *fs);
GLuint gl_texture(GLint internal, int w, int h, GLenum format, GLenum type, GLint filter);
GLuint gl_framebuffer(GLuint tex);
void gl_flush(void);
int gl_post_init(void);

#endif
