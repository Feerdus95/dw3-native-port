/* The bar's badge and the language flags, drawn from numbers: no image files. The badge is "DW3" slanted, yellow to orange with a
   navy edge, on the blue of the Digimon logo with its gold line under it. */
#include "launcher.h"

enum { ICON_W = 32, ICON_H = 22 };

static const char *const GLYPH[3][7] = {
    {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}, /* D */
    {"10001", "10001", "10001", "10101", "10101", "11011", "10001"}, /* W */
    {"01110", "10001", "00001", "00110", "00001", "10001", "01110"}, /* 3 */
};

/* the badge: w x h pixels, letters 5x7 cells drawn 2 wide (bold) and ry tall, pitch apart, slanted to the right going up */
static void badge(int x, int y, int w, int h, int ry, int pitch)
{
    static const Uint32 NAVY = 0x0A2A6A, BLUE = 0x1F5BC4, BLUE_HI = 0x3B7FE0, WHITE = 0xFFFFFF;
    bool m[ICON_H][ICON_W] = {{false}};
    const int top = (h - 7 * ry) / 2 - 1, rows = 7 * ry;
    for (int g = 0; g < 3; g++) {
        for (int r = 0; r < 7; r++) {
            for (int c = 0; c < 5; c++) {
                for (int sy = 0; GLYPH[g][r][c] == '1' && sy < ry; sy++) {
                    const int yy = r * ry + sy, py = top + yy, px = 3 + g * pitch + c + (rows - 1 - yy) / (5 * ry / 2);
                    if (py < h && px + 1 < w) {
                        m[py][px] = m[py][px + 1] = true;
                    }
                }
            }
        }
    }
    fill(x, y, w, h, NAVY);
    fill(x + 1, y + 1, w - 2, h - 2, BLUE);
    fill(x + 1, y + 1, w - 2, 3, BLUE_HI);
    fill(x, y, 1, 1, C_BG), fill(x + w - 1, y, 1, 1, C_BG);                      /* rounded corners */
    fill(x, y + h - 1, 1, 1, C_BG), fill(x + w - 1, y + h - 1, 1, 1, C_BG);
    fill(x + w - 4, y + 3, 1, 1, WHITE), fill(x + w - 3, y + h / 3, 1, 1, WHITE), fill(x + 2, y + h - 4, 1, 1, WHITE); /* the logo's sparkles */
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) { /* the edge: any neighbour of a letter pixel */
            for (int d = 0; d < 9 && !m[j][i]; d++) {
                const int a = i + d % 3 - 1, b = j + d / 3 - 1;
                if (a >= 0 && a < w && b >= 0 && b < h && m[b][a]) {
                    fill(x + i, y + j, 1, 1, NAVY);
                    break;
                }
            }
        }
    }
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (m[j][i]) { /* yellow to orange, top to bottom */
                const int t = (j - top) * 256 / rows;
                fill(x + i, y + j, 1, 1, 0xFF0000 | ((0xE6 - t * (0xE6 - 0x84) / 256) << 8) | (0x40 - t * 0x30 / 256));
            }
        }
    }
    fill(x + 4, y + h - 3, w - 8, 1, C_GOLD);
}

void art_icon(int x, int y) { badge(x, y, ICON_W, ICON_H, 2, 8); }

enum { FLAG_W = 16, FLAG_H = 10 };

/* a flag (en, es, fr, de, it) of FLAG_W x FLAG_H with a thin edge */
void art_flag(int x, int y, int lang)
{
    for (int j = 0; j < FLAG_H; j++) {
        for (int i = 0; i < FLAG_W; i++) {
            Uint32 c = C_EDGE;
            const int third = i * 3 / FLAG_W;
            if (lang == 1) {
                c = j < 3 || j > 6 ? 0xC60B1E : 0xFFC400;
            } else if (lang == 2 || lang == 4) {
                c = third == 0 ? (lang == 2 ? 0x0055A4 : 0x009246) : third == 1 ? 0xF4F4F4 : (lang == 2 ? 0xEF4135 : 0xCE2B37);
            } else if (lang == 3) {
                c = j < 4 ? 0x151515 : j < 7 ? 0xDD0000 : 0xFFCE00;
            } else { /* the Union Jack, as simply as it goes in 16x10 */
                const int d1 = SDL_abs(i * 10 - j * 16 - 5), d2 = SDL_abs((FLAG_W - 1 - i) * 10 - j * 16 - 5);
                const bool cross_w = SDL_abs(2 * i - 15) <= 4 || SDL_abs(2 * j - 9) <= 3, cross_r = SDL_abs(2 * i - 15) <= 1 || SDL_abs(2 * j - 9) <= 1;
                c = cross_r ? 0xC8102E : cross_w ? 0xFFFFFF : d1 < 9 || d2 < 9 ? (d1 < 4 || d2 < 4 ? 0xC8102E : 0xFFFFFF) : 0x1F3C88;
            }
            fill(x + i, y + j, 1, 1, c);
        }
    }
    outline(x - 1, y - 1, FLAG_W + 2, FLAG_H + 2, C_EDGE);
}
