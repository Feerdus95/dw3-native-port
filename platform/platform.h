/* The native platform layer: what replaces the PSX hardware and Sony's PsyQ
   libraries under the decompiled game. Its own types, not Sony's headers;
   built with the host's C library and SDL3. */
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stdio.h>

/* --- PsyQ types the platform needs, with the same layout --- */
typedef struct {
    int16_t x, y, w, h;
} RECT;

typedef struct {
    RECT disp;
    RECT screen;
    uint8_t isinter, isrgb24, pad0, pad1;
} DISPENV;

typedef struct {
    uint32_t pmode;
    int16_t px, py;
    uint16_t pw, ph;
    uint32_t *pixel;
    int16_t cx, cy;
    uint16_t cw, ch;
    uint32_t *clut;
} GsIMAGE;

/* --- platform state --- */
#define VRAM_W 1024
#define VRAM_H 512
extern uint16_t *g_vram; /* malloc'd: nothing in the OTs points at it */
extern DISPENV g_disp; /* the last PutDispEnv */
extern int g_disp_on;  /* SetDispMask */

const char *data_dir(void);
void native_log(const char *fmt, ...);
void present_frame(void); /* platform/video.c */
int pump_events(void);    /* 0 once the user closed the window */
struct SDL_Window *video_window(void);

/* The options menu (platform/menu.c) */
union SDL_Event;
struct SDL_Renderer;
int menu_active(void);   /* the game waits while it is open */
void menu_toggle(void);
int menu_event(const union SDL_Event *e); /* 1: the menu's */
void menu_draw(struct SDL_Renderer *r);
void set_fps(int sixty); /* the game's 60 Hz mode on or off */

/* --- sound: platform/spu.c, audio.c --- */
#define SPU_VOICES 24
#define AUDIO_RATE 44100
void spu_write(uint32_t addr, const void *src, uint32_t size);
void spu_key_on(int voice, uint32_t addr, uint16_t adsr1, uint16_t adsr2);
void spu_key_off(int voice);
void spu_voice_stop(int voice);
void spu_set_pitch(int voice, uint16_t pitch);
void spu_set_volume(int voice, int16_t l, int16_t r);
int spu_voice_state(int voice, int *env);
void spu_set_main_volume(int16_t l, int16_t r);
void spu_set_cd_volume(int16_t l, int16_t r);
void spu_set_reverb(const uint16_t regs[32], uint32_t base); /* a libspu preset, its work area in bytes */
void spu_set_reverb_on(int on);
void spu_set_reverb_depth(int16_t l, int16_t r); /* 0-0x7FFF */
void spu_set_voice_reverb(int n, int on);
void spu_cd_push(const int16_t *lr, int frames);
int spu_cd_queued(void);
void spu_cd_clear(void);
void spu_render(int16_t *out, int frames);
void audio_vblank(int rate); /* mixes and queues one vsync of sound */
void movie_vblank(int rate); /* platform/movie.c: the CD streams on */

/* Logs once per call site key: for stubs and first-time events */
void log_once(const char *what);
#define LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))

/* Per subsystem, off unless DW3_LOG (or native.ini's log) names it:
   DW3_LOG=cd,snd,pad,mc,ovl,gpu or all */
enum { LOG_GPU = 1, LOG_CD = 2, LOG_SND = 4, LOG_PAD = 8, LOG_MC = 16, LOG_OVL = 32 };
extern int g_log;
#define LOGC(cat, ...) do { if (g_log & (cat)) LOG(__VA_ARGS__); } while (0)

/* native.ini switches (main.c) */
extern int g_debug;         /* debug = 1: the debug keys (state.c) */
extern int g_dither_on;     /* dither = 0: no GPU dithering (gpu_draw.c) */
extern int g_smooth_movies; /* smooth_movies = 0: movies scaled by nearest pixel (video.c) */
extern int g_deadzone;      /* deadzone = <0..32767>: the sticks' and triggers' (pad.c) */
extern int g_rumble;        /* rumble = 0: the gamepad doesn't vibrate (pad.c) */

/* Platform state that is the console's, saved with the game's in a
   savestate (state.c): it sits in the game's variables' section */
#define SAVED __attribute__((section(".data$dw3g1")))
void state_key(int key);  /* a debug key (SDL keycode), from pump_events */
void state_frame(void);   /* at the frame's end (native_wait_vblank): runs what the keys asked */
int movie_playing(void);  /* movie.c */

#endif /* PLATFORM_H */
