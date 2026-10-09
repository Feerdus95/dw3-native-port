/* libetc: the vsync. There are no interrupts natively: the vsync "happens"
   when the game waits for it (VSync, native_wait_vblank), which keeps real
   time, runs the callback the interrupt would have run and shows the frame. */
#include <SDL3/SDL.h>
#include <stdlib.h>
#include "platform.h"

extern int NTSC_MODE; /* the game's, src/main/data/eu_settings.c */
void pad_update(void);

SAVED static void (*s_vsync_cb)(void);
static uint64_t s_next_ns;  /* when the next vsync is due */
SAVED static int s_vsyncs;        /* since boot */
static int s_in_cb;

static uint64_t period_ns(void) {
    return NTSC_MODE ? 1000000000ull / 60 : 1000000000ull / 50;
}

/* One vsync: wait for its time, run the callback, show the frame */
static void vblank(void) {
    uint64_t now = SDL_GetTicksNS();

    if (s_next_ns == 0 || now > s_next_ns + 100000000ull) {
        s_next_ns = now; /* first one, or we fell behind (debugger): don't catch up */
    } else if (now < s_next_ns) {
        SDL_DelayPrecise(s_next_ns - now);
    }
    s_next_ns += period_ns();
    s_vsyncs++;
    if (!pump_events()) {
        exit(0);
    }
    present_frame();
    pad_update();
    if (s_vsync_cb != NULL && !s_in_cb) {
        s_in_cb = 1;
        s_vsync_cb();
        s_in_cb = 0;
    }
    movie_vblank(NTSC_MODE ? 60 : 50);
    audio_vblank(NTSC_MODE ? 60 : 50);
}

/* The frame's end (display.c's flip): the one place the game waits for a
   vsync once it runs, with nothing on the stack but main and drawFrame */
void native_wait_vblank(void) {
    vblank();
    state_frame();
}

int ResetCallback(void) {
    s_vsync_cb = NULL;
    return 0;
}

int VSyncCallback(void (*f)(void)) {
    s_vsync_cb = f;
    return 0;
}

/* 0: wait for the next vsync; n > 1: for n since the last call; 1: don't
   wait; < 0: the vsyncs since boot. Returns the vsyncs since boot otherwise
   (the PSX returned the hblanks since the last one; nothing here reads it). */
int VSync(int mode) {
    SAVED static int s_last;
    int n;

    if (mode < 0) {
        return s_vsyncs;
    }
    if (mode == 0) {
        vblank();
    } else if (mode > 1) {
        for (n = s_last + mode - s_vsyncs; n > 0; n--) {
            vblank();
        }
    }
    s_last = s_vsyncs;
    return s_vsyncs;
}

/* The game's divisions by a variable (build.py's mips_divisions): what the
   R3000 gives where x86 would trap. By zero, the quotient is -1 (1 for a
   negative signed dividend, all ones unsigned) and the remainder the dividend;
   INT_MIN / -1 is INT_MIN, remainder 0. On the PSX the compiler's check would
   have stopped at a break, so each one is logged: the port went somewhere the
   PSX didn't. */
static void div_trap(const char *op, int32_t n, const void *from) {
    static int logged;

    if (logged < 20) {
        logged++;
        native_log("division by zero (%s %d) from %p", op, n, from);
    }
}

int32_t native_sdiv(int32_t n, int32_t d) {
    if (d == 0) {
        div_trap("sdiv", n, __builtin_return_address(0));
        return n < 0 ? 1 : -1;
    }
    return d == -1 ? (int32_t)(0u - (uint32_t)n) : n / d;
}

int32_t native_srem(int32_t n, int32_t d) {
    if (d == 0) {
        div_trap("srem", n, __builtin_return_address(0));
        return n;
    }
    return d == -1 ? 0 : n % d;
}

uint32_t native_udiv(uint32_t n, uint32_t d) {
    if (d == 0) {
        div_trap("udiv", (int32_t)n, __builtin_return_address(0));
        return 0xFFFFFFFFu;
    }
    return n / d;
}

uint32_t native_urem(uint32_t n, uint32_t d) {
    if (d == 0) {
        div_trap("urem", (int32_t)n, __builtin_return_address(0));
        return n;
    }
    return n % d;
}
