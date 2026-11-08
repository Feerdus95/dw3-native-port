/* dw3launcher: window, input and the --shot / --keys / --check test switches. */
#include "launcher.h"
#include <stdio.h>

static int key_action(SDL_Keycode key)
{
    switch (key) {
    case SDLK_UP: return A_UP;
    case SDLK_DOWN: return A_DOWN;
    case SDLK_LEFT: return A_LEFT;
    case SDLK_RIGHT: return A_RIGHT;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE: return A_OK;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE: return A_BACK;
    case SDLK_DELETE: return A_CLEAR;
    case SDLK_PAGEUP: return A_PREV;
    case SDLK_PAGEDOWN:
    case SDLK_TAB: return A_NEXT;
    default: return A_NONE;
    }
}

static int pad_action(int button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return A_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return A_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return A_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return A_RIGHT;
    case SDL_GAMEPAD_BUTTON_SOUTH: return A_OK;
    case SDL_GAMEPAD_BUTTON_EAST: return A_BACK;
    case SDL_GAMEPAD_BUTTON_NORTH: return A_CLEAR;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return A_PREV;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return A_NEXT;
    default: return A_NONE;
    }
}

/* the left stick moves like the d-pad: one step each time it crosses the threshold */
static int stick_action(int axis, int value)
{
    static int held[2];
    if (axis > SDL_GAMEPAD_AXIS_LEFTY) {
        return A_NONE;
    }
    const int was = held[axis];
    held[axis] = value > 16000 ? 1 : value < -16000 ? -1 : 0;
    if (held[axis] == was || !held[axis]) {
        return A_NONE;
    }
    if (axis == SDL_GAMEPAD_AXIS_LEFTX) {
        return held[axis] < 0 ? A_LEFT : A_RIGHT;
    }
    return held[axis] < 0 ? A_UP : A_DOWN;
}

static void handle_event(SDL_Renderer *r, SDL_Event *e)
{
    if (bind_capturing() >= 0) {
        bind_event(e);
        return;
    }
    switch (e->type) {
    case SDL_EVENT_MOUSE_WHEEL:
        if (e->wheel.y != 0) {
            const float y = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
            screens_wheel(y > 0 ? -1 : 1);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
        screens_action(key_action(e->key.key));
        break;
    case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e->gdevice.which); break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: screens_action(pad_action(e->gbutton.button)); break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: screens_action(stick_action(e->gaxis.axis, e->gaxis.value)); break;
    case SDL_EVENT_DROP_FILE:
        if (first_run_screen()) {
            first_run_image(e->drop.data);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        SDL_ConvertEventToRenderCoordinates(r, e);
        screens_mouse((int)e->motion.x, (int)e->motion.y, false);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e->button.button == SDL_BUTTON_LEFT) {
            SDL_ConvertEventToRenderCoordinates(r, e);
            screens_mouse((int)e->button.x, (int)e->button.y, true);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e->button.button == SDL_BUTTON_LEFT) {
            screens_mouse_release();
        }
        break;
    default: break;
    }
}

/* --keys: real events through the real handler, so that the shots test the input too */
static void press(const char *name)
{
    static const struct { const char *name; SDL_Keycode key; } KEYS[] = {
        {"up", SDLK_UP}, {"down", SDLK_DOWN}, {"left", SDLK_LEFT}, {"right", SDLK_RIGHT}, {"enter", SDLK_RETURN},
        {"esc", SDLK_ESCAPE}, {"pgup", SDLK_PAGEUP}, {"pgdn", SDLK_PAGEDOWN}, {"tab", SDLK_TAB}, {"del", SDLK_DELETE}, {"f", SDLK_F}, {"z", SDLK_Z},
    };
    static const struct { const char *name; int button; } PADS[] = {
        {"pad-up", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"pad-down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
        {"pad-left", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"pad-right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"pad-a", SDL_GAMEPAD_BUTTON_SOUTH}, {"pad-b", SDL_GAMEPAD_BUTTON_EAST},
        {"pad-lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"pad-rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    };
    SDL_Event e = {0};
    int x, y;
    if (SDL_sscanf(name, "click:%d:%d", &x, &y) == 2) {
        screens_mouse(x, y, true);
        return;
    }
    for (int i = 0; i < (int)SDL_arraysize(KEYS); i++) {
        if (!SDL_strcmp(name, KEYS[i].name)) {
            e.type = SDL_EVENT_KEY_DOWN;
            e.key.key = KEYS[i].key;
            e.key.scancode = SDL_GetScancodeFromKey(KEYS[i].key, NULL);
            handle_event(NULL, &e);
            return;
        }
    }
    for (int i = 0; i < (int)SDL_arraysize(PADS); i++) {
        if (!SDL_strcmp(name, PADS[i].name)) {
            e.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            e.gbutton.button = (Uint8)PADS[i].button;
            handle_event(NULL, &e);
            return;
        }
    }
    SDL_Log("unknown key: %s", name);
}

static void press_all(char *list)
{
    char *state = NULL;
    for (char *k = SDL_strtok_r(list, ",", &state); k; k = SDL_strtok_r(NULL, ",", &state)) {
        press(k);
    }
}

static void redraw(SDL_Renderer *r)
{
    int w, h;
    /* the background over the whole window, the picture centred on it at a whole scale */
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetRenderOutputSize(r, &w, &h);
    background(w, h);
    SDL_SetRenderLogicalPresentation(r, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    screens_draw();
}

/* the biggest whole multiple that leaves room around the window */
static int best_scale(void)
{
    SDL_Rect area;
    int k = 2;
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &area)) {
        k = SDL_min(area.w * 9 / 10 / SCREEN_W, area.h * 9 / 10 / SCREEN_H);
    }
    return SDL_max(k, 1);
}

/* the system's language, if it is one of the five */
static int system_lang(void)
{
    const char *const *CODES = LANG_CODES;
    int n = 0, lang = 0;
    SDL_Locale **l = SDL_GetPreferredLocales(&n);
    for (int i = n - 1; l && i >= 0; i--) { /* the first preferred wins */
        for (int k = 0; k < LANGS; k++) {
            if (!SDL_strcasecmp(l[i]->language, CODES[k])) {
                lang = k;
            }
        }
    }
    SDL_free(l);
    return lang;
}

int main(int argc, char **argv)
{
    int lang = -1, state = -1, w = 0, h = 0, update = -1;
    bool settings = false, check = false;
    const char *shot = NULL;
    char *keys = NULL;
    for (int i = 1; i < argc; i++) {
        const char *next = i + 1 < argc ? argv[i + 1] : "";
        if (!SDL_strcmp(argv[i], "--check")) check = true;
        else if (!SDL_strcmp(argv[i], "--lang")) lang = SDL_atoi(argv[++i]);
        else if (!SDL_strcmp(argv[i], "--page")) settings = !SDL_strcmp(argv[++i], "settings");
        else if (!SDL_strcmp(argv[i], "--state")) { state = !SDL_strcmp(next, "nogame") ? ST_NO_GAME : !SDL_strcmp(next, "error") ? ST_ERROR : ST_READY; i++; }
        else if (!SDL_strcmp(argv[i], "--size")) { SDL_sscanf(argv[++i], "%dx%d", &w, &h); }
        else if (!SDL_strcmp(argv[i], "--keys")) keys = argv[++i];
        else if (!SDL_strcmp(argv[i], "--fake-release")) release_fake(argv[++i]);
        else if (!SDL_strcmp(argv[i], "--fake-tool-update")) tool_update_fake(argv[++i]);
        else if (!SDL_strcmp(argv[i], "--tool-update")) update = SDL_atoi(argv[++i]);
        else if (!SDL_strcmp(argv[i], "--shot")) shot = argv[++i];
    }
    paths_init();
    options_load();
    if (lang < 0) {
        char code[8];
        lang = system_lang();
        for (int i = 0; ini_get("launcher.ini", "language", code, sizeof code) && i < LANGS; i++) {
            if (!SDL_strcasecmp(code, LANG_CODES[i])) {
                lang = i;
            }
        }
    }
    g_app.lang = lang;
    g_app.state = state >= 0 ? state : data_has_file("SLES_039.36") ? ST_READY : ST_NO_GAME;
    g_app.error = S_E_BAD;
    if (g_app.state == ST_READY && !check) {
        sounds_prepare();
    }
    g_app.screen = settings ? SC_SETTINGS : SC_HOME;
    if (check) {
        const int bad = strings_check();
        printf(bad ? "%d strings don't fit\n" : "all strings fit\n", bad);
        return bad != 0;
    }
    if (update >= 0) {
        return tool_update_now(update) ? 0 : 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2003", SDL_GetError(), NULL);
        return 1;
    }
    const int k = best_scale();
    SDL_Window *window;
    SDL_Renderer *r;
    if (!SDL_CreateWindowAndRenderer("Digimon World 2003", w ? w : SCREEN_W * k, h ? h : SCREEN_H * k,
                                     SDL_WINDOW_RESIZABLE | (shot ? SDL_WINDOW_HIDDEN : 0), &window, &r)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 2003", SDL_GetError(), NULL);
        return 1;
    }
    SDL_SetRenderLogicalPresentation(r, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_SetRenderVSync(r, 1);
    g_app.window = window;
    SDL_SetWindowAspectRatio(window, (float)SCREEN_W / SCREEN_H, (float)SCREEN_W / SCREEN_H); /* a resized window has no bars above and below */
    draw_init(r);
    if (keys) {
        press_all(keys);
    }
    if (shot) {
        redraw(r);
        SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED); /* the whole window */
        SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
        const bool ok = s && SDL_SavePNG(s, shot);
        SDL_Quit();
        return ok ? 0 : 1;
    }
    redraw(r);
    SDL_RenderPresent(r);
    port_cleanup();
    release_start(false);
    SDL_Event e;
    bool closing = false, busy = screens_tick(), was_running = false;
    while (!closing && !g_app.quit) {
        /* nothing to do: asleep until an event; while the game runs, a look now and then and no drawing */
        if (SDL_WaitEventTimeout(&e, game_running() ? 250 : busy ? 50 : -1)) {
            do {
                if (e.type == SDL_EVENT_QUIT) {
                    closing = true;
                } else {
                    handle_event(r, &e);
                }
            } while (SDL_PollEvent(&e));
        }
        busy = screens_tick();
        if (game_running() != was_running) {
            was_running = game_running();
            if (was_running) {
                SDL_MinimizeWindow(window);
            } else {
                SDL_RestoreWindow(window);
                SDL_RaiseWindow(window);
            }
        }
        if (!was_running) {
            redraw(r);
            SDL_RenderPresent(r);
        }
    }
    screens_shutdown();
    SDL_Quit();
    return 0;
}
