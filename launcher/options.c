/* Every option native.ini saves, read from it and written back when changed. Only what changed is written. */
#include "launcher.h"

enum { K_BOOL, K_FPS, K_TIMES, K_ICONS, K_PERCENT, K_FOCUS, K_NUMBER, K_FSR, K_UPMODE, K_RENDERER };

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
    [O_SHARPEN] = {"sharpen", K_NUMBER, 0, 0, 10, 1},
    [O_BRIGHTNESS] = {"brightness", K_NUMBER, 0, -10, 10, 1},
    [O_GAMMA] = {"gamma", K_NUMBER, 0, -10, 10, 1},
    [O_SATURATION] = {"saturation", K_NUMBER, 0, -10, 10, 1},
    [O_SCANLINES] = {"scanlines", K_NUMBER, 0, 0, 10, 1},
    [O_SMAA] = {"smaa", K_BOOL, 0},
    [O_FSR] = {"fsr", K_FSR, 0, 0, 3, 1},
    [O_UP_MODE] = {"upscale_mode", K_UPMODE, 1, 0, 3, 1},
    [O_INTEGER] = {"integer_scale", K_BOOL, 0},
    [O_INACTIVE] = {"inactive_window", K_FOCUS, 0, 0, 2, 1},
    [O_RENDERER] = {"renderer", K_RENDERER, 0, 0, 2, 1}, /* auto, d3d12, gl */
    [O_FF_MENUS] = {"ff_menu_speed", K_BOOL, 0},
    [O_BATTLE_FORM_NAME] = {"battle_form_name", K_BOOL, 0},
    [O_MENU_WRAP] = {"menu_wrap", K_BOOL, 0},
    [O_SKIP_START] = {"skip_press_start", K_BOOL, 0},
    [O_SKIP_SCENES] = {"skip_scenes", K_BOOL, 0},
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
    [O_TECH_VISUALS] = {"technique_visuals", K_BOOL, 1},
    [O_HEAL_LEVELUP] = {"heal_on_levelup", K_BOOL, 0},
    [O_SAVE_ANYWHERE] = {"save_anywhere", K_BOOL, 0},
    [O_FIX_EQUIP] = {"fix_equip_stacking", K_BOOL, 0},
    [O_CUSTOM_EFFECTS] = {"custom_effects", K_BOOL, 1},
    [O_CUSTOM_SOUNDS] = {"custom_sounds", K_BOOL, 1},
    [O_CUSTOM_SCRIPTS] = {"custom_scripts", K_BOOL, 1},
};
static const char *const ICON_KEYS[] = {"auto", "ps1", "ps3", "ps4", "ps5", "xbox360", "xboxone", "xboxseries", "switch", "keyboard"};
static const char *const RENDERER_KEYS[] = {"auto", "d3d12", "gl"};
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
        case K_UPMODE: case K_FSR: s_value[o] = (short)SDL_clamp(SDL_atoi(v), 0, 3); break;
        case K_RENDERER: s_value[o] = !SDL_strcasecmp(v, "d3d12") ? 1 : !SDL_strcasecmp(v, "gl") ? 2 : 0; break;
        case K_NUMBER: s_value[o] = (short)SDL_clamp(SDL_atoi(v), DEF[o].min, DEF[o].max); break;
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
            } else if (DEF[o].kind == K_RENDERER) {
                value = RENDERER_KEYS[v];
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
    if (o == O_FSR) { /* FSR 3.1 and DLSS are offered once their files are there */
        const bool have[4] = {true, true, game_x64() && fsr_dll_have(), game_x64() && dlss_dll_have()};
        int f = s_value[o];
        do {
            f = (f + (dir < 0 ? 3 : 1)) % 4;
        } while (!have[f]);
        s_value[o] = (short)f;
        return;
    }
    const int v = s_value[o] + dir * DEF[o].step, max = DEF[o].max;
    s_value[o] = (short)(v > max ? DEF[o].min : v < DEF[o].min ? max : v);
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
    case K_UPMODE: SDL_snprintf(out, size, "%s", v == 0 ? "DLAA" : v == 1 ? "Quality" : v == 2 ? "Balanced" : "Performance"); break;
    case K_RENDERER: SDL_snprintf(out, size, "%s", v == 1 ? "Direct3D 12" : v == 2 ? "OpenGL" : l->s[S_AUTO]); break;
    case K_FSR: SDL_snprintf(out, size, "%s", v == 3 ? "DLSS" : v == 2 ? "FSR 3.1" : v == 1 ? "FSR 1" : l->s[S_OFF]); break;
    case K_NUMBER: /* from 0: off or a level; around 0: signed */
        if (v == 0) {
            SDL_snprintf(out, size, "%s", DEF[o].min == 0 ? l->s[S_OFF] : "0");
        } else {
            SDL_snprintf(out, size, DEF[o].min == 0 ? "%d" : "%+d", v);
        }
        break;
    default: SDL_snprintf(out, size, "%s", v == 0 ? l->s[S_AUTO] : v == 9 ? l->s[S_KEYBOARD] : ICON_NAMES[v]); break;
    }
}

bool option_is_off(int o) { return (DEF[o].kind == K_BOOL && !s_value[o]); }
int option_int(int o) { return s_value[o]; }
