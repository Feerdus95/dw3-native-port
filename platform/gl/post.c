/* The OpenGL renderer's picture: the passes after the game's drawing (sharpen, SMAA, FSR 1, the upscalers' data) and the frame */
#include "gl.h"

/* The 3D's depth as the game gave it (MAIN_VS: depth = 1 - 64 / z) back to its view depth z, 0 where nothing 3D was
   drawn. view 1: the picture with the depth in grey (near bright) where there is any, to look at */
static const char *LIN_FS =
    "#version 330 core\n"
    "uniform sampler2D sd, col, fd;\n" /* the stage's depth, the picture, the fighters' (they are drawn over the stage) */
    "uniform ivec2 valid;\n" /* which of the two the frame has */
    "uniform vec4 asd, afd;\n" /* where the picture is in each (the buffer it was drawn in: corner and size) */
    "uniform int view;\n"
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    float ds = valid.x != 0 ? texture(sd, asd.xy + uv * asd.zw).r : 1.0, df = valid.y != 0 ? texture(fd, afd.xy + uv * afd.zw).r : 1.0;\n"
    "    float d = df < 1.0 ? df : ds;\n"
    "    float z = d < 1.0 ? 64.0 / (1.0 - d) : 0.0;\n"
    "    frag = view != 0 ? (z > 0.0 ? vec4(vec3(min(1.0, sqrt(3000.0 / z))), 1.0) : texture(col, uv)) : vec4(z);\n"
    "}\n";

/* The game's picture on its way to the window, when any of sharpen, brightness, gamma or saturation is set.
   The sharpening is contrast-adaptive, after AMD's FidelityFX CAS (CREDITS.md): a pixel and its four
   neighbours, a screen pixel apart, weighted less where they already span the whole range, so that edges
   don't ring and flat areas keep their noise down */
static const char *POST_FS =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform vec4 post;\n" /* sharpen 0-1, brightness added, gamma's exponent, saturation's factor */
    "uniform vec2 px;\n" /* a screen pixel, in uv */
    "uniform vec2 scan;\n" /* the scanlines' strength 0-1, and the rows of the texture */
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    vec3 c = texture(tex, uv).rgb;\n"
    "    if (post.x > 0.0) {\n"
    "        vec3 n = texture(tex, uv - vec2(0.0, px.y)).rgb, s = texture(tex, uv + vec2(0.0, px.y)).rgb;\n"
    "        vec3 w = texture(tex, uv - vec2(px.x, 0.0)).rgb, e = texture(tex, uv + vec2(px.x, 0.0)).rgb;\n"
    "        vec3 lo = min(c, min(min(n, s), min(w, e))), hi = max(c, max(max(n, s), max(w, e)));\n"
    "        vec3 k = sqrt(clamp(min(lo, 1.0 - hi) / max(hi, vec3(1.0 / 255.0)), 0.0, 1.0)) * mix(-0.125, -0.2, post.x);\n"
    "        c = clamp((c + (n + s + w + e) * k) / (1.0 + 4.0 * k), 0.0, 1.0);\n"
    "    }\n"
    "    c = mix(vec3(dot(c, vec3(0.299, 0.587, 0.114))), c, post.w);\n"
    /* each of the game's rows brightest at its middle, dark between it and the next */
    "    c *= 1.0 - scan.x * 0.6 * (0.5 + 0.5 * cos(6.2831853 * uv.y * scan.y));\n"
    "    frag = vec4(pow(clamp(c + post.y, 0.0, 1.0), vec3(post.z)), 1.0);\n"
    "}\n";

/* FSR 1's upscaling pass (EASU, of AMD's FidelityFX Super Resolution 1.0, MIT: CREDITS.md), written out for this
   renderer: the twelve texels around the output pixel, in a kernel turned along the edge found there and narrowed
   by how sharp it is, the result kept within the four nearest's range so that it does not ring */
static const char *EASU_FS =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform vec2 ratio;\n" /* the picture's pixels to one of the output's */
    "out vec4 frag;\n"
    "ivec2 at, top;\n"
    "vec3 T(int x, int y) { return texelFetch(tex, clamp(at + ivec2(x, y), ivec2(0), top), 0).rgb; }\n"
    "float L(vec3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }\n"
    /* one of the four nearest (c) and those above, left, right and below it: which way the edge runs, how sharp */
    "void edge(inout vec2 dir, inout float len, float w, float a, float b, float c, float d, float e) {\n"
    "    float dx = d - b, dy = e - a;\n"
    "    float lx = clamp(abs(dx) / max(max(abs(d - c), abs(c - b)), 1e-5), 0.0, 1.0);\n"
    "    float ly = clamp(abs(dy) / max(max(abs(e - c), abs(c - a)), 1e-5), 0.0, 1.0);\n"
    "    dir += vec2(dx, dy) * w;\n"
    "    len += (lx * lx + ly * ly) * w;\n"
    "}\n"
    "void tap(inout vec3 sum, inout float ws, vec2 off, vec2 dir, vec2 len, float lob, float clp, vec3 c) {\n"
    "    vec2 v = vec2(off.x * dir.x + off.y * dir.y, off.y * dir.x - off.x * dir.y) * len;\n"
    "    float d2 = min(dot(v, v), clp), wb = 0.4 * d2 - 1.0, wa = lob * d2 - 1.0;\n"
    "    wa = wa * wa * (1.5625 * wb * wb - 0.5625);\n"
    "    sum += c * wa;\n"
    "    ws += wa;\n"
    "}\n"
    "void main() {\n"
    "    vec2 p = gl_FragCoord.xy * ratio - 0.5, pp = fract(p);\n"
    "    at = ivec2(floor(p));\n"
    "    top = textureSize(tex, 0) - 1;\n"
    /*   b c
       e f g h
       i j k l
         n o   */
    "    vec3 b = T(0, -1), c = T(1, -1), e = T(-1, 0), f = T(0, 0), g = T(1, 0), h = T(2, 0);\n"
    "    vec3 i = T(-1, 1), j = T(0, 1), k = T(1, 1), l = T(2, 1), n = T(0, 2), o = T(1, 2);\n"
    "    float lb = L(b), lc = L(c), le = L(e), lf = L(f), lg = L(g), lh = L(h);\n"
    "    float li = L(i), lj = L(j), lk = L(k), ll = L(l), ln = L(n), lo = L(o);\n"
    "    vec2 dir = vec2(0.0);\n"
    "    float len = 0.0;\n"
    "    edge(dir, len, (1.0 - pp.x) * (1.0 - pp.y), lb, le, lf, lg, lj);\n"
    "    edge(dir, len, pp.x * (1.0 - pp.y), lc, lf, lg, lh, lk);\n"
    "    edge(dir, len, (1.0 - pp.x) * pp.y, lf, li, lj, lk, ln);\n"
    "    edge(dir, len, pp.x * pp.y, lg, lj, lk, ll, lo);\n"
    "    float r = dot(dir, dir);\n"
    "    dir = r < 1.0 / 32768.0 ? vec2(1.0, 0.0) : dir * inversesqrt(r);\n"
    "    len = len * 0.5;\n"
    "    len *= len;\n"
    "    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));\n"
    "    vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 - 0.29 * len, clp = 1.0 / lob, ws = 0.0;\n"
    "    vec3 sum = vec3(0.0);\n"
    "    tap(sum, ws, vec2(0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "    tap(sum, ws, vec2(1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "    tap(sum, ws, vec2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "    tap(sum, ws, vec2(0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "    tap(sum, ws, vec2(1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "    tap(sum, ws, vec2(2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "    tap(sum, ws, vec2(-1.0, 1.0) - pp, dir, len2, lob, clp, i);\n"
    "    tap(sum, ws, vec2(0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "    tap(sum, ws, vec2(1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "    tap(sum, ws, vec2(2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "    tap(sum, ws, vec2(0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "    tap(sum, ws, vec2(1.0, 2.0) - pp, dir, len2, lob, clp, o);\n"
    "    frag = vec4(min(max(max(f, g), max(j, k)), max(min(min(f, g), min(j, k)), sum / ws)), 1.0);\n"
    "}\n";

/* SMAA 1x (Jimenez, Echevarria, Masia, Navarro and Gutierrez; MIT): their shader as they publish it, in smaa/ with
   its licence and its two tables, between a head that sets it up and the three passes (stage 0: the edges, 1: how
   much each blends, 2: the picture blended). Its rows run down, as the copy's do */
/* in a section of their own, after the game's memory: that has to end below 16 MB (main.c) */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
__attribute__((section(".zsmaa"))) static char SMAA_SRC[] = {
#embed "../smaa/SMAA.hlsl"
    , 0};
#pragma clang diagnostic pop
#define areaTexBytes __attribute__((section(".zsmaar"))) areaTexBytes
#define searchTexBytes __attribute__((section(".zsmaar"))) searchTexBytes
#include "../smaa/AreaTex.h"
#include "../smaa/SearchTex.h"
#undef areaTexBytes
#undef searchTexBytes

static const char *SMAA_VS =
    "\nlayout(location = 0) in vec4 pos_uv;\n"
    "uniform int stage;\n"
    "out vec2 uv, pix;\n"
    "out vec4 off[3];\n"
    "void main() {\n"
    "    gl_Position = vec4(pos_uv.xy, 0.0, 1.0);\n"
    "    uv = pos_uv.zw;\n"
    "    pix = vec2(0.0);\n"
    "    off[0] = off[1] = off[2] = vec4(0.0);\n"
    "    if (stage == 0) SMAAEdgeDetectionVS(uv, off);\n"
    "    else if (stage == 1) SMAABlendingWeightCalculationVS(uv, pix, off);\n"
    "    else SMAANeighborhoodBlendingVS(uv, off[0]);\n"
    "}\n";

static const char *SMAA_FS =
    "\nuniform sampler2D t0, t1, t2;\n"
    "uniform int stage;\n"
    "in vec2 uv, pix;\n"
    "in vec4 off[3];\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    if (stage == 0) frag = vec4(SMAAColorEdgeDetectionPS(uv, off, t0), 0.0, 0.0);\n"
    "    else if (stage == 1) frag = SMAABlendingWeightCalculationPS(uv, pix, off, t0, t1, t2, vec4(0.0));\n"
    "    else frag = SMAANeighborhoodBlendingPS(uv, off[0], t0, t1);\n"
    "}\n";

/* The picture's passes and their data, at the start: where an optional one doesn't build, its option does nothing */
int gl_post_init(void) {
    if ((GLS.post = gl_program(BLIT_VS, POST_FS)) == 0) {
        return 0;
    }
    GL(glUseProgram)(GLS.post);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.post, "tex"), 0);
    {
        const char *e = SDL_getenv("DW3_UPSCALE_DATA");

        GLS.data_depth = e != NULL && SDL_strstr(e, "depth") != NULL;
        GLS.data_jitter = e != NULL && SDL_strstr(e, "jitter") != NULL;
        GLS.data_view = e != NULL && SDL_strstr(e, "view") != NULL;
        GLS.data_selftest = e != NULL && SDL_strstr(e, "selftest") != NULL;
        if ((GLS.data_depth || GLS.data_view) && (GLS.lin = gl_program(BLIT_VS, LIN_FS)) != 0) {
            GL(glUseProgram)(GLS.lin);
            GL(glUniform1i)(GL(glGetUniformLocation)(GLS.lin, "sd"), 0);
            GL(glUniform1i)(GL(glGetUniformLocation)(GLS.lin, "col"), 1);
            GL(glUniform1i)(GL(glGetUniformLocation)(GLS.lin, "fd"), 2);
        }
    }
    { /* the picture's optional passes: where one doesn't build, its option does nothing */
        const char *vs[3] = {SMAA_HEAD(1, 0), SMAA_SRC, SMAA_VS}, *fs[3] = {SMAA_HEAD(0, 1), SMAA_SRC, SMAA_FS};
        char *c;

        for (c = SMAA_SRC; *c != 0; c++) { /* its comments have a letter or two that aren't ASCII */
            if ((unsigned char)*c > 126) {
                *c = ' ';
            }
        }
        if ((GLS.smaa = gl_program_of(vs, fs, 3)) != 0) {
            GL(glUseProgram)(GLS.smaa);
            GL(glUniform1i)(GL(glGetUniformLocation)(GLS.smaa, "t0"), 0);
            GL(glUniform1i)(GL(glGetUniformLocation)(GLS.smaa, "t1"), 1);
            GL(glUniform1i)(GL(glGetUniformLocation)(GLS.smaa, "t2"), 2);
        }
        if ((GLS.easu = gl_program(BLIT_VS, EASU_FS)) != 0) {
            GL(glUseProgram)(GLS.easu);
            GL(glUniform1i)(GL(glGetUniformLocation)(GLS.easu, "tex"), 0);
        }
    }
    GLS.area_tex = gl_texture(GL_RG8, AREATEX_WIDTH, AREATEX_HEIGHT, GL_RG, GL_UNSIGNED_BYTE, GL_LINEAR);
    GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, AREATEX_WIDTH, AREATEX_HEIGHT, GL_RG, GL_UNSIGNED_BYTE, areaTexBytes);
    GLS.search_tex = gl_texture(GL_R8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
    GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, searchTexBytes);
    return 1;
}

/* A texture onto the window's 4:3 area: uv from (u0, v0) top left to (u1, v1) */
/* rows: the game's rows shown, for the post-process pass when one of its options is set (the game's picture);
   0 without it (the movies, the menu) */
static void quad(GLuint tex, const int vp[4], float u0, float v0, float u1, float v1, int linear, int rows) {
    float q[16] = {-1, 1, u0, v0, 1, 1, u1, v0, -1, -1, u0, v1, 1, -1, u1, v1};

    GL(glViewport)(vp[0], vp[1], vp[2], vp[3]);
    if (rows && (g_sharpen | g_brightness | g_gamma | g_saturation | g_scanlines) != 0) {
        GL(glUseProgram)(GLS.post);
        /* gamma 10 halves the exponent (brighter middles), -10 doubles it */
        GL(glUniform4f)(GL(glGetUniformLocation)(GLS.post, "post"), g_sharpen / 10.0f, g_brightness * 0.02f,
                        SDL_powf(2.0f, -g_gamma / 10.0f), 1.0f + g_saturation / 10.0f);
        GL(glUniform2f)(GL(glGetUniformLocation)(GLS.post, "px"), (u1 - u0) / vp[2], (v1 - v0) / vp[3]);
        GL(glUniform2f)(GL(glGetUniformLocation)(GLS.post, "scan"), g_scanlines / 10.0f, rows / (v1 - v0));
    } else {
        GL(glUseProgram)(GLS.blit);
    }
    GL(glBindVertexArray)(GLS.quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, tex);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
}

static void target(GLuint *tex, GLuint *fbo, int w, int h) {
    if (*tex != 0) {
        GL(glDeleteFramebuffers)(1, fbo);
        GL(glDeleteTextures)(1, tex);
    }
    *tex = gl_texture(GL_RGBA8, w, h, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    *fbo = gl_framebuffer(*tex);
}

/* The whole of a target drawn with the program in use, its rows in the order of the textures' */
static void pass(GLuint fbo, int w, int h, int clear) {
    static const float Q[16] = {-1, -1, 0, 0, 1, -1, 1, 0, -1, 1, 0, 1, 1, 1, 1, 1};

    GL(glBindFramebuffer)(GL_FRAMEBUFFER, fbo);
    GL(glViewport)(0, 0, w, h);
    /* a battle leaves its own state behind (shadows, props): a plain overwrite of every channel */
    GL(glDisable)(GL_BLEND);
    GL(glDisable)(GL_DEPTH_TEST);
    GL(glDisable)(GL_STENCIL_TEST);
    GL(glDisable)(GL_SCISSOR_TEST);
    GL(glDisable)(GL_CULL_FACE);
    GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (clear) { /* SMAA leaves out what has no edge */
        GL(glClearColor)(0, 0, 0, 0);
        GL(glClear)(GL_COLOR_BUFFER_BIT);
    }
    GL(glBindVertexArray)(GLS.quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof Q, Q, GL_STREAM_DRAW);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
}

static void bind(int unit, GLuint tex) {
    GL(glActiveTexture)(GL_TEXTURE0 + unit);
    GL(glBindTexture)(GL_TEXTURE_2D, tex);
    GL(glActiveTexture)(GL_TEXTURE0);
}

/* Measures the depth data a few times (DW3_UPSCALE_DATA with "depth"): how many pixels of the saved depths and of the
   view depth hold anything, and the view depth's range. For checking that they arrive */
static void probe(int pw, int ph, const PSX_RECT *d) {
    static int n, logged;
    int w = VRAM_W * GLS.scale, h = VRAM_H * GLS.scale, i, nz = 0, ns = 0, nf = 0;
    float *z, zmin = 1e30f, zmax = 0;
    uint32_t *a, *b;

    if (!GLS.data_depth || logged >= 6 || n++ % 90 != 0) {
        return;
    }
    z = SDL_malloc((size_t)pw * ph * 4), a = SDL_malloc((size_t)w * h * 4), b = SDL_malloc((size_t)w * h * 4);
    if (z && a && b) {
        GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, GLS.lin_fbo);
        GL(glReadPixels)(0, 0, pw, ph, GL_RED, GL_FLOAT, z);
        GL(glBindTexture)(GL_TEXTURE_2D, GLS.sd_tex);
        GL(glGetTexImage)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, a);
        GL(glBindTexture)(GL_TEXTURE_2D, GLS.fd_tex);
        GL(glGetTexImage)(GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, b);
        for (i = 0; i < pw * ph; i++) {
            if (z[i] > 0) {
                nz++, zmin = SDL_min(zmin, z[i]), zmax = SDL_max(zmax, z[i]);
            }
        }
        for (i = 0; i < w * h; i++) {
            ns += (a[i] >> 8) != 0xFFFFFF, nf += (b[i] >> 8) != 0xFFFFFF;
        }
        LOG("upscale depth: view depth %d of %d pixels (z %.0f to %.0f); saved stage depth %d, fighters' %d of %d; drawn at (%d,%d) and (%d,%d), shown at (%d,%d)",
            nz, pw * ph, nz ? zmin : 0, zmax, ns, nf, w * h, GLS.sd_org[0], GLS.sd_org[1], GLS.fd_org[0], GLS.fd_org[1], d->x, d->y);
        logged++;
    }
    SDL_free(z), SDL_free(a), SDL_free(b);
}

/* The game's picture before the window: the display area D of the copy, through SMAA and through FSR 1's upscaling
   to the W x H it is shown at, when they are on. FSR only where it has something to do: a frame with 3D in it shown
   bigger (the 2D is the PlayStation's pixels enlarged, which it would tangle). The texture to show and, in uv, its
   corners */
static GLuint picture(const PSX_RECT *d, int w, int h, float uv[4]) {
    int s = GLS.scale, pw = d->w * s, ph = d->h * s, smaa = g_smaa && GLS.smaa != 0, fsr = g_fsr && GLS.easu != 0 && h > ph && GLS.frame_3d, i;
    int data;
    static int shown = -1;

    if (GLS.data_selftest && GLS.sd_tex != 0 && !GLS.sd_valid) { /* the stage's depth: z = 1000 on the picture's left half, none on the right */
        int sw = VRAM_W * s, sh = VRAM_H * s, x, y;
        uint32_t *a = SDL_malloc((size_t)sw * sh * 4);

        if (a != NULL) {
            for (y = 0; y < sh; y++) {
                for (x = 0; x < sw; x++) {
                    a[y * sw + x] = (x < (d->x + d->w / 2) * s ? (uint32_t)((1.0 - 64.0 / 1000.0) * 16777215.0) : 0xFFFFFFu) << 8;
                }
            }
            GL(glBindTexture)(GL_TEXTURE_2D, GLS.sd_tex);
            GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, sw, sh, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, a);
            GLS.sd_valid = 1, GLS.sd_org[0] = d->x, GLS.sd_org[1] = d->y;
            SDL_free(a);
        }
    }
    data = GLS.lin != 0 && (GLS.frame_3d || GLS.data_selftest) && (GLS.sd_valid || GLS.fd_valid); /* the 2D fields have none */
    int now = (GLS.lin != 0) << 3 | (GLS.frame_3d != 0) << 2 | (GLS.sd_valid != 0) << 1 | (GLS.fd_valid != 0);

    if (GLS.lin != 0 && shown != now) { /* what the data waits for, once per change */
        shown = now;
        LOG("upscale data: program %d, 3D frame %d, stage depth %d, fighters' depth %d", GLS.lin != 0, GLS.frame_3d, GLS.sd_valid, GLS.fd_valid);
    }
    GLuint tex = GLS.pic_tex[0];

    uv[0] = d->x / (float)VRAM_W, uv[1] = d->y / (float)VRAM_H;
    uv[2] = (d->x + d->w) / (float)VRAM_W, uv[3] = (d->y + d->h) / (float)VRAM_H;
    if (!smaa && !fsr && !data) {
        return GLS.fbo_tex;
    }
    if (pw != GLS.pic_w || ph != GLS.pic_h) {
        for (i = 0; i < 3; i++) {
            target(&GLS.pic_tex[i], &GLS.pic_fbo[i], pw, ph);
        }
        if (GLS.lin != 0) {
            if (GLS.lin_tex != 0) {
                GL(glDeleteFramebuffers)(1, &GLS.lin_fbo);
                GL(glDeleteTextures)(1, &GLS.lin_tex);
            }
            GLS.lin_tex = gl_texture(GL_R32F, pw, ph, GL_RED, GL_FLOAT, GL_NEAREST);
            GLS.lin_fbo = gl_framebuffer(GLS.lin_tex);
        }
        GLS.pic_w = pw, GLS.pic_h = ph;
        tex = GLS.pic_tex[0];
    }
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, GLS.fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, GLS.pic_fbo[0]);
    GL(glBlitFramebuffer)(d->x * s, d->y * s, d->x * s + pw, d->y * s + ph, 0, 0, pw, ph, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (data) { /* the depth of the same area, as view depth: the saved ones, the buffer itself is emptied per table */
        GL(glUseProgram)(GLS.lin);
        GL(glUniform1i)(GL(glGetUniformLocation)(GLS.lin, "view"), 0);
        GL(glUniform2i)(GL(glGetUniformLocation)(GLS.lin, "valid"), GLS.sd_valid && g_depth, GLS.fd_valid && g_depth);
        GL(glUniform4f)(GL(glGetUniformLocation)(GLS.lin, "asd"), GLS.sd_org[0] / (float)VRAM_W, GLS.sd_org[1] / (float)VRAM_H, uv[2] - uv[0], uv[3] - uv[1]);
        GL(glUniform4f)(GL(glGetUniformLocation)(GLS.lin, "afd"), GLS.fd_org[0] / (float)VRAM_W, GLS.fd_org[1] / (float)VRAM_H, uv[2] - uv[0], uv[3] - uv[1]);
        bind(0, GLS.sd_tex);
        bind(2, GLS.fd_tex);
        pass(GLS.lin_fbo, pw, ph, 0);
        probe(pw, ph, d);
    }
    if (smaa) {
        GLint stage = GL(glGetUniformLocation)(GLS.smaa, "stage");

        GL(glUseProgram)(GLS.smaa);
        GL(glUniform4f)(GL(glGetUniformLocation)(GLS.smaa, "rt"), 1.0f / pw, 1.0f / ph, (float)pw, (float)ph);
        GL(glUniform1i)(stage, 0);
        bind(0, GLS.pic_tex[0]);
        pass(GLS.pic_fbo[1], pw, ph, 1);
        GL(glUniform1i)(stage, 1);
        bind(0, GLS.pic_tex[1]);
        bind(1, GLS.area_tex);
        bind(2, GLS.search_tex);
        pass(GLS.pic_fbo[2], pw, ph, 1);
        GL(glUniform1i)(stage, 2);
        bind(0, GLS.pic_tex[0]);
        bind(1, GLS.pic_tex[2]);
        pass(GLS.pic_fbo[1], pw, ph, 0);
        tex = GLS.pic_tex[1];
    }
    if (fsr) {
        if (w != GLS.up_w || h != GLS.up_h) {
            target(&GLS.up_tex, &GLS.up_fbo, w, h);
            GLS.up_w = w, GLS.up_h = h;
        }
        GL(glUseProgram)(GLS.easu);
        GL(glUniform2f)(GL(glGetUniformLocation)(GLS.easu, "ratio"), (float)pw / w, (float)ph / h);
        bind(0, tex);
        pass(GLS.up_fbo, w, h, 0);
        tex = GLS.up_tex;
    }
    if (data && GLS.data_view) { /* the depth itself on the screen, to see it */
        GL(glUseProgram)(GLS.lin);
        GL(glUniform1i)(GL(glGetUniformLocation)(GLS.lin, "view"), 1);
        bind(0, GLS.sd_tex);
        bind(1, GLS.pic_tex[0]);
        bind(2, GLS.fd_tex);
        pass(GLS.pic_fbo[2], pw, ph, 0);
        tex = GLS.pic_tex[2];
    }
    uv[0] = uv[1] = 0, uv[2] = uv[3] = 1;
    return tex;
}

/* The frame: the display area of the copy (or the 1x VRAM's 24-bit movie)
   letterboxed to 4:3, then the menu (a 1280x960 RGBA surface, or NULL) */
void gl_present(const SDL_Surface *menu, SDL_Surface **shot) {
    PSX_RECT d = g_disp.disp;
    int w, h, vp[4], menu_vp[4], game;
    GLuint tex = 0;
    float uv[4];
    int aw = gte_widescreen() ? 16 : 4, ah = gte_widescreen() ? 9 : 3; /* the picture's aspect */

    static int vsync = -1;

    gl_flush();
    /* the fog for the next frame: on while a sky is drawn, its depths eased
       toward this frame's farthest (a camera's cut) */
    {
        float bright;
        const StageFx *fx = stagefx_current(&bright);
        int i;

        GLS.fog_shown = GLS.fog_on && fx != NULL && fx->fog > 0;
        if (GLS.fog_shown) {
            for (i = 0; i < 3; i++) {
                GLS.fog[i] = fx->fog_color[i] >= 0 ? fx->fog_color[i] * bright / 8.0f : GLS.bg[i];
            }
            GLS.fog_amount = fx->fog, GLS.fog_from = fx->fog_start;
        }
        stagefx_frame();
    }
    GLS.zfar = !GLS.fog_on || GLS.zframe <= 0 ? 0 : GLS.zfar > 0 ? GLS.zfar + (GLS.zframe - GLS.zfar) * 0.2f : GLS.zframe;
    GLS.frame_3d = GLS.zframe > 0;
    GLS.fog_on = 0, GLS.zframe = 0;
    if (GLS.data_jitter) { /* the next frame's: Halton (2, 3), 8 of them, centred on the pixel */
        unsigned n = ++GLS.jit_n % 8 + 1;
        float f, b;
        int k;

        for (k = 0; k < 2; k++) {
            unsigned i = n, base = k == 0 ? 2 : 3;

            for (f = 1, b = 0; i > 0; i /= base) {
                f /= base, b += f * (i % base);
            }
            GLS.jit_px[k] = b - 0.5f;
        }
    }
    if (vsync != g_vsync) {
        /* the game keeps its own time (etc.c); vsync only holds the swap for
           the screen's refresh, so that a frame isn't shown torn */
        vsync = g_vsync;
        SDL_GL_SetSwapInterval(vsync);
    }
    SDL_GetWindowSizeInPixels(GLS.window, &w, &h);
    vp[2] = w * ah > h * aw ? h * aw / ah : w;
    vp[3] = w * ah > h * aw ? h : w * ah / aw;
    if (g_integer_scale && vp[3] >= 240) { /* every row of the game's as many of the screen's */
        vp[3] = vp[3] / 240 * 240;
        vp[2] = vp[3] * aw / ah;
    }
    vp[0] = (w - vp[2]) / 2;
    vp[1] = (h - vp[3]) / 2;
    menu_vp[2] = w * 3 > h * 4 ? h * 4 / 3 : w;
    menu_vp[3] = w * 3 > h * 4 ? h : w * 3 / 4;
    menu_vp[0] = (w - menu_vp[2]) / 2;
    menu_vp[1] = (h - menu_vp[3]) / 2;
    if (d.x + d.w > VRAM_W) d.w = VRAM_W - d.x;
    if (d.y + d.h > VRAM_H) d.h = VRAM_H - d.y;
    game = g_disp_on && d.w > 0 && d.h > 0 && !g_disp.isrgb24 && GLS.scale > 0;
    if (game) {
        tex = picture(&d, vp[2], vp[3], uv);
    }
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    GL(glViewport)(0, 0, w, h);
    GL(glClearColor)(0, 0, 0, 1);
    GL(glClear)(GL_COLOR_BUFFER_BIT);
    GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); /* the copy's alpha is the mask bit: the window stays opaque */
    if (g_disp_on && d.w > 0 && d.h > 0 && g_disp.isrgb24) {
        /* 24-bit (the movies): d.w pixels of 3 bytes from d.x on, as RGBA */
        static uint32_t *rgba;
        int x, y;

        if (rgba == NULL && (rgba = SDL_malloc(VRAM_W * VRAM_H * 4)) == NULL) {
            return;
        }
        soft_read(d.x, d.y, d.w, d.h);
        for (y = 0; y < d.h; y++) {
            const uint8_t *b = (const uint8_t *)&g_vram[(d.y + y) * VRAM_W + d.x];

            for (x = 0; x < d.w && (d.x * 2 + x * 3 + 3) <= VRAM_W * 2; x++) {
                rgba[y * d.w + x] = (uint32_t)(b[x * 3] | b[x * 3 + 1] << 8 | b[x * 3 + 2] << 16 | 0xFF000000u);
            }
        }
        GL(glBindTexture)(GL_TEXTURE_2D, GLS.movie_tex);
        GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
        GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, d.w, d.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        quad(GLS.movie_tex, vp, 0, 0, d.w / (float)VRAM_W, d.h / (float)VRAM_H, g_smooth_movies, 0);
    } else if (game) {
        /* the copy's rows go up with VRAM's y: its top row on top */
        quad(tex, vp, uv[0], uv[1], uv[2], uv[3], 1, d.h);
    }
    if (menu != NULL) {
        GL(glBindTexture)(GL_TEXTURE_2D, GLS.menu_tex);
        GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
        GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, menu->pitch / 4);
        GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, 1280, 960, GL_RGBA, GL_UNSIGNED_BYTE, menu->pixels);
        GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
        GL(glEnable)(GL_BLEND);
        GL(glBlendFuncSeparate)(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        quad(GLS.menu_tex, menu_vp, 0, 0, 1, 1, 0, 0);
        GL(glDisable)(GL_BLEND);
    }
    GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (shot != NULL && (*shot = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888)) != NULL) {
        GL(glPixelStorei)(GL_PACK_ALIGNMENT, 4);
        GL(glReadPixels)(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (*shot)->pixels);
        SDL_FlipSurface(*shot, SDL_FLIP_VERTICAL);
    }
    gl_query_end();
    SDL_GL_SwapWindow(GLS.window);
    gl_query_frame();
}
