/* The OpenGL renderer's drawing of the game's primitives: the batch, the battle shadows, VRAM copies and refreshes */
#include "gl.h"

/* A rect of the 1x VRAM onto the card */
static void upload(int x, int y, int w, int h) {
    GL(glBindTexture)(GL_TEXTURE_2D, GLS.vram_tex);
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
static void flush_batch(void) {
    int s = GLS.scale;

    if (GLS.count == 0) {
        return;
    }
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.semi == 5 ? GLS.mask_fbo : GLS.fbo);
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(GLS.clip[0] * s, GLS.clip[1] * s, (GLS.clip[2] - GLS.clip[0] + 1) * s, (GLS.clip[3] - GLS.clip[1] + 1) * s);
    GL(glUseProgram)(GLS.main);
    GL(glBindVertexArray)(GLS.vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)(GLS.count * sizeof(GVtx)), GLS.batch, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, GLS.vram_tex);
    GL(glUniform1f)(GLS.scale_loc, (float)s);
    GL(glUniform1i)(GLS.true_loc, g_true_color);
    GL(glUniform1i)(GLS.persp_loc, g_perspective);
    GL(glUniform1i)(GLS.smooth_loc, g_smooth_3d);
    GL(glUniform2f)(GLS.jit_loc, GLS.jit_px[0] * 2.0f / (VRAM_W * s), GLS.jit_px[1] * 2.0f / (VRAM_H * s));
    GL(glUniform4f)(GLS.fog_loc, GLS.fog[0], GLS.fog[1], GLS.fog[2], GLS.fog_shown && GLS.zfar > 0 ? GLS.fog_amount : 0.0f);
    GL(glUniform2f)(GLS.fogz_loc, GLS.zfar * GLS.fog_from, GLS.zfar);
    GL(glDisable)(GL_BLEND);
    if (GLS.deep) {
        GL(glEnable)(GL_DEPTH_TEST);
        GL(glDepthFunc)(GL_LEQUAL);
        GL(glDepthMask)(GL_TRUE);
    }
    GL(glUniform1i)(GLS.pass_loc, GLS.semi < 0 ? 0 : 1);
    GL(glDrawArrays)(GL_TRIANGLES, 0, GLS.count);
    GL(glDepthMask)(GL_FALSE); /* the blended pixels test, don't hide what's behind */
    if (GLS.semi >= 0) {
        GL(glEnable)(GL_BLEND);
        GL(glBlendColor)(0.25f, 0.25f, 0.25f, 0.5f);
        switch (GLS.semi) {
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
        case 4: /* back * (1 - front): a soft shadow (gpu_draw.c) */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
            break;
        case 5: /* a battle shadow (gpu_draw.c): into the mask, once a pixel */
            GL(glBlendEquation)(GL_MAX);
            GL(glBlendFuncSeparate)(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
            GL(glEnable)(GL_POLYGON_OFFSET_FILL); /* on the ground it lies on */
            GL(glPolygonOffset)(-1.0f, -4.0f);
            break;
        default: /* back + front / 4 */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_CONSTANT_COLOR, GL_ONE, GL_ONE, GL_ZERO);
            break;
        }
        GL(glUniform1i)(GLS.pass_loc, 2);
        GL(glDrawArrays)(GL_TRIANGLES, 0, GLS.count);
        GL(glDisable)(GL_BLEND);
        GL(glDisable)(GL_POLYGON_OFFSET_FILL);
        GL(glBlendEquation)(GL_FUNC_ADD);
    }
    GL(glDisable)(GL_DEPTH_TEST);
    GL(glDisable)(GL_SCISSOR_TEST);
    GLS.count = 0;
    GLS.semi = -1;
}

void gl_flush(void) {
    if (GLS.count == 0) {
        return;
    }
    gl_query_begin(PF_GPU);
    flush_batch();
    gl_query_end();
}

/* Triangles (n a multiple of 3) for the copy, with gpu_draw.c's state: the
   drawing area, the semi-transparency mode (4: a soft shadow's) */
void gl_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {
    int semi_prim = (v[0].mode & 8) != 0, deep = v[0].z > 0;

    if (GLS.scale == 0) {
        return;
    }
    if (GLS.count + n > BATCH || deep != GLS.deep || x1 != GLS.clip[0] || y1 != GLS.clip[1] || x2 != GLS.clip[2] || y2 != GLS.clip[3] ||
        (semi_prim && GLS.semi >= 0 && semi != GLS.semi) || (!semi_prim && GLS.semi >= 0) ||
        (semi == 5 && GLS.semi != 5 && GLS.count > 0)) { /* battle shadows go to the mask: a batch of their own */
        gl_flush();
        GLS.clip[0] = x1, GLS.clip[1] = y1, GLS.clip[2] = x2, GLS.clip[3] = y2;
        GLS.deep = deep;
    }
    if (v[0].mode & 2) {
        /* the page and the CLUT as the card has them: what the CPU drew there
           since (a texture drawn to) goes up first */
        uint16_t need[8] = {0};
        int depth = v[0].mode >> 4 & 3;

        need_rect(need, v[0].tx, v[0].ty, 64 << depth, 256);
        need_rect(need, v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        soft_read(v[0].tx, v[0].ty, 64 << depth, 256);
        soft_read(v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        if ((need[0] & g_vram_dirty[0]) | (need[1] & g_vram_dirty[1]) | (need[2] & g_vram_dirty[2]) | (need[3] & g_vram_dirty[3]) |
            (need[4] & g_vram_dirty[4]) | (need[5] & g_vram_dirty[5]) | (need[6] & g_vram_dirty[6]) | (need[7] & g_vram_dirty[7])) {
            gl_flush();
            upload_blocks(need);
        }
    }
    if (semi_prim) {
        GLS.semi = semi;
    } else if (v[0].w > 0 && !(v[0].mode & 2048)) {
        int i;

        for (i = 0; i < n; i++) {
            GLS.zframe = SDL_max(GLS.zframe, v[i].w);
        }
    }
    if (semi == 5) { /* where the mask has shadows */
        int i;

        for (i = 0; i < n; i++) {
            GLS.mask_box[0] = SDL_min(GLS.mask_box[0], (int)v[i].x);
            GLS.mask_box[1] = SDL_min(GLS.mask_box[1], (int)v[i].y);
            GLS.mask_box[2] = SDL_max(GLS.mask_box[2], (int)v[i].x + 1);
            GLS.mask_box[3] = SDL_max(GLS.mask_box[3], (int)v[i].y + 1);
        }
    }
    SDL_memcpy(&GLS.batch[GLS.count], v, n * sizeof(GVtx));
    GLS.count += n;
}

/* An ordering table drawn: the battle shadows in the mask, blurred, darken
   the copy, and the mask is emptied */
void gl_shadows_resolve(void) {
    static const float QUAD[16] = {-1, -1, 0, 0, 1, -1, 0, 0, -1, 1, 0, 0, 1, 1, 0, 0};
    int s = GLS.scale, step = SDL_max(1, s * 3 / 4), pad = 5 * step, r[4];

    if (s == 0 || GLS.mask_box[2] < 0) {
        return;
    }
    gl_flush();
    r[0] = SDL_max(0, GLS.mask_box[0] * s - pad);
    r[1] = SDL_max(0, GLS.mask_box[1] * s - pad);
    r[2] = SDL_min(VRAM_W * s, GLS.mask_box[2] * s + pad) - r[0];
    r[3] = SDL_min(VRAM_H * s, GLS.mask_box[3] * s + pad) - r[1];
    GLS.mask_box[0] = VRAM_W, GLS.mask_box[1] = VRAM_H, GLS.mask_box[2] = GLS.mask_box[3] = -1;
    if (r[2] <= 0 || r[3] <= 0) {
        return;
    }
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(r[0], r[1], r[2], r[3]);
    GL(glUseProgram)(GLS.soft);
    GL(glBindVertexArray)(GLS.quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, GLS.quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof QUAD, QUAD, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    /* across */
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.blur_fbo);
    GL(glBindTexture)(GL_TEXTURE_2D, GLS.mask_tex);
    GL(glUniform2i)(GLS.soft_dir_loc, step, 0);
    GL(glUniform1f)(GLS.soft_str_loc, 0.0f);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
    /* down, darkening the copy: back * (1 - front) */
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.fbo);
    GL(glBindTexture)(GL_TEXTURE_2D, GLS.blur_tex);
    GL(glUniform2i)(GLS.soft_dir_loc, 0, step);
    GL(glUniform1f)(GLS.soft_str_loc, 0.5f);
    GL(glEnable)(GL_BLEND);
    GL(glBlendEquation)(GL_FUNC_ADD);
    GL(glBlendFuncSeparate)(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
    GL(glDisable)(GL_BLEND);
    /* empty for the next */
    GL(glClearColor)(0, 0, 0, 0);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.mask_fbo);
    GL(glClear)(GL_COLOR_BUFFER_BIT);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.blur_fbo);
    GL(glClear)(GL_COLOR_BUFFER_BIT);
    GL(glDisable)(GL_SCISSOR_TEST);
}

/* The depth buffer emptied: an ordering table's 3D models are drawn by their
   depth among themselves, over what earlier tables drew */
void gl_depth_clear(void) {
    if (GLS.scale == 0 || !GLS.fbo_depth) {
        return;
    }
    gl_flush();
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, GLS.fbo);
    GL(glDepthMask)(GL_TRUE);
    GL(glClearDepth)(1.0);
    GL(glClearStencil)(0);
    GL(glClear)(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

/* A quad of the copy that shows the 1x VRAM under it: after the CPU wrote
   VRAM (an image, a fill) */
void gl_refresh(int x, int y, int w, int h) {
    GVtx q[6];
    int i;

    if (GLS.scale == 0 || w <= 0 || h <= 0) {
        return;
    }
    gl_flush();
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
    SDL_memcpy(GLS.batch, q, sizeof q);
    GLS.count = 6;
    GLS.clip[0] = 0, GLS.clip[1] = 0, GLS.clip[2] = 1023, GLS.clip[3] = 511;
    gl_flush();
}

/* A battle's sky drawn (gpu_draw.c), its colour at the horizon the fog's:
   the stage's background colour, which fades in and out with the stage
   (the profile's colour fades with its brightness). The painted horizon
   (mode 2048) stays out of the fog and of its range.
   ponytail: the fog spans the farthest opaque 3D of the last frames (the
   stage's edge), as the stages' sizes are not known; a per-stage range
   when one looks wrong */
void gl_battle_fog(int r, int gg, int b) {
    GLS.bg[0] = r / 8.0f, GLS.bg[1] = gg / 8.0f, GLS.bg[2] = b / 8.0f;
    GLS.fog_on = 1;
}

/* VRAM to VRAM, in the copy */
void gl_copy(int sx, int sy, int w, int h, int dx, int dy) {
    gl_copy_to(sx, sy, w, h, (float)dx, dy, (float)w);
}

/* The same, to dw pixels across from dx (a widescreen battle's menus) */
void gl_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) {
    int s = GLS.scale;

    if (s == 0 || w <= 0 || h <= 0) {
        return;
    }
    gl_flush();
    if (sx + w > VRAM_W) w = VRAM_W - sx;
    if (dx + dw > VRAM_W) dw = VRAM_W - dx;
    if (sy + h > VRAM_H) h = VRAM_H - sy;
    if (dy + h > VRAM_H) h = VRAM_H - dy;
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, GLS.fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, GLS.tmp_fbo);
    GL(glBlitFramebuffer)(sx * s, sy * s, (sx + w) * s, (sy + h) * s, 0, 0, w * s, h * s, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, GLS.tmp_fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, GLS.fbo);
    GL(glBlitFramebuffer)(0, 0, w * s, h * s, (int)(dx * s + 0.5f), dy * s, (int)((dx + dw) * s + 0.5f), (dy + h) * s, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
}
