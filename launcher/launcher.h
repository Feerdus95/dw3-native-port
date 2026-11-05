/* dw3launcher: a fixed 480x270 picture scaled by whole numbers, like the old first-run screen. It reads the
   disc, edits native.ini and starts the game. It never kills a process. */
#pragma once
#include <SDL3/SDL.h>
#include <stdbool.h>

enum { SCREEN_W = 480, SCREEN_H = 270, LANGS = 5, LINE_H = 12, ROW_H_PIXELS = 13 };

/* the old first-run screen's palette: every colour on screen comes from here */
enum {
    C_BG = 0x0B1220, C_GLOW = 0x2A4A8A, C_PANEL = 0x1C2A44, C_EDGE = 0x4F6AA0, C_RULE = 0x3A4E78, C_SELECT = 0x2A4A8A,
    C_TEXT = 0xE8ECF4, C_SOFT = 0x8FA3C8, C_DIM = 0x6C7FA6, C_FAINT = 0x4A5A7A, C_GOLD = 0xF5A623, C_GOLD_HI = 0xFFB940,
    C_RED = 0xFF6B6B,
    /* the home scene */
    C_MOON = 0xE8D9A8, C_MOON_DARK = 0xCDBF8F, C_HILL_FAR = 0x22386A, C_HILL_NEAR = 0x142348,
};

#define CREDIT "\xC2\xA9 2026 Feerdus  -  MIT License"
#define GAME_LINE "Digimon World 2003 (Europe), SLES-03936"

/* draw.c */
void draw_init(SDL_Renderer *r);
void fill(int x, int y, int w, int h, Uint32 colour);
void fill_alpha(int x, int y, int w, int h, Uint32 colour, Uint8 alpha);
void outline(int x, int y, int w, int h, Uint32 colour);
void panel(int x, int y, int w, int h);
void background(int w, int h); /* in window pixels */
int text_width(const char *s, int scale);
void text(int x, int y, int scale, Uint32 colour, const char *s); /* with a shadow */
void text_flat(int x, int y, Uint32 colour, const char *s);
void text_right(int right, int y, Uint32 colour, const char *s);
void text_centre(int y, int scale, Uint32 colour, const char *s);
int wrap(int x, int y, int width, Uint32 colour, const char *s); /* returns its line count */
void triangle(int x, int y, int half, int dir, Uint32 colour);   /* dir: -1 left, 1 right */
void art_disc(int x, int y);

/* lang_*.c */
enum {
    O_FPS, O_FULLSCREEN, O_WINDOW, O_WIDESCREEN, O_VSYNC, O_MOVIES,
    O_RESOLUTION, O_PERSPECTIVE, O_SMOOTH_3D, O_TRUE_COLOR, O_DITHER, O_SOFT_SHADOWS, O_BATTLE_SHADOWS, O_BATTLE_SKY,
    O_ICONS, O_RUMBLE, O_DEADZONE,
    O_FAST_TEXT, O_FAST_TRAVEL, O_OBJECTIVE, O_JOURNAL, O_PACKS,
    O_BLADEGARURUMON, O_FLAMEDRAMON,
    O_COUNT
};
enum {
    S_TAGLINE, S_FAN,
    S_PLAY, S_PLAY_D, S_SETTINGS, S_SETTINGS_D, S_TOOLS, S_TOOLS_D, S_LANGUAGE, S_LANGUAGE_D, S_QUIT, S_QUIT_D,
    S_RELEASE, S_RELEASE_D, S_READY, S_DISC_OK, S_VERSION,
    S_BROWSE, S_NEED, S_ONCE, S_DROP, S_OR, S_ISO, S_KEYS, S_READING, S_CANCEL,
    S_E_OPEN, S_E_GAME, S_E_BAD, S_E_WRITE, S_E_READ, S_E_TRUNCATED, S_E_CUE, S_E_SPACE, S_SEE_LOG,
    S_DISPLAY, S_GRAPHICS, S_INPUT, S_MODS, S_PLAYABLE, S_RESET, S_RESET_D, S_ON, S_OFF, S_AUTO, S_KEYBOARD,
    S_BACK, S_BACK_D, S_FOLDER, S_FOLDER_D, S_LOG, S_LOG_D, S_VIEWER, S_VIEWER_D, S_EDITOR, S_EDITOR_D, S_CHECK, S_CHECK_D,
    S_CHECKING, S_UP_TO_DATE, S_CHECK_FAILED, S_GAME_RUNNING, S_GAME_FAILED, S_NO_EXE, S_SAVE_FAILED, S_NO_LOG,
    S_HINT_MENU, S_HINT_SETTINGS,
    S_CREDITS, S_CREDITS_D, S_BUTTONS, S_GAMEPAD, S_BIND_D, S_PRESS_KEY, S_PRESS_PAD,
    S_B_UP, S_B_RIGHT, S_B_DOWN, S_B_LEFT, S_B_TRIANGLE, S_B_CIRCLE, S_B_CROSS, S_B_SQUARE,
    S_T_GET, S_T_OPEN, S_T_SETUP, S_T_DOWNLOADING, S_T_UNPACKING, S_T_PREPARING, S_T_READY, S_T_FAILED, S_T_CANCELLED, S_T_NO_START, S_T_BUSY, S_T_NEED_DISC,
    S_TEAM, S_LAST_SAVE, S_LEVEL, S_NO_TEAM,
    S_COUNT
};
typedef struct { const char *name, *desc; } Text;
typedef struct {
    const char *name;
    const char *s[S_COUNT];
    Text option[O_COUNT];
} Lang;
extern const Lang LANG_EN, LANG_ES, LANG_FR, LANG_DE, LANG_IT;
extern const Lang *const LANG_TABLE[LANGS];
extern const char *const LANG_CODES[LANGS];

/* paths.c: the folders, the log, and ini files that keep what they don't touch */
typedef struct { const char *key, *value; bool done; } IniEdit; /* value NULL: the line goes */
void paths_init(void);
void path_in_root(char *out, int size, const char *name);
const char *path_data(void);
bool data_has_file(const char *name);
void native_log(const char *fmt, ...);
bool ini_get(const char *file, const char *key, char *value, int size);
bool ini_write(const char *file, IniEdit *edits, int count);
char *ini_apply(const char *in, IniEdit *edits, int count); /* SDL_free it */

/* options.c */
void options_load(void);
void options_reset(void);
bool options_save(void);
void option_step(int option, int dir);
void option_value(int option, const Lang *lang, char *out, int size);
bool option_is_off(int option);

/* binds.c: the controls, as the game's own menu sets them */
enum { BIND_COUNT = 16 };
void binds_load(void);
void binds_reset(void);
bool binds_save(void);
const char *bind_text(int bit, int slot); /* slots: two keys, the pad's button */
const char *bind_label(int bit);
int bind_slot(void);
void bind_slot_set(int slot);
int bind_capturing(void); /* the button waiting for its key, or -1 */
void bind_capture(int bit);
void bind_clear(int bit);
void bind_event(const SDL_Event *e);
int binds_selftest(void);

/* game.c: other programs. None is ever stopped from here. */
bool game_start(void);
bool game_running(void);
bool game_ended(int *code); /* once, when it ends */
bool process_open(const char *const *args, const char *cwd); /* started and left alone */
int process_wait(const char *const *args, const char *cwd);  /* hidden, to its end: its exit code, or -1 */

/* net.c: HTTPS, blocking */
typedef bool (*NetSink)(const void *data, int size, void *ctx); /* false stops */
bool net_get(const char *url, NetSink sink, void *ctx, SDL_AtomicInt *percent);
char *net_text(const char *url); /* SDL_free it; NULL if it failed */

/* tools.c: the model viewer (0) and the field editor (1) */
void tool_activate(int tool);
void tool_value(int tool, char *out, int size);
bool tools_busy(void);
void tools_poll(void);
void tools_shutdown(void);
int tools_selftest(void);

/* release.c: is there a newer port on GitHub? */
void release_start(bool manual);
void release_fake(const char *tag);
const char *release_tag(void); /* the newer version, or NULL */
bool release_busy(void);
void release_url(char *out, int size);
int release_finished_manual(void); /* once after a manual check: a string to show, or -1 */
int release_selftest(void);

/* screens.c: what's on screen */
enum { R_ACTION, R_OPTION, R_LANGUAGE, R_HEADING, R_BIND, R_END };
enum { SC_HOME, SC_SETTINGS, SC_TOOLS, SC_COUNT };
enum { ST_READY, ST_NO_GAME, ST_ERROR };
typedef struct { Uint8 kind; short id; } Row; /* action: its name's string; the description is the next one */
typedef struct {
    SDL_Window *window;
    int lang, state, screen, tab, error;
    int sel[SC_COUNT];
    int scroll; /* the first row in sight of a list too long for its panel */
    bool quit;
    const char *note; /* shown instead of the description until the next move */
} App;
extern App g_app;
#define L (LANG_TABLE[g_app.lang])
const Row *rows(void);
int row_count(const Row *r);
int list_rows(const Row *r); /* those drawn in the list: all but the language */
int list_first(void);       /* the first in sight, with the selected one kept in sight */
int bind_column(int slot);  /* where a binding's slot starts on screen */
SDL_Rect list_rect(void);
SDL_Rect tab_rect(int tab);
bool first_run_screen(void);
void screens_draw(void);

/* input.c: what's done with it */
enum { A_NONE, A_UP, A_DOWN, A_LEFT, A_RIGHT, A_OK, A_BACK, A_PREV, A_NEXT, A_CLEAR };
void screens_action(int action);
void screens_mouse(int x, int y, bool clicked);
bool screens_tick(void); /* true while something's going on in the background */
void screens_shutdown(void);
void language_set(int lang);

/* picker.c: the language list, top right */
enum { PICKER_NONE = -2, PICKER_BUTTON = -1 }; /* or the list's row */
void picker_draw(int lang, bool focused);
bool picker_is_open(void);
void picker_set_open(bool open);
int picker_hit(int x, int y);
void picker_hover(int row);

/* team.c: the party in the save on the memory card, read only */
typedef struct { int partner[3], level[3], count, hours, minutes; char when[20]; } Team;
const Team *team_get(void); /* NULL without a save that has a team; read again when the card changes */
const char *team_name(int partner);
Uint32 team_colour(int partner);

/* home.c: the home screen once the disc is read, and the bar on top of the others */
void home_draw(void);
void bar_draw(int page, int selected); /* page and selected: S_PLAY, S_SETTINGS or S_TOOLS */
int bar_hit(int x, int y);             /* the page under the mouse, or -1 */
int home_hit(int x, int y); /* the row under the mouse, or -1 */
bool home_fits(const Lang *l);

/* first_run.c: the screen that asks for the disc, and the reading */
void first_run_draw(void);
void first_run_browse(void);
void disc_ask(void (*then)(const char *path)); /* the dialog for the disc; `then` hears the answer, "" if none */
int first_run_error(int extract_result);       /* the string for extract.c's error */
void first_run_image(const char *path);
void first_run_poll(void);
void first_run_stop(void);
void first_run_wait(void);
bool first_run_reading(void);
bool first_run_hit(int x, int y);
bool first_run_button_hit(int x, int y);

/* check.c */
int strings_check(void); /* every string of every language fits, and the self-tests: returns the failures */
