/* The language list, top right, as on the old first-run screen. */
#include "launcher.h"

enum { X = SCREEN_W - 92, Y = 8, W = 84, H = 14 };

static bool s_open;
static int s_hover = PICKER_NONE;

static int row_y(int row) { return Y + H + 2 + row * H; }

void picker_draw(int lang, bool focused)
{
    fill(X, Y, W, H, C_PANEL);
    outline(X, Y, W, H, focused ? C_GOLD : C_EDGE);
    text_flat(X + 5, Y + 1, C_TEXT, LANG_TABLE[lang]->name);
    for (int i = 0; i < 3; i++) { /* the arrow */
        fill(X + W - 12 + i, Y + 5 + i, 7 - 2 * i, 1, C_TEXT);
    }
    if (!s_open) {
        return;
    }
    for (int i = 0; i < LANGS; i++) {
        fill(X, row_y(i), W, H, i == lang ? C_SELECT : i == s_hover ? C_RULE : C_PANEL);
        text_flat(X + 5, row_y(i) + 1, i == lang ? C_GOLD : C_TEXT, LANG_TABLE[i]->name);
    }
    outline(X, row_y(0), W, LANGS * H, C_EDGE);
}

bool picker_is_open(void) { return s_open; }

void picker_set_open(bool open)
{
    s_open = open;
    s_hover = PICKER_NONE;
}

int picker_hit(int x, int y)
{
    if (x < X || x >= X + W) {
        return PICKER_NONE;
    }
    if (y >= Y && y < Y + H) {
        return PICKER_BUTTON;
    }
    if (s_open && y >= row_y(0) && y < row_y(LANGS)) {
        return (y - row_y(0)) / H;
    }
    return PICKER_NONE;
}

void picker_hover(int row) { s_hover = row; }
