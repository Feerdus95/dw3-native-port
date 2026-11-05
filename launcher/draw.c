/* Rectangles, the baked font and the disc. */
#include "launcher.h"
#include "font_data.inc"

static SDL_Renderer *s_r; /* NULL: measuring only (--check) */
static Uint8 s_alpha = 255;

void draw_init(SDL_Renderer *r)
{
    s_r = r;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

void fill_alpha(int x, int y, int w, int h, Uint32 colour, Uint8 alpha)
{
    if (!s_r) {
        return;
    }
    SDL_FRect rect = {(float)x, (float)y, (float)w, (float)h};
    SDL_SetRenderDrawColor(s_r, colour >> 16 & 255, colour >> 8 & 255, colour & 255, alpha);
    SDL_RenderFillRect(s_r, &rect);
}

void fill(int x, int y, int w, int h, Uint32 colour) { fill_alpha(x, y, w, h, colour, s_alpha); }

void outline(int x, int y, int w, int h, Uint32 colour)
{
    fill(x, y, w, 1, colour);
    fill(x, y + h - 1, w, 1, colour);
    fill(x, y, 1, h, colour);
    fill(x + w - 1, y, 1, h, colour);
}

void panel(int x, int y, int w, int h)
{
    fill(x, y, w, h, C_PANEL);
    outline(x, y, w, h, C_EDGE);
}

/* the old screen's soft glow from the top */
void background(int w, int h)
{
    const int step = SDL_max(2, h / 135);
    fill(0, 0, w, h, C_BG);
    for (int y = 0; y < h; y += step) {
        const float k = 1 - (float)y / h;
        fill_alpha(0, y, w, step, C_GLOW, (Uint8)(70 * k * k));
    }
}

static int glyph(Uint32 cp)
{
    static short index[0x180];
    static bool ready;
    if (!ready) {
        for (int i = 0; i < (int)SDL_arraysize(FONT); i++) {
            index[FONT[i].cp] = (short)i;
        }
        ready = true;
    }
    return index[cp < 0x180 ? cp : '?'];
}

static int line(int x, int y, int scale, Uint32 colour, const char *s, const char *end)
{
    int start = x;
    while (s < end && *s) {
        const int g = glyph(SDL_StepUTF8(&s, NULL));
        for (int row = 0; row < FONT_H; row++) {
            for (int bits = FONT[g].rows[row], column = 0; bits; bits >>= 1, column++) {
                if (bits & 1) {
                    fill(x + column * scale, y + row * scale, scale, scale, colour);
                }
            }
        }
        x += FONT[g].advance * scale;
    }
    return x - start;
}

static int span_width(const char *s, const char *end)
{
    SDL_Renderer *kept = s_r;
    s_r = NULL;
    const int width = line(0, 0, 1, 0, s, end);
    s_r = kept;
    return width;
}

int text_width(const char *s, int scale) { return span_width(s, s + SDL_strlen(s)) * scale; }

static void shadow(int x, int y, int scale, const char *s, const char *end)
{
    s_alpha = 160;
    line(x + 1, y + 1, scale, 0, s, end);
    s_alpha = 255;
}

void text(int x, int y, int scale, Uint32 colour, const char *s)
{
    const char *end = s + SDL_strlen(s);
    shadow(x, y, scale, s, end);
    line(x, y, scale, colour, s, end);
}

void text_flat(int x, int y, Uint32 colour, const char *s) { line(x, y, 1, colour, s, s + SDL_strlen(s)); }

void text_centre(int y, int scale, Uint32 colour, const char *s) { text((SCREEN_W - text_width(s, scale)) / 2, y, scale, colour, s); }

void text_right(int right, int y, Uint32 colour, const char *s) { text(right - text_width(s, 1), y, 1, colour, s); }

int wrap(int x, int y, int width, Uint32 colour, const char *s)
{
    int lines = 0;
    while (*s) {
        const char *fit = s, *p = s;
        while (*p) {
            while (*p && *p != ' ') {
                p++;
            }
            if (span_width(s, p) > width && fit != s) {
                break;
            }
            fit = p;
            while (*p == ' ') {
                p++;
            }
        }
        shadow(x, y + lines * LINE_H, 1, s, fit);
        line(x, y + lines * LINE_H, 1, colour, s, fit);
        lines++;
        s = fit;
        while (*s == ' ') {
            s++;
        }
    }
    return lines;
}

void triangle(int x, int y, int half, int dir, Uint32 colour)
{
    for (int i = 0; i <= half; i++) {
        const int column = dir > 0 ? x + i : x + half - i;
        fill(column, y - (half - i), 1, (half - i) * 2 + 1, colour);
    }
}
