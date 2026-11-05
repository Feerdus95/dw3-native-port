/* The language list: a flag and its name. On the first-run screen it is the button top right; with the bar, a flag and an arrow
   at the bar's end, and the list drops under it. */
#include "launcher.h"

enum { LIST_X = SCREEN_W - 104, W = 96, H = 14, BUTTON_RIGHT = SCREEN_W - 6, COMPACT_W = 32 };

static bool s_open;
static int s_hover = PICKER_NONE;

static bool compact(void) { return !first_run_screen(); }
static SDL_Rect button(void)
{
    return compact() ? (SDL_Rect){BUTTON_RIGHT - COMPACT_W, 7, COMPACT_W, H} : (SDL_Rect){LIST_X + 8, 8, W - 8, H};
}
static int row_y(int row) { return (compact() ? 30 : 8 + H + 2) + row * H; }

void picker_draw(int lang, bool focused)
{
    const SDL_Rect b = button();
    fill(b.x, b.y, b.w, b.h, C_PANEL);
    outline(b.x, b.y, b.w, b.h, focused || s_open ? C_GOLD : C_EDGE);
    art_flag(b.x + 4, b.y + 2, lang);
    if (!compact()) {
        text_flat(b.x + 24, b.y + 1, C_TEXT, LANG_TABLE[lang]->name);
    }
    for (int i = 0; i < 3; i++) { /* the arrow */
        fill(b.x + b.w - 10 + i, b.y + 5 + i, 7 - 2 * i, 1, C_TEXT);
    }
    if (!s_open) {
        return;
    }
    for (int i = 0; i < LANGS; i++) {
        fill(LIST_X, row_y(i), W, H, i == lang ? C_SELECT : i == s_hover ? C_RULE : C_PANEL);
        art_flag(LIST_X + 6, row_y(i) + 2, i);
        text_flat(LIST_X + 28, row_y(i) + 1, i == lang ? C_GOLD : C_TEXT, LANG_TABLE[i]->name);
    }
    outline(LIST_X, row_y(0), W, LANGS * H, C_EDGE);
}

bool picker_is_open(void) { return s_open; }

void picker_set_open(bool open)
{
    s_open = open;
    s_hover = PICKER_NONE;
}

int picker_hit(int x, int y)
{
    const SDL_Rect b = button();
    if (x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h) {
        return PICKER_BUTTON;
    }
    if (s_open && x >= LIST_X && x < LIST_X + W && y >= row_y(0) && y < row_y(LANGS)) {
        return (y - row_y(0)) / H;
    }
    return PICKER_NONE;
}

void picker_hover(int row) { s_hover = row; }
