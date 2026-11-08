/* What the keys, the pad and the mouse do to the screens. */
#include "launcher.h"

void first_run_wait(void);

static void go(int screen)
{
    g_app.screen = screen;
    g_app.sel[screen] = 0;
    g_app.btn = 0;
    g_app.tabfocus = false;
    g_app.playtab = false;
}

static void go_home(void)
{
    if (g_app.screen == SC_SETTINGS && !options_save()) {
        g_app.note = L->s[S_SAVE_FAILED];
    }
    g_app.screen = SC_HOME;
}

static int first_selectable(void)
{
    int i = 0;
    while (rows()[i].kind == R_HEADING) {
        i++;
    }
    return i;
}

/* Home has two levels. The bar: Play | Settings | Saves | Tools | Language, moved with
   Left/Right only (Down from the Play tab goes under it). Under it: the release chip
   above the Play and Quit buttons; Left/Right swap those two, Up/Down go between levels */
static int find_row(int kind, int id)
{
    const Row *r = rows();
    for (int i = 0; i < row_count(r); i++) {
        if (r[i].kind == kind && (kind == R_LANGUAGE || r[i].id == id)) {
            return i;
        }
    }
    return -1;
}

static void home_move(int a)
{
    static const short BAR[5] = {S_PLAY, S_SETTINGS, S_SAVES, S_TOOLS, -1}; /* -1: the language */
    const Row *cur = &rows()[g_app.sel[SC_HOME]];
    const bool bar = home_on_bar();
    int to = -1;
    if (bar && (a == A_LEFT || a == A_RIGHT)) {
        int at = 0;
        for (int k = 0; k < 5; k++) {
            at = (cur->kind == R_LANGUAGE ? BAR[k] == -1 : cur->id == BAR[k]) ? k : at;
        }
        const int want = BAR[(at + (a == A_RIGHT ? 1 : 4)) % 5];
        to = want == -1 ? find_row(R_LANGUAGE, 0) : find_row(R_ACTION, want);
        g_app.playtab = want == S_PLAY;
    } else if (bar && a == A_DOWN && cur->id == S_PLAY) {
        to = find_row(R_ACTION, S_PLAY);
        g_app.playtab = false;
    } else if (!bar && (a == A_LEFT || a == A_RIGHT) && cur->id != S_RELEASE) {
        to = find_row(R_ACTION, cur->id == S_PLAY ? S_QUIT : S_PLAY);
    } else if (!bar && a == A_UP) {
        to = cur->id != S_RELEASE ? find_row(R_ACTION, S_RELEASE) : -1;
        if (to < 0) {
            to = find_row(R_ACTION, S_PLAY);
            g_app.playtab = true;
        }
    } else if (!bar && a == A_DOWN && cur->id == S_RELEASE) {
        to = find_row(R_ACTION, S_PLAY);
    }
    if (to >= 0) {
        g_app.sel[SC_HOME] = to;
    }
    g_app.btn = 0;
}

static void move_selection(int dir)
{
    const Row *r = rows();
    const int n = row_count(r);
    int i = g_app.sel[g_app.screen];
    do {
        i = (i + dir + n) % n;
    } while (r[i].kind == R_HEADING);
    g_app.sel[g_app.screen] = i;
    g_app.btn = 0;
}

void language_set(int lang)
{
    IniEdit edit = {"language", LANG_CODES[lang], false};
    g_app.lang = lang;
    ini_write("launcher.ini", &edit, 1);
}

static void play(void)
{
    if (game_running()) {
        g_app.note = L->s[S_GAME_RUNNING];
    } else if (!options_save()) {
        g_app.note = L->s[S_SAVE_FAILED];
    } else if (!game_start()) {
        g_app.note = L->s[S_NO_EXE];
    }
}

static void open_log(void)
{
    char path[1200];
    path_in_root(path, sizeof path, "log.txt");
    if (!SDL_GetPathInfo(path, NULL)) {
        g_app.note = L->s[S_NO_LOG];
    } else {
        SDL_OpenURL(path);
    }
}

/* next to the exe in a release, a folder up from build/ */
static void open_credits(void)
{
    char path[1200];
    path_in_root(path, sizeof path, "CREDITS.md");
    if (!SDL_GetPathInfo(path, NULL)) {
        path_in_root(path, sizeof path, "../CREDITS.md");
    }
    SDL_OpenURL(path);
}

/* a tool's row: its program, or its update when that button has the focus */
static void tool_row(int tool)
{
    if (tool_update_focus(tool)) {
        tool_update_activate(tool);
    } else {
        tool_activate(tool);
    }
}

static void activate(int id)
{
    switch (id) {
    case S_PLAY: play(); break;
    case S_SETTINGS:
        /* the game writes native.ini too */
        if (game_running()) {
            g_app.note = L->s[S_GAME_RUNNING];
        } else {
            options_load();
            go(SC_SETTINGS);
        }
        break;
    case S_TOOLS: go(SC_TOOLS); break;
    case S_SAVES:
        saves_open();
        go(SC_SAVES);
        break;
    case S_EXPORT:
    case S_IMPORT:
    case S_UNDO:
    case S_SV_FOLDER: saves_activate(id); break;
    case S_BACK: go_home(); break;
    case S_QUIT: g_app.quit = true; break;
    case S_RESET: options_reset(); break;
    case S_SWAP: binds_swap_cancel(); break;
    case S_RELEASE:
        port_update_activate();
        break;
    case S_BROWSE: first_run_browse(); break;
    case S_VIEWER: tool_row(0); break;
    case S_EDITOR: tool_row(1); break;
    case S_FOLDER: SDL_OpenURL(SDL_GetBasePath()); break;
    case S_LOG: open_log(); break;
    case S_CREDITS: open_credits(); break;
    case S_CHECK:
        if (tools_busy()) { /* the check would write what the download reads */
            g_app.note = L->s[S_T_BUSY];
            break;
        }
        release_start(true);
        g_app.note = L->s[S_CHECKING];
        break;
    default: break;
    }
}

void screens_action(int a)
{
    if (a == A_NONE) {
        return;
    }
    if (first_run_screen() && first_run_reading()) {
        if (a == A_OK || a == A_BACK) {
            first_run_stop();
        }
        return;
    }
    const Row *r = &rows()[g_app.sel[g_app.screen]];
    g_app.note = NULL;
    if (picker_is_open()) {
        if (a == A_UP || a == A_DOWN) {
            language_set((g_app.lang + (a == A_DOWN ? 1 : LANGS - 1)) % LANGS);
        } else {
            picker_set_open(false);
        }
        return;
    }
    if (g_app.tabfocus) { /* the tabs: Left/Right switch, Down goes back to the rows */
        if (a == A_LEFT || a == A_RIGHT) {
            a = a == A_RIGHT ? A_NEXT : A_PREV;
        } else if (a == A_DOWN || a == A_OK) {
            g_app.tabfocus = false;
            return;
        } else if (a == A_UP || a == A_CLEAR) {
            return;
        }
    } else if (a == A_UP && g_app.screen == SC_SETTINGS && first_selectable() == g_app.sel[SC_SETTINGS]) {
        g_app.tabfocus = true;
        return;
    }
    if (g_app.screen == SC_HOME && !first_run_screen() && a >= A_UP && a <= A_RIGHT) {
        home_move(a);
        return;
    }
    switch (a) {
    case A_UP: move_selection(-1); break;
    case A_DOWN: move_selection(1); break;
    case A_CLEAR:
        if (r->kind == R_BIND) {
            bind_clear(r->id);
        }
        break;
    case A_LEFT:
    case A_RIGHT:
    case A_OK: {
        const int dir = a == A_LEFT ? -1 : 1;
        if (r->kind == R_BIND) {
            if (a == A_OK) {
                bind_capture(r->id);
            } else {
                bind_slot_set(bind_slot() + dir);
            }
        } else if (r->kind == R_OPTION) {
            option_step(r->id, dir);
        } else if (r->kind == R_LANGUAGE) {
            if (a == A_OK) {
                picker_set_open(true);
            } else {
                language_set((g_app.lang + dir + LANGS) % LANGS);
            }
        } else if (a != A_OK && row_tool(r) >= 0) { /* between the tool's two buttons */
            g_app.btn = dir > 0 && tool_update_has(row_tool(r));
        } else if (a == A_OK) {
            activate(r->id);
        }
        break;
    }
    case A_BACK:
        /* as on the old screen, Esc there quits */
        if (first_run_screen()) {
            g_app.quit = true;
        }
        go_home();
        break;
    case A_PREV:
    case A_NEXT:
        if (g_app.screen == SC_SETTINGS) {
            g_app.tab = (g_app.tab + (a == A_NEXT ? 1 : 3)) % 4;
            g_app.sel[SC_SETTINGS] = 0;
        }
        break;
    default: break;
    }
}

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

bool screens_tick(void)
{
    static char failed[160];
    int code = 0;
    first_run_poll();
    tools_poll();
    port_update_poll();
    saves_poll();
    if (game_ended(&code) && code != 0) {
        SDL_snprintf(failed, sizeof failed, L->s[S_GAME_FAILED], code);
        g_app.note = failed;
    }
    const int message = release_finished_manual();
    if (message >= 0) {
        g_app.note = L->s[message];
    }
    return first_run_reading() || game_running() || release_busy() || tools_busy() || port_updating() || saves_busy() || bind_capturing() >= 0;
}

/* the settings kept, the disc's reading stopped cleanly; the game, if running, goes on */
void screens_shutdown(void)
{
    first_run_wait();
    port_update_shutdown();
    tools_shutdown();
    if (g_app.screen == SC_SETTINGS) {
        options_save();
    }
}
