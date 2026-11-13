#include "menu_skin.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>

/* The game's panel colours (the Digilog's COLOR_*) */
#define FILL 0x00, 0x00, 0x21
#define BORDER 0x6B, 0x6B, 0xD6
#define RULE 0x32, 0x32, 0x8B
#define CHOSEN 0x14, 0x3B, 0x65
#define MARK 0x34, 0xC5, 0xF4
#define CUT 3

static void color(SDL_Renderer *r, Uint8 red, Uint8 green, Uint8 blue) {
    SDL_SetRenderDrawColor(r, red, green, blue, 255);
}

void skin_panel(SDL_Renderer *r, const SDL_FRect *box) {
    float right = box->x + box->w - 1, bottom = box->y + box->h - 1;

    color(r, FILL);
    SDL_RenderFillRect(r, box);
    color(r, BORDER);
    SDL_RenderLine(r, box->x, box->y, right, box->y);
    SDL_RenderLine(r, right, box->y, right, bottom);
    SDL_RenderLine(r, right, bottom, box->x + CUT, bottom);
    SDL_RenderLine(r, box->x + CUT, bottom, box->x, bottom - CUT);
    SDL_RenderLine(r, box->x, bottom - CUT, box->x, box->y);
}

void skin_rule(SDL_Renderer *r, float x0, float x1, float y) {
    color(r, RULE);
    SDL_RenderLine(r, x0, y, x1, y);
}

void skin_row(SDL_Renderer *r, const SDL_FRect *bar) {
    SDL_FRect mark = {bar->x, bar->y, 2, bar->h};

    color(r, CHOSEN);
    SDL_RenderFillRect(r, bar);
    color(r, MARK);
    SDL_RenderFillRect(r, &mark);
}

void skin_tab(SDL_Renderer *r, const SDL_FRect *plate) {
    skin_row(r, plate);
}

void skin_thumb(SDL_Renderer *r, const SDL_FRect *thumb) {
    color(r, 255, 255, 255);
    SDL_RenderFillRect(r, thumb);
}

void skin_ink(SDL_Renderer *r, int which) {
    switch (which) {
    case INK_DIM: color(r, 0x8A, 0x8A, 0xB8); break;
    case INK_HEAD: color(r, 0x6B, 0x6B, 0xD6); break;
    case INK_VALUE: color(r, 0x91, 0xDB, 0xB6); break; /* the Digilog's "done" green */
    case INK_NOTE: color(r, 0x34, 0xC5, 0xF4); break;
    default: color(r, 255, 255, 255); break;
    }
}

/* The game's font (FONT_STYLES[3], 9 px lines), drawn from the glyph sheets the game
   loaded into VRAM (loadFont: pages at x 0x140 + 64 * page, y 0; the glyph's 4-bit
   colours through its palette at x 0x140 + clutX, y clutY). A string becomes a white
   texture, tinted by the ink and kept; the debug font stands in until the sheets are there. */
extern uint8_t FONT_GLYPHS_3[]; /* Glyph, 11 bytes: page, u, v, clutX, clutY, w, h, dx, dy, advance */
typedef struct GlyphMap {
    uint16_t code;
    uint8_t index, pad;
} GlyphMap;
extern GlyphMap FONT_GLYPH_MAP[];
extern GlyphMap FONT_ICON_MAP[];
extern uint8_t FONT_ICONS_3[];

#define FONT_X 0x140
#define GLYPH_SIZE 11
#define PAD_TOP 4 /* room over the line for a glyph's dy */
#define LINE_H 20
#define SPACE 4

/* The Shift-JIS code of an ASCII character, as the font maps it: wide letters and digits, the
   punctuation of the first block */
static unsigned sjis(int c) {
    static const struct {
        char c;
        unsigned short code;
    } PUNCT[] = {{' ', 0x8140}, {',', 0x8143}, {'.', 0x8144}, {':', 0x8146}, {'?', 0x8148}, {'!', 0x8149}, {'\'', 0x8166},
                 {'(', 0x8169}, {')', 0x816A}, {'+', 0x817B}, {'-', 0x817C}, {'/', 0x815E}, {'%', 0x8193}, {'<', 0x8183}, {'>', 0x8184}};
    unsigned i;

    if (c >= '0' && c <= '9') {
        return 0x824F + c - '0';
    }
    if (c >= 'A' && c <= 'Z') {
        return 0x8260 + c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return 0x8281 + c - 'a';
    }
    for (i = 0; i < SDL_arraysize(PUNCT); i++) {
        if (PUNCT[i].c == c) {
            return PUNCT[i].code;
        }
    }
    return 0;
}

/* The glyph of a character, or NULL (a space or nothing the font has): letters and digits are in the
   glyph map (an index counts its 4 control codes), the punctuation in the icon map (1 control code) */
static const uint8_t *glyph_of(int c) {
    unsigned code = sjis(c);
    int j;

    for (j = 4; code != 0 && FONT_GLYPH_MAP[j].code != 0xFFFF && j < 1000; j++) {
        if (FONT_GLYPH_MAP[j].code == code) {
            const uint8_t *gl = FONT_GLYPHS_3 + (FONT_GLYPH_MAP[j].index - 4) * GLYPH_SIZE;

            return gl[0] == 0xFF ? NULL : gl;
        }
    }
    for (j = 1; code != 0 && FONT_ICON_MAP[j].code != 0xFFFF && j < 1000; j++) {
        if (FONT_ICON_MAP[j].code == code) {
            const uint8_t *gl = FONT_ICONS_3 + (FONT_ICON_MAP[j].index - 1) * GLYPH_SIZE;

            return gl[0] == 0xFF ? NULL : gl;
        }
    }
    return NULL;
}

/* The font's file (FILE_FONT, EU), read from the disc into a copy of its own: the menu does not wait
   for the game's loadFont, which runs after the language selector. Same layout as the game's VRAM. */
#define FILE_FONT 0x287
#define RLEN_MAGIC 0x4E454C52
static uint16_t *s_sheet; /* VRAM_W x 512 halfwords */

static uint32_t word(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void load_sheet(void) {
    static int tried;
    uint8_t *file, *buf;
    const uint8_t *archive;
    long size;
    int i;

    if (tried) {
        return;
    }
    tried = 1;
    size = native_read_raw(FILE_FONT, &file);
    buf = SDL_malloc(0x20000);
    s_sheet = SDL_calloc(VRAM_W * 512, sizeof *s_sheet);
    if (file == NULL || buf == NULL || s_sheet == NULL) {
        SDL_free(s_sheet);
        s_sheet = NULL;
        size = 0;
    }
    archive = size > 8 ? file + word(file) : NULL; /* the file is a table of one entry, the archive */
    if (archive != NULL && word(file) > (uint32_t)size - 8) {
        archive = NULL;
    }
    for (i = 0; archive != NULL && word(archive + i * 4) != 0 && word(archive + i * 4) < (uint32_t)(file + size - archive) && i < 16; i++) { /* the table ends with a 0 */
        const uint8_t *tim = archive + word(archive + i * 4), *blk;
        int x, y, w, h, r;

        if (word(tim) == RLEN_MAGIC) { /* run-length packed */
            const uint8_t *src = tim + 8;
            uint8_t *dst = buf;

            while (*src != 0 && dst < buf + 0x1FF00) {
                int n = *src & 0x7F;

                if (*src++ & 0x80) {
                    memset(dst, *src++, n);
                    dst += n;
                } else {
                    memcpy(dst, src, n);
                    dst += n;
                    src += n;
                }
            }
            tim = buf;
        }
        blk = tim + 8;
        if (word(tim + 4) & 8) { /* a CLUT first */
            blk += word(blk);
        }
        x = FONT_X + i * 64;
        y = 0;
        w = blk[8] | blk[9] << 8;
        h = blk[10] | blk[11] << 8;
        blk += 12;
        for (r = 0; r < h && y + r < 512; r++) {
            for (int c = 0; c < w && x + c < VRAM_W; c++) {
                s_sheet[(y + r) * VRAM_W + x + c] = blk[(r * w + c) * 2] | blk[(r * w + c) * 2 + 1] << 8;
            }
        }
    }
    free(file); /* native_read_raw's calloc */
    SDL_free(buf);
}

static int nibble(const uint8_t *gl, int x, int y) {
    int u = (gl[1] + x) & 255, v = (gl[2] + y) & 255; /* texture coordinates wrap at 256 */

    return s_sheet[v * VRAM_W + FONT_X + gl[0] * 64 + u / 4] >> (u % 4 * 4) & 15;
}

/* Whether the sheets are read: an 'A' with some ink */
static int font_ready(void) {
    const uint8_t *gl;
    int x, y;

    load_sheet();
    gl = glyph_of('A');
    if (gl == NULL || s_sheet == NULL || gl[5] == 0 || gl[6] == 0) {
        return 0;
    }
    for (y = 0; y < gl[6]; y++) {
        for (x = 0; x < gl[5]; x++) {
            if (nibble(gl, x, y) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Signs the font has no glyph for in this style, drawn as bitmaps (the font's capitals are 7 high) */
static const struct {
    char c;
    uint8_t w, rows[7]; /* bit 0 is the left column */
} SIGNS[] = {
    {'>', 5, {0x01, 0x02, 0x04, 0x08, 0x04, 0x02, 0x01}},
    {'<', 5, {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'%', 7, {0x23, 0x33, 0x10, 0x18, 0x0C, 0x66, 0x62}},
};

static int sign_of(int c) {
    int i;

    for (i = 0; i < (int)SDL_arraysize(SIGNS); i++) {
        if (SIGNS[i].c == c) {
            return i;
        }
    }
    return -1;
}

static int advance(int c) {
    const uint8_t *gl = glyph_of(c);
    int i;

    if (gl == NULL && (i = sign_of(c)) >= 0) {
        return SIGNS[i].w + 1 + (c == '%'); /* a % stands off its number */
    }
    return gl != NULL ? gl[9] + (int8_t)gl[7] : SPACE; /* as measureText: advance + dx */
}

float skin_text_width(const char *s) {
    float w = 0;

    if (!font_ready()) {
        return 8.0f * SDL_strlen(s);
    }
    for (; *s; s++) {
        w += advance((unsigned char)*s);
    }
    return w;
}

static SDL_Texture *line_texture(SDL_Renderer *r, const char *s, int width) {
    SDL_Surface *surf = SDL_CreateSurface(width, LINE_H, SDL_PIXELFORMAT_ABGR8888);
    SDL_Texture *t = NULL;
    int pen = 0;

    if (surf == NULL) {
        return NULL;
    }
    SDL_ClearSurface(surf, 0, 0, 0, 0);
    for (; *s; s++) {
        const uint8_t *gl = glyph_of((unsigned char)*s);
        int x, y;

        if (gl == NULL) {
            int i = sign_of((unsigned char)*s), row, col;
            const uint8_t *ref = glyph_of('5'); /* a digit's top and shading, row by row */

            if (i >= 0 && s[0] == '%') {
                pen++;
            }
            if (i >= 0 && ref != NULL) {
                for (row = 0; row < 7 && row < ref[6]; row++) {
                    uint32_t shade = 0;
                    int py = PAD_TOP + (int8_t)ref[8] + row;

                    for (col = 0; col < ref[5]; col++) {
                        int n = nibble(ref, col, row);
                        unsigned c = n != 0 ? s_sheet[ref[4] * VRAM_W + FONT_X + ref[3] + n] : 0;
                        unsigned l = SDL_max(SDL_max(c & 31, c >> 5 & 31), c >> 10 & 31) << 3;

                        if (c != 0 && l >= (shade & 255)) {
                            shade = l | l << 8 | l << 16 | 0xFF000000u;
                        }
                    }
                    for (col = 0; col < SIGNS[i].w; col++) {
                        if (SIGNS[i].rows[row] >> col & 1 && pen + col < width && py >= 0 && py < LINE_H) {
                            ((uint32_t *)((uint8_t *)surf->pixels + py * surf->pitch))[pen + col] = shade;
                        }
                    }
                }
            }
            pen += i >= 0 ? SIGNS[i].w + 1 : SPACE;
            continue;
        }
        for (y = 0; y < gl[6]; y++) {
            for (x = 0; x < gl[5]; x++) {
                int i = nibble(gl, x, y), px = pen + (int8_t)gl[7] + x, py = PAD_TOP + (int8_t)gl[8] + y;
                unsigned c;

                if (i == 0 || px < 0 || px >= width || py < 0 || py >= LINE_H) {
                    continue;
                }
                c = s_sheet[gl[4] * VRAM_W + FONT_X + gl[3] + i];
                if (c != 0) { /* the palette's brightness only: the ink tints it, whichever palette row the font has */
                    unsigned l = SDL_max(SDL_max(c & 31, c >> 5 & 31), c >> 10 & 31) << 3;

                    ((uint32_t *)((uint8_t *)surf->pixels + py * surf->pitch))[px] = l | l << 8 | l << 16 | 0xFF000000u;
                }
            }
        }
        pen += gl[9] + (int8_t)gl[7];
    }
    t = SDL_CreateTextureFromSurface(r, surf);
    SDL_DestroySurface(surf);
    if (t != NULL) {
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    }
    return t;
}

void skin_text(SDL_Renderer *r, float x, float y, const char *s) {
    enum { SLOTS = 96 };
    static struct {
        char s[48];
        SDL_Texture *t;
        int w;
    } cache[SLOTS];
    static int used;
    Uint8 red, green, blue, alpha;
    int i, w;

    if (SDL_strlen(s) >= sizeof cache[0].s || !font_ready()) {
        SDL_RenderDebugText(r, x, y, s);
        return;
    }
    for (i = 0; i < used && SDL_strcmp(cache[i].s, s) != 0; i++) {
    }
    if (i == used) {
        if (used == SLOTS) { /* full: start over */
            for (i = 0; i < SLOTS; i++) {
                SDL_DestroyTexture(cache[i].t);
            }
            used = 0;
        }
        w = (int)skin_text_width(s) + 2;
        SDL_strlcpy(cache[used].s, s, sizeof cache[used].s);
        cache[used].w = w;
        cache[used].t = line_texture(r, s, w);
        i = used++;
    }
    if (cache[i].t == NULL) {
        SDL_RenderDebugText(r, x, y, s);
        return;
    }
    SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
    SDL_SetTextureColorMod(cache[i].t, red, green, blue);
    SDL_RenderTexture(r, cache[i].t, NULL, &(SDL_FRect){x, y - PAD_TOP, (float)cache[i].w, LINE_H});
}
