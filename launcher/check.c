/* --check: every string of every language exists and fits where it is drawn. */
#include "launcher.h"

enum { DESC_W = SCREEN_W - 48, DESC_LINES = 4, ZONE_W = 340, BUTTON_W = 120, PICKER_W = 60, BOTTOM_W = SCREEN_W - 16, MENU_W = 160, STATUS_W = 224, LABEL_W = 270, TOOL_VALUE_W = 200, BIND_NAME_W = 100, UPDATE_W = 150 };

static const short DESCRIPTIONS[] = {S_PLAY_D, S_SETTINGS_D, S_TOOLS_D, S_LANGUAGE_D, S_QUIT_D, S_RELEASE_D,
                                     S_RESET_D, S_BACK_D, S_CREDITS_D, S_FOLDER_D, S_LOG_D, S_VIEWER_D, S_EDITOR_D, S_CHECK_D,
                                     S_CHECKING, S_UP_TO_DATE, S_CHECK_FAILED, S_GAME_RUNNING, S_GAME_FAILED, S_NO_EXE, S_SAVE_FAILED, S_NO_LOG,
                                     S_BIND_D, S_PRESS_KEY, S_PRESS_PAD, S_T_READY, S_T_FAILED, S_T_CANCELLED, S_T_NO_START, S_T_BUSY, S_T_NEED_DISC,
                                     S_SAVES_D, S_EXPORT_D, S_IMPORT_D, S_UNDO_D, S_SV_FOLDER_D, S_SV_EXPORTED, S_SV_IMPORTED, S_SV_WARN, S_SV_REJECT,
                                     S_SV_RESTORED, S_SV_NO_BACKUP, S_SV_FAILED, S_T_UPDATE_D, S_T_UPDATED, S_T_CLOSE_IT, S_U_CLOSE_GAME};
static const short TOOL_VALUES[] = {S_T_GET, S_T_OPEN, S_T_SETUP, S_T_DOWNLOADING, S_T_UNPACKING, S_T_PREPARING};
static const short BUTTON_NAMES[] = {S_B_UP, S_B_RIGHT, S_B_DOWN, S_B_LEFT, S_B_TRIANGLE, S_B_CIRCLE, S_B_CROSS, S_B_SQUARE};
static const short MENU[] = {S_PLAY, S_SETTINGS, S_TOOLS, S_RELEASE, S_QUIT, S_VIEWER, S_EDITOR, S_FOLDER, S_LOG, S_BACK, S_CHECK, S_CREDITS, S_SAVES, S_EXPORT, S_IMPORT, S_UNDO, S_SV_FOLDER, S_T_UPDATE};
static const short STATUS[] = {S_READY, S_DISC_OK, S_VERSION, S_RELEASE, S_TAGLINE};
static const short CENTRED[] = {S_NEED, S_ONCE, S_ISO, S_E_OPEN, S_E_GAME, S_E_BAD, S_E_WRITE, S_E_READ, S_E_TRUNCATED, S_E_CUE, S_E_SPACE, S_SEE_LOG, S_READING, S_NO_EXE, S_SAVE_FAILED, S_GAME_RUNNING};

static int s_bad;

static void fail(const Lang *l, const char *what, const char *s)
{
    s_bad++;
    SDL_Log("%s: %s: %s", l->name, what, s ? s : "(missing)");
}

static void check_lines(const Lang *l, const char *what, const char *s, int width, int most)
{
    if (wrap(0, 0, width, 0, s) > most) {
        fail(l, what, s);
    }
}

static void check_width(const Lang *l, const char *what, const char *s, int width)
{
    if (text_width(s, 1) > width) {
        fail(l, what, s);
    }
}

static void check_lang(const Lang *l)
{
    for (int i = 0; i < S_COUNT; i++) {
        if (!l->s[i]) {
            fail(l, "string", NULL);
            return;
        }
    }
    for (int i = 0; i < O_COUNT; i++) {
        if (!l->option[i].name || !l->option[i].desc) {
            fail(l, "option", l->option[i].name);
            return;
        }
        check_lines(l, "description", l->option[i].desc, DESC_W, DESC_LINES);
        check_width(l, "label", l->option[i].name, LABEL_W);
    }
    for (int i = 0; i < (int)SDL_arraysize(DESCRIPTIONS); i++) {
        check_lines(l, "description", l->s[DESCRIPTIONS[i]], DESC_W, DESC_LINES);
    }
    for (int i = 0; i < (int)SDL_arraysize(MENU); i++) {
        check_width(l, "menu row", l->s[MENU[i]], MENU_W);
    }
    for (int i = 0; i < (int)SDL_arraysize(STATUS); i++) {
        check_width(l, "status", l->s[STATUS[i]], STATUS_W);
    }
    for (int i = 0; i < (int)SDL_arraysize(CENTRED); i++) {
        check_width(l, "first-run line", l->s[CENTRED[i]], BOTTOM_W);
    }
    for (int i = 0; i < (int)SDL_arraysize(TOOL_VALUES); i++) {
        check_width(l, "tool's state", l->s[TOOL_VALUES[i]], TOOL_VALUE_W);
    }
    for (int i = 0; i < (int)SDL_arraysize(BUTTON_NAMES); i++) {
        check_width(l, "button's name", l->s[BUTTON_NAMES[i]], BIND_NAME_W);
    }
    char button[96];
    SDL_snprintf(button, sizeof button, "%s v99.99.99", l->s[S_T_UPDATE]);
    check_width(l, "update button", button, UPDATE_W);
    check_width(l, "column", l->s[S_KEYBOARD], BIND_NAME_W);
    check_width(l, "column", l->s[S_GAMEPAD], BIND_NAME_W);
    check_width(l, "heading", l->s[S_BUTTONS], BIND_NAME_W);
    check_width(l, "drop line", l->s[S_DROP], ZONE_W);
    check_width(l, "button", l->s[S_BROWSE], BUTTON_W);
    check_width(l, "button", l->s[S_CANCEL], BUTTON_W);
    check_width(l, "picker", l->name, PICKER_W);
    /* a hint shares its line with the credit */
    check_width(l, "hint", l->s[S_KEYS], DESC_W + 16 - 6 - text_width(CREDIT, 1));
    check_width(l, "hint", l->s[S_HINT_MENU], DESC_W + 16 - 6 - text_width(CREDIT, 1));
    check_width(l, "hint", l->s[S_HINT_SETTINGS], DESC_W + 16 - 6 - text_width(CREDIT, 1));
    check_width(l, "fan line", l->s[S_FAN], DESC_W + 16);
    for (int i = 0; i < 7; i++) { /* what the home screen's two lines hold */
        static const short HOME_TEXTS[7] = {S_PLAY_D, S_SETTINGS_D, S_SAVES_D, S_TOOLS_D, S_LANGUAGE_D, S_QUIT_D, S_RELEASE_D};
        check_lines(l, "home description", l->s[HOME_TEXTS[i]], DESC_W, 2);
    }
    if (!saves_fits(l, SCREEN_W - 2 * 16 - 16)) {
        fail(l, "saves page", l->s[S_SV_NOCARD]);
    }
    if (!home_fits(l)) {
        fail(l, "home screen", l->s[S_READY]);
    }
    int tabs = 0;
    for (int i = S_DISPLAY; i <= S_MODS; i++) {
        tabs += text_width(l->s[i], 1) + 18;
    }
    if (tabs > DESC_W) {
        fail(l, "tabs", l->s[S_DISPLAY]);
    }
}

/* line endings, comments, bindings and other keys survive; a None value removes; a new key comes last */
static int ini_selftest(void)
{
    IniEdit edits[] = {{"fps", "60"}, {"scale", NULL}, {"new_key", "1"}, {"VSYNC", "0"}};
    char *out = ini_apply("# fps = comment\r\nfps = 50\r\nkeys_up = w\r\nscale = 4\r\n; x\r\nvsync=1\r\nlast = 2", edits, 4);
    const int bad = SDL_strcmp(out, "# fps = comment\r\nfps = 60\r\nkeys_up = w\r\n; x\r\nVSYNC = 0\r\nlast = 2\r\nnew_key = 1\r\n") != 0;
    if (bad) {
        SDL_Log("ini: %s", out);
    }
    SDL_free(out);
    return bad;
}

/* a tool's update: the new files replace the old, in subfolders too, and what only the tool's folder has stays */
static int move_tree_selftest(void)
{
    char base[1200], from[1300], to[1300], path[1400];
    int bad = 0;
    path_in_root(base, sizeof base, ".move-test");
    SDL_snprintf(from, sizeof from, "%s/from", base);
    SDL_snprintf(to, sizeof to, "%s/to", base);
#define PATH(root, name) (SDL_snprintf(path, sizeof path, "%s/%s", root, name), path)
#define PUT(root, name, s) SDL_SaveFile(PATH(root, name), s, SDL_strlen(s))
    SDL_CreateDirectory(PATH(from, "sub"));
    SDL_CreateDirectory(PATH(to, "sub"));
    PUT(from, "a.txt", "new");
    PUT(from, "sub/b.txt", "new");
    PUT(to, "a.txt", "old");
    PUT(to, "settings.ini", "mine");
    PUT(to, "sub/mine.txt", "mine");
    bad += !path_move_tree(from, to);
    const char *const WANT[][2] = {{"a.txt", "new"}, {"sub/b.txt", "new"}, {"settings.ini", "mine"}, {"sub/mine.txt", "mine"}};
    for (int i = 0; i < 4; i++) {
        char *s = SDL_LoadFile(PATH(to, WANT[i][0]), NULL);
        bad += !s || SDL_strcmp(s, WANT[i][1]);
        SDL_free(s);
    }
    bad += SDL_GetPathInfo(PATH(from, "a.txt"), NULL) || SDL_GetPathInfo(PATH(from, "sub"), NULL); /* nothing left behind */
    for (int i = 0; i < 4; i++) {
        SDL_RemovePath(PATH(to, WANT[i][0]));
    }
    SDL_RemovePath(PATH(to, "sub"));
    SDL_RemovePath(to);
    SDL_RemovePath(from);
    SDL_RemovePath(base);
#undef PUT
#undef PATH
    if (bad) {
        SDL_Log("move tree: %d wrong", bad);
    }
    return bad;
}

int strings_check(void)
{
    s_bad += ini_selftest() + move_tree_selftest() + release_selftest() + binds_selftest() + tools_selftest() + saves_selftest();
    for (int i = 0; i < LANGS; i++) {
        check_lang(LANG_TABLE[i]);
    }
    return s_bad;
}
