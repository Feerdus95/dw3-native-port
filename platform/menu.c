/* The options menu: Esc, or Back on the gamepad. The game stops
   while it is open; what changes there goes to native.ini when it closes. */
#include <SDL3/SDL.h>
#include "platform.h"

extern int NTSC_MODE; /* the game's: 60 Hz (its NTSC mode) rather than 50 */
void SsSetTickMode(long mode);

enum { FPS, SCALE, SUBPIXEL, DEPTH, DITHER, MOVIES, FULLSCREEN, WINDOW, RUMBLE, DEADZONE, CLOSE, ITEMS };

static const char *LABELS[ITEMS] = {
    "Frame rate", "Resolution", "Sub-pixel 3D", "Depth buffer", "Dithering", "Smooth movies", "Fullscreen", "Window size", "Vibration", "Stick deadzone", "Close",
};

static int s_open, s_cursor, s_changed;

int menu_active(void) {
    return s_open;
}

/* The game's 60 Hz mode: its clocks, the music's tempo and the vsync follow
   at once; a walking speed is set when the area loads */
void set_fps(int sixty) {
    NTSC_MODE = sixty;
    SsSetTickMode(sixty ? 0x1000 : 0x1000 | 50);
}

static int window_scale(void) {
    int w, h;

    SDL_GetWindowSize(video_window(), &w, &h);
    return SDL_clamp((h + 120) / 240, 1, 8);
}

static void value(int item, char *out, size_t size) {
    int on = 0;

    switch (item) {
    case FPS: SDL_snprintf(out, size, NTSC_MODE ? "60 Hz" : "50 Hz (original)"); return;
    case SCALE: SDL_snprintf(out, size, g_res_scale > 1 ? "%dx%d" : "%dx%d (original)", 320 * g_res_scale, 240 * g_res_scale); return;
    case WINDOW: SDL_snprintf(out, size, "%dx%d", 320 * window_scale(), 240 * window_scale()); return;
    case DEADZONE: SDL_snprintf(out, size, "%d%%", (g_deadzone * 100 + 16383) / 32767); return;
    case CLOSE: out[0] = 0; return;
    case DITHER: on = g_dither_on; break;
    case SUBPIXEL: on = g_subpixel; break;
    case DEPTH: on = g_depth; break;
    case MOVIES: on = g_smooth_movies; break;
    case FULLSCREEN: on = (SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN) != 0; break;
    case RUMBLE: on = g_rumble; break;
    }
    SDL_snprintf(out, size, on ? "On" : "Off");
}

/* Left / right (dir -1 / 1), or Enter / A (dir 0, as right) */
static void change(int item, int dir) {
    int scale;

    switch (item) {
    case FPS: set_fps(!NTSC_MODE); break;
    case SCALE: g_res_scale = (g_res_scale + (dir < 0 ? 6 : 0)) % 8 + 1; break; /* 1-8, round */
    case DITHER: g_dither_on = !g_dither_on; break;
    case SUBPIXEL: g_subpixel = !g_subpixel; break;
    case DEPTH: g_depth = !g_depth; break;
    case MOVIES: g_smooth_movies = !g_smooth_movies; break;
    case FULLSCREEN: SDL_SetWindowFullscreen(video_window(), !(SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN)); break;
    case RUMBLE: g_rumble = !g_rumble; break;
    case WINDOW:
        scale = SDL_clamp(window_scale() + (dir < 0 ? -1 : 1), 1, 8);
        SDL_SetWindowFullscreen(video_window(), false);
        SDL_SetWindowSize(video_window(), 320 * scale, 240 * scale);
        break;
    case DEADZONE: /* 5% steps, up to 50% */
        g_deadzone = SDL_clamp(((g_deadzone * 100 + 16383) / 32767 / 5 + (dir < 0 ? -1 : 1)) * 5, 0, 50) * 32767 / 100;
        break;
    case CLOSE: menu_toggle(); return;
    }
    s_changed = 1;
}

/* native.ini's "key = value" lines get the new values; missing keys are added */
static void save(void) {
    char path[1100], line[1200], key[32], vals[9][16];
    static const char *KEYS[] = {"fps", "render_scale", "dither", "smooth_movies", "fullscreen", "scale", "rumble", "deadzone"};
    int done[9] = {0}, i, n = SDL_arraysize(KEYS);
    size_t len = 0;
    char *out = SDL_malloc(65536);
    FILE *f;

    SDL_snprintf(vals[0], 16, "%d", NTSC_MODE ? 60 : 50);
    SDL_snprintf(vals[1], 16, "%d", g_res_scale);
    SDL_snprintf(vals[2], 16, "%d", g_dither_on);
    SDL_snprintf(vals[3], 16, "%d", g_smooth_movies);
    SDL_snprintf(vals[4], 16, "%d", (SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN) != 0);
    SDL_snprintf(vals[5], 16, "%d", window_scale());
    SDL_snprintf(vals[6], 16, "%d", g_rumble);
    SDL_snprintf(vals[7], 16, "%d", g_deadzone);
    SDL_snprintf(path, sizeof path, "%snative.ini", SDL_GetBasePath());
    out[0] = 0;
    if ((f = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof line, f) != NULL && len < 60000) {
            for (i = 0; i < n; i++) {
                if (line[0] != '#' && line[0] != ';' && SDL_sscanf(line, " %31[^= ] =", key) == 1 && SDL_strcasecmp(key, KEYS[i]) == 0) {
                    SDL_snprintf(line, sizeof line, "%s = %s\n", KEYS[i], vals[i]);
                    done[i] = 1;
                }
            }
            len += SDL_strlcpy(out + len, line, 65536 - len);
        }
        fclose(f);
    }
    for (i = 0; i < n; i++) {
        if (!done[i]) {
            len += SDL_snprintf(out + len, 65536 - len, "%s = %s\n", KEYS[i], vals[i]);
        }
    }
    if ((f = fopen(path, "w")) != NULL) {
        fwrite(out, 1, len, f);
        fclose(f);
    } else {
        LOG("menu: can't write %s", path);
    }
    SDL_free(out);
}

void menu_toggle(void) {
    s_open = !s_open;
    if (!s_open && s_changed) {
        save();
        s_changed = 0;
    }
}

static void move(int up, int down, int left, int right, int ok) {
    if (up) s_cursor = (s_cursor + ITEMS - 1) % ITEMS;
    if (down) s_cursor = (s_cursor + 1) % ITEMS;
    if (left || right || ok) change(s_cursor, left ? -1 : 1);
}

/* 1 when the event was the menu's */
int menu_event(const SDL_Event *e) {
    if (e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_ESCAPE && !e->key.repeat) {
        menu_toggle();
        return 1;
    }
    if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        int b = e->gbutton.button;

        if (b == SDL_GAMEPAD_BUTTON_BACK || (s_open && b == SDL_GAMEPAD_BUTTON_EAST)) {
            menu_toggle();
            return 1;
        }
        if (s_open) {
            move(b == SDL_GAMEPAD_BUTTON_DPAD_UP, b == SDL_GAMEPAD_BUTTON_DPAD_DOWN, b == SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                 b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT, b == SDL_GAMEPAD_BUTTON_SOUTH);
        }
    }
    if (s_open && e->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = e->key.key;

        move(k == SDLK_UP, k == SDLK_DOWN, k == SDLK_LEFT, k == SDLK_RIGHT, k == SDLK_RETURN || k == SDLK_Z || k == SDLK_SPACE);
    }
    return s_open && (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
}

/* Over the frame, in 320x240 */
void menu_draw(SDL_Renderer *r) {
    SDL_FRect box = {16, 44, 288, 24 + ITEMS * 14 + 22};
    char text[32];
    int i;

    if (!s_open) {
        return;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 235);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, box.x + (box.w - 7 * 8) / 2, box.y + 8, "OPTIONS");
    for (i = 0; i < ITEMS; i++) {
        float y = box.y + 26 + i * 14;

        if (i == s_cursor) {
            SDL_FRect bar = {box.x + 4, y - 3, box.w - 8, 14};

            SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
            SDL_RenderFillRect(r, &bar);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDebugText(r, box.x + 10, y, LABELS[i]);
        value(i, text, sizeof text);
        SDL_RenderDebugText(r, box.x + box.w - 10 - 8 * SDL_strlen(text), y, text);
    }
    SDL_SetRenderDrawColor(r, 160, 160, 160, 255);
    SDL_RenderDebugText(r, box.x + 10, box.y + box.h - 14, "Arrows: change    Esc / B: close");
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
