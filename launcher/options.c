/* Every option native.ini saves, read from it and written back when changed. Only what changed is written. */
#include "launcher.h"

enum { K_BOOL, K_FPS, K_TIMES, K_ICONS, K_PERCENT, K_FOCUS };

static const struct { const char *key; Uint8 kind; short def, min, max, step; } DEF[O_COUNT] = {
    [O_FPS] = {"fps", K_FPS, 50, 50, 60, 10},
    [O_FULLSCREEN] = {"fullscreen", K_BOOL, 0},
    [O_WINDOW] = {"scale", K_TIMES, 0, 0, 8, 1}, /* 0, Auto: no line, the biggest that fits the screen */
    [O_WIDESCREEN] = {"widescreen", K_BOOL, 0},
    [O_VSYNC] = {"vsync", K_BOOL, 1},
    [O_MOVIES] = {"smooth_movies", K_BOOL, 1},
    [O_RESOLUTION] = {"render_scale", K_TIMES, 0, 0, 8, 1}, /* 0, Auto: no line, as the window up to 4 */
    [O_PERSPECTIVE] = {"perspective", K_BOOL, 1},
    [O_SMOOTH_3D] = {"smooth_3d", K_BOOL, 1},
    [O_TRUE_COLOR] = {"true_color", K_BOOL, 1},
    [O_DITHER] = {"dither", K_BOOL, 1},
    [O_SOFT_SHADOWS] = {"soft_shadows", K_BOOL, 0},
    [O_BATTLE_SHADOWS] = {"battle_shadows", K_BOOL, 1},
    [O_BATTLE_SKY] = {"battle_sky", K_BOOL, 0},
    [O_INACTIVE] = {"inactive_window", K_FOCUS, 0, 0, 2, 1},
    [O_FF_MENUS] = {"ff_menu_speed", K_BOOL, 0},
    [O_MENU_WRAP] = {"menu_wrap", K_BOOL, 0},
    [O_SKIP_START] = {"skip_press_start", K_BOOL, 0},
    [O_ICONS] = {"icons", K_ICONS, 4, 0, 9, 1},
    [O_RUMBLE] = {"rumble", K_BOOL, 1},
    [O_DEADZONE] = {"deadzone", K_PERCENT, 10, 0, 50, 5},
    [O_FAST_TEXT] = {"fast_text", K_BOOL, 1},
    [O_FAST_TRAVEL] = {"fast_travel", K_BOOL, 0},
    [O_OBJECTIVE] = {"map_objective", K_BOOL, 0},
    [O_JOURNAL] = {"journal", K_BOOL, 0},
    [O_PACKS] = {"extra_packs", K_BOOL, 1},
    [O_BLADEGARURUMON] = {"bladegarurumon", K_BOOL, 1},
    [O_FULL_EXP_SHARE] = {"full_exp_share", K_BOOL, 0},
    [O_HEAL_LEVELUP] = {"heal_on_levelup", K_BOOL, 0},
    [O_SAVE_ANYWHERE] = {"save_anywhere", K_BOOL, 0},
    [O_CUSTOM_EFFECTS] = {"custom_effects", K_BOOL, 1},
    [O_CUSTOM_SOUNDS] = {"custom_sounds", K_BOOL, 1},
    [O_CUSTOM_SCRIPTS] = {"custom_scripts", K_BOOL, 1},
};
static const char *const ICON_KEYS[] = {"auto", "ps1", "ps3", "ps4", "ps5", "xbox360", "xboxone", "xboxseries", "switch", "keyboard"};
static const char *const ICON_NAMES[] = {NULL, "PS1", "PS3", "PS4", "PS5", "Xbox 360", "Xbox One", "Xbox Series", "Switch", NULL};

static short s_value[O_COUNT];
static bool s_dirty[O_COUNT];

void options_load(void)
{
    char v[32];
    binds_load();
    for (int o = 0; o < O_COUNT; o++) {
        s_value[o] = DEF[o].def;
        s_dirty[o] = false;
        if (!ini_get("native.ini", DEF[o].key, v, sizeof v)) {
            continue;
        }
        switch (DEF[o].kind) {
        case K_BOOL: s_value[o] = SDL_atoi(v) != 0; break;
        case K_FPS: s_value[o] = SDL_atoi(v) == 60 ? 60 : 50; break;
        case K_TIMES: s_value[o] = (short)SDL_clamp(SDL_atoi(v), 0, 8); break;
        case K_FOCUS: s_value[o] = (short)SDL_clamp(SDL_atoi(v), 0, 2); break;
        case K_PERCENT: s_value[o] = (short)SDL_clamp((SDL_atoi(v) * 100 + 16383) / 32767, 0, 100); break;
        default:
            for (int i = 0; i < (int)SDL_arraysize(ICON_KEYS); i++) {
                if (!SDL_strcasecmp(v, ICON_KEYS[i])) {
                    s_value[o] = (short)i;
                }
            }
            break;
        }
    }
}

void options_reset(void)
{
    binds_reset();
    for (int o = 0; o < O_COUNT; o++) {
        s_dirty[o] |= s_value[o] != DEF[o].def;
        s_value[o] = DEF[o].def;
    }
}

bool options_save(void)
{
    static char text[O_COUNT][16];
    IniEdit edits[O_COUNT];
    int n = 0;
    if (!binds_save()) {
        return false;
    }
    for (int o = 0; o < O_COUNT; o++) {
        if (s_dirty[o]) {
            const int v = s_value[o];
            const char *value = text[o];
            if (DEF[o].kind == K_ICONS) {
                value = ICON_KEYS[v];
            } else if (DEF[o].kind == K_TIMES && v == 0) {
                value = NULL;
            } else {
                SDL_snprintf(text[o], sizeof text[o], "%d", DEF[o].kind == K_PERCENT ? v * 32767 / 100 : v);
            }
            edits[n++] = (IniEdit){DEF[o].key, value, false};
        }
    }
    if (n == 0) {
        return true;
    }
    if (!ini_write("native.ini", edits, n)) {
        return false;
    }
    SDL_memset(s_dirty, 0, sizeof s_dirty);
    return true;
}

void option_step(int o, int dir)
{
    s_dirty[o] = true;
    if (DEF[o].kind == K_BOOL) {
        s_value[o] = !s_value[o];
        return;
    }
    const int v = s_value[o] + dir * DEF[o].step;
    s_value[o] = (short)(v > DEF[o].max ? DEF[o].min : v < DEF[o].min ? DEF[o].max : v);
}

void option_value(int o, const Lang *l, char *out, int size)
{
    const int v = s_value[o];
    switch (DEF[o].kind) {
    case K_BOOL: SDL_snprintf(out, size, "%s", l->s[v ? S_ON : S_OFF]); break;
    case K_FPS: SDL_snprintf(out, size, "%d", v); break;
    case K_TIMES:
        if (v == 0) {
            SDL_snprintf(out, size, "%s", l->s[S_AUTO]);
        } else {
            SDL_snprintf(out, size, "%dx (%dx%d)", v, 320 * v, 240 * v);
        }
        break;
    case K_FOCUS: SDL_snprintf(out, size, "%s", l->s[v == 2 ? S_PAUSE : v == 1 ? S_MUTE : S_OFF]); break;
    case K_PERCENT: SDL_snprintf(out, size, "%d%%", v); break;
    default: SDL_snprintf(out, size, "%s", v == 0 ? l->s[S_AUTO] : v == 9 ? l->s[S_KEYBOARD] : ICON_NAMES[v]); break;
    }
}

bool option_is_off(int o) { return (DEF[o].kind == K_BOOL && !s_value[o]); }
