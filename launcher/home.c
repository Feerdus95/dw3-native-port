/* The home screen once the disc is read: a night scene, a bar on top with the pages, the title, chips and Play. */
#include "launcher.h"

#ifndef LAUNCHER_VERSION
#define LAUNCHER_VERSION "dev"
#endif

#define GAME_NAME "Digimon World 2003 (Europe)"

enum {
    BAR_H = 28, MARGIN = 16, NAV_X = 160, NAV_PAD = 3, NAV_END_MAX = 444, TITLE_X = 38, TITLE_Y = 38, SUB_Y = 66,
    CHIPS_Y = 86, CHIP_H = 14, CHIP_GAP = 6, BTN_Y = 108, BTN_H = 34, BTN_GAP = 10, SCENE_BASE = 200,
    TEAM_Y = 152, CARD_Y = 166, CARD_W = 144, CARD_H = 30, CARD_GAP = 8
};

static bool inside(SDL_Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

/* ---- where things are, for the language given: drawing, the mouse and --check use the same numbers ---- */

/* the pages, left to right */
static const short PAGES[4] = {S_PLAY, S_SETTINGS, S_SAVES, S_TOOLS};

static SDL_Rect nav_rect(const Lang *l, int id)
{
    int x = NAV_X;
    for (int i = 0; PAGES[i] != id; i++) {
        x += text_width(l->s[PAGES[i]], 1) + 2 * NAV_PAD + 1;
    }
    return (SDL_Rect){x, 4, text_width(l->s[id], 1) + 2 * NAV_PAD, BAR_H - 8};
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
    case S_SAVES:
    case S_TOOLS: return nav_rect(l, id);
    case S_RELEASE: return chip_rect(l, 2, tag);
    default: return (SDL_Rect){0, 0, 0, 0};
    }
}

static void team_info(const Lang *l, const Team *t, char *out, int size)
{
    SDL_snprintf(out, size, "%s %s  -  %d:%02d", l->s[S_LAST_SAVE], t->when, t->hours, t->minutes);
}

int bar_hit(int x, int y)
{
    for (int i = 0; i < 4; i++) {
        if (inside(nav_rect(L, PAGES[i]), x, y)) {
            return PAGES[i];
        }
    }
    return -1;
}

bool home_fits(const Lang *l)
{
    const Team worst = {.when = "2026-10-28 13:23", .hours = 999, .minutes = 59};
    char info[96];
    team_info(l, &worst, info, sizeof info);
    const SDL_Rect tools = nav_rect(l, S_TOOLS), chip = chip_rect(l, 2, "v99.99.99"), quit = quit_rect(l);
    return tools.x + tools.w <= NAV_END_MAX && text_width(l->s[S_READY], 2) <= 340 && text_width(l->s[S_TAGLINE], 1) <= NAV_X - TITLE_X - 2 &&
           chip.x + chip.w <= SCREEN_W - MARGIN && quit.x + quit.w <= SCREEN_W - MARGIN &&
           text_width(l->s[S_TEAM], 1) + text_width(info, 1) + 24 <= SCREEN_W - 2 * MARGIN && text_width(l->s[S_NO_TEAM], 1) <= SCREEN_W - 2 * MARGIN;
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

void bar_draw(int page, int cur)
{
    const Lang *l = L;
    fill_alpha(0, 0, SCREEN_W, BAR_H, C_BG, 235);
    fill(0, BAR_H - 1, SCREEN_W, 1, C_RULE);
    art_icon(3, 3);
    text(TITLE_X, 3, 1, C_GOLD, "DIGIMON WORLD 2003");
    text(TITLE_X, 14, 1, C_SOFT, l->s[S_TAGLINE]);
    for (int i = 0; i < 4; i++) {
        const SDL_Rect t = nav_rect(l, PAGES[i]);
        const bool sel = PAGES[i] == cur, here = PAGES[i] == page;
        if (sel) {
            fill(t.x, t.y, t.w, t.h, C_SELECT);
        }
        text(t.x + NAV_PAD, t.y + 5, 1, sel ? C_GOLD : here ? C_TEXT : C_SOFT, l->s[PAGES[i]]);
        if (here) {
            fill(t.x + 3, BAR_H - 5, t.w - 6, 2, C_GOLD);
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

/* the party, as cards: a coloured square with the partner's initial, the name and the level */
static void draw_team(const Lang *l)
{
    const Team *t = team_get();
    const int head = text_width(l->s[S_TEAM], 1);
    char info[96];
    text(MARGIN, TEAM_Y, 1, C_GOLD, l->s[S_TEAM]);
    if (!t) {
        fill(MARGIN + head + 8, TEAM_Y + 8, SCREEN_W - 2 * MARGIN - head - 8, 1, C_RULE);
        text(MARGIN, CARD_Y + 9, 1, C_SOFT, l->s[S_NO_TEAM]);
        return;
    }
    team_info(l, t, info, sizeof info);
    text_right(SCREEN_W - MARGIN, TEAM_Y, C_SOFT, info);
    fill(MARGIN + head + 8, TEAM_Y + 8, SCREEN_W - MARGIN - text_width(info, 1) - 8 - (MARGIN + head + 8) - 8, 1, C_RULE);
    for (int i = 0; i < t->count; i++) {
        const int x = MARGIN + i * (CARD_W + CARD_GAP);
        char level[32], initial[2] = {team_name(t->partner[i])[0], 0};
        fill_alpha(x, CARD_Y, CARD_W, CARD_H, C_PANEL, 225);
        outline(x, CARD_Y, CARD_W, CARD_H, C_EDGE);
        fill(x + 4, CARD_Y + 4, 22, 22, team_colour(t->partner[i]));
        text(x + 4 + (22 - text_width(initial, 1)) / 2, CARD_Y + 9, 1, C_BG, initial);
        SDL_snprintf(level, sizeof level, "%s %d", l->s[S_LEVEL], t->level[i]);
        text(x + 32, CARD_Y + 3, 1, C_TEXT, team_name(t->partner[i]));
        text(x + 32, CARD_Y + 15, 1, C_SOFT, level);
    }
}

bool home_on_bar(void)
{
    const Row *r = &rows()[g_app.sel[SC_HOME]];
    return r->kind == R_LANGUAGE || (r->kind == R_ACTION && (r->id == S_SETTINGS || r->id == S_SAVES || r->id == S_TOOLS || (r->id == S_PLAY && g_app.playtab)));
}

void home_draw(void)
{
    const Lang *l = L;
    const Row *r = &rows()[g_app.sel[SC_HOME]];
    const int cur = r->kind == R_ACTION ? r->id : -1;
    const bool bar = home_on_bar();
    scene();
    bar_draw(S_PLAY, bar ? cur : -1);
    text(MARGIN, TITLE_Y, 2, C_TEXT, l->s[S_READY]);
    text(MARGIN, SUB_Y, 1, C_SOFT, GAME_NAME);
    draw_chips(l, bar ? -1 : cur);
    draw_buttons(l, bar ? -1 : cur);
    draw_team(l);
}
