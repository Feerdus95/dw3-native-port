/* libpad: a DualShock in port 1, in analog mode, from the keyboard or the
   first SDL gamepad. Each vsync fills the receive buffer the game gave
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

/* keyboard: arrows, Z/Space cross, X circle, A square, S triangle, Q/W L1/R1,
   E/R L2/R2, Enter start, Backspace select */
static const struct {
    SDL_Scancode key;
    int bit;
} KEYS[] = {
    {SDL_SCANCODE_UP, B_UP}, {SDL_SCANCODE_DOWN, B_DOWN}, {SDL_SCANCODE_LEFT, B_LEFT}, {SDL_SCANCODE_RIGHT, B_RIGHT},
    {SDL_SCANCODE_Z, B_CROSS}, {SDL_SCANCODE_SPACE, B_CROSS}, {SDL_SCANCODE_X, B_CIRCLE},
    {SDL_SCANCODE_A, B_SQUARE}, {SDL_SCANCODE_S, B_TRIANGLE},
    {SDL_SCANCODE_Q, B_L1}, {SDL_SCANCODE_W, B_R1}, {SDL_SCANCODE_E, B_L2}, {SDL_SCANCODE_R, B_R2},
    {SDL_SCANCODE_RETURN, B_START}, {SDL_SCANCODE_BACKSPACE, B_SELECT},
};

static const struct {
    SDL_GamepadButton button;
    int bit;
} BUTTONS[] = {
    {SDL_GAMEPAD_BUTTON_SOUTH, B_CROSS}, {SDL_GAMEPAD_BUTTON_EAST, B_CIRCLE},
    {SDL_GAMEPAD_BUTTON_WEST, B_SQUARE}, {SDL_GAMEPAD_BUTTON_NORTH, B_TRIANGLE},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, B_L1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, B_R1},
    {SDL_GAMEPAD_BUTTON_BACK, B_SELECT}, {SDL_GAMEPAD_BUTTON_START, B_START},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, B_L3}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, B_R3},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, B_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, B_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, B_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, B_RIGHT},
};

static uint8_t axis(SDL_GamepadAxis a) {
    return (uint8_t)((SDL_GetGamepadAxis(s_gamepad, a) + 32768) >> 8);
}

/* DW3_PRESS="300:cross,500:start": holds a button for 6 vsyncs at a vsync
   count, to drive the game in tests without anyone at the keyboard */
static uint16_t scripted(int frame) {
    static const char *names[16] = {"select", "l3", "r3", "start", "up", "right", "down", "left",
                                    "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square"};
    const char *env = SDL_getenv("DW3_PRESS"), *p;
    uint16_t held = 0;
    int at, i;
    char name[16];

    for (p = env; p != NULL && SDL_sscanf(p, "%d:%15[a-z0-9]", &at, name) == 2; p = SDL_strchr(p, ',') ? SDL_strchr(p, ',') + 1 : NULL) {
        if (frame >= at && frame < at + 6) {
            for (i = 0; i < 16; i++) {
                if (SDL_strcmp(name, names[i]) == 0) {
                    held |= 1 << i;
                }
            }
        }
    }
    return held;
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
    if (s_gamepad == NULL) {
        SDL_JoystickID *ids = SDL_GetGamepads(NULL);

        if (ids != NULL && ids[0] != 0) {
            s_gamepad = SDL_OpenGamepad(ids[0]);
        }
        SDL_free(ids);
    }
    for (i = 0; i < SDL_arraysize(KEYS); i++) {
        if (keys[KEYS[i].key]) {
            held |= 1 << KEYS[i].bit;
        }
    }
    b[4] = b[5] = b[6] = b[7] = 0x80; /* right x, right y, left x, left y */
    if (s_gamepad != NULL) {
        for (i = 0; i < SDL_arraysize(BUTTONS); i++) {
            if (SDL_GetGamepadButton(s_gamepad, BUTTONS[i].button)) {
                held |= 1 << BUTTONS[i].bit;
            }
        }
        if (SDL_GetGamepadAxis(s_gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) held |= 1 << B_L2;
        if (SDL_GetGamepadAxis(s_gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) held |= 1 << B_R2;
        b[4] = axis(SDL_GAMEPAD_AXIS_RIGHTX);
        b[5] = axis(SDL_GAMEPAD_AXIS_RIGHTY);
        b[6] = axis(SDL_GAMEPAD_AXIS_LEFTX);
        b[7] = axis(SDL_GAMEPAD_AXIS_LEFTY);
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

/* No vibration yet: no actuators */
int PadInfoAct(int port, int act, int term) {
    return 0;
}

void PadSetAct(int port, uint8_t *data, int len) {}

int PadSetActAlign(int port, uint8_t *data) {
    return 1;
}

int PadSetMainMode(int port, int offs, int lock) {
    return 1;
}
