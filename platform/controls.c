/* The options menu's Controls page (its Input tab): each PS button's two keys
   and gamepad button, set by pressing them; and the button icons the menu
   shows, from Xelu's free prompts pack (CC0, assets/prompts/ by
   tools/prompts.py), for the pad in use or the one chosen */
#include <SDL3/SDL.h>
#include "platform.h"

int g_icons = ICONS_PS5; /* native.ini; the game's own buttons by default */
const char *const ICON_NAMES[ICONS_COUNT] = {"auto", "ps3", "ps4", "ps5", "xbox360", "xboxone", "xboxseries", "switch", "keyboard"};

/* the pads' icon sets, as the choices after Auto */
enum { FAM_PS3, FAM_PS4, FAM_PS5, FAM_XBOX360, FAM_XBOXONE, FAM_XBOXSERIES, FAM_SWITCH, FAMS_COUNT };
static const char *FAMS[FAMS_COUNT] = {"ps3", "ps4", "ps5", "xbox360", "xboxone", "xboxseries", "switch"};
#define DPAD 102 /* the whole D-pad's icon, for the help line */

/* The page's lines: the buttons, face ones first, then Reset */
#define LINES 17
static const int ORDER[16] = {14, 13, 15, 12, 10, 11, 8, 9, 1, 2, 3, 0, 4, 6, 7, 5};
static const char *LABELS[16] = {"Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left",
                                 "L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square"};
/* each button's place on a gamepad: its PlayStation icon */
static const int PLACE[16] = {
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    PAD_LT, PAD_RT, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST,
};
static const float COL[3] = {130, 182, 250}; /* the slots: two keys, the pad's button */

static int s_open, s_line, s_slot, s_capture, s_pad_used;

void controls_note(const SDL_Event *e) {
    if (e->type == SDL_EVENT_KEY_DOWN) {
        s_pad_used = 0;
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        s_pad_used = 1;
    }
}

/* The icons' pad: the one chosen, else the one plugged in, else an Xbox Series */
static int family(void) {
    if (g_icons >= ICONS_PS3 && g_icons <= ICONS_SWITCH) {
        return g_icons - ICONS_PS3;
    }
    switch (pad_gamepad() != NULL ? SDL_GetGamepadType(pad_gamepad()) : SDL_GAMEPAD_TYPE_UNKNOWN) {
    case SDL_GAMEPAD_TYPE_PS3: return FAM_PS3;
    case SDL_GAMEPAD_TYPE_PS4: return FAM_PS4;
    case SDL_GAMEPAD_TYPE_PS5: return FAM_PS5;
    case SDL_GAMEPAD_TYPE_XBOX360: return FAM_XBOX360;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return FAM_SWITCH;
    default: return FAM_XBOXSERIES;
    }
}

/* The help line's keys rather than buttons: chosen, or the keyboard used last */
static int keyboard_hints(void) {
    return g_icons == ICONS_KEYBOARD || (g_icons == ICONS_AUTO && (!s_pad_used || pad_gamepad() == NULL));
}

static SDL_Texture *load(SDL_Renderer *r, const char *name) {
    static char dir[1100];
    char path[1200];
    SDL_Texture *t = NULL;
    SDL_Surface *s;

    if (dir[0] == 0) { /* by the exe, or the repository's from build/ */
        SDL_snprintf(dir, sizeof dir, "%sprompts", SDL_GetBasePath());
        if (!SDL_GetPathInfo(dir, NULL)) {
            SDL_snprintf(dir, sizeof dir, "%s../assets/prompts", SDL_GetBasePath());
        }
    }
    SDL_snprintf(path, sizeof path, "%s/%s.png", dir, name);
    if ((s = SDL_LoadPNG(path)) != NULL) {
        t = SDL_CreateTextureFromSurface(r, s);
        SDL_DestroySurface(s);
    }
    return t;
}

static SDL_Texture *pad_icon(SDL_Renderer *r, int fam, int src) {
    static SDL_Texture *tex[FAMS_COUNT][DPAD + 1];
    static uint8_t tried[FAMS_COUNT][DPAD + 1];
    const char *name = src == DPAD ? "dpad" : pad_source_name(src);
    char file[64];

    if (src < 0 || src > DPAD || name == NULL) {
        return NULL;
    }
    if (!tried[fam][src]) {
        tried[fam][src] = 1;
        SDL_snprintf(file, sizeof file, "%s_%s", FAMS[fam], name);
        tex[fam][src] = load(r, file);
    }
    return tex[fam][src];
}

static SDL_Texture *key_icon(SDL_Renderer *r, int key) {
    static SDL_Texture *tex[SDL_SCANCODE_COUNT];
    static uint8_t tried[SDL_SCANCODE_COUNT];
    char file[32];

    if (key <= 0 || key >= SDL_SCANCODE_COUNT) {
        return NULL;
    }
    if (!tried[key]) {
        tried[key] = 1;
        SDL_snprintf(file, sizeof file, "key_%d", key);
        tex[key] = load(r, file);
    }
    return tex[key];
}

/* An icon, 12 by 12 on a text line at y, or its name there, up to chars
   letters; its width */
static float icon(SDL_Renderer *r, SDL_Texture *t, const char *name, float x, float y, int chars, int draw) {
    char text[16];

    if (t != NULL) {
        SDL_FRect d = {x, y - 2, 12, 12};

        if (draw) {
            SDL_RenderTexture(r, t, NULL, &d);
        }
        return 12;
    }
    SDL_snprintf(text, SDL_min((size_t)chars + 1, sizeof text), "%s", name != NULL ? name : "?");
    if (draw) {
        SDL_RenderDebugText(r, x, y, text);
    }
    return 8.0f * SDL_strlen(text);
}

static float key(SDL_Renderer *r, int sc, float x, float y, int chars, int draw) {
    return icon(r, key_icon(r, sc), SDL_GetScancodeName(sc), x, y, chars, draw);
}

static float button(SDL_Renderer *r, int fam, int src, float x, float y, int chars, int draw) {
    return icon(r, pad_icon(r, fam, src), src == DPAD ? "D-pad" : pad_source_name(src), x, y, chars, draw);
}

/* "[icon][icon] text  [icon] text": the help line, the tabs' or the page's,
   by keyboard or pad; its width */
float controls_hint(SDL_Renderer *r, float x, float y, int draw) {
    static const struct {
        int key, src;
        const char *text;
    } PARTS[4][6] = {
        {{SDL_SCANCODE_Q, 0, NULL}, {SDL_SCANCODE_E, 0, "tab"}, {SDL_SCANCODE_LEFT, 0, NULL}, {SDL_SCANCODE_RIGHT, 0, "set"}, {SDL_SCANCODE_ESCAPE, 0, "close"}},
        {{0, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, NULL}, {0, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "tab"}, {0, DPAD, "set"}, {0, SDL_GAMEPAD_BUTTON_EAST, "close"}},
        {{SDL_SCANCODE_RETURN, 0, "set"}, {SDL_SCANCODE_DELETE, 0, "clear"}, {SDL_SCANCODE_ESCAPE, 0, "back"}},
        {{0, SDL_GAMEPAD_BUTTON_SOUTH, "set"}, {0, SDL_GAMEPAD_BUTTON_NORTH, "clear"}, {0, SDL_GAMEPAD_BUTTON_EAST, "back"}},
    };
    int set = (s_open ? 2 : 0) + !keyboard_hints(), fam = family(), i;
    float x0 = x;

    if (s_open && s_capture) {
        const char *text = s_slot < 2 ? "Press a key (Esc: cancel)" : "Press a button (Back: cancel)";

        if (draw) {
            SDL_RenderDebugText(r, x, y, text);
        }
        return 8.0f * SDL_strlen(text);
    }
    for (i = 0; i < 6 && (PARTS[set][i].key || PARTS[set][i].src || PARTS[set][i].text); i++) {
        x += PARTS[set][i].key ? key(r, PARTS[set][i].key, x, y, 5, draw) : button(r, fam, PARTS[set][i].src, x, y, 5, draw);
        if (PARTS[set][i].text != NULL) {
            if (draw) {
                SDL_RenderDebugText(r, x + 3, y, PARTS[set][i].text);
            }
            x += 3 + 8.0f * SDL_strlen(PARTS[set][i].text) + 10;
        }
    }
    return x - x0 - 10;
}

/* The tab bar's ends: L1 / R1 on the pad, Q / E on the keyboard */
void controls_tab_icons(SDL_Renderer *r, float left, float right, float y) {
    if (keyboard_hints()) {
        key(r, SDL_SCANCODE_Q, left, y, 1, 1);
        key(r, SDL_SCANCODE_E, right, y, 1, 1);
    } else {
        button(r, family(), SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, left, y, 2, 1);
        button(r, family(), SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, right, y, 2, 1);
    }
}

void controls_open(void) {
    s_open = 1;
    s_line = s_slot = s_capture = 0;
}

int controls_active(void) {
    return s_open;
}

/* A key or button set to one slot leaves any other it was in */
static void set_key(int bit, int sc) {
    int b, i;

    for (b = 0; b < 16; b++) {
        for (i = 0; i < 2; i++) {
            if (pad_keys(b)[i] == sc) {
                pad_keys(b)[i] = 0;
            }
        }
    }
    pad_keys(bit)[s_slot] = sc;
}

static void set_button(int bit, int src) {
    int b;

    for (b = 0; b < 16; b++) {
        if (*pad_source(b) == src) {
            *pad_source(b) = PAD_NONE;
        }
    }
    *pad_source(bit) = src;
}

/* Waiting for a key (Esc cancels) or a pad's button (Back cancels): 1 when set */
static int capture(const SDL_Event *e, int bit) {
    if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        if (e->key.key == SDLK_ESCAPE) {
            s_capture = 0;
        } else if (s_slot < 2) {
            s_capture = 0;
            set_key(bit, e->key.scancode);
            return 1;
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        int b = e->gbutton.button;

        if (b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_GUIDE) {
            s_capture = 0;
        } else if (s_slot == 2) {
            s_capture = 0;
            set_button(bit, b);
            return 1;
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && s_slot == 2 && e->gaxis.value > 16384 &&
               (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        s_capture = 0;
        set_button(bit, e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PAD_LT : PAD_RT);
        return 1;
    }
    return 0;
}

/* The page's keys: arrows / D-pad to move, Enter / A to set, Del / Y to
   clear, Esc / B / Back to go back; 1 when a binding changed */
int controls_event(const SDL_Event *e) {
    int bit = s_line < 16 ? ORDER[s_line] : -1, up = 0, down = 0, left = 0, right = 0, ok = 0, clear = 0, back = 0;

    if (s_capture) {
        return capture(e, bit);
    }
    if (e->type == SDL_EVENT_KEY_DOWN) {
        SDL_Keycode k = e->key.key;

        up = k == SDLK_UP, down = k == SDLK_DOWN, left = k == SDLK_LEFT, right = k == SDLK_RIGHT;
        ok = !e->key.repeat && (k == SDLK_RETURN || k == SDLK_Z || k == SDLK_SPACE);
        clear = k == SDLK_DELETE || k == SDLK_BACKSPACE, back = k == SDLK_ESCAPE;
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        int b = e->gbutton.button;

        up = b == SDL_GAMEPAD_BUTTON_DPAD_UP, down = b == SDL_GAMEPAD_BUTTON_DPAD_DOWN;
        left = b == SDL_GAMEPAD_BUTTON_DPAD_LEFT, right = b == SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
        ok = b == SDL_GAMEPAD_BUTTON_SOUTH, clear = b == SDL_GAMEPAD_BUTTON_NORTH;
        back = b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_BACK;
    } else if (e->type == SDL_EVENT_MOUSE_WHEEL && e->wheel.y != 0) {
        float y = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;

        up = y > 0, down = y < 0;
    }
    s_line = (s_line + LINES + down - up) % LINES;
    s_slot = SDL_clamp(s_slot + right - left, 0, 2);
    if (back) {
        s_open = 0;
    } else if (ok && bit < 0) {
        pad_bind_reset();
        return 1;
    } else if (ok) {
        s_capture = 1;
    } else if (clear && bit >= 0) {
        if (s_slot < 2) {
            pad_keys(bit)[s_slot] = 0;
        } else {
            *pad_source(bit) = PAD_NONE;
        }
        return 1;
    }
    return 0;
}

/* In the menu's box (at x, y, w wide, rows lines of options): a title for the
   tab bar, then the lines, scrolled to keep the cursor in sight */
void controls_draw(SDL_Renderer *r, float x, float y, float w, int rows) {
    int first = SDL_clamp(s_line - rows / 2, 0, LINES - rows), fam = family(), i, slot;
    int blink = (SDL_GetTicks() / 267) & 1;

    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    SDL_RenderDebugText(r, x + 10, y + 8, "Controls");
    SDL_SetRenderDrawColor(r, 150, 170, 220, 255);
    SDL_RenderDebugText(r, x + COL[0] + 14, y + 8, "Keyboard");
    SDL_RenderDebugText(r, x + COL[2] + 6, y + 8, "Pad");
    SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
    SDL_RenderLine(r, x + 4, y + 21, x + w - 4, y + 21);

    for (i = 0; i < rows && first + i < LINES; i++) {
        int line = first + i, bit = line < 16 ? ORDER[line] : -1;
        float ly = y + 32 + i * 14;

        if (line == s_line) {
            SDL_FRect bar = {x + 4, ly - 3, w - 8, 14};

            SDL_SetRenderDrawColor(r, 60, 90, 160, 255);
            SDL_RenderFillRect(r, &bar);
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        if (bit < 0) {
            SDL_RenderDebugText(r, x + 26, ly, "Reset to defaults");
            continue;
        }
        button(r, FAM_PS5, PLACE[bit], x + 10, ly, 1, 1);
        SDL_RenderDebugText(r, x + 26, ly, LABELS[bit]);
        for (slot = 0; slot < 3; slot++) {
            float sx = x + COL[slot];
            int code = slot < 2 ? pad_keys(bit)[slot] : *pad_source(bit);

            if (line == s_line && slot == s_slot) {
                SDL_FRect box = {sx - 2, ly - 3, 48, 14};

                SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
                SDL_RenderRect(r, &box);
                if (s_capture) {
                    if (blink) {
                        SDL_RenderDebugText(r, sx + 8, ly, "...");
                    }
                    continue;
                }
            }
            if (slot < 2 ? code == 0 : code == PAD_NONE) {
                SDL_SetRenderDrawColor(r, 110, 110, 120, 255);
                SDL_RenderDebugText(r, sx + 16, ly, "-");
                SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
            } else if (slot < 2) {
                key(r, code, sx, ly, 5, 1);
            } else {
                button(r, fam, code, sx, ly, 5, 1);
            }
        }
    }
    SDL_SetRenderDrawColor(r, 150, 170, 220, 255); /* more above or below */
    if (first > 0) {
        SDL_RenderDebugText(r, x + w - 14, y + 30, "^");
    }
    if (first + rows < LINES) {
        SDL_RenderDebugText(r, x + w - 14, y + 30 + (rows - 1) * 14, "v");
    }
}
