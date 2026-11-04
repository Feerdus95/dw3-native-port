/* The home screen once the disc is read: a night scene, a bar on top with the pages, the title, chips and Play. */
#include "launcher.h"

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif

#define GAME_NAME "Digimon World 2003 (Europe)"

enum {
    BAR_H = 24, MARGIN = 24, NAV_X = 160, NAV_PAD = 5, NAV_END_MAX = 380, TITLE_Y = 42, SUB_Y = 72,
    CHIPS_Y = 96, CHIP_H = 14, CHIP_GAP = 6, BTN_Y = 122, BTN_H = 34, BTN_GAP = 10, SCENE_BASE = 176
};

static bool inside(SDL_Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

/* ---- where things are, for the language given: drawing, the mouse and --check use the same numbers ---- */

/* the pages: Play (this one), Settings and Tools */
static SDL_Rect nav_rect(const Lang *l, int id)
{
    int x = NAV_X;
    if (id != S_PLAY) {
        x += text_width(l->s[S_PLAY], 1) + 2 * NAV_PAD + 2;
    }
    if (id == S_TOOLS) {
        x += text_width(l->s[S_SETTINGS], 1) + 2 * NAV_PAD + 2;
    }
    return (SDL_Rect){x, 3, text_width(l->s[id], 1) + 2 * NAV_PAD, BAR_H - 6};
}

static void chip_text(const Lang *l, int i, const char *tag, char *out, int size)
{
    if (i == 0) {
        SDL_snprintf(out, size, "%s SLES-03936", l->s[S_DISC_OK]);
    } else if (i == 1) {
        SDL_snprintf(out, size, "%s", LAUNCHER_VERSION);
    } else {
        SDL_snprintf(out, size, "%s %s", l->s[S_RELEASE], tag);
    }
}

static SDL_Rect chip_rect(const Lang *l, int i, const char *tag)
{
    char s[96];
    int x = MARGIN;
    for (int k = 0; k < i; k++) {
        chip_text(l, k, tag, s, sizeof s);
        x += text_width(s, 1) + 12 + CHIP_GAP;
    }
    chip_text(l, i, tag, s, sizeof s);
    return (SDL_Rect){x, CHIPS_Y, text_width(s, 1) + 12, CHIP_H};
}

static SDL_Rect play_rect(const Lang *l) { return (SDL_Rect){MARGIN, BTN_Y, SDL_max(104, text_width(l->s[S_PLAY], 2) + 52), BTN_H}; }

static SDL_Rect quit_rect(const Lang *l)
{
    const SDL_Rect p = play_rect(l);
    return (SDL_Rect){p.x + p.w + BTN_GAP, BTN_Y, text_width(l->s[S_QUIT], 1) + 28, BTN_H};
}

static SDL_Rect rect_of(const Lang *l, int id, const char *tag)
{
    switch (id) {
    case S_PLAY: return play_rect(l);
    case S_QUIT: return quit_rect(l);
    case S_SETTINGS:
    case S_TOOLS: return nav_rect(l, id);
    case S_RELEASE: return chip_rect(l, 2, tag);
    default: return (SDL_Rect){0, 0, 0, 0};
    }
}

bool home_fits(const Lang *l)
{
    const SDL_Rect tools = nav_rect(l, S_TOOLS), chip = chip_rect(l, 2, "v99.99.99"), quit = quit_rect(l);
    return tools.x + tools.w <= NAV_END_MAX && text_width(l->s[S_READY], 2) <= 340 && text_width(l->s[S_TAGLINE], 1) <= NAV_X - 40 &&
           chip.x + chip.w <= SCREEN_W - MARGIN && quit.x + quit.w <= SCREEN_W - MARGIN;
}

int home_hit(int x, int y)
{
    const Row *r = rows();
    for (int i = 0; i < list_rows(r); i++) {
        if (r[i].kind == R_ACTION && inside(rect_of(L, r[i].id, release_tag() ? release_tag() : ""), x, y)) {
            return i;
        }
    }
    return -1;
}

/* ---- the scene ---- */

static int tri(int x, int period) /* a triangle wave from 0 to period / 2 */
{
    const int p = x % period;
    return p < period / 2 ? p : period - p;
}

static void disc(int cx, int cy, int r, Uint32 colour, Uint8 alpha)
{
    for (int dy = -r; dy <= r; dy++) {
        const int w = (int)SDL_sqrtf((float)(r * r - dy * dy));
        fill_alpha(cx - w, cy + dy, 2 * w + 1, 1, colour, alpha);
    }
}

/* stars, a moon with a halo, two ranges of hills that sink to the left, and a few lights in the valley */
static void scene(void)
{
    Uint32 h = 12345;
    for (int i = 0; i < 48; i++) {
        h = h * 1103515245u + 12345u;
        fill(150 + (int)(h >> 8) % (SCREEN_W - 150), BAR_H + 4 + (int)(h >> 20) % 100, 1, 1, i % 5 ? C_FAINT : C_SOFT);
    }
    disc(392, 64, 40, C_SOFT, 10);
    disc(392, 64, 28, C_SOFT, 14);
    disc(392, 64, 13, C_MOON, 255);
    disc(388, 60, 3, C_MOON_DARK, 255);
    disc(397, 68, 2, C_MOON_DARK, 255);
    for (int layer = 0; layer < 2; layer++) {
        for (int x = 0; x < SCREEN_W; x++) {
            int height = layer ? 14 + tri(x + 40, 90) * 2 / 3 + tri(x, 30) / 3 : 30 + tri(x, 140) / 2 + tri(x + 17, 46) / 3;
            height = height * (40 + x * 60 / SCREEN_W) / 100;
            fill(x, SCENE_BASE - height, 1, height, layer ? C_HILL_NEAR : C_HILL_FAR);
        }
    }
    for (int x = 300; x < 460; x += 23) {
        fill_alpha(x, SCENE_BASE - 5 - (x % 3) * 4, 2, 2, C_GOLD, 170);
    }
}

/* ---- the screen ---- */

static void draw_bar(const Lang *l, int cur)
{
    fill_alpha(0, 0, SCREEN_W, BAR_H, C_BG, 235);
    fill(0, BAR_H - 1, SCREEN_W, 1, C_RULE);
    art_disc(6, 1);
    text(32, 3, 1, C_GOLD, "DIGIMON WORLD 2003");
    text(32, 13, 1, C_SOFT, l->s[S_TAGLINE]);
    static const short PAGES[3] = {S_PLAY, S_SETTINGS, S_TOOLS};
    for (int i = 0; i < 3; i++) {
        const SDL_Rect t = nav_rect(l, PAGES[i]);
        const bool sel = PAGES[i] == cur;
        if (sel) {
            fill(t.x, t.y, t.w, t.h, C_SELECT);
        }
        text(t.x + NAV_PAD, t.y + 3, 1, sel ? C_GOLD : i == 0 ? C_TEXT : C_SOFT, l->s[PAGES[i]]);
        if (i == 0) {
            fill(t.x + 4, BAR_H - 4, t.w - 8, 2, C_GOLD);
        }
    }
}

static void draw_chips(const Lang *l, int cur)
{
    const char *tag = release_tag();
    for (int i = 0; i < (tag ? 3 : 2); i++) {
        char s[96];
        const SDL_Rect c = chip_rect(l, i, tag ? tag : "");
        const bool news = i == 2, sel = news && cur == S_RELEASE;
        chip_text(l, i, tag ? tag : "", s, sizeof s);
        fill_alpha(c.x, c.y, c.w, c.h, sel ? C_SELECT : C_PANEL, 225);
        outline(c.x, c.y, c.w, c.h, news ? C_GOLD : C_EDGE);
        text(c.x + 6, c.y + 1, 1, news ? C_GOLD : C_TEXT, s);
    }
}

static void draw_buttons(const Lang *l, int cur)
{
    const SDL_Rect p = play_rect(l), q = quit_rect(l);
    const bool play = cur == S_PLAY, quit = cur == S_QUIT;
    fill_alpha(p.x, p.y + 3, p.w, p.h, 0x000000, 120);
    fill(p.x, p.y, p.w, p.h, play ? C_GOLD_HI : C_GOLD);
    fill_alpha(p.x, p.y + p.h - 3, p.w, 3, 0x000000, 50);
    if (play) {
        outline(p.x, p.y, p.w, p.h, C_TEXT);
    }
    triangle(p.x + 14, p.y + BTN_H / 2, 6, 1, C_BG);
    text(p.x + 34, p.y + (BTN_H - 2 * LINE_H) / 2 + 1, 2, C_BG, l->s[S_PLAY]);
    fill_alpha(q.x, q.y, q.w, q.h, quit ? C_SELECT : C_PANEL, 225);
    outline(q.x, q.y, q.w, q.h, quit ? C_GOLD : C_EDGE);
    text(q.x + 14, q.y + (BTN_H - LINE_H) / 2, 1, quit ? C_GOLD : C_TEXT, l->s[S_QUIT]);
}

void home_draw(void)
{
    const Lang *l = L;
    const Row *r = &rows()[g_app.sel[SC_HOME]];
    const int cur = r->kind == R_ACTION ? r->id : -1;
    scene();
    draw_bar(l, cur);
    text(MARGIN, TITLE_Y, 2, C_TEXT, l->s[S_READY]);
    text(MARGIN, SUB_Y, 1, C_SOFT, GAME_NAME);
    draw_chips(l, cur);
    draw_buttons(l, cur);
}
