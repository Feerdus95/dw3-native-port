/* The journal's pictures of the fields (native.h): <data>/postcards/XXX.jpg, a
   field's map seen from afar, XXX its mode in hexadecimal. They are the
   game's art, so none ship: the game makes them from the player's own files
   the first time it starts, in a process of its own (dw2003 --postcards),
   and a field without one yet keeps the journal's cross. Drawn by the
   graphics card, from the journal's own primitive (gpu_draw.c) */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "stage_tables.h"
/* JPEG, which SDL has not: a field's picture is a quarter of its PNG */
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function" /* the formats left out */
#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#pragma GCC diagnostic pop

#define FIRST 0x200 /* the fields' modes */
#define COUNT 0x100
#define MADE "complete-v2" /* the file that says they are all made; another version makes them anew */
#define PIC_W 624 /* the frame's inside (156 x 50), four times over */
#define PIC_H 200
#define TILE 128

static SDL_Surface *s_img[COUNT];
static unsigned s_tex[COUNT];
static Uint64 s_next[COUNT]; /* when to look for a missing one again: they may be being made */

static void picture_path(char *path, size_t size, int mode, const char *ext) {
    SDL_snprintf(path, size, "%s/postcards/%03X.%s", data_dir(), mode, ext);
}

static SDL_Surface *postcard(int mode) {
    char path[1200];
    SDL_Surface *s;
    uint8_t *px;
    int i = mode - FIRST, w, h, n;

    if (i < 0 || i >= COUNT) {
        return NULL;
    }
    if (s_img[i] == NULL && SDL_GetTicks() >= s_next[i]) {
        s_next[i] = SDL_GetTicks() + 1000;
        picture_path(path, sizeof path, mode, "jpg");
        if ((px = stbi_load(path, &w, &h, &n, 4)) != NULL) {
            if ((s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32)) != NULL) {
                for (n = 0; n < h; n++) {
                    memcpy((uint8_t *)s->pixels + n * s->pitch, px + n * w * 4, (size_t)w * 4);
                }
            }
            s_img[i] = s;
            stbi_image_free(px);
        }
    }
    return s_img[i];
}

int native_postcard(int mode) {
    return postcard(mode) != NULL;
}

void postcard_draw(int mode, float x, float y, float w, float h, int cx1, int cy1, int cx2, int cy2) {
    if (postcard(mode) != NULL) {
        gl_image(s_img[mode - FIRST], &s_tex[mode - FIRST], x, y, x + w, y + h, 0, cx1, cy1, cx2, cy2);
    }
}

/* --- making them --- */

#define FIELD_SPRITES_FILE 0x160 /* the field's own sprites (the hidden spots' among them) */
#define MAX_OBJECTS 256

static uint16_t s_vram[1024 * 512]; /* the stage's images, where the game loads them */
#define VRAM(x, y) s_vram[((y) & 511) * 1024 + ((x) & 1023)]

static uint32_t rd32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static int rd16(const uint8_t *p) {
    return (int16_t)(p[0] | p[1] << 8);
}

/* RLEN (data without the name is as it is): "RLEN", a size (not always the
   unpacked one), then runs (0x80 | n, the byte) and literals (n, n bytes),
   up to a 0; into out (malloc'd), its size */
static long unrlen(const uint8_t *p, long n, uint8_t **out) {
    long room = 0, o = 0, s = 8;

    *out = NULL;
    if (n < 8 || memcmp(p, "RLEN", 4) != 0) { /* not packed */
        if (n > 0 && (*out = malloc((size_t)n)) != NULL) {
            memcpy(*out, p, (size_t)n);
            return n;
        }
        return 0;
    }
    while (s < n && p[s] != 0) {
        int c = p[s++], len = c & 0x7F;

        if (s + (c & 0x80 ? 1 : len) > n) {
            break;
        }
        if (o + len > room) {
            uint8_t *more = realloc(*out, (size_t)(room = room * 2 + 65536));

            if (more == NULL) {
                break;
            }
            *out = more;
        }
        if (c & 0x80) {
            memset(*out + o, p[s++], (size_t)len);
        } else {
            memcpy(*out + o, p + s, (size_t)len);
            s += len;
        }
        o += len;
    }
    return o;
}

/* An archive's entry (a table of offsets, the unused ones 0, up to the
   first entry's data) and its size, or NULL */
static const uint8_t *archive_entry(const uint8_t *a, long size, int i, long *n) {
    uint32_t count = 0, off, next;
    long k;

    for (k = 0; a != NULL && k + 4 <= size && k < 4096 && count == 0; k += 4) {
        count = rd32(a + k) / 4;
    }
    if (i < 0 || (uint32_t)i >= count || ((long)i + 1) * 4 > size || (off = rd32(a + i * 4)) == 0 || (long)off >= size) {
        return NULL;
    }
    next = (uint32_t)i + 1 < count ? rd32(a + (i + 1) * 4) : 0;
    *n = (next > off && (long)next <= size ? (long)next : size) - (long)off;
    return a + off;
}

/* A TIM's block of w by h halfwords (after its 12-byte header) to (x0, y0) */
static void load_block(const uint8_t *p, const uint8_t *end, int x0, int y0) {
    int w = rd16(p + 8), h = rd16(p + 10), x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w && p + 12 + (y * w + x) * 2 + 2 <= end; x++) {
            VRAM(x0 + x, y0 + y) = (uint16_t)rd16(p + 12 + (y * w + x) * 2);
        }
    }
}

/* An archive's TIMs side by side from (ix, iy), 0x40 apart, the CLUTs of the
   4 and 8-bit ones to (cx, cy) (timLoaderLoadArchive) */
static void load_images(const uint8_t *a, long size, int ix, int iy, int cx, int cy) {
    const uint8_t *e, *p;
    uint8_t *tim;
    long n, len;
    int i;

    for (i = 0; (e = archive_entry(a, size, i, &n)) != NULL; i++, ix += 0x40) {
        if ((len = unrlen(e, n, &tim)) >= 20) {
            p = tim + 8;
            if (rd32(tim + 4) & 8) {
                if ((rd32(tim + 4) & 7) <= 1) {
                    load_block(p, tim + len, cx, cy);
                }
                p += rd32(p);
            }
            if (p + 12 <= tim + len) {
                load_block(p, tim + len, ix, iy);
            }
        }
        free(tim);
    }
}

/* A 15-bit colour as RGBA, by the field's (128: as it is) */
static uint32_t rgba15(int c, const int k[3]) {
    uint32_t out = 0xFFu << 24;
    int i, v;

    for (i = 0; i < 3; i++) {
        v = c >> i * 5 & 31;
        v = (v << 3 | v >> 2) * k[i] / 128;
        out |= (uint32_t)(v > 255 ? 255 : v) << i * 8;
    }
    return out;
}

/* One depth of a map's unpacked tile onto px (stride pixels a row)
   (FIELDSTG_loadStreamSprites: the sprites of each of the three depths, then
   the 8-bit TIM they are cut from; FIELDSTG_drawStream) */
static void draw_tile(const uint8_t *d, long size, int depth, const uint8_t color[4], uint32_t *px, int stride) {
    /* depth 2's sprites always take the field's colour, the others only when it is set */
    int tint = depth == 2 || color[3] != 0, k[3] = {tint ? color[0] : 128, tint ? color[1] : 128, tint ? color[2] : 128};
    const uint8_t *tim, *clut, *img, *pix;
    long p = 4;
    int tw, th, i, s, count, yy, xx;

    if (size < 8 || rd32(d) + 20 > (uint32_t)size) {
        return;
    }
    tim = d + rd32(d);
    clut = tim + 20, img = tim + 8;
    if (rd32(tim + 4) & 8) {
        img = tim + 8 + rd32(tim + 8);
    }
    if (img + 12 > d + size) {
        return;
    }
    tw = rd16(img + 8) * 2, th = rd16(img + 10), pix = img + 12;
    for (i = 0; i < depth && p + 4 <= size; i++) {
        p += 4 + (long)rd32(d + p) * 12;
    }
    if (p + 4 > size) {
        return;
    }
    count = (int)rd32(d + p);
    for (s = 0, p += 4; s < count && p + 12 <= size; s++, p += 12) {
        int x = rd16(d + p), y = rd16(d + p + 2), u = rd16(d + p + 4), v = rd16(d + p + 6), w = rd16(d + p + 8), h = rd16(d + p + 10);

        for (yy = 0; yy < h; yy++) {
            for (xx = 0; xx < w; xx++) {
                int dx = x + xx, dy = y + yy, tu = u + xx, tv = v + yy, c;

                if (dx < 0 || dy < 0 || dx >= TILE || dy >= TILE || tu < 0 || tv < 0 || tu >= tw || tv >= th ||
                    pix + (long)tv * tw + tu >= d + size) {
                    continue;
                }
                c = (uint16_t)rd16(clut + pix[tv * tw + tu] * 2);
                if (c != 0) {
                    px[dy * stride + dx] = rgba15(c, k);
                }
            }
        }
    }
}

/* A sheet's frame at (x, y) of px, w by h: its parts, the last first so the
   first ends on top, from the images at (tx, ty) (8-bit parts' CLUTs from
   (ax, ay)); a semi-transparent frame at half */
static void draw_frame(const uint8_t *sheet, long size, int frame, int tx, int ty, int ax, int ay, int clutRow, const int color[3],
                       int x, int y, uint32_t *px, int w, int h) {
    uint32_t partsOff, idsOff, frameOff;
    const uint8_t *p;
    int i = 0, count, frameRow, abr, k, yy, xx, c;

    if (sheet == NULL || size < 12 || (partsOff = rd32(sheet)) >= (uint32_t)size || (idsOff = rd32(sheet + 4)) >= (uint32_t)size) {
        return;
    }
    while ((long)idsOff + i < size && sheet[idsOff + i] != frame && i < 255) {
        i++;
    }
    if ((long)idsOff + i >= size || sheet[idsOff + i] != frame || ((long)i + 3) * 4 > size ||
        (frameOff = rd32(sheet + (i + 2) * 4)) + 6 > (uint32_t)size) {
        return;
    }
    p = sheet + frameOff;
    count = rd16(p), frameRow = rd16(p + 2), abr = rd16(p + 4);
    if (count <= 0 || (long)frameOff + 6 + (long)count * 6 > size) {
        return;
    }
    for (k = count - 1; k >= 0; k--) {
        const uint8_t *e = p + 6 + k * 6, *q = sheet + partsOff + (long)(uint16_t)rd16(e) * 14;
        int px0, py0, u, v, pw, ph, mode, cx, cy, pageX;

        if (q + 14 > sheet + size) {
            continue;
        }
        px0 = x + rd16(e + 2), py0 = y + rd16(e + 4);
        u = rd16(q), v = rd16(q + 2) & 0xFF, pw = rd16(q + 4), ph = rd16(q + 6), mode = rd16(q + 12);
        cx = ((mode ? ax : tx) + rd16(q + 8)) & 0x3F0;
        cy = ((mode ? ay - 0x100 : ty) + rd16(q + 10) + frameRow + clutRow) & 0x1FF;
        pageX = (tx + (mode ? u / 2 : u / 4)) & 0x3C0;
        u = mode ? u & 0x7F : u & 0xFF;
        for (yy = 0; yy < ph; yy++) {
            for (xx = 0; xx < pw; xx++) {
                int su = (u + xx) & 0xFF, sv = (v + yy) & 0xFF, dx = px0 + xx, dy = py0 + yy;
                int idx = mode ? VRAM(pageX + su / 2, (ty & 0x100) + sv) >> (su & 1) * 8 & 0xFF
                               : VRAM(pageX + su / 4, (ty & 0x100) + sv) >> (su & 3) * 4 & 0xF;

                if (dx < 0 || dy < 0 || dx >= w || dy >= h || (c = VRAM(cx + idx, cy)) == 0) {
                    continue;
                }
                px[dy * w + dx] = abr == -1 ? rgba15(c, color)
                                            : ((px[dy * w + dx] >> 1 & 0x7F7F7F) + (rgba15(c, color) >> 1 & 0x7F7F7F)) | 0xFFu << 24;
            }
        }
    }
}

/* The file of fid << 16 | entry, read (malloc'd), and the entry in it */
static const uint8_t *read_entry(int entry, uint8_t **file, long *n) {
    long size = 0;

    *file = NULL;
    if (entry != 0) {
        size = native_read_raw(entry >> 16, file);
    }
    return archive_entry(*file, size, entry & 0xFFFF, n);
}

/* The stage FIELDSTG_state is set up for, as the field shows it: its map's
   tiles (a header sector: the tiles across and down and a tile's room; then
   each tile's data) at their three depths and its objects at theirs, the
   farthest first. w by h (malloc'd), or NULL */
static uint32_t *draw_stage(int *w, int *h) {
    static const int TILE_DEPTHS[3] = {7, 3, 11};
    static struct {
        uint8_t *d;
        long size;
    } tiles[1000];
    struct {
        int depth, tile;
        const StageTile *object;
    } items[3 + MAX_OBJECTS], item;
    FieldState *st = &FIELDSTG_state;
    uint8_t *map, *sheetFile, *spritesFile, *imageFile = NULL;
    long size = native_read_raw(st->mapFile, &map), sheetSize = 0, spriteSheetSize = 0, spritesSize, n;
    int tw = size >= 2048 ? (int)rd32(map + 4) : 0, th = tw ? (int)rd32(map + 8) : 0, room = tw ? (int)rd32(map + 12) : 0;
    int count = 0, i, j, t, col[3] = {128, 128, 128}, tinted[3] = {st->spriteColor[0], st->spriteColor[1], st->spriteColor[2]};
    const uint8_t *sheet = read_entry(st->sheetEntry, &sheetFile, &sheetSize), *spriteSheet, *e;
    const StageTile *o;
    uint32_t *px = NULL;

    spritesSize = native_read_raw(FIELD_SPRITES_FILE, &spritesFile);
    spriteSheet = archive_entry(spritesFile, spritesSize, 0, &spriteSheetSize);
    if (tw > 0 && th > 0 && tw * th <= 1000 && room >= 2048 && (px = malloc((size_t)tw * th * TILE * TILE * 4)) != NULL) {
        *w = tw * TILE, *h = th * TILE;
        for (t = 0; t < *w * *h; t++) {
            px[t] = 0xFFu << 24;
        }
        for (t = 0; t < tw * th; t++) {
            long len = (uint16_t)rd16(map + 16 + t * 2), off = ((long)t * (room / 2048) + 1) * 2048;

            tiles[t].d = NULL;
            tiles[t].size = len != 0 && off + 8 <= size ? unrlen(map + off, off + len <= size ? len : size - off, &tiles[t].d) : 0;
        }
        /* the objects' images: the field's sprites at (0x200, 0x100) and (0x240, 0x100), the stage's (a file of
           its own, or an entry of one) at (0x140, 0x100) with their CLUTs at (0, 0x1F0) (FIELDSTG_loadFieldFiles) */
        memset(s_vram, 0, sizeof s_vram);
        if ((e = archive_entry(spritesFile, spritesSize, 2, &n)) != NULL) {
            load_images(e, n, 0x200, 0x100, 0, 0);
        }
        if ((e = archive_entry(spritesFile, spritesSize, 3, &n)) != NULL) {
            load_images(e, n, 0x240, 0x100, 0, 0);
        }
        if (st->imageFile != 0) {
            n = native_read_raw(st->imageFile, &imageFile);
            load_images(imageFile, n, 0x140, 0x100, 0, 0x1F0);
        } else if ((e = read_entry(st->imageEntry, &imageFile, &n)) != NULL) {
            load_images(e, n, 0x140, 0x100, 0, 0x1F0);
        }
        /* by depth, the farthest first; the tiles before the objects of their depth */
        for (i = 0; i < 3; i++) {
            items[count].depth = TILE_DEPTHS[i], items[count].tile = i, items[count++].object = NULL;
        }
        for (o = st->objects; o != NULL && o->margin != 0 && count < 3 + MAX_OBJECTS; o++) { /* they end with margin 0 */
            if (o->visible) {
                items[count].depth = o->depth, items[count].tile = -1, items[count++].object = o;
            }
        }
        for (i = 1; i < count; i++) {
            for (item = items[i], j = i; j > 0 && items[j - 1].depth < item.depth; j--) {
                items[j] = items[j - 1];
            }
            items[j] = item;
        }
        for (i = 0; i < count; i++) {
            o = items[i].object;
            if (o == NULL) {
                for (t = 0; t < tw * th; t++) {
                    draw_tile(tiles[t].d, tiles[t].size, items[i].tile, st->spriteColor, px + (t / tw * TILE) * *w + t % tw * TILE, *w);
                }
            } else if (o->anim == 0xFF) { /* a hidden spot: the field's own sprites */
                draw_frame(spriteSheet, spriteSheetSize, o->frame, 0x200, 0x100, 0, 0x1F0, o->clutRow, col, o->x, o->y, px, *w, *h);
            } else {
                draw_frame(sheet, sheetSize, o->frame, 0x140, 0x100, 0, 0x1F0, o->clutRow, st->spriteColor[3] != 0 ? tinted : col, o->x,
                           o->y, px, *w, *h);
            }
        }
        for (t = 0; t < tw * th; t++) {
            free(tiles[t].d);
        }
    }
    free(map), free(sheetFile), free(spritesFile), free(imageFile);
    return px;
}

/* The picture's middle at the frame's shape, as wide a view as fills it,
   each of the frame's pixels the mean of what it covers */
static void shrink(const uint32_t *px, int w, int h, uint32_t *out) {
    int cw = w < h * PIC_W / PIC_H ? w : h * PIC_W / PIC_H, ch = h < w * PIC_H / PIC_W ? h : w * PIC_H / PIC_W;
    int left = (w - cw) / 2, top = (h - ch) / 2, x, y, sx, sy, k;

    for (y = 0; y < PIC_H; y++) {
        for (x = 0; x < PIC_W; x++) {
            int x0 = left + x * cw / PIC_W, x1 = left + (x + 1) * cw / PIC_W, y0 = top + y * ch / PIC_H, y1 = top + (y + 1) * ch / PIC_H;
            unsigned sum[3] = {0, 0, 0}, n;

            x1 = x1 > x0 ? x1 : x0 + 1, y1 = y1 > y0 ? y1 : y0 + 1;
            n = (unsigned)((x1 - x0) * (y1 - y0));
            for (sy = y0; sy < y1; sy++) {
                for (sx = x0; sx < x1; sx++) {
                    for (k = 0; k < 3; k++) {
                        sum[k] += px[sy * w + sx] >> k * 8 & 255;
                    }
                }
            }
            out[y * PIC_W + x] = (sum[0] + n / 2) / n | (sum[1] + n / 2) / n << 8 | (sum[2] + n / 2) / n << 16 | 0xFFu << 24;
        }
    }
}

/* The modes a table's stage runs that have no picture yet: counted, or (s)
   each one's written whole, then named */
static int save(const uint32_t *s, const StageEntry *e, const StageEntry *end, void *start, uint8_t *made) {
    char path[1200], tmp[1200];
    const StageEntry *table = e, *first;
    int n = 0;

    for (; e != end && e->mode != 0; e++) {
        for (first = table; first->mode != e->mode; first++) { /* the one FIELDSTG_pickStage finds */
        }
        if (first == e && e->init == start && e->mode >= FIRST && e->mode < FIRST + COUNT && !made[e->mode - FIRST]) {
            n++;
            picture_path(path, sizeof path, e->mode, "jpg");
            picture_path(tmp, sizeof tmp, e->mode, "tmp");
            if (s != NULL && stbi_write_jpg(tmp, PIC_W, PIC_H, 4, s, 80)) {
                SDL_RemovePath(path);
                SDL_RenamePath(tmp, path);
                made[e->mode - FIRST] = 1;
            }
        }
    }
    return n;
}

/* dw2003 --postcards: each stage's setup run on a cleared FIELDSTG_state, as
   the stage dump does, and the stage drawn. The story's table first: the
   extra chapter's has other stages for some of its modes */
int postcards_make(void) {
    static uint32_t out[PIC_W * PIC_H];
    static uint8_t made[COUNT];
    FieldState *st = &FIELDSTG_state;
    char path[1200];
    const StageFuncs *stage;
    int n = 0, extra, w, h;

    SDL_snprintf(path, sizeof path, "%s/postcards", data_dir());
    SDL_CreateDirectory(path);
    for (w = FIRST; w < FIRST + COUNT; w++) { /* the PNGs they were at first */
        picture_path(path, sizeof path, w, "png");
        SDL_RemovePath(path);
    }
    SDL_snprintf(path, sizeof path, "%s/postcards/made1", data_dir()); /* and their marker */
    SDL_RemovePath(path);
    for (extra = 0; extra < 2; extra++) {
        const StageEntry *table = extra ? FIELDSTG_extraStages : FIELDSTG_stages, *end = extra ? FIELDSTG_stages : NULL;

        for (stage = native_stage_list; stage->name != NULL; stage++) {
            uint32_t *px;

            if (save(NULL, table, end, stage->start, made) == 0) {
                continue;
            }
            native_reset_overlays(1);
            memset(st, 0, 0x64);
            stage->setup();
            if (st->mapFile == 0 || (px = draw_stage(&w, &h)) == NULL) {
                continue;
            }
            shrink(px, w, h, out);
            free(px);
            n += save(out, table, end, stage->start, made);
        }
    }
    native_reset_overlays(1);
    SDL_snprintf(path, sizeof path, "%s/postcards/" MADE, data_dir());
    SDL_SaveFile(path, "", 0);
    LOG("%d fields' pictures in %s/postcards", n, data_dir());
    return 0;
}

/* The game starting: made now, apart, unless they were */
void postcards_start(void) {
    char path[1200], exe[1200];
    const char *args[] = {exe, "--postcards", NULL};
    SDL_Process *p;

    SDL_snprintf(path, sizeof path, "%s/postcards/" MADE, data_dir());
    SDL_snprintf(exe, sizeof exe, "%sdw2003.exe", SDL_GetBasePath());
    if (!SDL_GetPathInfo(path, NULL) && (p = SDL_CreateProcess(args, false)) != NULL) {
        SDL_DestroyProcess(p); /* it goes on by itself */
    }
}
