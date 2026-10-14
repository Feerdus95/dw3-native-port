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

/* The drawing on the graphics card (gl.c), render_scale times finer than
   VRAM; the GTE keeps each vertex's fraction for it (gte.c) */
extern int g_res_scale;  /* render_scale = 1-8 */
typedef struct {
    float x, y, u, v;  /* VRAM pixels (fractions kept), texels */
    uint8_t r, g, b, a;
    uint16_t clut_x, clut_y, tx, ty;
    uint16_t umin, vmin, umax, vmax;
    uint16_t twmx, twmy, twox, twoy; /* texture window: mask, offset */
    uint32_t mode;     /* gl.c's MAIN_FS */
    float z;           /* the GTE's view depth, 0 for none: drawn in order, not by the depth buffer */
    float w;           /* the same, for the textures in perspective, 0 for none */
} GVtx;
struct SDL_Surface;
extern uint16_t g_vram_dirty[8]; /* 64x64 blocks the CPU wrote, a bit each */
int gl_init(struct SDL_Window *window);
void gl_sync(int force);         /* follows render_scale; force: VRAM was replaced */
void gl_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi);
void gl_vram_dirty(int x, int y, int w, int h);
void gl_shadows_resolve(void); /* an ordering table drawn: its battle shadows onto the copy */
void gl_depth_clear(void); /* a new ordering table: its 3D drawn over what came before */
void gl_refresh(int x, int y, int w, int h); /* the copy from VRAM, after the CPU wrote it */
void gl_copy(int sx, int sy, int w, int h, int dx, int dy);
void gl_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw);
void gl_present(const struct SDL_Surface *menu, struct SDL_Surface **shot);
int gte_precise(const uint32_t *word, float *x, float *y, float *z);
void gte_precise_copy(void *dst, const void *src); /* the game's own code calls it too */
void gte_precise_frame(void);

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
extern int g_depth; /* the options menu's Depth buffer: the 3D models' polygons by their depth, not the PSX's order (gl.c) */
extern int g_subpixel; /* the options menu's Sub-pixel 3D: 0 snaps vertices to the 1x pixel (gte.c), for comparing */
extern int g_dither_on;     /* dither = 0: no GPU dithering (gpu_draw.c) */
extern int g_true_color;    /* true_color = 0: the card's colours cut to the PSX's 5 bits a channel (gl.c) */
extern int g_perspective;   /* perspective = 0: the 3D's textures without perspective, as the PSX's (gl.c) */
extern int g_smooth_3d;     /* smooth_3d = 0: the 3D's textures by their nearest texel, as the PSX's (gl.c) */
extern int g_widescreen;     /* widescreen = 1: battles 16:9, the window too (gte.c) */
int gte_widescreen(void);    /* this frame is (gte.c) */
int gte_in_battle(void);     /* this frame is a battle's (gte.c) */
int video_window_width(int scale); /* 320 * scale, or 16:9 with widescreen (video.c) */
void video_resize(int scale);      /* the window, windowed, fitting the screen (video.c) */
extern int g_vsync;                /* vsync = 0: frames shown at once, torn (gl.c) */
extern int g_soft_shadows;
extern int g_battle_shadows; /* battle_shadows = 0: the fighters cast none (gl.c, native.h) */  /* soft_shadows = 1: the field's shadows see-through and smooth (gl.c, native.h) */
extern int g_smooth_movies; /* smooth_movies = 0: movies scaled by nearest pixel (video.c) */
extern int g_deadzone;      /* deadzone = <0..32767>: the sticks' and triggers' (pad.c) */
extern int g_rumble;        /* rumble = 0: the gamepad doesn't vibrate (pad.c) */
extern int g_fast_text;     /* fast_text = 0: X held doesn't advance the texts (mods.c) */
extern int g_ff_speed;      /* fast forward: the game's ticks a frame shown, 1 normal (mods.c, etc.c) */
void ff_cycle(void);        /* Tab / R3: the next speed */
extern int g_fast_travel;   /* fast_travel = 1: X on the map screen travels there (mods.c) */
extern int g_extra_packs;   /* extra_packs = 0: the starter choice has the game's three (packs.c) */
extern int g_bladegarurumon; /* bladegarurumon = 0: partners don't learn it (mods.c) */
/* a file an option makes anew, whole, or NULL for the game's (mods.c; packs.c, bladegaru.c) */
const uint8_t *made_file(int fid, long *size);
void native_bladegarurumon_setup(void); /* the game's main/game/digimon.c (patched) */
const uint8_t *packs_file(int fid, long *size);
const uint8_t *bladegaru_file(int fid, long *size);
long native_read_raw(int fid, uint8_t **out); /* files.c: a file on disk whole, malloc'd; its size */

/* Platform state that is the console's, saved with the game's in a
   savestate (state.c): it sits in the game's variables' section */
#define SAVED __attribute__((section(".data$dw3g1")))
void state_key(int key);  /* a debug key (SDL keycode), from pump_events */
void state_frame(void);   /* at the frame's end (native_wait_vblank): runs what the keys asked */
int movie_playing(void);  /* movie.c */
int stage_dump(const char *dir); /* stagedump.c: --dump-stages DIR */
extern int g_field_mode, g_field_progress, g_field_arg; /* state.c: --field MODE --progress N */
extern int g_field_sets[64][2], g_field_set_count; /* --set CODE=VALUE (hex code), repeated */
extern int g_field_party; /* --party N: the starter set given when there is no party */
extern int g_field_at_x, g_field_at_y; /* --at X,Y: the player's start in the first field (map pixels), x -1 for none */

#endif /* PLATFORM_H */
