/* The OpenGL renderer's context and copy of VRAM: set up, resized with render_scale, and the card's own times */
#include "gl.h"

#define X(f) __typeof__(f) *p_##f;
GL_FUNCS(X)
#undef X
static SDL_GLContext s_ctx;

static GLint s_prop_depth_loc, s_prop_fog_loc, s_prop_fogz_loc;

static GLuint s_q[QSLOTS][QMAX];

static unsigned char s_qkind[QSLOTS][QMAX];

static int s_qn[QSLOTS], s_qslot, s_qopen = -1, s_q_made;

void gl_query_begin(int kind) {
    int n;

    if (!perf_on() || s_qopen >= 0) {
        return;
    }
    if (!s_q_made) {
        int i;

        for (i = 0; i < QSLOTS; i++) {
            GL(glGenQueries)(QMAX, s_q[i]);
        }
        s_q_made = 1;
    }
    n = s_qn[s_qslot];
    if (n >= QMAX) {
        return;
    }
    GL(glBeginQuery)(GL_TIME_ELAPSED, s_q[s_qslot][n]);
    s_qkind[s_qslot][n] = (unsigned char)kind;
    s_qopen = n;
}

void gl_query_end(void) {
    if (s_qopen >= 0) {
        GL(glEndQuery)(GL_TIME_ELAPSED);
        s_qn[s_qslot]++;
        s_qopen = -1;
    }
}

/* Once a frame, after the swap: the oldest slot's results go to the meter and it is the one used next */
void gl_query_frame(void) {
    int old = (s_qslot + 1) % QSLOTS, i;

    if (!s_q_made) {
        return;
    }
    for (i = 0; i < s_qn[old]; i++) {
        GLint ok = 0;
        GLuint64 ns = 0;

        GL(glGetQueryObjectiv)(s_q[old][i], GL_QUERY_RESULT_AVAILABLE, &ok);
        if (ok) {
            GL(glGetQueryObjectui64v)(s_q[old][i], GL_QUERY_RESULT, &ns);
            perf_gpu(s_qkind[old][i], (double)ns / 1e6);
        }
    }
    s_qn[old] = 0;
    s_qslot = old;
}

int gl_fsr3_status(void) {
    return -1;
}

int gl_scale_tied(void) {
    return 0;
}

int gl_dlss_status(void) {
    return -1;
}

int gl_scale(void) {
    return SDL_max(GLS.scale, 1);
}

GLuint gl_texture(GLint internal, int w, int h, GLenum format, GLenum type, GLint filter) {
    GLuint t;

    GL(glGenTextures)(1, &t);
    GL(glBindTexture)(GL_TEXTURE_2D, t);
    GL(glTexImage2D)(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, NULL);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint gl_framebuffer(GLuint tex) {
    GLuint f;

    GL(glGenFramebuffers)(1, &f);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, f);
    GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    GLenum status = GL(glCheckFramebufferStatus)(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        LOG("gl: framebuffer incomplete (%x, error %x, scale %d)", status, GL(glGetError)(), GLS.scale);
    }
    return f;
}

/* Follows render_scale: a new copy, made from the 1x VRAM; force: VRAM
   changed under it (a loaded state) */
void gl_sync(int force) {
    int want = SDL_clamp(g_res_scale, 1, GLS.max_scale);

    if (GLS.main == 0 || g_vram == NULL) { /* VRAM comes with ResetGraph */
        return;
    }
    if (want != GLS.scale) {
        gl_flush();
        if (GLS.fbo) {
            GL(glDeleteFramebuffers)(1, &GLS.fbo);
            GL(glDeleteTextures)(1, &GLS.fbo_tex);
            GL(glDeleteRenderbuffers)(1, &GLS.fbo_depth);
            GL(glDeleteFramebuffers)(1, &GLS.tmp_fbo);
            GL(glDeleteTextures)(1, &GLS.tmp_tex);
            GL(glDeleteFramebuffers)(1, &GLS.mask_fbo);
            GL(glDeleteTextures)(1, &GLS.mask_tex);
            GL(glDeleteFramebuffers)(1, &GLS.blur_fbo);
            GL(glDeleteTextures)(1, &GLS.blur_tex);
            GL(glDeleteFramebuffers)(1, &GLS.sd_fbo);
            GL(glDeleteTextures)(1, &GLS.sd_tex);
            GL(glDeleteFramebuffers)(1, &GLS.fd_fbo);
            GL(glDeleteTextures)(1, &GLS.fd_tex);
        }
        GLS.sd_valid = 0, GLS.fd_valid = 0;
        GLS.scale = want;
        GLS.fbo_tex = gl_texture(GL_RGBA8, VRAM_W * want, VRAM_H * want, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        GLS.fbo = gl_framebuffer(GLS.fbo_tex);
        GL(glGenRenderbuffers)(1, &GLS.fbo_depth);
        GL(glBindRenderbuffer)(GL_RENDERBUFFER, GLS.fbo_depth);
        GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, VRAM_W * want, VRAM_H * want); /* the stencil: battle shadows */
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.fbo);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, GLS.fbo_depth);
        GLS.tmp_tex = gl_texture(GL_RGBA8, VRAM_W * want, VRAM_H * want, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
        GLS.tmp_fbo = gl_framebuffer(GLS.tmp_tex);
        GLS.mask_tex = gl_texture(GL_R8, VRAM_W * want, VRAM_H * want, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
        GLS.mask_fbo = gl_framebuffer(GLS.mask_tex);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, GLS.fbo_depth);
        GLS.blur_tex = gl_texture(GL_R8, VRAM_W * want, VRAM_H * want, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
        GLS.blur_fbo = gl_framebuffer(GLS.blur_tex);
        GLS.sd_tex = gl_texture(GL_DEPTH24_STENCIL8, VRAM_W * want, VRAM_H * want, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, GL_NEAREST);
        GL(glGenFramebuffers)(1, &GLS.sd_fbo);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.sd_fbo);
        GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, GLS.sd_tex, 0);
        GLS.fd_tex = gl_texture(GL_DEPTH24_STENCIL8, VRAM_W * want, VRAM_H * want, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, GL_NEAREST);
        GL(glGenFramebuffers)(1, &GLS.fd_fbo);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.fd_fbo);
        GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, GLS.fd_tex, 0);
        GL(glClearColor)(0, 0, 0, 0);
        GL(glClear)(GL_COLOR_BUFFER_BIT);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.mask_fbo);
        GL(glClear)(GL_COLOR_BUFFER_BIT);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    } else if (!force) {
        return;
    }
    soft_all();
    gl_refresh(0, 0, VRAM_W, VRAM_H);
}

uint64_t gl_window_flags(void) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    return SDL_WINDOW_OPENGL;
}

int gl_init(SDL_Window *window) {
    GLint max = 0;

    GLS.window = window;
    if ((GLS.batch = SDL_malloc(BATCH * sizeof(GVtx))) == NULL) {
        return 0;
    }
    if ((s_ctx = SDL_GL_CreateContext(window)) == NULL) {
        LOG("gl: %s", SDL_GetError());
        return 0;
    }
#define X(f)                                                         \
    if ((p_##f = (__typeof__(f) *)SDL_GL_GetProcAddress(#f)) == NULL) { \
        LOG("gl: no %s", #f);                                        \
        return 0;                                                    \
    }
    GL_FUNCS(X)
#undef X
    if ((GLS.main = gl_program(MAIN_VS, MAIN_FS)) == 0 || (GLS.blit = gl_program(BLIT_VS, BLIT_FS)) == 0 ||
        (GLS.soft = gl_program(BLIT_VS, SOFT_FS)) == 0 || (GLS.prop = gl_program(PROP_VS, PROP_FS)) == 0 || (GLS.pinst = gl_program(PINST_VS, PROP_FS)) == 0) {
        return 0;
    }
    GLS.soft_dir_loc = GL(glGetUniformLocation)(GLS.soft, "dir");
    GLS.soft_str_loc = GL(glGetUniformLocation)(GLS.soft, "strength");
    GL(glUseProgram)(GLS.soft);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.soft, "src"), 0);
    GLS.pass_loc = GL(glGetUniformLocation)(GLS.main, "pass");
    GLS.scale_loc = GL(glGetUniformLocation)(GLS.main, "scale");
    GLS.size_loc = GL(glGetUniformLocation)(GLS.main, "size");
    GLS.true_loc = GL(glGetUniformLocation)(GLS.main, "truecolor");
    GLS.persp_loc = GL(glGetUniformLocation)(GLS.main, "persp");
    GLS.smooth_loc = GL(glGetUniformLocation)(GLS.main, "smooth3d");
    GLS.fog_loc = GL(glGetUniformLocation)(GLS.main, "fog");
    GLS.fogz_loc = GL(glGetUniformLocation)(GLS.main, "fogz");
    GLS.jit_loc = GL(glGetUniformLocation)(GLS.main, "jit");
    GL(glUseProgram)(GLS.main);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.main, "vram"), 0);
    GL(glUniform2f)(GLS.size_loc, VRAM_W, VRAM_H);
    GL(glUseProgram)(GLS.blit);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.blit, "tex"), 0);
    GL(glUseProgram)(GLS.prop);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.prop, "tex"), 0);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.prop, "sdepth"), 1);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.prop, "fdepth"), 2);
    GL(glUniform2f)(GL(glGetUniformLocation)(GLS.prop, "size"), VRAM_W, VRAM_H);
    s_prop_depth_loc = GL(glGetUniformLocation)(GLS.prop, "usedepth");
    s_prop_fog_loc = GL(glGetUniformLocation)(GLS.prop, "fog");
    s_prop_fogz_loc = GL(glGetUniformLocation)(GLS.prop, "fogz");
    GL(glUseProgram)(GLS.pinst);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.pinst, "tex"), 0);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.pinst, "sdepth"), 1);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.pinst, "fdepth"), 2);
    GL(glUniform2f)(GL(glGetUniformLocation)(GLS.pinst, "size"), VRAM_W, VRAM_H);

    GL(glGenVertexArrays)(1, &GLS.vao);
    GL(glBindVertexArray)(GLS.vao);
    GL(glGenBuffers)(1, &GLS.vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.vbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 4, GL_FLOAT, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, x));
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(1, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, r));
    GL(glEnableVertexAttribArray)(2);
    GL(glVertexAttribIPointer)(2, 4, GL_UNSIGNED_SHORT, sizeof(GVtx), (void *)offsetof(GVtx, clut_x));
    GL(glEnableVertexAttribArray)(3);
    GL(glVertexAttribIPointer)(3, 4, GL_UNSIGNED_SHORT, sizeof(GVtx), (void *)offsetof(GVtx, umin));
    GL(glEnableVertexAttribArray)(4);
    GL(glVertexAttribIPointer)(4, 4, GL_UNSIGNED_SHORT, sizeof(GVtx), (void *)offsetof(GVtx, twmx));
    GL(glEnableVertexAttribArray)(5);
    GL(glVertexAttribIPointer)(5, 1, GL_UNSIGNED_INT, sizeof(GVtx), (void *)offsetof(GVtx, mode));
    GL(glEnableVertexAttribArray)(6);
    GL(glVertexAttribPointer)(6, 1, GL_FLOAT, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, z));
    GL(glEnableVertexAttribArray)(7);
    GL(glVertexAttribPointer)(7, 1, GL_FLOAT, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, w));

    GL(glGenVertexArrays)(1, &GLS.prop_vao);
    GL(glBindVertexArray)(GLS.prop_vao);
    GL(glGenBuffers)(1, &GLS.prop_vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.prop_vbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, sizeof(PropVtx), (void *)offsetof(PropVtx, x));
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(1, 2, GL_FLOAT, GL_FALSE, sizeof(PropVtx), (void *)offsetof(PropVtx, u));
    GL(glEnableVertexAttribArray)(2);
    GL(glVertexAttribPointer)(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PropVtx), (void *)offsetof(PropVtx, r));

    GL(glGenVertexArrays)(1, &GLS.inst_vao);
    GL(glGenVertexArrays)(1, &GLS.part_vao); /* no attributes: a particle is its number */

    GL(glGenVertexArrays)(1, &GLS.quad_vao);
    GL(glBindVertexArray)(GLS.quad_vao);
    GL(glGenBuffers)(1, &GLS.quad_vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.quad_vbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 4, GL_FLOAT, GL_FALSE, 16, NULL);

    GLS.vram_tex = gl_texture(GL_R16UI, VRAM_W, VRAM_H, GL_RED_INTEGER, GL_UNSIGNED_SHORT, GL_NEAREST);
    GLS.movie_tex = gl_texture(GL_RGBA8, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    GLS.menu_tex = gl_texture(GL_RGBA8, 1280, 960, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 1);
    if (!gl_post_init()) {
        return 0;
    }
    GL(glGetIntegerv)(GL_MAX_TEXTURE_SIZE, &max);
    GLS.max_scale = SDL_clamp(max / VRAM_W, 1, 8);
    if (GL(glGetError)() != GL_NO_ERROR) {
        LOG("gl: setting up failed");
        return 0;
    }
    return 1;
}

const struct Gfx GFX_GL = GFX_TABLE(gl, "OpenGL");

struct GlState GLS = {.mask_box = {VRAM_W, VRAM_H, -1, -1}, .max_scale = 8, .semi = -1, .clip = {0, 0, 1023, 511}};
