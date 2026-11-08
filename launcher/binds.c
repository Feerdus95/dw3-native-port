/* The controls: each PlayStation button's two keys and gamepad button, as the in-game menu's Controls page has them
   (platform/pad.c, platform/controls.c): the same lines in native.ini, the same defaults, the same rules. */
#include "launcher.h"

enum { PAD_NONE = -1, PAD_LT = 100, PAD_RT = 101 };

static const char *const NAMES[BIND_COUNT] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                              "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
static const int DEFAULT_KEYS[BIND_COUNT][2] = {
    {SDL_SCANCODE_BACKSPACE}, {0}, {0}, {SDL_SCANCODE_RETURN},
    {SDL_SCANCODE_UP}, {SDL_SCANCODE_RIGHT}, {SDL_SCANCODE_DOWN}, {SDL_SCANCODE_LEFT},
    {SDL_SCANCODE_E}, {SDL_SCANCODE_R}, {SDL_SCANCODE_Q}, {SDL_SCANCODE_W},
    {SDL_SCANCODE_S}, {SDL_SCANCODE_X}, {SDL_SCANCODE_Z, SDL_SCANCODE_SPACE}, {SDL_SCANCODE_A},
};
static const int DEFAULT_PAD[BIND_COUNT] = {
    PAD_NONE, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    PAD_LT, PAD_RT, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST,
};

static int s_keys[BIND_COUNT][2], s_pad[BIND_COUNT], s_slot, s_capture = -1;
static bool s_dirty[BIND_COUNT];

static int source_from_name(const char *name)
{
    return !SDL_strcasecmp(name, "lefttrigger") ? PAD_LT : !SDL_strcasecmp(name, "righttrigger") ? PAD_RT : SDL_GetGamepadButtonFromString(name);
}

static const char *source_name(int src)
{
    return src == PAD_LT ? "lefttrigger" : src == PAD_RT ? "righttrigger" : SDL_GetGamepadStringForButton((SDL_GamepadButton)src);
}

static void parse_keys(int bit, const char *value)
{
    char name[64];
    s_keys[bit][0] = s_keys[bit][1] = 0;
    for (int n = 0; n < 2 && SDL_sscanf(value, " %63[^,]", name) == 1; n++) {
        size_t len = SDL_strlen(name);
        while (len > 0 && name[len - 1] == ' ') {
            name[--len] = 0;
        }
        s_keys[bit][n] = SDL_GetScancodeFromName(name);
        value = SDL_strchr(value, ',');
        if (!value) {
            break;
        }
        value++;
    }
}

static void keys_line(int bit, char *out, int size)
{
    const int a = s_keys[bit][0], b = s_keys[bit][1];
    SDL_snprintf(out, size, "%s%s%s", a ? SDL_GetScancodeName(a) : "none", b ? ", " : "", b ? SDL_GetScancodeName(b) : "");
}

void binds_load(void)
{
    char key[32], value[128];
    SDL_memcpy(s_keys, DEFAULT_KEYS, sizeof s_keys);
    SDL_memcpy(s_pad, DEFAULT_PAD, sizeof s_pad);
    SDL_zeroa(s_dirty);
    for (int bit = 0; bit < BIND_COUNT; bit++) {
        SDL_snprintf(key, sizeof key, "keys_%s", NAMES[bit]);
        if (ini_get("native.ini", key, value, sizeof value)) {
            parse_keys(bit, value);
        }
        SDL_snprintf(key, sizeof key, "pad_%s", NAMES[bit]);
        if (ini_get("native.ini", key, value, sizeof value)) {
            s_pad[bit] = source_from_name(value);
        }
    }
}

void binds_reset(void)
{
    for (int bit = 0; bit < BIND_COUNT; bit++) {
        s_dirty[bit] |= SDL_memcmp(s_keys[bit], DEFAULT_KEYS[bit], sizeof s_keys[bit]) != 0 || s_pad[bit] != DEFAULT_PAD[bit];
    }
    SDL_memcpy(s_keys, DEFAULT_KEYS, sizeof s_keys);
    SDL_memcpy(s_pad, DEFAULT_PAD, sizeof s_pad);
}

/* Cancel on B: Triangle (the game's cancel, bit 12) and Circle (13) trade keys and pad button; again, it undoes */
void binds_swap_cancel(void)
{
    for (int i = 0; i < 2; i++) {
        const int k = s_keys[12][i];
        s_keys[12][i] = s_keys[13][i];
        s_keys[13][i] = k;
    }
    const int p = s_pad[12];
    s_pad[12] = s_pad[13];
    s_pad[13] = p;
    s_dirty[12] = s_dirty[13] = true;
}

bool binds_swapped(void)
{
    return !SDL_memcmp(s_keys[12], DEFAULT_KEYS[13], sizeof s_keys[12]) && !SDL_memcmp(s_keys[13], DEFAULT_KEYS[12], sizeof s_keys[13]) &&
           s_pad[12] == DEFAULT_PAD[13] && s_pad[13] == DEFAULT_PAD[12];
}

/* a button back at its default loses its lines, as the game writes only what differs */
bool binds_save(void)
{
    static char keys[BIND_COUNT * 2][32], values[BIND_COUNT * 2][128];
    IniEdit edits[BIND_COUNT * 2];
    int n = 0;
    for (int bit = 0; bit < BIND_COUNT; bit++) {
        if (s_dirty[bit]) {
            const bool keys_default = !SDL_memcmp(s_keys[bit], DEFAULT_KEYS[bit], sizeof s_keys[bit]);
            SDL_snprintf(keys[n], sizeof keys[n], "keys_%s", NAMES[bit]);
            keys_line(bit, values[n], sizeof values[n]);
            edits[n] = (IniEdit){keys[n], keys_default ? NULL : values[n], false};
            n++;
            SDL_snprintf(keys[n], sizeof keys[n], "pad_%s", NAMES[bit]);
            SDL_strlcpy(values[n], s_pad[bit] != PAD_NONE ? source_name(s_pad[bit]) : "none", sizeof values[n]);
            edits[n] = (IniEdit){keys[n], s_pad[bit] == DEFAULT_PAD[bit] ? NULL : values[n], false};
            n++;
        }
    }
    if (n == 0) {
        return true;
    }
    if (!ini_write("native.ini", edits, n)) {
        return false;
    }
    SDL_zeroa(s_dirty);
    return true;
}

/* what the gamepad's button is called on screen: by place, as the icons of the game's menu are */
static const char *pad_label(int src)
{
    switch (src) {
    case PAD_LT: return "LT";
    case PAD_RT: return "RT";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "A";
    case SDL_GAMEPAD_BUTTON_EAST: return "B";
    case SDL_GAMEPAD_BUTTON_WEST: return "X";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Y";
    case SDL_GAMEPAD_BUTTON_START: return "Start";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "LS";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "RS";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return "D-pad Up";
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return "D-pad Down";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "D-pad Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "D-pad Right";
    default: return source_name(src);
    }
}

const char *bind_text(int bit, int slot)
{
    if (slot < 2) {
        return s_keys[bit][slot] ? SDL_GetScancodeName(s_keys[bit][slot]) : "-";
    }
    return s_pad[bit] != PAD_NONE ? pad_label(s_pad[bit]) : "-";
}

const char *bind_label(int bit)
{
    static const char *const FIXED[BIND_COUNT] = {"Select", "L3", "R3", "Start", NULL, NULL, NULL, NULL, "L2", "R2", "L1", "R1"};
    return FIXED[bit] ? FIXED[bit] : L->s[S_B_UP + bit - (bit < 8 ? 4 : 8)];
}

int bind_slot(void) { return s_slot; }

void bind_slot_set(int slot) { s_slot = SDL_clamp(slot, 0, 2); }

int bind_capturing(void) { return s_capture; }

void bind_capture(int bit)
{
    s_capture = bit;
    g_app.note = L->s[s_slot < 2 ? S_PRESS_KEY : S_PRESS_PAD];
}

void bind_clear(int bit)
{
    s_dirty[bit] = true;
    if (s_slot < 2) {
        s_keys[bit][s_slot] = 0;
    } else {
        s_pad[bit] = PAD_NONE;
    }
}

/* a key or button set to one slot leaves any other it was in */
static void set_key(int bit, int sc)
{
    for (int b = 0; b < BIND_COUNT; b++) {
        for (int i = 0; i < 2; i++) {
            if (s_keys[b][i] == sc) {
                s_keys[b][i] = 0;
                s_dirty[b] = true;
            }
        }
    }
    s_keys[bit][s_slot] = sc;
    s_dirty[bit] = true;
}

static void set_button(int bit, int src)
{
    for (int b = 0; b < BIND_COUNT; b++) {
        if (s_pad[b] == src) {
            s_pad[b] = PAD_NONE;
            s_dirty[b] = true;
        }
    }
    s_pad[bit] = src;
    s_dirty[bit] = true;
}

static void capture_end(void)
{
    s_capture = -1;
    g_app.note = NULL;
}

/* waiting for a key (Esc cancels) or a pad's button (Back cancels; a click cancels both) */
void bind_event(const SDL_Event *e)
{
    const int bit = s_capture;
    if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        capture_end();
    } else if (e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat) {
        if (e->key.key == SDLK_ESCAPE) {
            capture_end();
        } else if (s_slot < 2) {
            set_key(bit, e->key.scancode);
            capture_end();
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        const int b = e->gbutton.button;
        if (b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_GUIDE) {
            capture_end();
        } else if (s_slot == 2) {
            set_button(bit, b);
            capture_end();
        }
    } else if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION && s_slot == 2 && e->gaxis.value > 16384 &&
               (e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)) {
        set_button(bit, e->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? PAD_LT : PAD_RT);
        capture_end();
    }
}

/* the lines the game reads, and its rule that a key has one place */
int binds_selftest(void)
{
    enum { B_CROSS = 14, B_CIRCLE = 13 };
    char line[128];
    int bad = 0;
    SDL_memcpy(s_keys, DEFAULT_KEYS, sizeof s_keys);
    SDL_memcpy(s_pad, DEFAULT_PAD, sizeof s_pad);
    keys_line(B_CROSS, line, sizeof line);
    bad += SDL_strcmp(line, "Z, Space") != 0;
    parse_keys(B_CIRCLE, "Left Shift, F");
    keys_line(B_CIRCLE, line, sizeof line);
    bad += SDL_strcmp(line, "Left Shift, F") != 0;
    parse_keys(B_CIRCLE, "none, F");
    bad += s_keys[B_CIRCLE][0] != 0 || s_keys[B_CIRCLE][1] != SDL_SCANCODE_F;
    s_slot = 0;
    set_key(B_CIRCLE, SDL_SCANCODE_Z);
    bad += s_keys[B_CROSS][0] != 0 || s_keys[B_CROSS][1] != SDL_SCANCODE_SPACE || s_keys[B_CIRCLE][0] != SDL_SCANCODE_Z;
    set_button(B_CIRCLE, SDL_GAMEPAD_BUTTON_SOUTH);
    bad += s_pad[B_CROSS] != PAD_NONE || SDL_strcmp(bind_text(B_CIRCLE, 2) , "A") != 0 || source_from_name(source_name(PAD_LT)) != PAD_LT;
    binds_load();
    return bad;
}
