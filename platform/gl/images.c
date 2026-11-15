/* The OpenGL renderer's images of the port's own (icons, effects) and props (billboards, meshes, particles) */
#include "gl.h"

/* An image's texture, made on first use: premultiplied, so that filtering
   doesn't bleed the transparent texels' colour (black, mostly) into the edges,
   and mipmapped, so that a big image shrunk to the picture keeps its detail */
static int image_tex(const SDL_Surface *img, unsigned *tex) {
    void *pm;

    if (*tex != 0) {
        return 1;
    }
    pm = SDL_malloc((size_t)img->w * img->h * 4);
    if (pm == NULL || !SDL_PremultiplyAlpha(img->w, img->h, img->format, img->pixels, img->pitch, SDL_PIXELFORMAT_RGBA32, pm,
                                            img->w * 4, false)) {
        SDL_free(pm);
        return 0;
    }
    *tex = gl_texture(GL_RGBA8, img->w, img->h, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, img->w, img->h, GL_RGBA, GL_UNSIGNED_BYTE, pm);
    GL(glGenerateMipmap)(GL_TEXTURE_2D);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    SDL_free(pm);
    return 1;
}

/* An image's texture made ahead of its first use (effects.c's warm-up), not in the frame it first shows */
int gl_image_prepare(const SDL_Surface *img, unsigned *tex) {
    return GLS.scale != 0 && img != NULL && image_tex(img, tex); /* 0: the card isn't up yet */
}

/* An image of the port's own over the copy (platform.h) */
void gl_image(const SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2) {
    int s = GLS.scale;
    float q[16] = {x0, y0, 0, 0, x1, y0, 1, 0, x0, y1, 0, 1, x1, y1, 1, 1};
    int i;

    if (s == 0) {
        return;
    }
    gl_flush();
    if (!image_tex(img, tex)) {
        return;
    }
    for (i = 0; i < 16; i += 4) { /* VRAM pixels to the copy's -1..1, as MAIN_VS */
        q[i] = q[i] / VRAM_W * 2 - 1;
        q[i + 1] = q[i + 1] / VRAM_H * 2 - 1;
    }
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.fbo);
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(cx1 * s, cy1 * s, (cx2 - cx1 + 1) * s, (cy2 - cy1 + 1) * s);
    GL(glEnable)(GL_BLEND);
    GL(glBlendEquation)(GL_FUNC_ADD);
    GL(glBlendFuncSeparate)(GL_ONE, add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); /* premultiplied; the copy's alpha is the mask bit: kept */
    GL(glUseProgram)(GLS.blit);
    GL(glBindVertexArray)(GLS.quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, *tex);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
    GL(glDisable)(GL_BLEND);
    GL(glDisable)(GL_SCISSOR_TEST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
}

/* The depth buffer as the stage's table left it, before the models' table
   empties it: what the props are hidden by (stagefx.c) */
void gl_depth_save(void) {
    int s = GLS.scale;

    GLS.sd_valid = 0;
    if (s == 0 || !GLS.sd_fbo || !g_depth) {
        return;
    }
    gl_flush();
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, GLS.fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, GLS.sd_fbo);
    GL(glBlitFramebuffer)(0, 0, VRAM_W * s, VRAM_H * s, 0, 0, VRAM_W * s, VRAM_H * s, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    GLS.sd_valid = 1, GLS.sd_org[0] = GLS.clip[0], GLS.sd_org[1] = GLS.clip[1];
}

/* The depth buffer as the fighters' table left it (it started empty): the grass is trodden down where a fighter is */
void gl_depth_models(void) {
    int s = GLS.scale;

    GLS.fd_valid = 0;
    if (s == 0 || !GLS.fd_fbo || !g_depth) {
        return;
    }
    gl_flush();
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, GLS.fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, GLS.fd_fbo);
    GL(glBlitFramebuffer)(0, 0, VRAM_W * s, VRAM_H * s, 0, 0, VRAM_W * s, VRAM_H * s, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    GLS.fd_valid = 1, GLS.fd_org[0] = GLS.clip[0], GLS.fd_org[1] = GLS.clip[1];
}

/* Triangles (n a multiple of 3) textured with an image of the port's own
   (platform.h's PropVtx), cut out by its alpha, by the depth where the depth
   buffer is on. FOGO: NULL for the battle's fog, else {r, g, b, amount, start} */
static int props_begin(GLuint prog, const SDL_Surface *img, unsigned *tex, int x1, int y1, int x2, int y2, const float *fogo) {
    int s = GLS.scale;
    float zf = GLS.zfar > 0 ? GLS.zfar : 24000.0f;

    if (s == 0) {
        return 0;
    }
    perf_begin(PF_PGL);
    perf_begin(PF_PFLUSH);
    gl_flush();
    perf_end(PF_PFLUSH);
    perf_begin(PF_PTEX);
    if (!image_tex(img, tex)) {
        perf_end(PF_PTEX);
        return 0;
    }
    perf_end(PF_PTEX);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.fbo);
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(x1 * s, y1 * s, (x2 - x1 + 1) * s, (y2 - y1 + 1) * s);
    GL(glUseProgram)(prog);
    GL(glActiveTexture)(GL_TEXTURE1);
    GL(glBindTexture)(GL_TEXTURE_2D, GLS.sd_tex);
    GL(glActiveTexture)(GL_TEXTURE2);
    GL(glBindTexture)(GL_TEXTURE_2D, GLS.fd_tex);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, *tex);
    GL(glUniform1i)(GL(glGetUniformLocation)(prog, "usedepth"), g_depth && GLS.sd_valid);
    GL(glUniform1i)(GL(glGetUniformLocation)(prog, "usefd"), g_depth && GLS.fd_valid);
    GL(glUniform1f)(GL(glGetUniformLocation)(prog, "clearz"), PROP_CLEAR);
    if (fogo != NULL) { /* the list's own fog: colour 0-255, how much, where it starts (0-1 of the far side) */
        GL(glUniform4f)(GL(glGetUniformLocation)(prog, "fog"), fogo[0] / 255.0f * 31.0f, fogo[1] / 255.0f * 31.0f, fogo[2] / 255.0f * 31.0f, fogo[3]);
        GL(glUniform2f)(GL(glGetUniformLocation)(prog, "fogz"), zf * fogo[4], zf);
    } else {
        GL(glUniform4f)(GL(glGetUniformLocation)(prog, "fog"), GLS.fog[0], GLS.fog[1], GLS.fog[2], GLS.fog_shown && GLS.zfar > 0 ? GLS.fog_amount : 0.0f); /* as flush() */
        GL(glUniform2f)(GL(glGetUniformLocation)(prog, "fogz"), GLS.zfar * GLS.fog_from, GLS.zfar);
    }
    if (g_depth) {
        GL(glEnable)(GL_DEPTH_TEST);
        GL(glDepthFunc)(GL_LEQUAL);
        GL(glDepthMask)(GL_TRUE);
    }
    GL(glDisable)(GL_BLEND);
    gl_query_begin(PF_GPU_PROPS);
    return 1;
}

static void props_end(void) {
    gl_query_end();
    perf_end(PF_PGL);
    GL(glDisable)(GL_DEPTH_TEST);
    GL(glDisable)(GL_SCISSOR_TEST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
}

void gl_props(const SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo) {
    if (n <= 0 || !props_begin(GLS.prop, img, tex, x1, y1, x2, y2, fogo)) {
        return;
    }
    GL(glBindVertexArray)(GLS.prop_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.prop_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof *v), v, GL_STREAM_DRAW);
    GL(glDrawArrays)(GL_TRIANGLES, 0, n);
    props_end();
}

/* A table of billboards, uploaded once (the card keeps it); 0 when it can't */
unsigned gl_inst_make(const PInst *v, int n) {
    GLuint b = 0;

    if (GLS.scale == 0 || n <= 0) {
        return 0;
    }
    GL(glGenBuffers)(1, &b);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, b);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)n * sizeof *v, v, GL_STATIC_DRAW);
    return b;
}

void gl_inst_free(unsigned b) {
    if (b != 0) {
        GL(glDeleteBuffers)(1, &b);
    }
}

static void props_cam(const PCam *c) {
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "cam0"), c->row[0][0], c->row[0][1], c->row[0][2], c->row[0][3]);
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "cam1"), c->row[1][0], c->row[1][1], c->row[1][2], c->row[1][3]);
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "cam2"), c->row[2][0], c->row[2][1], c->row[2][2], c->row[2][3]);
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "camp"), c->h, c->ofx, c->ofy, c->wide);
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "clk"), c->t, c->bright, c->ox, c->oy);
    GL(glUniform2f)(GL(glGetUniformLocation)(GLS.pinst, "wnd"), c->wind[0], c->wind[1]);
}

/* N billboards of the table B from FIRST, with image IMG, by the camera */
void gl_props_inst(const SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    const size_t st = sizeof(PInst);

    if (n <= 0 || b == 0 || !props_begin(GLS.pinst, img, tex, x1, y1, x2, y2, fogo)) {
        return;
    }
    props_cam(cam);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.pinst, "mode"), 0);
    GL(glBindVertexArray)(GLS.inst_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, b);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PInst, pos)));
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(1, 4, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PInst, w)));
    GL(glEnableVertexAttribArray)(2);
    GL(glVertexAttribPointer)(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void *)(first * st + offsetof(PInst, c)));
    GL(glEnableVertexAttribArray)(3);
    GL(glVertexAttribPointer)(3, 1, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PInst, fixed)));
    for (int a = 0; a < 4; a++) {
        GL(glVertexAttribDivisor)(a, 1);
    }
    GL(glDrawArraysInstanced)(GL_TRIANGLES, 0, 6, n);
    props_end();
}

/* A table (PInst, PModel...) uploaded once; 0 when it can't */
unsigned gl_buf_make(const void *p, unsigned long bytes) {
    GLuint b = 0;

    if (GLS.scale == 0 || bytes == 0) {
        return 0;
    }
    GL(glGenBuffers)(1, &b);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, b);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, p, GL_STATIC_DRAW);
    return b;
}

/* A mesh kept on the card: position, texture coordinate, normal */
void gl_mesh_make(GMesh *g, const float *v, int n) {
    const GLsizei st = 8 * sizeof(float);

    g->n = 0;
    if (GLS.scale == 0 || n <= 0) {
        return;
    }
    GL(glGenVertexArrays)(1, &g->vao);
    GL(glBindVertexArray)(g->vao);
    GL(glGenBuffers)(1, &g->vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, g->vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)n * st, v, GL_STATIC_DRAW);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, st, (void *)0);
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(1, 2, GL_FLOAT, GL_FALSE, st, (void *)(3 * sizeof(float)));
    GL(glEnableVertexAttribArray)(4);
    GL(glVertexAttribPointer)(4, 3, GL_FLOAT, GL_FALSE, st, (void *)(5 * sizeof(float)));
    g->n = n;
}

/* N instances of the mesh G, from FIRST of the table INST (PModel), with image IMG: one draw */
void gl_props_model(const SDL_Surface *img, unsigned *tex, const GMesh *g, float miny, float h, unsigned inst, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    const size_t st = sizeof(PModel);

    if (n <= 0 || g->n <= 0 || inst == 0 || !props_begin(GLS.pinst, img, tex, x1, y1, x2, y2, fogo)) {
        return;
    }
    props_cam(cam);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.pinst, "mode"), 3);
    GL(glUniform2f)(GL(glGetUniformLocation)(GLS.pinst, "mmesh"), miny, h);
    GL(glBindVertexArray)(g->vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, inst);
    GL(glEnableVertexAttribArray)(5);
    GL(glVertexAttribPointer)(5, 4, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PModel, pos)));
    GL(glEnableVertexAttribArray)(6);
    GL(glVertexAttribPointer)(6, 4, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PModel, rot)));
    GL(glEnableVertexAttribArray)(7);
    GL(glVertexAttribPointer)(7, 4, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PModel, tint)));
    for (int a = 5; a < 8; a++) {
        GL(glVertexAttribDivisor)(a, 1);
    }
    GL(glDrawArraysInstanced)(GL_TRIANGLES, 0, g->n, n);
    props_end();
}

/* An emitter's particles, or the stage's leaves (P->leaf): no data at all, each one's place is worked out from its number */
void gl_props_part(const SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    GLint loc;

    if (p->count <= 0 || !props_begin(GLS.pinst, img, tex, x1, y1, x2, y2, fogo)) {
        return;
    }
    props_cam(cam);
    GL(glUniform1i)(GL(glGetUniformLocation)(GLS.pinst, "mode"), p->leaf ? 2 : 1);
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "pc"), p->center[0], p->center[1], p->center[2], p->radius);
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "pa"), p->top, p->fall, p->wind, p->above);
    GL(glUniform4f)(GL(glGetUniformLocation)(GLS.pinst, "pb"), p->size[0], p->size[1], p->floor, (float)p->count);
    loc = GL(glGetUniformLocation)(GLS.pinst, "ptint");
    GL(glUniform4f)(loc, p->tint[0] / 255.0f, p->tint[1] / 255.0f, p->tint[2] / 255.0f, SDL_clamp(p->fog, 0.0f, 1.0f));
    GL(glBindVertexArray)(GLS.part_vao);
    GL(glDrawArraysInstanced)(GL_TRIANGLES, 0, 6, p->count);
    props_end();
}
