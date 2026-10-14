/* The options menu: Esc, or Back on the gamepad. The game stops
   while it is open; what changes there goes to native.ini when it closes. */
#include <SDL3/SDL.h>
#include "platform.h"

extern int NTSC_MODE; /* the game's: 60 Hz (its NTSC mode) rather than 50 */
void SsSetTickMode(long mode);

enum { FPS, SCALE, SUBPIXEL, DEPTH, PERSPECTIVE, TRUE_COLOR, DITHER, SOFT_SHADOWS, MOVIES, FULLSCREEN, WINDOW, RUMBLE, DEADZONE, FAST_TEXT, FAST_FORWARD, EXTRA_PACKS, BLADEGARURUMON, CLOSE, ITEMS };

static const char *LABELS[ITEMS] = {
    "Frame rate", "Resolution", "Sub-pixel 3D", "Depth buffer", "Perspective 3D", "True colour", "Dithering", "Soft shadows", "Smooth movies", "Fullscreen", "Window size", "Vibration", "Stick deadzone",
    "Fast text (hold X)", "Fast forward (Tab/R3)", "Extra starter packs", "BladeGarurumon", "Close",
};

static int s_open, s_cursor, s_changed;
static int s_top; /* the first row shown */
#define ROWS 9    /* shown at once, the rest scrolls: the box stays inside 240 */

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
    case FAST_FORWARD: SDL_snprintf(out, size, g_ff_speed > 1 ? "x%d" : "Off", g_ff_speed); return;
    case CLOSE: out[0] = 0; return;
    case DITHER: on = g_dither_on; break;
    case SOFT_SHADOWS: on = g_soft_shadows; break;
    case SUBPIXEL: on = g_subpixel; break;
    case DEPTH: on = g_depth; break;
    case PERSPECTIVE: on = g_perspective; break;
    case TRUE_COLOR: on = g_true_color; break;
    case MOVIES: on = g_smooth_movies; break;
    case FULLSCREEN: on = (SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN) != 0; break;
    case RUMBLE: on = g_rumble; break;
    case FAST_TEXT: on = g_fast_text; break;
    case EXTRA_PACKS: on = g_extra_packs; break;
    case BLADEGARURUMON: on = g_bladegarurumon; break;
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
    case SOFT_SHADOWS: g_soft_shadows = !g_soft_shadows; break;
    case SUBPIXEL: g_subpixel = !g_subpixel; break;
    case DEPTH: g_depth = !g_depth; break;
    case PERSPECTIVE: g_perspective = !g_perspective; break;
    case TRUE_COLOR: g_true_color = !g_true_color; break;
    case MOVIES: g_smooth_movies = !g_smooth_movies; break;
    case FULLSCREEN: SDL_SetWindowFullscreen(video_window(), !(SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN)); break;
    case RUMBLE: g_rumble = !g_rumble; break;
    case FAST_TEXT: g_fast_text = !g_fast_text; break;
    case EXTRA_PACKS: g_extra_packs = !g_extra_packs; break;
    case BLADEGARURUMON: g_bladegarurumon = !g_bladegarurumon; break;
    case FAST_FORWARD: ff_cycle(); return; /* not saved */
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
    static const char *KEYS[] = {"fps", "render_scale", "dither", "smooth_movies", "fullscreen", "scale", "rumble", "deadzone", "fast_text", "extra_packs", "bladegarurumon", "soft_shadows", "true_color", "perspective"};
    char path[1100], line[1200], key[32], vals[SDL_arraysize(KEYS)][16];
    int done[SDL_arraysize(KEYS)] = {0}, i, n = SDL_arraysize(KEYS);
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
    SDL_snprintf(vals[8], 16, "%d", g_fast_text);
    SDL_snprintf(vals[9], 16, "%d", g_extra_packs);
    SDL_snprintf(vals[10], 16, "%d", g_bladegarurumon);
    SDL_snprintf(vals[11], 16, "%d", g_soft_shadows);
    SDL_snprintf(vals[12], 16, "%d", g_true_color);
    SDL_snprintf(vals[13], 16, "%d", g_perspective);
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
    /* the cursor's row in view */
    s_top = SDL_clamp(s_top, s_cursor - ROWS + 1, s_cursor);
    s_top = SDL_clamp(s_top, 0, SDL_max(ITEMS - ROWS, 0));
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
    if (s_open && e->type == SDL_EVENT_MOUSE_WHEEL && e->wheel.y != 0) {
        float y = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        move(y > 0, y < 0, 0, 0, 0);
    }
    return s_open && (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                      e->type == SDL_EVENT_MOUSE_WHEEL);
}

/* Over the frame, in 320x240, centred; ROWS rows at a time, a scrollbar for the rest */
void menu_draw(SDL_Renderer *r) {
    int rows = SDL_min(ITEMS, ROWS);
    SDL_FRect box = {16, 0, 288, 26 + rows * 14 + 22};
    char text[32];
    int i;

    box.y = (240 - box.h) / 2;

    if (!s_open) {
        return;
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 235);
    SDL_RenderFillRect(r, &box);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, box.x + (box.w - 7 * 8) / 2, box.y + 8, "OPTIONS");
    for (i = s_top; i < s_top + rows; i++) {
        float y = box.y + 26 + (i - s_top) * 14;

        if (i == s_cursor) {
            SDL_FRect bar = {box.x + 4, y - 3, box.w - (ITEMS > rows ? 12 : 8), 14};

            SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
            SDL_RenderFillRect(r, &bar);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDebugText(r, box.x + 10, y, LABELS[i]);
        value(i, text, sizeof text);
        SDL_RenderDebugText(r, box.x + box.w - 14 - 8 * SDL_strlen(text), y, text);
    }
    if (ITEMS > rows) {
        /* the scrollbar, and arrows where more is hidden */
        SDL_FRect track = {box.x + box.w - 5, box.y + 23, 2, rows * 14};
        SDL_FRect thumb = {track.x, track.y + track.h * s_top / ITEMS, 2, track.h * rows / ITEMS};

        SDL_SetRenderDrawColor(r, 60, 60, 70, 255);
        SDL_RenderFillRect(r, &track);
        SDL_SetRenderDrawColor(r, 160, 160, 160, 255);
        SDL_RenderFillRect(r, &thumb);
        for (i = 0; i < 3; i++) {
            float cx = box.x + box.w / 2;
            if (s_top > 0) {
                SDL_RenderLine(r, cx - i, box.y + 18 + i, cx + i, box.y + 18 + i);
            }
            if (s_top + rows < ITEMS) {
                SDL_RenderLine(r, cx - i, box.y + 26 + rows * 14 + 2 - i, cx + i, box.y + 26 + rows * 14 + 2 - i);
            }
        }
    }
    SDL_SetRenderDrawColor(r, 160, 160, 160, 255);
    SDL_RenderDebugText(r, box.x + (box.w - 8 * 35) / 2, box.y + box.h - 13, "Arrows/wheel: choose   Esc/B: close");
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}
