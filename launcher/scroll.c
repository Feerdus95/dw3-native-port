/* The settings list's scrolling by mouse: the wheel, and the scroll bar, which is dragged like a web page's
   (its thumb lights when the pointer is over it and more when it is held). */
#include "launcher.h"

static int s_mx = -1, s_my = -1; /* the pointer's last place, for the wheel and the bar's light */
static bool s_drag;               /* the scroll bar held down */

/* the bar's grip: the strip along the panel's edge, a little wider than it is drawn */
static bool over_bar(int x, int y)
{
    const SDL_Rect a = list_rect();
    return list_inset() > 0 && x >= a.x + a.w - 12 && x < a.x + a.w && y >= a.y && y < a.y + a.h;
}

/* 0 at rest, 1 with the pointer over it, 2 held */
int scrollbar_state(void) { return s_drag ? 2 : over_bar(s_mx, s_my) ? 1 : 0; }

/* the selection kept among the rows in sight, off a heading */
static void keep_selection_in_view(void)
{
    const Row *r = rows();
    const int n = list_rows(r), visible = list_in_sight();
    int sel = SDL_clamp(g_app.sel[g_app.screen], g_app.scroll, g_app.scroll + visible - 1);
    while (sel < n - 1 && r[sel].kind == R_HEADING) {
        sel++;
    }
    if (g_app.sel[g_app.screen] != sel) {
        g_app.note = NULL;
        g_app.sel[g_app.screen] = sel;
        g_app.btn = 0;
    }
}

/* A list longer than its panel scrolls under the wheel by itself, three rows a notch, and the selection
   goes to the row under the pointer (or stays in sight); a short one moves the selection a row */
void screens_wheel(int dir)
{
    const int n = list_rows(rows()), visible = list_in_sight();
    if (g_app.screen != SC_SETTINGS || n <= visible || picker_is_open() || bind_capturing() >= 0) {
        screens_action(dir < 0 ? A_UP : A_DOWN);
        return;
    }
    g_app.scroll = SDL_clamp(g_app.scroll + dir * 3, 0, n - visible);
    keep_selection_in_view();
    const SDL_Rect a = list_rect();
    if (s_mx >= a.x && s_mx < a.x + a.w && s_my >= a.y && s_my < a.y + a.h) {
        screens_mouse(s_mx, s_my, false);
    }
}

/* the thumb follows the pointer's height while the button is down */
static void drag_to(int y)
{
    const SDL_Rect a = list_rect();
    const int n = list_rows(rows()), visible = list_in_sight(), track = a.h - 4, thumb = track * visible / n;
    const int top = SDL_clamp(y - a.y - 2 - thumb / 2, 0, track - thumb);
    g_app.scroll = track > thumb ? (top * (n - visible) + (track - thumb) / 2) / (track - thumb) : 0;
    keep_selection_in_view();
}

void screens_mouse_release(void) { s_drag = false; }

/* true when the bar took the pointer: pressed on it, or held and moving */
bool scroll_mouse(int x, int y, bool clicked)
{
    s_mx = x, s_my = y;
    if (bind_capturing() >= 0) {
        return false;
    }
    if (clicked && over_bar(x, y)) {
        s_drag = true;
    }
    if (s_drag) {
        drag_to(y);
    }
    return s_drag;
}
