/* The GPU's drawing on the graphics card (OpenGL 3.3), at render_scale
   times the PSX's resolution. gpu_draw.c still draws every primitive into
   VRAM in software, at 1x: that VRAM is what the game reads back and what
   the textures come from (uploaded here as an integer texture, CLUTs looked
   up in the shader). The card draws the same primitives, finer, into its
   own copy of VRAM, which is what the screen shows. */
#define RECT PSX_RECT /* windows.h, through SDL_opengl.h, has its own */
#include "platform.h"
#undef RECT
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

/* --- the functions, through SDL (opengl32 only exports 1.1) --- */
#define GL_FUNCS(X)                                                                                                  \
    X(glGetError) X(glGetIntegerv) X(glColorMask) X(glEnable) X(glDisable) X(glViewport) X(glScissor) X(glClear) X(glClearColor)   \
    X(glGenTextures) X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexSubImage2D) X(glTexParameteri)     \
    X(glPixelStorei) X(glReadPixels) X(glDrawArrays) X(glActiveTexture) X(glBlendFuncSeparate) X(glBlendEquation)   \
    X(glBlendColor) X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog)  \
    X(glCreateProgram) X(glAttachShader) X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glUseProgram) \
    X(glGetUniformLocation) X(glUniform1i) X(glUniform1f) X(glUniform2f) X(glGenVertexArrays) X(glBindVertexArray)  \
    X(glGenBuffers) X(glBindBuffer) X(glBufferData) X(glEnableVertexAttribArray) X(glVertexAttribPointer)           \
    X(glVertexAttribIPointer) X(glGenFramebuffers) X(glDeleteFramebuffers) X(glBindFramebuffer)                     \
    X(glFramebufferTexture2D) X(glCheckFramebufferStatus) X(glBlitFramebuffer)

#define X(f) static __typeof__(f) *p_##f;
GL_FUNCS(X)
#undef X
#define GL(f) p_##f

/* --- shaders --- */

static const char *MAIN_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec4 pos_uv;\n" /* VRAM pixels, texels */
    "layout(location = 1) in vec3 color;\n"
    "layout(location = 2) in uvec4 tex;\n" /* CLUT x, y, page x, y */
    "layout(location = 3) in uvec4 limits;\n" /* u, v min, max */
    "layout(location = 4) in uvec4 window;\n" /* texture window mask x, y, offset x, y */
    "layout(location = 5) in uint mode;\n"
    "uniform vec2 size;\n" /* VRAM's, 1024x512 */
    "noperspective out vec2 uv;\n"
    "noperspective out vec3 col;\n"
    "flat out uvec4 t, lim, tw;\n"
    "flat out uint md;\n"
    "void main() {\n"
    "    gl_Position = vec4(pos_uv.xy / size * 2.0 - 1.0, 0.0, 1.0);\n"
    "    uv = pos_uv.zw; col = color; t = tex; lim = limits; tw = window; md = mode;\n"
    "}\n";

/* mode: 2 textured, 4 raw texture, 8 semi-transparent, 16-48 texel depth,
   64 dithered, 128 the copy of VRAM itself, 256 sets the mask bit */
static const char *MAIN_FS =
    "#version 330 core\n"
    "uniform usampler2D vram;\n"
    "uniform int pass;\n" /* 0 everything, 1 all but the semi-transparent, 2 only those */
    "uniform float scale;\n"
    "noperspective in vec2 uv;\n"
    "noperspective in vec3 col;\n"
    "flat in uvec4 t, lim, tw;\n"
    "flat in uint md;\n"
    "out vec4 frag;\n"
    "const float DITHER[16] = float[16](-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0);\n"
    "uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "vec3 rgb5(uint p) { return vec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u); }\n"
    "void main() {\n"
    "    uint pix = 0u;\n"
    "    bool semi = (md & 8u) != 0u;\n"
    "    vec3 c8 = col, c5;\n"
    "    if ((md & 128u) != 0u) {\n"
    "        pix = fetch(int(gl_FragCoord.x / scale), int(gl_FragCoord.y / scale));\n"
    "        frag = vec4(rgb5(pix) / 31.0, float(pix >> 15));\n"
    "        return;\n"
    "    }\n"
    "    if ((md & 2u) != 0u) {\n"
    /* at the pixel's corner, where the PSX samples: mirrored textures keep their texels */
    "        vec2 at = uv - 0.5 * dFdx(uv) - 0.5 * dFdy(uv);\n"
    "        int u = clamp(int(floor(at.x + 0.002)), int(lim.x), int(lim.z)) & 255;\n"
    "        int v = clamp(int(floor(at.y + 0.002)), int(lim.y), int(lim.w)) & 255;\n"
    "        u = (u & ~int(tw.x)) | (int(tw.z) & int(tw.x));\n"
    "        v = (v & ~int(tw.y)) | (int(tw.w) & int(tw.y));\n"
    "        uint depth = (md >> 4) & 3u;\n"
    "        if (depth == 0u) {\n"
    "            uint w = fetch(int(t.z) + u / 4, int(t.w) + v);\n"
    "            pix = fetch(int(t.x) + int((w >> uint((u & 3) * 4)) & 15u), int(t.y));\n"
    "        } else if (depth == 1u) {\n"
    "            uint w = fetch(int(t.z) + u / 2, int(t.w) + v);\n"
    "            pix = fetch(int(t.x) + int((w >> uint((u & 1) * 8)) & 255u), int(t.y));\n"
    "        } else {\n"
    "            pix = fetch(int(t.z) + u, int(t.w) + v);\n"
    "        }\n"
    "        if (pix == 0u) discard;\n"
    "        semi = semi && (pix & 0x8000u) != 0u;\n"
    "        c8 = floor(rgb5(pix) * col / 16.0);\n"
    "    }\n"
    "    if ((pass == 1 && semi) || (pass == 2 && !semi)) discard;\n"
    "    if ((md & 6u) == 6u) {\n"
    "        c5 = rgb5(pix);\n"
    "    } else {\n"
    "        float d = (md & 64u) != 0u ? DITHER[(int(gl_FragCoord.y) & 3) * 4 + (int(gl_FragCoord.x) & 3)] : 0.0;\n"
    "        c5 = clamp(floor((c8 + d) / 8.0), 0.0, 31.0);\n"
    "    }\n"
    "    frag = vec4(c5 / 31.0, (md & 256u) != 0u || (pix & 0x8000u) != 0u ? 1.0 : 0.0);\n"
    "}\n";

/* The screen: a texture on a quad */
static const char *BLIT_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec4 pos_uv;\n"
    "out vec2 uv;\n"
    "void main() { gl_Position = vec4(pos_uv.xy, 0.0, 1.0); uv = pos_uv.zw; }\n";

static const char *BLIT_FS =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "void main() { frag = texture(tex, uv); }\n";

/* --- state --- */

static SDL_Window *s_window;
static SDL_GLContext s_ctx;
static GLuint s_main, s_blit, s_vao, s_vbo, s_quad_vao, s_quad_vbo;
static GLint s_pass_loc, s_scale_loc, s_size_loc;
static GLuint s_vram_tex;            /* the 1x VRAM, R16UI */
static GLuint s_fbo, s_fbo_tex;      /* the copy, render_scale times bigger */
static GLuint s_tmp_fbo, s_tmp_tex;  /* for a VRAM to VRAM copy */
static GLuint s_movie_tex, s_menu_tex;
int g_res_scale = 1; /* native.ini, the menu */
static int s_scale, s_max_scale = 8;
uint16_t g_vram_dirty[8]; /* 64x64 blocks of VRAM written since they were uploaded */

#define BATCH 6144
static GVtx *s_batch; /* malloc'd: the image has to stay under 16 MB (main.c) */
static int s_count;
static int s_semi = -1;        /* the batch's semi-transparency mode, -1: none in it yet */
static int s_clip[4] = {0, 0, 1023, 511}; /* the drawing area the batch was made with */

static GLuint shader(GLenum type, const char *src) {
    GLuint s = GL(glCreateShader)(type);
    GLint ok;
    char log[1024];

    GL(glShaderSource)(s, 1, &src, NULL);
    GL(glCompileShader)(s);
    GL(glGetShaderiv)(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GL(glGetShaderInfoLog)(s, sizeof log, NULL, log);
        LOG("gl: shader: %s", log);
    }
    return s;
}

static GLuint program(const char *vs, const char *fs) {
    GLuint p = GL(glCreateProgram)();
    GLint ok;
    char log[1024];

    GL(glAttachShader)(p, shader(GL_VERTEX_SHADER, vs));
    GL(glAttachShader)(p, shader(GL_FRAGMENT_SHADER, fs));
    GL(glLinkProgram)(p);
    GL(glGetProgramiv)(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GL(glGetProgramInfoLog)(p, sizeof log, NULL, log);
        LOG("gl: program: %s", log);
        return 0;
    }
    return p;
}

static GLuint texture(GLint internal, int w, int h, GLenum format, GLenum type, GLint filter) {
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

static GLuint framebuffer(GLuint tex) {
    GLuint f;

    GL(glGenFramebuffers)(1, &f);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, f);
    GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (GL(glCheckFramebufferStatus)(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG("gl: framebuffer incomplete");
    }
    return f;
}

/* A rect of the 1x VRAM onto the card */
static void upload(int x, int y, int w, int h) {
    GL(glBindTexture)(GL_TEXTURE_2D, s_vram_tex);
    GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, VRAM_W);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 2);
    GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED_INTEGER, GL_UNSIGNED_SHORT, &g_vram[y * VRAM_W + x]);
    GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
}

/* The dirty blocks among `need` (a mask per block row), as runs of a row */
static void upload_blocks(const uint16_t need[8]) {
    int row, col;

    for (row = 0; row < 8; row++) {
        uint16_t bits = need[row] & g_vram_dirty[row];

        for (col = 0; col < 16; col++) {
            if (bits >> col & 1) {
                int start = col;

                while (col < 16 && (bits >> col & 1)) {
                    col++;
                }
                upload(start * 64, row * 64, (col - start) * 64, 64);
            }
        }
        g_vram_dirty[row] &= ~need[row];
    }
}

static void need_rect(uint16_t need[8], int x, int y, int w, int h) {
    int r, c;

    for (r = (y & 511) >> 6; r <= ((y + h - 1) & 511) >> 6 && r < 8; r++) {
        for (c = (x & 1023) >> 6; c <= ((x + w - 1) & 1023) >> 6 && c < 16; c++) {
            need[r] |= (uint16_t)(1u << c);
        }
    }
}

void gl_vram_dirty(int x, int y, int w, int h) {
    need_rect(g_vram_dirty, x, y, w, h);
}

/* The batch, in its two passes when some of it is semi-transparent: the
   opaque pixels first, then the blended ones over them */
static void flush(void) {
    int s = s_scale;

    if (s_count == 0) {
        return;
    }
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_fbo);
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(s_clip[0] * s, s_clip[1] * s, (s_clip[2] - s_clip[0] + 1) * s, (s_clip[3] - s_clip[1] + 1) * s);
    GL(glUseProgram)(s_main);
    GL(glBindVertexArray)(s_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)(s_count * sizeof(GVtx)), s_batch, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, s_vram_tex);
    GL(glUniform1f)(s_scale_loc, (float)s);
    GL(glDisable)(GL_BLEND);
    GL(glUniform1i)(s_pass_loc, s_semi < 0 ? 0 : 1);
    GL(glDrawArrays)(GL_TRIANGLES, 0, s_count);
    if (s_semi >= 0) {
        GL(glEnable)(GL_BLEND);
        GL(glBlendColor)(0.25f, 0.25f, 0.25f, 0.5f);
        switch (s_semi) {
        case 0: /* (back + front) / 2 */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA, GL_ONE, GL_ZERO);
            break;
        case 1: /* back + front */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
            break;
        case 2: /* back - front */
            GL(glBlendEquation)(GL_FUNC_REVERSE_SUBTRACT);
            GL(glBlendFuncSeparate)(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            break;
        default: /* back + front / 4 */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_CONSTANT_COLOR, GL_ONE, GL_ONE, GL_ZERO);
            break;
        }
        GL(glUniform1i)(s_pass_loc, 2);
        GL(glDrawArrays)(GL_TRIANGLES, 0, s_count);
        GL(glDisable)(GL_BLEND);
        GL(glBlendEquation)(GL_FUNC_ADD);
    }
    GL(glDisable)(GL_SCISSOR_TEST);
    s_count = 0;
    s_semi = -1;
}

/* Triangles (n a multiple of 3) for the copy, with gpu_draw.c's state: the
   drawing area, the semi-transparency mode */
void gl_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {
    int semi_prim = (v[0].mode & 8) != 0;

    if (s_scale == 0) {
        return;
    }
    if (s_count + n > BATCH || x1 != s_clip[0] || y1 != s_clip[1] || x2 != s_clip[2] || y2 != s_clip[3] ||
        (semi_prim && s_semi >= 0 && semi != s_semi) || (!semi_prim && s_semi >= 0)) {
        flush();
        s_clip[0] = x1, s_clip[1] = y1, s_clip[2] = x2, s_clip[3] = y2;
    }
    if (v[0].mode & 2) {
        /* the page and the CLUT as the card has them: what the CPU drew there
           since (a texture drawn to) goes up first */
        uint16_t need[8] = {0};
        int depth = v[0].mode >> 4 & 3;

        need_rect(need, v[0].tx, v[0].ty, 64 << depth, 256);
        need_rect(need, v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        if ((need[0] & g_vram_dirty[0]) | (need[1] & g_vram_dirty[1]) | (need[2] & g_vram_dirty[2]) | (need[3] & g_vram_dirty[3]) |
            (need[4] & g_vram_dirty[4]) | (need[5] & g_vram_dirty[5]) | (need[6] & g_vram_dirty[6]) | (need[7] & g_vram_dirty[7])) {
            flush();
            upload_blocks(need);
        }
    }
    if (semi_prim) {
        s_semi = semi;
    }
    SDL_memcpy(&s_batch[s_count], v, n * sizeof(GVtx));
    s_count += n;
}

/* A quad of the copy that shows the 1x VRAM under it: after the CPU wrote
   VRAM (an image, a fill) */
void gl_refresh(int x, int y, int w, int h) {
    GVtx q[6];
    int i;

    if (s_scale == 0 || w <= 0 || h <= 0) {
        return;
    }
    flush();
    if (x + w > VRAM_W) w = VRAM_W - x;
    if (y + h > VRAM_H) h = VRAM_H - y;
    upload(x, y, w, h);
    SDL_memset(q, 0, sizeof q);
    for (i = 0; i < 6; i++) {
        static const int CX[6] = {0, 1, 0, 1, 1, 0}, CY[6] = {0, 0, 1, 0, 1, 1};

        q[i].x = (float)(x + CX[i] * w);
        q[i].y = (float)(y + CY[i] * h);
        q[i].mode = 128;
    }
    SDL_memcpy(s_batch, q, sizeof q);
    s_count = 6;
    s_clip[0] = 0, s_clip[1] = 0, s_clip[2] = 1023, s_clip[3] = 511;
    flush();
}

/* VRAM to VRAM, in the copy */
void gl_copy(int sx, int sy, int w, int h, int dx, int dy) {
    int s = s_scale;

    if (s == 0 || w <= 0 || h <= 0) {
        return;
    }
    flush();
    if (sx + w > VRAM_W) w = VRAM_W - sx;
    if (dx + w > VRAM_W) w = VRAM_W - dx;
    if (sy + h > VRAM_H) h = VRAM_H - sy;
    if (dy + h > VRAM_H) h = VRAM_H - dy;
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, s_fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, s_tmp_fbo);
    GL(glBlitFramebuffer)(sx * s, sy * s, (sx + w) * s, (sy + h) * s, 0, 0, w * s, h * s, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, s_tmp_fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, s_fbo);
    GL(glBlitFramebuffer)(0, 0, w * s, h * s, dx * s, dy * s, (dx + w) * s, (dy + h) * s, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
}

/* Follows render_scale: a new copy, made from the 1x VRAM; force: VRAM
   changed under it (a loaded state) */
void gl_sync(int force) {
    int want = SDL_clamp(g_res_scale, 1, s_max_scale);

    if (s_main == 0 || g_vram == NULL) { /* VRAM comes with ResetGraph */
        return;
    }
    if (want != s_scale) {
        flush();
        if (s_fbo) {
            GL(glDeleteFramebuffers)(1, &s_fbo);
            GL(glDeleteTextures)(1, &s_fbo_tex);
            GL(glDeleteFramebuffers)(1, &s_tmp_fbo);
            GL(glDeleteTextures)(1, &s_tmp_tex);
        }
        s_scale = want;
        s_fbo_tex = texture(GL_RGBA8, VRAM_W * want, VRAM_H * want, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        s_fbo = framebuffer(s_fbo_tex);
        s_tmp_tex = texture(GL_RGBA8, VRAM_W * want, VRAM_H * want, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
        s_tmp_fbo = framebuffer(s_tmp_tex);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    } else if (!force) {
        return;
    }
    gl_refresh(0, 0, VRAM_W, VRAM_H);
}

int gl_init(SDL_Window *window) {
    GLint max = 0;

    s_window = window;
    if ((s_batch = SDL_malloc(BATCH * sizeof(GVtx))) == NULL) {
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
    SDL_GL_SetSwapInterval(0); /* the game keeps its own time (etc.c) */
    if ((s_main = program(MAIN_VS, MAIN_FS)) == 0 || (s_blit = program(BLIT_VS, BLIT_FS)) == 0) {
        return 0;
    }
    s_pass_loc = GL(glGetUniformLocation)(s_main, "pass");
    s_scale_loc = GL(glGetUniformLocation)(s_main, "scale");
    s_size_loc = GL(glGetUniformLocation)(s_main, "size");
    GL(glUseProgram)(s_main);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_main, "vram"), 0);
    GL(glUniform2f)(s_size_loc, VRAM_W, VRAM_H);
    GL(glUseProgram)(s_blit);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_blit, "tex"), 0);

    GL(glGenVertexArrays)(1, &s_vao);
    GL(glBindVertexArray)(s_vao);
    GL(glGenBuffers)(1, &s_vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_vbo);
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

    GL(glGenVertexArrays)(1, &s_quad_vao);
    GL(glBindVertexArray)(s_quad_vao);
    GL(glGenBuffers)(1, &s_quad_vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_quad_vbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 4, GL_FLOAT, GL_FALSE, 16, NULL);

    s_vram_tex = texture(GL_R16UI, VRAM_W, VRAM_H, GL_RED_INTEGER, GL_UNSIGNED_SHORT, GL_NEAREST);
    s_movie_tex = texture(GL_RGBA8, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    s_menu_tex = texture(GL_RGBA8, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
    GL(glGetIntegerv)(GL_MAX_TEXTURE_SIZE, &max);
    s_max_scale = SDL_clamp(max / VRAM_W, 1, 8);
    if (GL(glGetError)() != GL_NO_ERROR) {
        LOG("gl: setting up failed");
        return 0;
    }
    return 1;
}

/* A texture onto the window's 4:3 area: uv from (u0, v0) top left to (u1, v1) */
static void quad(GLuint tex, const int vp[4], float u0, float v0, float u1, float v1, int linear) {
    float q[16] = {-1, 1, u0, v0, 1, 1, u1, v0, -1, -1, u0, v1, 1, -1, u1, v1};

    GL(glViewport)(vp[0], vp[1], vp[2], vp[3]);
    GL(glUseProgram)(s_blit);
    GL(glBindVertexArray)(s_quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, tex);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
}

/* The frame: the display area of the copy (or the 1x VRAM's 24-bit movie)
   letterboxed to 4:3, then the menu (a 640x480 RGBA surface, or NULL) */
void gl_present(const SDL_Surface *menu, SDL_Surface **shot) {
    PSX_RECT d = g_disp.disp;
    int w, h, vp[4];

    flush();
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    vp[2] = w * 3 > h * 4 ? h * 4 / 3 : w;
    vp[3] = w * 3 > h * 4 ? h : w * 3 / 4;
    vp[0] = (w - vp[2]) / 2;
    vp[1] = (h - vp[3]) / 2;
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    GL(glViewport)(0, 0, w, h);
    GL(glClearColor)(0, 0, 0, 1);
    GL(glClear)(GL_COLOR_BUFFER_BIT);
    GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); /* the copy's alpha is the mask bit: the window stays opaque */
    if (d.x + d.w > VRAM_W) d.w = VRAM_W - d.x;
    if (d.y + d.h > VRAM_H) d.h = VRAM_H - d.y;
    if (g_disp_on && d.w > 0 && d.h > 0 && g_disp.isrgb24) {
        /* 24-bit (the movies): d.w pixels of 3 bytes from d.x on, as RGBA */
        static uint32_t *rgba;
        int x, y;

        if (rgba == NULL && (rgba = SDL_malloc(VRAM_W * VRAM_H * 4)) == NULL) {
            return;
        }
        for (y = 0; y < d.h; y++) {
            const uint8_t *b = (const uint8_t *)&g_vram[(d.y + y) * VRAM_W + d.x];

            for (x = 0; x < d.w && (d.x * 2 + x * 3 + 3) <= VRAM_W * 2; x++) {
                rgba[y * d.w + x] = (uint32_t)(b[x * 3] | b[x * 3 + 1] << 8 | b[x * 3 + 2] << 16 | 0xFF000000u);
            }
        }
        GL(glBindTexture)(GL_TEXTURE_2D, s_movie_tex);
        GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
        GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, d.w, d.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        quad(s_movie_tex, vp, 0, 0, d.w / (float)VRAM_W, d.h / (float)VRAM_H, g_smooth_movies);
    } else if (g_disp_on && d.w > 0 && d.h > 0 && s_scale > 0) {
        /* the copy's rows go up with VRAM's y: its top row on top */
        quad(s_fbo_tex, vp, d.x / (float)VRAM_W, d.y / (float)VRAM_H, (d.x + d.w) / (float)VRAM_W, (d.y + d.h) / (float)VRAM_H, 1);
    }
    if (menu != NULL) {
        GL(glBindTexture)(GL_TEXTURE_2D, s_menu_tex);
        GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
        GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, menu->pitch / 4);
        GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, menu->pixels);
        GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
        GL(glEnable)(GL_BLEND);
        GL(glBlendFuncSeparate)(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        quad(s_menu_tex, vp, 0, 0, 1, 1, 0);
        GL(glDisable)(GL_BLEND);
    }
    GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (shot != NULL && (*shot = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888)) != NULL) {
        GL(glPixelStorei)(GL_PACK_ALIGNMENT, 4);
        GL(glReadPixels)(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (*shot)->pixels);
        SDL_FlipSurface(*shot, SDL_FLIP_VERTICAL);
    }
    SDL_GL_SwapWindow(s_window);
}
