/* libpad: a DualShock in port 1, in analog mode, from the keyboard or the
   first SDL gamepad (its motors too). Each vsync fills the receive buffer the game gave
   PadInitDirect, as libpad did in its interrupt. */
#include <SDL3/SDL.h>
#include "platform.h"

/* Raw button bits; the buffer has them clear while pressed */
enum {
    B_SELECT = 0, B_L3, B_R3, B_START, B_UP, B_RIGHT, B_DOWN, B_LEFT,
    B_L2, B_R2, B_L1, B_R1, B_TRIANGLE, B_CIRCLE, B_CROSS, B_SQUARE
};

static uint8_t *s_buf[2];
static SDL_Gamepad *s_gamepad;

/* The buttons' names in their bits' order: native.ini's keys_<name> and
   pad_<name>, DW3_PRESS's */
static const char *NAMES[16] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};

/* Each button's two keys and gamepad button, the options menu's Controls
   (controls.c). By default: arrows, Z/Space cross, X circle, A square, S
   triangle, Q/W L1/R1, E/R L2/R2, Enter start, Backspace select; on the pad
   its own places, the triggers L2/R2, nothing Select (Back opens the menu) */
static const int DEFAULT_KEYS[16][2] = {
    {SDL_SCANCODE_BACKSPACE}, {0}, {0}, {SDL_SCANCODE_RETURN},
    {SDL_SCANCODE_UP}, {SDL_SCANCODE_RIGHT}, {SDL_SCANCODE_DOWN}, {SDL_SCANCODE_LEFT},
    {SDL_SCANCODE_E}, {SDL_SCANCODE_R}, {SDL_SCANCODE_Q}, {SDL_SCANCODE_W},
    {SDL_SCANCODE_S}, {SDL_SCANCODE_X}, {SDL_SCANCODE_Z, SDL_SCANCODE_SPACE}, {SDL_SCANCODE_A},
};
static const int DEFAULT_PAD[16] = {
    PAD_NONE, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    PAD_LT, PAD_RT, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_WEST,
};
static int s_keys[16][2], s_pad[16], s_bound;

static void bound(void) {
    if (!s_bound) {
        pad_bind_reset();
    }
}

void pad_bind_reset(void) {
    SDL_memcpy(s_keys, DEFAULT_KEYS, sizeof s_keys);
    SDL_memcpy(s_pad, DEFAULT_PAD, sizeof s_pad);
    s_bound = 1;
}

const char *pad_button_name(int bit) {
    return NAMES[bit];
}

int *pad_keys(int bit) {
    bound();
    return s_keys[bit];
}

int *pad_source(int bit) {
    bound();
    return &s_pad[bit];
}

/* SDL's name of a gamepad button, or of a trigger */
const char *pad_source_name(int src) {
    return src == PAD_LT ? "lefttrigger" : src == PAD_RT ? "righttrigger" : src >= 0 ? SDL_GetGamepadStringForButton(src) : NULL;
}

static int source_from_name(const char *name) {
    return SDL_strcasecmp(name, "lefttrigger") == 0 ? PAD_LT : SDL_strcasecmp(name, "righttrigger") == 0 ? PAD_RT
                                                                                                         : SDL_GetGamepadButtonFromString(name);
}

/* keys_<button> = <key>[, <key>] (SDL's names), pad_<button> = <SDL's name of
   the button> or none; 1 when key was one of these */
int pad_bind_ini(const char *key, const char *value) {
    const char *button = SDL_strncasecmp(key, "keys_", 5) == 0 ? key + 5 : SDL_strncasecmp(key, "pad_", 4) == 0 ? key + 4 : NULL;
    char name[64];
    int bit, n;

    bound();
    for (bit = 0; button != NULL && bit < 16; bit++) {
        if (SDL_strcasecmp(button, NAMES[bit]) != 0) {
            continue;
        }
        if (button == key + 4) {
            s_pad[bit] = source_from_name(value);
            return 1;
        }
        s_keys[bit][0] = s_keys[bit][1] = 0;
        for (n = 0; n < 2 && SDL_sscanf(value, " %63[^,]", name) == 1; n++) {
            size_t len = SDL_strlen(name);

            while (len > 0 && name[len - 1] == ' ') {
                name[--len] = 0;
            }
            s_keys[bit][n] = SDL_GetScancodeFromName(name);
            if ((value = SDL_strchr(value, ',')) == NULL) {
                break;
            }
            value++;
        }
        return 1;
    }
    return 0;
}

/* Line i of native.ini's bindings (0-15 the keys, 16-31 the pad's), as
   key and value; 1 when it isn't the default */
int pad_bind_line(int i, char *key, char *value, size_t size) {
    int bit = i % 16;

    bound();
    if (i < 16) {
        SDL_snprintf(key, 32, "keys_%s", NAMES[bit]);
        SDL_snprintf(value, size, "%s%s%s", s_keys[bit][0] ? SDL_GetScancodeName(s_keys[bit][0]) : "none",
                     s_keys[bit][1] ? ", " : "", s_keys[bit][1] ? SDL_GetScancodeName(s_keys[bit][1]) : "");
        return SDL_memcmp(s_keys[bit], DEFAULT_KEYS[bit], sizeof s_keys[bit]) != 0;
    }
    SDL_snprintf(key, 32, "pad_%s", NAMES[bit]);
    SDL_snprintf(value, size, "%s", s_pad[bit] != PAD_NONE ? pad_source_name(s_pad[bit]) : "none");
    return s_pad[bit] != DEFAULT_PAD[bit];
}

SDL_Gamepad *pad_gamepad(void) {
    return s_gamepad;
}

int VSync(int mode);

int g_deadzone = 3277, g_rumble = 1; /* native.ini */

/* A stick's two axes to DualShock bytes with a round deadzone: inside it the
   stick is centred, past it the push is scaled back to the full range along
   its own direction (a square one per axis would snap diagonals) */
static void stick(SDL_GamepadAxis ax, SDL_GamepadAxis ay, uint8_t *out) {
    float x = SDL_GetGamepadAxis(s_gamepad, ax), y = SDL_GetGamepadAxis(s_gamepad, ay);
    float mag = SDL_sqrtf(x * x + y * y), k;

    out[0] = out[1] = 0x80;
    if (mag <= g_deadzone || g_deadzone >= 32767) {
        return;
    }
    /* a full diagonal is past 32767: capped, as the stick's round gate */
    k = ((SDL_min(mag, 32767.0f) - g_deadzone) / (32767.0f - g_deadzone)) * 127.0f / mag;
    out[0] = (uint8_t)SDL_clamp(128.0f + x * k, 0.0f, 255.0f);
    out[1] = (uint8_t)SDL_clamp(128.0f + y * k, 0.0f, 255.0f);
}

/* The motors: the game's two bytes go where PadSetActAlign put them; the
   small one only spins or not, the large one has a strength */
static SAVED uint8_t s_align[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static SAVED uint8_t s_small, s_large;

/* Every vsync, while a motor runs, for 100 ms: a crash or a pulled pad
   doesn't leave it spinning */
static void rumble(void) {
    static uint8_t last_small, last_large;

    if (s_gamepad == NULL || !g_rumble) {
        return;
    }
    if (s_small || s_large || last_small || last_large) {
        if (!SDL_RumbleGamepad(s_gamepad, (Uint16)(s_large * 257), s_small ? 0xFFFF : 0, s_small || s_large ? 100 : 0)) {
            LOGC(LOG_PAD, "pad: no rumble: %s", SDL_GetError());
        }
    }
    if (s_small != last_small || s_large != last_large) {
        LOGC(LOG_PAD, "pad: motors %02X %02X (vsync %d)", s_small, s_large, VSync(-1));
    }
    last_small = s_small;
    last_large = s_large;
}

/* The first gamepad, again when it's plugged back or another one is */
static void open_gamepad(void) {
    SDL_JoystickID *ids;

    if (s_gamepad != NULL && !SDL_GamepadConnected(s_gamepad)) {
        LOGC(LOG_PAD, "pad: %s unplugged", SDL_GetGamepadName(s_gamepad));
        SDL_CloseGamepad(s_gamepad);
        s_gamepad = NULL;
    }
    if (s_gamepad != NULL || (ids = SDL_GetGamepads(NULL)) == NULL) {
        return;
    }
    if (ids[0] != 0 && (s_gamepad = SDL_OpenGamepad(ids[0])) != NULL) {
        LOGC(LOG_PAD, "pad: %s%s", SDL_GetGamepadName(s_gamepad),
            SDL_GetBooleanProperty(SDL_GetGamepadProperties(s_gamepad), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false) ? "" : " (no rumble)");
    }
    SDL_free(ids);
}

/* DW3_PRESS="300:cross,500:start,600:f5": holds a button for 6 vsyncs at a vsync
   count, to drive the game in tests without anyone at the keyboard */
static uint16_t scripted(int frame) {
    const char *env = SDL_getenv("DW3_PRESS"), *p;
    uint16_t held = 0;
    int at, i;
    char name[16];

    for (p = env; p != NULL && SDL_sscanf(p, "%d:%15[a-z0-9]", &at, name) == 2; p = SDL_strchr(p, ',') ? SDL_strchr(p, ',') + 1 : NULL) {
        if (frame == at && name[0] == 'f') {
            state_key(SDL_GetKeyFromName(name)); /* f1, f2, f5, f9: the debug keys */
        }
        if (frame == at && SDL_strcmp(name, "esc") == 0) {
            menu_toggle(); /* the options menu */
        }
        if (frame >= at && frame < at + 6) {
            for (i = 0; i < 16; i++) {
                if (SDL_strcmp(name, NAMES[i]) == 0) {
                    held |= 1 << i;
                }
            }
        }
    }
    return held;
}

/* DW3_RECORD=<file> writes every vsync's input from boot (buttons, 2 bytes,
   then the sticks, 4); DW3_REPLAY=<file> plays one back instead of the keys,
   and hands them back at its end. The game runs the same from boot: its
   random numbers and loads go by frames (the card image has to be the same) */
static void record_replay(uint16_t *held, uint8_t *sticks) {
    static FILE *rec, *rep;
    static int opened;
    uint8_t f[6];

    if (!opened) {
        opened = 1;
        if (SDL_getenv("DW3_REPLAY") != NULL && (rep = fopen(SDL_getenv("DW3_REPLAY"), "rb")) == NULL) {
            LOG("replay: can't open %s", SDL_getenv("DW3_REPLAY"));
        }
        if (SDL_getenv("DW3_RECORD") != NULL && (rec = fopen(SDL_getenv("DW3_RECORD"), "wb")) == NULL) {
            LOG("record: can't create %s", SDL_getenv("DW3_RECORD"));
        }
    }
    if (rep != NULL) {
        if (fread(f, sizeof f, 1, rep) == 1) {
            *held = (uint16_t)(f[0] | f[1] << 8);
            SDL_memcpy(sticks, f + 2, 4);
        } else {
            fclose(rep);
            rep = NULL;
            LOG("replay: ended, the keys are back");
        }
    }
    if (rec != NULL) {
        f[0] = (uint8_t)*held;
        f[1] = (uint8_t)(*held >> 8);
        SDL_memcpy(f + 2, sticks, 4);
        fwrite(f, sizeof f, 1, rec);
        fflush(rec); /* a crash keeps what led to it */
    }
}

/* Called every vsync (etc.c) */
void pad_update(void) {
    const bool *keys = SDL_GetKeyboardState(NULL);
    static int frame;
    uint16_t held = scripted(frame++);
    uint8_t *b = s_buf[0];
    size_t i;

    if (b == NULL) {
        return;
    }
    open_gamepad();
    rumble();
    bound();
    for (i = 0; i < 16; i++) {
        int src = s_pad[i];

        if ((s_keys[i][0] && keys[s_keys[i][0]]) || (s_keys[i][1] && keys[s_keys[i][1]])) {
            held |= 1 << i;
        }
        if (s_gamepad != NULL && src != PAD_NONE &&
            (src == PAD_LT || src == PAD_RT ? SDL_GetGamepadAxis(s_gamepad, src == PAD_LT ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > g_deadzone
                                            : SDL_GetGamepadButton(s_gamepad, src))) {
            held |= 1 << i;
        }
    }
    b[4] = b[5] = b[6] = b[7] = 0x80; /* right x, right y, left x, left y */
    if (s_gamepad != NULL) {
        stick(SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, b + 4);
        stick(SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, b + 6);
    }
    record_replay(&held, b + 4);
    {
        static uint16_t last;

        if (held != last) {
            LOGC(LOG_PAD, "pad: %04X", held);
            last = held;
        }
    }
    b[0] = 0;    /* answered */
    b[1] = 0x73; /* analog, 3 halfwords */
    b[2] = (uint8_t)~held;
    b[3] = (uint8_t)(~held >> 8);
    if (s_buf[1] != NULL) {
        s_buf[1][0] = 0xFF; /* nothing in port 2 */
    }
}

void PadInitDirect(uint8_t *a, uint8_t *b) {
    s_buf[0] = a;
    s_buf[1] = b;
    /* SDL's own HIDAPI drivers, Xbox pads included, rather
       than RawInput, whose XInput pairing for the motors can miss */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_RAWINPUT, "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1");
    SDL_InitSubSystem(SDL_INIT_GAMEPAD);
}

void PadInitMtap(uint8_t *a, uint8_t *b) {
    PadInitDirect(a, b);
}

void PadStartCom(void) {}
void PadStopCom(void) {}

/* 1: the buffers were refreshed this vsync */
int PadChkVsync(void) {
    return 1;
}

int PadGetState(int port) {
    return port == 0 ? 6 /* PadStateStable */ : 0 /* PadStateDiscon */;
}

/* InfoModeCurExID: 7 analog; InfoModeCurExOffs: its index; else 0 */
int PadInfoMode(int port, int term, int offs) {
    if (port != 0) {
        return 0;
    }
    switch (term) {
    case 1: return 7; /* InfoModeCurID */
    case 2: return 7; /* InfoModeCurExID */
    case 3: return 1; /* InfoModeCurExOffs */
    default: return 0;
    }
}

/* A DualShock's two actuators, as its command 46h tells them: 0 the small
   motor (on/off), 1 the large one (a byte of strength) */
int PadInfoAct(int port, int act, int term) {
    static const uint8_t INFO[2][4] = {{1, 2, 0, 10}, {1, 1, 1, 20}}; /* func, sub, size, current */

    if (port != 0) {
        return 0;
    }
    if (act == -1) {
        return 2;
    }
    return act >= 0 && act < 2 && term >= 1 && term <= 4 ? INFO[act][term - 1] : 0;
}

/* The game's motor bytes, routed by the alignment: 0 small, 1 large */
void PadSetAct(int port, uint8_t *data, int len) {
    int i;

    for (i = 0; i < len && i < 6 && port == 0; i++) {
        if (s_align[i] == 0) {
            s_small = data[i];
        } else if (s_align[i] == 1) {
            s_large = data[i];
        }
    }
}

int PadSetActAlign(int port, uint8_t *data) {
    if (port == 0) {
        SDL_memcpy(s_align, data, 6);
    }
    return 1;
}

int PadSetMainMode(int port, int offs, int lock) {
    return 1;
}
