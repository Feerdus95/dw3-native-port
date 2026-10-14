/* The options menu: Esc, or Back on the gamepad. The game stops
   while it is open; what changes there goes to native.ini when it closes.
   Its options in tabs, L1 / R1 (Q / E, Page Up / Down) between them, with
   the battle menu's page arrows. */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include "platform.h"

extern int NTSC_MODE; /* the game's: 60 Hz (its NTSC mode) rather than 50 */
void SsSetTickMode(long mode);

enum { FPS, SCALE, SUBPIXEL, DEPTH, WIDESCREEN, PERSPECTIVE, SMOOTH_3D, TRUE_COLOR, DITHER, SOFT_SHADOWS, BATTLE_SHADOWS, MOVIES, VSYNC, FULLSCREEN, WINDOW, RUMBLE, DEADZONE, FAST_TEXT, FAST_FORWARD, FAST_TRAVEL, MAP_OBJECTIVE, JOURNAL, EXTRA_PACKS, BLADEGARURUMON, ICONS, CONTROLS, ITEMS };

static const char *LABELS[ITEMS] = {
    "Frame rate", "Resolution", "Sub-pixel 3D", "Depth buffer", "Widescreen battles", "Perspective 3D", "Smooth 3D textures", "True colour", "Dithering", "Soft shadows", "Battle shadows", "Smooth movies", "VSync", "Fullscreen", "Window size", "Vibration", "Stick deadzone",
    "Fast text (hold X)", "Fast forward (Tab/R3)", "Fast travel (map, X)", "Objective on map", "Digilog (field menu)", "Extra starter packs", "BladeGarurumon",
    "Button icons", "Controls",
};

/* the tabs and their options, each list ending with -1 */
#define TABS 4
#define ROWS 9 /* the longest tab's: the box stays the same size */
static const char *TAB_NAMES[TABS] = {"Display", "Graphics", "Input", "Mods"};
static const int TAB_ITEMS[TABS][ROWS + 1] = {
    {FPS, FULLSCREEN, WINDOW, WIDESCREEN, VSYNC, MOVIES, -1},
    {SCALE, SUBPIXEL, DEPTH, PERSPECTIVE, SMOOTH_3D, TRUE_COLOR, DITHER, SOFT_SHADOWS, BATTLE_SHADOWS, -1},
    {ICONS, CONTROLS, RUMBLE, DEADZONE, -1},
    {FAST_TEXT, FAST_FORWARD, FAST_TRAVEL, MAP_OBJECTIVE, JOURNAL, EXTRA_PACKS, BLADEGARURUMON, -1},
};

static int s_open, s_tab, s_cursor[TABS], s_changed;

static int tab_count(int tab) {
    int n = 0;

    while (n < ROWS && TAB_ITEMS[tab][n] >= 0) {
        n++;
    }
    return n;
}

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
    case WINDOW: SDL_snprintf(out, size, "%dx%d", video_window_width(window_scale()), 240 * window_scale()); return;
    case DEADZONE: SDL_snprintf(out, size, "%d%%", (g_deadzone * 100 + 16383) / 32767); return;
    case FAST_FORWARD: SDL_snprintf(out, size, g_ff_speed > 1 ? "x%d" : "Off", g_ff_speed); return;
    case ICONS: {
        static const char *SHOWN[ICONS_COUNT] = {"Auto", "PS3", "PS4", "PS5", "Xbox 360", "Xbox One", "Xbox Series", "Switch", "Keyboard"};

        SDL_snprintf(out, size, "%s", SHOWN[g_icons]);
        return;
    }
    case CONTROLS: SDL_snprintf(out, size, ">"); return;
    case DITHER: on = g_dither_on; break;
    case SOFT_SHADOWS: on = g_soft_shadows; break;
    case BATTLE_SHADOWS: on = g_battle_shadows; break;
    case SUBPIXEL: on = g_subpixel; break;
    case DEPTH: on = g_depth; break;
    case WIDESCREEN: on = g_widescreen; break;
    case PERSPECTIVE: on = g_perspective; break;
    case SMOOTH_3D: on = g_smooth_3d; break;
    case TRUE_COLOR: on = g_true_color; break;
    case MOVIES: on = g_smooth_movies; break;
    case VSYNC: on = g_vsync; break;
    case FULLSCREEN: on = (SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN) != 0; break;
    case RUMBLE: on = g_rumble; break;
    case FAST_TEXT: on = g_fast_text; break;
    case FAST_TRAVEL: on = g_fast_travel; break;
    case MAP_OBJECTIVE: on = g_map_objective; break;
    case JOURNAL: on = g_journal; break;
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
    case BATTLE_SHADOWS: g_battle_shadows = !g_battle_shadows; break;
    case SUBPIXEL: g_subpixel = !g_subpixel; break;
    case DEPTH: g_depth = !g_depth; break;
    case WIDESCREEN: /* the window follows, unless fullscreen */
        g_widescreen = !g_widescreen;
        if (!(SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN)) {
            video_resize(window_scale());
        }
        break;
    case PERSPECTIVE: g_perspective = !g_perspective; break;
    case SMOOTH_3D: g_smooth_3d = !g_smooth_3d; break;
    case TRUE_COLOR: g_true_color = !g_true_color; break;
    case MOVIES: g_smooth_movies = !g_smooth_movies; break;
    case VSYNC: g_vsync = !g_vsync; break;
    case FULLSCREEN: SDL_SetWindowFullscreen(video_window(), !(SDL_GetWindowFlags(video_window()) & SDL_WINDOW_FULLSCREEN)); break;
    case RUMBLE: g_rumble = !g_rumble; break;
    case FAST_TEXT: g_fast_text = !g_fast_text; break;
    case FAST_TRAVEL: g_fast_travel = !g_fast_travel; break;
    case MAP_OBJECTIVE: g_map_objective = !g_map_objective; break;
    case JOURNAL: g_journal = !g_journal; break;
    case EXTRA_PACKS: g_extra_packs = !g_extra_packs; break;
    case BLADEGARURUMON: g_bladegarurumon = !g_bladegarurumon; break;
    case FAST_FORWARD: ff_cycle(); return; /* not saved */
    case ICONS: g_icons = (g_icons + (dir < 0 ? ICONS_COUNT - 1 : 1)) % ICONS_COUNT; break;
    case CONTROLS:
        if (dir >= 0) {
            controls_open();
        }
        return;
    case WINDOW:
        scale = SDL_clamp(window_scale() + (dir < 0 ? -1 : 1), 1, 8);
        video_resize(scale);
        break;
    case DEADZONE: /* 5% steps, up to 50% */
        g_deadzone = SDL_clamp(((g_deadzone * 100 + 16383) / 32767 / 5 + (dir < 0 ? -1 : 1)) * 5, 0, 50) * 32767 / 100;
        break;
    }
    s_changed = 1;
}

/* native.ini's "key = value" lines get the new values; missing keys are
   added, the bindings' only when not the default */
static void save(void) {
    static const char *KEYS[] = {"fps", "render_scale", "dither", "smooth_movies", "fullscreen", "scale", "rumble", "deadzone", "fast_text", "extra_packs", "bladegarurumon", "soft_shadows", "true_color", "perspective", "smooth_3d", "battle_shadows", "widescreen", "vsync", "fast_travel", "map_objective", "journal", "icons"};
    enum { FIXED = SDL_arraysize(KEYS), ALL = FIXED + 32 };
    char path[1100], line[1200], key[32], names[ALL][32], vals[ALL][64];
    int done[ALL] = {0}, i, n = ALL;
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
    SDL_snprintf(vals[14], 16, "%d", g_smooth_3d);
    SDL_snprintf(vals[15], 16, "%d", g_battle_shadows);
    SDL_snprintf(vals[16], 16, "%d", g_widescreen);
    SDL_snprintf(vals[17], 16, "%d", g_vsync);
    SDL_snprintf(vals[18], 16, "%d", g_fast_travel);
    SDL_snprintf(vals[19], 16, "%d", g_map_objective);
    SDL_snprintf(vals[20], 16, "%d", g_journal);
    SDL_snprintf(vals[21], 16, "%s", ICON_NAMES[g_icons]);
    for (i = 0; i < ALL; i++) {
        if (i < FIXED) {
            SDL_strlcpy(names[i], KEYS[i], sizeof names[i]);
        } else {
            done[i] = !pad_bind_line(i - FIXED, names[i], vals[i], sizeof vals[i]); /* the default: not added */
        }
    }
    SDL_snprintf(path, sizeof path, "%snative.ini", SDL_GetBasePath());
    out[0] = 0;
    if ((f = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof line, f) != NULL && len < 60000) {
            for (i = 0; i < n; i++) {
                if (line[0] != '#' && line[0] != ';' && SDL_sscanf(line, " %31[^= ] =", key) == 1 && SDL_strcasecmp(key, names[i]) == 0) {
                    SDL_snprintf(line, sizeof line, "%s = %s\n", names[i], vals[i]);
                    done[i] = 1;
                }
            }
            len += SDL_strlcpy(out + len, line, 65536 - len);
        }
        fclose(f);
    }
    for (i = 0; i < n; i++) {
        if (!done[i]) {
            len += SDL_snprintf(out + len, 65536 - len, "%s = %s\n", names[i], vals[i]);
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

static void move(int up, int down, int left, int right, int ok, int tab) {
    int n = tab_count(s_tab), *c = &s_cursor[s_tab];

    if (tab) s_tab = (s_tab + tab + TABS) % TABS;
    if (up) *c = (*c + n - 1) % n;
    if (down) *c = (*c + 1) % n;
    if (left || right || ok) change(TAB_ITEMS[s_tab][*c], left ? -1 : 1);
}

/* 1 when the event was the menu's */
int menu_event(const SDL_Event *e) {
    controls_note(e);
    if (s_open && controls_active()) {
        if (controls_event(e)) {
            s_changed = 1;
        }
        return e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_WHEEL ||
               e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION;
    }
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
                 b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT, b == SDL_GAMEPAD_BUTTON_SOUTH,
                 (b == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) - (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        }
    }
    if (s_open && e->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = e->key.key;

        move(k == SDLK_UP, k == SDLK_DOWN, k == SDLK_LEFT, k == SDLK_RIGHT, k == SDLK_RETURN || k == SDLK_Z || k == SDLK_SPACE,
             (k == SDLK_E || k == SDLK_PAGEDOWN) - (k == SDLK_Q || k == SDLK_PAGEUP));
    }
    if (s_open && e->type == SDL_EVENT_MOUSE_WHEEL && e->wheel.y != 0) {
        float y = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        move(y > 0, y < 0, 0, 0, 0, 0);
    }
    return s_open && (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                      e->type == SDL_EVENT_MOUSE_WHEEL);
}

/* The battle menu's page arrows (FILE_BATTLE_MENU's frames 0x1F and 0x20,
   textured from FILE_BATTLE_IMAGES' first image), read from the player's disc */
#define FILE_BATTLE_MENU 0x455
#define FILE_BATTLE_IMAGES 0x456

static const uint8_t *archive_entry(const uint8_t *a, long size, int i) {
    uint32_t off;

    if ((i + 1) * 4 > size) {
        return NULL;
    }
    SDL_memcpy(&off, a + i * 4, 4);
    return off && off < (uint32_t)size ? a + off : NULL;
}

/* An RLEN entry unpacked (malloc'd), or the entry itself copied */
static uint8_t *unpack(const uint8_t *p, long room) {
    uint8_t *out = malloc(0x10000), *o = out;
    long s = 8;

    if (out == NULL) {
        return NULL;
    }
    if (SDL_memcmp(p, "RLEN", 4) != 0) {
        SDL_memcpy(out, p, SDL_min(room, 0x10000));
        return out;
    }
    while (s < room && p[s] != 0 && o - out < 0xFF00) {
        int c = p[s++];

        if (c & 0x80) {
            SDL_memset(o, p[s++], c & 0x7F), o += c & 0x7F;
        } else {
            SDL_memcpy(o, p + s, c), o += c, s += c;
        }
    }
    return out;
}

static SDL_Texture *game_arrow(SDL_Renderer *r, int frame) {
    uint8_t *menu = NULL, *images = NULL, *tim = NULL;
    long menu_size = native_read_raw(FILE_BATTLE_MENU, &menu), images_size = native_read_raw(FILE_BATTLE_IMAGES, &images);
    const uint8_t *sheet, *arch, *e;
    SDL_Texture *t = NULL;
    SDL_Surface *s = NULL;

    if (menu_size > 0 && images_size > 0 && (sheet = archive_entry(menu, menu_size, 0)) != NULL &&
        (arch = archive_entry(images, images_size, 1)) != NULL &&
        (e = archive_entry(arch, images_size - (arch - images), 0)) != NULL &&
        (tim = unpack(e, images_size - (e - images))) != NULL) {
        /* the image (64x256 halfwords at 0x200, 0) after the TIM's header */
        const uint16_t *pix = (const uint16_t *)(tim + 8 + 12);
        int32_t parts_off, ids_off, frame_off;
        int k = 0, count, row, idx, px, py, u, v, w, h, cy, x, y;
        const int16_t *f, *part;

        SDL_memcpy(&parts_off, sheet, 4);
        SDL_memcpy(&ids_off, sheet + 4, 4);
        while (k < 256 && sheet[ids_off + k] != frame) {
            k++;
        }
        SDL_memcpy(&frame_off, sheet + (k + 2) * 4, 4);
        f = (const int16_t *)(sheet + frame_off);
        count = f[0], row = f[1], idx = f[3], px = f[4], py = f[5];
        part = (const int16_t *)(sheet + parts_off) + idx * 7;
        u = part[0], v = part[1], w = part[2], h = part[3], cy = part[5];
        if (count == 1 && part[6] == 0 && w <= 16 && h <= 16 && (s = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888)) != NULL) {
            for (y = 0; y < h; y++) {
                for (x = 0; x < w; x++) {
                    int uu = u + x, i = (pix[(v + y) * 64 + uu / 4] >> (uu % 4 * 4)) & 15;
                    uint16_t c = pix[(cy + row) * 64 + part[4] + i];

                    ((uint32_t *)s->pixels)[y * (s->pitch / 4) + x] =
                        c == 0 ? 0 : (uint32_t)((c & 31) << 3 | (c >> 5 & 31) << 11 | (c >> 10 & 31) << 19 | 0xFF000000u);
                }
            }
            t = SDL_CreateTextureFromSurface(r, s);
            SDL_DestroySurface(s);
            (void)px, (void)py;
        }
    }
    free(tim);
    free(menu);
    free(images);
    return t;
}

/* The help line at the box's foot, centred, with the pad's or keys' icons */
static void hint(SDL_Renderer *r, const SDL_FRect *box) {
    SDL_SetRenderDrawColor(r, 160, 160, 160, 255);
    controls_hint(r, box->x + (box->w - controls_hint(r, 0, 0, 0)) / 2, box->y + box->h - 13, 1);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

/* Over the frame, in 320x240, centred: the tabs (L1 / R1 at their ends, with
   the game's arrows, blinking as in its menus), then the tab's options */
void menu_draw(SDL_Renderer *r) {
    static SDL_Texture *arrows[2];
    static int tried;
    SDL_FRect box = {8, 0, 304, 46 + ROWS * 14 + 18};
    int n = tab_count(s_tab), i, blink = (SDL_GetTicks() / 267) & 1;
    float x;
    char text[32];

    box.y = (240 - box.h) / 2;
    if (!s_open) {
        return;
    }
    if (!tried) {
        tried = 1;
        arrows[0] = game_arrow(r, 0x1F);
        arrows[1] = game_arrow(r, 0x20);
    }
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 235);
    SDL_RenderFillRect(r, &box);
    if (controls_active()) {
        controls_draw(r, box.x, box.y, box.w, ROWS);
        hint(r, &box);
        return;
    }

    /* the tab bar: "<L1  Display Graphics Controls Mods  R1>" */
    for (i = 0; i < 2; i++) {
        SDL_FRect a = {i ? box.x + box.w - 14 : box.x + 6, box.y + 8, 8, 8};

        if (blink) {
            if (arrows[i] != NULL) {
                SDL_RenderTexture(r, arrows[i], NULL, &a);
            } else {
                SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
                SDL_RenderDebugText(r, a.x, a.y, i ? ">" : "<");
            }
        }
    }
    SDL_SetRenderDrawColor(r, 150, 170, 220, 255);
    controls_tab_icons(r, box.x + 18, box.x + box.w - 30, box.y + 8);
    for (i = 0, x = 0; i < TABS; i++) {
        x += 8.0f * SDL_strlen(TAB_NAMES[i]) + (i ? 8 : 0);
    }
    x = box.x + (box.w - x) / 2; /* centred between L1 and R1 */
    for (i = 0; i < TABS; i++) {
        float w = 8.0f * SDL_strlen(TAB_NAMES[i]);

        if (i == s_tab) {
            SDL_FRect t = {x - 3, box.y + 5, w + 6, 14};

            SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
            SDL_RenderFillRect(r, &t);
            SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        } else {
            SDL_SetRenderDrawColor(r, 130, 130, 140, 255);
        }
        SDL_RenderDebugText(r, x, box.y + 8, TAB_NAMES[i]);
        x += w + 8;
    }
    SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
    SDL_RenderLine(r, box.x + 4, box.y + 21, box.x + box.w - 4, box.y + 21);

    /* the options */
    for (i = 0; i < n; i++) {
        int item = TAB_ITEMS[s_tab][i];
        float y = box.y + 32 + i * 14;

        if (i == s_cursor[s_tab]) {
            SDL_FRect bar = {box.x + 4, y - 3, box.w - 8, 14};

            SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
            SDL_RenderFillRect(r, &bar);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_RenderDebugText(r, box.x + 10, y, LABELS[item]);
        value(item, text, sizeof text);
        SDL_RenderDebugText(r, box.x + box.w - 10 - 8 * SDL_strlen(text), y, text);
    }
    hint(r, &box);
}
