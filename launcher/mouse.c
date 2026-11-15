/* What the mouse does to the screens. */
#include "launcher.h"

static void select_row(int i)
{
    if (g_app.sel[g_app.screen] != i) {
        g_app.note = NULL;
    }
    g_app.sel[g_app.screen] = i;
    g_app.tabfocus = false;
    g_app.playtab = false;
}

/* the language list, over everything: true when it took the mouse */
static bool mouse_picker(int x, int y, bool clicked)
{
    if (g_app.screen != SC_HOME || first_run_reading()) {
        return false;
    }
    const int hit = picker_hit(x, y);
    if (picker_is_open()) {
        picker_hover(hit);
        if (clicked) {
            if (hit >= 0) {
                language_set(hit);
            }
            picker_set_open(false);
        }
        return true;
    }
    if (hit != PICKER_BUTTON) {
        return false;
    }
    select_row(row_count(rows()) - 1);
    if (clicked) {
        picker_set_open(true);
    }
    return true;
}

void screens_mouse(int x, int y, bool clicked)
{
    if (mouse_picker(x, y, clicked)) {
        return;
    }
    if (scroll_mouse(x, y, clicked)) { /* the scroll bar held */
        return;
    }
    if (first_run_screen()) {
        if (first_run_reading()) {
            if (clicked && first_run_button_hit(x, y)) {
                first_run_stop();
            }
        } else if (first_run_hit(x, y)) {
            select_row(0);
            if (clicked) {
                activate(S_BROWSE);
            }
        }
        return;
    }
    const int page = g_app.screen == SC_HOME ? -1 : bar_hit(x, y);
    if (page >= 0) { /* the bar's pages from Settings, Saves or Tools */
        const int here = g_app.screen == SC_SETTINGS ? S_SETTINGS : g_app.screen == SC_SAVES ? S_SAVES : S_TOOLS;
        if (clicked && page != here && bind_capturing() < 0) {
            go_home();
            if (page != S_PLAY) {
                activate(page);
            }
        }
        return;
    }
    if (g_app.screen == SC_HOME) {
        const int i = home_hit(x, y);
        if (i >= 0) {
            select_row(i);
            if (clicked) {
                screens_action(A_OK);
            }
        }
        return;
    }
    if (g_app.screen == SC_SETTINGS && clicked) {
        for (int i = 0; i < 4; i++) {
            const SDL_Rect t = tab_rect(i);
            if (x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) {
                g_app.tab = i;
                g_app.sel[SC_SETTINGS] = 0;
                return;
            }
        }
    }
    const SDL_Rect a = list_rect();
    const Row *r = rows();
    const int i = list_first() + (y - a.y - 4) / ROW_H_PIXELS;
    if (x < a.x || x >= a.x + a.w || y < a.y + 4 || y >= a.y + a.h - 2 || i >= list_rows(r) || r[i].kind == R_HEADING) {
        return;
    }
    select_row(i);
    if (row_tool(&r[i]) >= 0) {
        g_app.btn = tool_update_has(row_tool(&r[i])) && x >= update_button(row_tool(&r[i])).x;
    }
    if (r[i].kind == R_BIND) {
        bind_slot_set(x >= bind_column(2) ? 2 : x >= bind_column(1) ? 1 : 0);
        if (clicked) {
            screens_action(A_OK);
        }
    } else if (clicked && r[i].kind == R_OPTION) {
        /* on the left arrow or just around it, down; anywhere else on the row, up */
        char value[32];
        option_value(r[i].id, L, value, sizeof value);
        const int left_arrow = a.x + a.w - 13 - list_inset() - text_width(value, 1) - 8;
        screens_action(x >= left_arrow - 3 && x <= left_arrow + 6 ? A_LEFT : A_RIGHT);
    } else if (clicked) {
        screens_action(A_OK);
    }
}
