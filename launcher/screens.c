/* The menus: one list of rows per screen, a description box under it. */
#include "launcher.h"

enum { DESC_Y = 176, DESC_H = 58, HINT_Y = 240, BOTTOM_Y = 256, ROW_H = ROW_H_PIXELS, PAD = 16, SETTINGS_H = 124, ROWS_IN_SIGHT = (SETTINGS_H - 4) / ROW_H };

/* the language row is last, and drawn as the list top right: up from the first row reaches it */
#define ACT(s) {R_ACTION, s}
#define OPT(o) {R_OPTION, o}
#define BIND(b) {R_BIND, b}
static const Row HOME[] = {ACT(S_PLAY), ACT(S_SETTINGS), ACT(S_SAVES), ACT(S_TOOLS), ACT(S_QUIT), {R_LANGUAGE}, {R_END}};
static const Row HOME_NEWS[] = {ACT(S_PLAY), ACT(S_SETTINGS), ACT(S_SAVES), ACT(S_TOOLS), ACT(S_RELEASE), ACT(S_QUIT), {R_LANGUAGE}, {R_END}};
static const Row SAVES[] = {ACT(S_EXPORT), ACT(S_IMPORT), ACT(S_UNDO), ACT(S_SV_FOLDER), ACT(S_BACK), {R_END}};
static const Row FIRST_RUN[] = {ACT(S_BROWSE), {R_LANGUAGE}, {R_END}};
static const Row TOOLS[] = {ACT(S_VIEWER), ACT(S_EDITOR), ACT(S_FOLDER), ACT(S_LOG), ACT(S_CHECK), ACT(S_CREDITS), ACT(S_BACK), {R_END}};
/* the Input tab's buttons in the order of the game's Controls page */
static const Row TABS[4][27] = {
    {OPT(O_FPS), OPT(O_FULLSCREEN), OPT(O_WINDOW), OPT(O_WIDESCREEN), OPT(O_VSYNC), OPT(O_MOVIES), OPT(O_INACTIVE), ACT(S_RESET), ACT(S_BACK), {R_END}},
    {OPT(O_RESOLUTION), OPT(O_PERSPECTIVE), OPT(O_SMOOTH_3D), OPT(O_TRUE_COLOR), OPT(O_DITHER), OPT(O_SOFT_SHADOWS), OPT(O_BATTLE_SHADOWS), OPT(O_BATTLE_SKY), ACT(S_RESET), ACT(S_BACK), {R_END}},
    {OPT(O_ICONS), OPT(O_RUMBLE), OPT(O_DEADZONE), OPT(O_FF_MENUS), OPT(O_SKIP_START), OPT(O_MENU_WRAP), ACT(S_SWAP), {R_HEADING, S_BUTTONS},
     BIND(14), BIND(13), BIND(15), BIND(12), BIND(10), BIND(11), BIND(8), BIND(9), BIND(1), BIND(2), BIND(3), BIND(0), BIND(4), BIND(6), BIND(7), BIND(5),
     ACT(S_RESET), ACT(S_BACK), {R_END}},
    {OPT(O_FAST_TEXT), OPT(O_FAST_TRAVEL), OPT(O_OBJECTIVE), OPT(O_JOURNAL), OPT(O_PACKS), OPT(O_FULL_EXP_SHARE), OPT(O_HEAL_LEVELUP), OPT(O_SAVE_ANYWHERE), OPT(O_CUSTOM_EFFECTS), OPT(O_CUSTOM_SOUNDS), OPT(O_CUSTOM_SCRIPTS), {R_HEADING, S_PLAYABLE}, OPT(O_BLADEGARURUMON), ACT(S_RESET), ACT(S_BACK), {R_END}},
};
static const short TAB_NAME[4] = {S_DISPLAY, S_GRAPHICS, S_INPUT, S_MODS};
const Lang *const LANG_TABLE[LANGS] = {&LANG_EN, &LANG_ES, &LANG_FR, &LANG_DE, &LANG_IT};

const char *const LANG_CODES[LANGS] = {"en", "es", "fr", "de", "it"};
App g_app;

#define s_lang g_app.lang
#define s_state g_app.state
#define s_screen g_app.screen
#define s_tab g_app.tab
#define s_sel g_app.sel
#define s_note g_app.note

bool first_run_screen(void) { return s_screen == SC_HOME && s_state != ST_READY; }

int row_tool(const Row *r)
{
    return r->kind != R_ACTION ? -1 : r->id == S_VIEWER ? 0 : r->id == S_EDITOR ? 1 : -1;
}

static void update_label(int tool, char *out, int size)
{
    SDL_snprintf(out, size, "%s %s", L->s[S_T_UPDATE], tool_update_tag(tool));
}

SDL_Rect update_button(int tool)
{
    char label[64];
    const SDL_Rect a = list_rect();
    update_label(tool, label, sizeof label);
    const int w = text_width(label, 1) + 12;
    return (SDL_Rect){a.x + a.w - 6 - w, 0, w, ROW_H - 2};
}

const Row *rows(void)
{
    if (s_screen == SC_SETTINGS) {
        return TABS[s_tab];
    }
    if (s_screen == SC_TOOLS) {
        return TOOLS;
    }
    if (s_screen == SC_SAVES) {
        return SAVES;
    }
    return s_state != ST_READY ? FIRST_RUN : release_tag() ? HOME_NEWS : HOME;
}

int row_count(const Row *r)
{
    int n = 0;
    while (r[n].kind != R_END) {
        n++;
    }
    return n;
}

int list_rows(const Row *r) /* those drawn in the list: all but the language */
{
    const int n = row_count(r);
    return n && r[n - 1].kind == R_LANGUAGE ? n - 1 : n;
}

SDL_Rect list_rect(void)
{
    if (s_screen == SC_SETTINGS) {
        return (SDL_Rect){PAD, 48, SCREEN_W - 2 * PAD, SETTINGS_H};
    }
    return (SDL_Rect){PAD, s_screen == SC_SAVES ? 86 : 54, s_screen == SC_HOME ? 178 : SCREEN_W - 2 * PAD, list_rows(rows()) * ROW_H + 8};
}

int list_in_sight(void) { return ROWS_IN_SIGHT; }

int list_inset(void) { return s_screen == SC_SETTINGS && list_rows(rows()) > ROWS_IN_SIGHT ? 6 : 0; }

int list_first(void)
{
    const int n = list_rows(rows()), sel = s_sel[s_screen];
    if (sel < n && sel < g_app.scroll) {
        g_app.scroll = sel;
    } else if (sel < n && sel >= g_app.scroll + ROWS_IN_SIGHT) {
        g_app.scroll = sel - ROWS_IN_SIGHT + 1;
    }
    g_app.scroll = SDL_clamp(g_app.scroll, 0, SDL_max(n - ROWS_IN_SIGHT, 0));
    return g_app.scroll;
}

int bind_column(int slot)
{
    static const short COLUMN[3] = {130, 230, 330};
    return list_rect().x + COLUMN[slot];
}

static void draw_bind(const Row *row, int y, bool sel)
{
    text(list_rect().x + 13, y, 1, sel ? C_GOLD : C_TEXT, bind_label(row->id));
    for (int slot = 0; slot < 3; slot++) {
        const int x = bind_column(slot);
        const bool here = sel && slot == bind_slot();
        const char *s = bind_text(row->id, slot);
        if (here) {
            fill(x - 3, y + 1, slot < 2 ? 96 : 104, ROW_H - 2, C_PANEL);
            if (bind_capturing() >= 0) {
                s = SDL_GetTicks() / 267 & 1 ? "..." : "";
            }
        }
        text(x, y, 1, here ? C_GOLD : s[0] == '-' ? C_SOFT : C_TEXT, s);
    }
}

static void draw_heading(const Row *row, int y)
{
    const SDL_Rect a = list_rect();
    const int w = text_width(L->s[row->id], 1);
    text(a.x + 6, y, 1, C_GOLD, L->s[row->id]);
    if (row->id == S_BUTTONS) {
        text(bind_column(0), y, 1, C_GOLD, L->s[S_KEYBOARD]);
        text(bind_column(2), y, 1, C_GOLD, L->s[S_GAMEPAD]);
    } else {
        fill(a.x + 12 + w, y + 8, a.w - 18 - w - list_inset(), 1, C_RULE);
    }
}

/* a bar on the panel's edge, when the list is longer than it */
static void draw_scrollbar(int n, int first)
{
    const SDL_Rect a = list_rect();
    if (n > ROWS_IN_SIGHT) {
        const int track = a.h - 4, thumb = track * ROWS_IN_SIGHT / n;
        fill(a.x + a.w - 7, a.y + 2, 4, track, scrollbar_state() ? C_EDGE : C_RULE);
        const int state = scrollbar_state(); /* as a web page's: lit under the pointer, brightest held */
        fill(a.x + a.w - 7, a.y + 2 + (track - thumb) * first / (n - ROWS_IN_SIGHT), 4, thumb, state == 2 ? C_GOLD_HI : state == 1 ? C_GOLD : C_SOFT);
    }
}

static void draw_list(void)
{
    const SDL_Rect a = list_rect();
    const Row *r = rows();
    panel(a.x, a.y, a.w, a.h);
    const int first = list_first(), n = list_rows(r), end = s_screen == SC_SETTINGS ? SDL_min(n, first + ROWS_IN_SIGHT) : n;
    for (int i = first; i < end; i++) {
        const int y = a.y + 4 + (i - first) * ROW_H;
        const bool sel = i == s_sel[s_screen] && !g_app.tabfocus;
        if (r[i].kind == R_HEADING) {
            draw_heading(&r[i], y);
            continue;
        }
        if (sel) {
            fill(a.x + 1, y, a.w - 2, ROW_H, C_SELECT);
            triangle(a.x + 5, y + 7, 3, 1, C_GOLD);
        }
        if (r[i].kind == R_BIND) {
            draw_bind(&r[i], y, sel);
            continue;
        }
        const int tool = row_tool(&r[i]);
        if (tool >= 0) {
            char value[64];
            int right = a.x + a.w - 8 - list_inset();
            const bool on = sel && g_app.btn && tool_update_has(tool);
            if (tool_update_has(tool)) {
                char label[64];
                const SDL_Rect b = update_button(tool);
                update_label(tool, label, sizeof label);
                fill(b.x, y + 1, b.w, ROW_H - 2, C_EDGE);
                fill(b.x + 1, y + 2, b.w - 2, ROW_H - 4, on ? C_PANEL : C_BG);
                text(b.x + 6, y, 1, on ? C_GOLD : C_TEXT, label);
                right = b.x - 8;
            }
            tool_value(tool, value, sizeof value);
            text_right(right, y, sel && !on ? C_GOLD : C_SOFT, value);
        }
        text(a.x + 13, y, 1, sel ? C_GOLD : C_TEXT, r[i].kind == R_OPTION ? L->option[r[i].id].name : L->s[r[i].id]);
        if (r[i].kind == R_ACTION && r[i].id == S_SWAP) { /* on or off, so it shows it took */
            text_right(a.x + a.w - 12 - list_inset(), y, binds_swapped() ? C_GOLD : C_SOFT, L->s[binds_swapped() ? S_ON : S_OFF]);
        }
        if (r[i].kind == R_OPTION) {
            char value[32];
            const int right = a.x + a.w - (sel ? 13 : 8) - list_inset();
            option_value(r[i].id, L, value, sizeof value);
            text_right(right, y, option_is_off(r[i].id) ? C_SOFT : C_TEXT, value);
            if (sel) {
                triangle(right - text_width(value, 1) - 8, y + 7, 3, -1, C_GOLD);
                triangle(right + 3, y + 7, 3, 1, C_GOLD);
            }
        }
    }
    if (s_screen == SC_SETTINGS) {
        draw_scrollbar(n, first);
    }
}

static void draw_description(void)
{
    const Row *r = &rows()[s_sel[s_screen]];
    const char *s = s_note ? s_note
                  : r->kind == R_OPTION ? L->option[r->id].desc
                  : r->kind == R_LANGUAGE ? L->s[S_LANGUAGE_D]
                  : r->kind == R_BIND ? L->s[S_BIND_D]
                  : row_tool(r) >= 0 && g_app.btn && tool_update_has(row_tool(r)) ? L->s[S_T_UPDATE_D]
                  : L->s[r->id + 1];
    const bool home = s_screen == SC_HOME; /* two lines under the team, with room around it */
    const int y = home ? 204 : DESC_Y, h = home ? 28 : DESC_H;
    panel(PAD, y, SCREEN_W - 2 * PAD, h);
    wrap(PAD + 8, y + (home ? 2 : 3), SCREEN_W - 2 * PAD - 16, C_TEXT, s);
}

SDL_Rect tab_rect(int tab)
{
    int x = PAD;
    for (int i = 0; i < tab; i++) {
        x += text_width(L->s[TAB_NAME[i]], 1) + 18;
    }
    return (SDL_Rect){x, 32, text_width(L->s[TAB_NAME[tab]], 1) + 16, 17};
}

static void draw_tabs(void)
{
    for (int i = 0; i < 4; i++) {
        const SDL_Rect t = tab_rect(i);
        const bool sel = i == s_tab;
        fill(t.x, t.y, t.w, t.h, sel && g_app.tabfocus ? C_GOLD : C_EDGE);
        fill(t.x + 1, t.y + 1, t.w - 2, t.h - 1, sel ? C_PANEL : C_BG);
        text(t.x + 8, t.y + 2, 1, sel ? C_GOLD : C_SOFT, L->s[TAB_NAME[i]]);
    }
}

/* the old screen's heading: the title, what this is, a thin line */
static void draw_title(void)
{
    text_centre(4, 2, C_GOLD, "DIGIMON WORLD 2003");
    text_centre(28, 1, C_SOFT, L->s[S_TAGLINE]);
    fill(SCREEN_W / 2 - 120, 44, 240, 1, C_RULE);
}

void screens_draw(void)
{
    const bool home = s_screen == SC_HOME && !first_run_screen();
    if (home) {
        home_draw();
    } else if (s_screen == SC_SETTINGS) {
        bar_draw(S_SETTINGS, -1);
        draw_tabs();
    } else if (s_screen == SC_TOOLS) {
        bar_draw(S_TOOLS, -1);
    } else if (s_screen == SC_SAVES) {
        bar_draw(S_SAVES, -1);
        saves_draw();
    } else {
        draw_title();
    }
    if (first_run_screen()) {
        first_run_draw();
        text(PAD, HINT_Y, 1, C_FAINT, L->s[S_KEYS]);
    } else {
        if (!home) {
            draw_list();
        }
        draw_description();
        text(PAD, HINT_Y, 1, C_DIM, L->s[s_screen == SC_SETTINGS ? S_HINT_SETTINGS : S_HINT_MENU]);
    }
    text_right(SCREEN_W - PAD, HINT_Y, C_FAINT, CREDIT);
    text_right(SCREEN_W - PAD, BOTTOM_Y, C_FAINT, L->s[S_FAN]);
    if (s_screen == SC_HOME && !first_run_reading()) {
        picker_draw(s_lang, rows()[s_sel[SC_HOME]].kind == R_LANGUAGE);
    }
}
