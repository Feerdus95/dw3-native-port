/* The screen that asks for the disc: the old first-run screen's text, colours and drop zone. The reading goes on a
   thread (extract.c), so that the window answers and it can be cancelled or closed. */
#include "launcher.h"
#include "extract.h"

enum { ZONE_X = 64, ZONE_Y = 94, ZONE_W = SCREEN_W - 128, ZONE_H = 100, BUTTON_W = 128, BUTTON_H = 18, BUTTON_X = (SCREEN_W - BUTTON_W) / 2, BUTTON_Y = ZONE_Y + 56 };

static SDL_Thread *s_thread;
static SDL_AtomicInt s_percent, s_stop, s_done, s_dialog; /* s_dialog: 2 once answered */
static char s_image[1200], s_answer[1200];
static void (*s_then)(const char *path);

static void dashed(int x, int y, int w, int h, Uint32 colour)
{
    for (int i = 0; i < w; i += 8) {
        fill(x + i, y, SDL_min(4, w - i), 1, colour);
        fill(x + i, y + h, SDL_min(4, w - i), 1, colour);
    }
    for (int i = 0; i < h; i += 8) {
        fill(x, y + i, 1, SDL_min(4, h - i), colour);
        fill(x + w, y + i, 1, SDL_min(4, h - i), colour);
    }
}

static void draw_button(const char *label, bool focused)
{
    fill(BUTTON_X, BUTTON_Y, BUTTON_W, BUTTON_H, focused ? C_GOLD_HI : C_GOLD);
    text_flat((SCREEN_W - text_width(label, 1)) / 2, BUTTON_Y + 2, C_BG, label);
}

void first_run_draw(void)
{
    const bool focused = rows()[g_app.sel[SC_HOME]].kind != R_LANGUAGE;
    text_centre(50, 1, C_TEXT, L->s[S_NEED]);
    text_centre(63, 1, C_GOLD, GAME_LINE);
    text_centre(76, 1, C_SOFT, L->s[S_ONCE]);
    fill_alpha(ZONE_X, ZONE_Y, ZONE_W, ZONE_H, 0xFFFFFF, focused ? 14 : 8);
    dashed(ZONE_X, ZONE_Y, ZONE_W, ZONE_H, C_EDGE);
    if (s_thread) {
        char pct[16];
        const int percent = SDL_GetAtomicInt(&s_percent);
        SDL_snprintf(pct, sizeof pct, "%d%%", percent);
        text_centre(ZONE_Y + 8, 1, C_TEXT, L->s[S_READING]);
        fill(112, ZONE_Y + 26, SCREEN_W - 224, 10, C_PANEL);
        fill(112, ZONE_Y + 26, (SCREEN_W - 224) * percent / 100, 10, C_GOLD);
        text_centre(ZONE_Y + 38, 1, C_SOFT, pct);
        draw_button(L->s[S_CANCEL], true);
        return;
    }
    text_centre(ZONE_Y + 14, 1, C_TEXT, L->s[S_DROP]);
    text_centre(ZONE_Y + 32, 1, C_DIM, L->s[S_OR]);
    draw_button(L->s[S_BROWSE], focused);
    if (g_app.note) {
        text_centre(ZONE_Y + ZONE_H + 8, 1, C_SOFT, g_app.note);
    } else if (g_app.state == ST_ERROR) {
        text_centre(ZONE_Y + ZONE_H + 8, 1, C_RED, L->s[g_app.error]);
        text_centre(ZONE_Y + ZONE_H + 21, 1, C_DIM, L->s[S_SEE_LOG]);
    } else {
        text_centre(ZONE_Y + ZONE_H + 8, 1, C_DIM, L->s[S_ISO]);
    }
}

bool first_run_hit(int x, int y) { return x >= ZONE_X && x < ZONE_X + ZONE_W && y >= ZONE_Y && y < ZONE_Y + ZONE_H; }

bool first_run_button_hit(int x, int y) { return x >= BUTTON_X && x < BUTTON_X + BUTTON_W && y >= BUTTON_Y && y < BUTTON_Y + BUTTON_H; }

bool first_run_reading(void) { return s_thread != NULL; }

static int progress(int percent)
{
    SDL_SetAtomicInt(&s_percent, percent);
    return !SDL_GetAtomicInt(&s_stop);
}

static int SDLCALL read_thread(void *unused)
{
    (void)unused;
    const int result = native_extract(s_image, path_data(), progress);
    if (result == E_NONE) {
        sounds_render(); /* the viewer's original hit sounds, from the data just written */
    }
    SDL_SetAtomicInt(&s_done, 1);
    return result;
}

void first_run_image(const char *path)
{
    if (s_thread || !path || !path[0]) {
        return;
    }
    SDL_strlcpy(s_image, path, sizeof s_image);
    SDL_SetAtomicInt(&s_percent, 0);
    SDL_SetAtomicInt(&s_stop, 0);
    SDL_SetAtomicInt(&s_done, 0);
    g_app.note = NULL;
    picker_set_open(false);
    s_thread = SDL_CreateThread(read_thread, "extract", NULL);
}

static void SDLCALL answered(void *unused, const char *const *files, int filter)
{
    (void)unused;
    (void)filter;
    s_answer[0] = 0;
    if (files && files[0]) {
        SDL_strlcpy(s_answer, files[0], sizeof s_answer);
    }
    SDL_SetAtomicInt(&s_dialog, 2);
    SDL_Event e = {.type = SDL_EVENT_USER};
    SDL_PushEvent(&e);
}

void disc_ask(void (*then)(const char *path))
{
    static const SDL_DialogFileFilter FILTERS[] = {{"Disc images (.cue, .bin, .iso)", "cue;bin;iso"}};
    if (!s_thread && SDL_GetAtomicInt(&s_dialog) == 0) {
        s_then = then;
        SDL_SetAtomicInt(&s_dialog, 1);
        SDL_ShowOpenFileDialog(answered, NULL, g_app.window, FILTERS, 1, NULL, false);
    }
}

void first_run_browse(void) { disc_ask(first_run_image); }

int first_run_error(int e)
{
    static const short ERRORS[] = {[E_OPEN] = S_E_OPEN, [E_GAME] = S_E_GAME, [E_BAD] = S_E_BAD, [E_WRITE] = S_E_WRITE,
                                   [E_READ] = S_E_READ, [E_TRUNCATED] = S_E_TRUNCATED, [E_CUE] = S_E_CUE, [E_SPACE] = S_E_SPACE};
    return ERRORS[e];
}

static void finish(void)
{
    int result = E_WRITE;
    SDL_WaitThread(s_thread, &result);
    s_thread = NULL;
    if (result == E_NONE) {
        g_app.state = ST_READY;
        g_app.sel[SC_HOME] = 0;
    } else if (result == E_CANCELLED) {
        g_app.state = ST_NO_GAME;
    } else {
        g_app.state = ST_ERROR;
        g_app.error = first_run_error(result);
    }
}

void first_run_poll(void)
{
    if (SDL_GetAtomicInt(&s_dialog) == 2) {
        SDL_SetAtomicInt(&s_dialog, 0);
        s_then(s_answer);
    }
    if (s_thread && SDL_GetAtomicInt(&s_done)) {
        finish();
    }
}

/* cancels and waits: extract.c then leaves the data folder as it was */
void first_run_stop(void)
{
    SDL_SetAtomicInt(&s_stop, 1);
}

void first_run_wait(void)
{
    if (s_thread) {
        first_run_stop();
        finish();
    }
}
