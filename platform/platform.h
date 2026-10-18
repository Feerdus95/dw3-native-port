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
/* A small JSON reader (stagemods.c): numbers, strings, arrays and objects */
typedef struct Json {
    int type; /* 'n' number, 's' string, '[' array, '{' object, 0 null */
    double num;
    char *str;           /* a string */
    char *key;           /* an object member's key */
    struct Json *child;  /* the first item of an array or object */
    struct Json *next;   /* the next item */
} Json;
Json *json_value(const char **p); /* NULL when it isn't JSON */
void json_free(Json *j);
Json *json_get(Json *obj, const char *key);
Json *json_at(Json *arr, int i);
int json_len(Json *arr);
int json_int(Json *j);
/* an RGBA image over the copy, from (x0, y0) to (x1, y1), blended by its
   alpha (or added, ADD) inside the drawing area; its texture made in *tex on first use */
typedef struct {
    float x, y, z, u, v; /* VRAM pixels, view depth, texels 0-1 */
    uint8_t r, g, b, a;
} PropVtx;
/* The props drawn from tables (gl.c's PINST_VS): the camera as gte_camera_project has it, the clock and wind */
typedef struct {
    float row[3][4];         /* view = row . (x, y, z, 1) */
    float h, ofx, ofy, wide; /* focal length, screen offset, 0.75 on the wide screen else 1 */
    float t, bright, ox, oy; /* the frame clock, the stage's brightness, the drawing offset */
    float wind[2];           /* the billboards' */
} PCam;
typedef struct {
    float pos[3];                  /* the ground point, y up */
    float w, h, sway, rot, fixed;  /* size, sway, turn (degrees), 1: flat in the world */
    uint8_t c[4];                  /* tint, fog */
} PInst;
typedef struct {
    float center[3], radius, top, fall, wind, above, size[2], floor, tint[3], fog;
    int count, leaf;
} PPart;
unsigned gl_inst_make(const PInst *v, int n);
void gl_inst_free(unsigned b);
void gl_props_inst(const struct SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
void gl_props_part(const struct SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo);
int gte_camera_get(PCam *c); /* the battle camera (gte.c); 0 when no stage was drawn lately */
/* The battle's performance meter (perf.c): CPU sections timed with perf_begin / perf_end, the card's times from gl.c's queries */
enum { PF_DRAW, PF_PROPS, PF_PRESENT, PF_WAIT, PF_GPU, PF_GPU_PROPS, PF_PLOAD, PF_PGL, PF_PFLUSH, PF_PTEX, PF_SPOLY, PF_BRIGHT, PF_PDRAW, PF_N };
extern int g_perf; /* F10, perf in native.ini */
int perf_on(void); /* the meter is on and a fight is running */
void perf_begin(int section);
void perf_end(int section);
void perf_gpu(int section, double ms);
void perf_enter(double budget_ms); /* at each vsync */
void perf_reset(void);
void perf_draw(struct SDL_Renderer *r);
void gl_depth_save(void); /* the stage's depth, kept for the props (gl.c) */
void gl_props(const struct SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo);
void gl_image(const struct SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2);
/* GP0 1D (native.h): images KEY >> 12 at tick KEY & 0xFFF around (x, y),
   their widths times xs (effects.c) */
void native_effect_draw(unsigned key, float x, float y, float xs, int cx1, int cy1, int cx2, int cy2);
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
extern int g_battle_sky; /* battle_sky = 1: a sky behind the battles and fog over the stage's far side (gl.c, gpu_draw.c) */
/* A fight stage's profile for it (stages.ini; colours -1 for the stage's own) */
typedef struct StageFx {
    int sky;           /* a gradient behind the stage */
    int sky_top[3];    /* its top */
    float fog, fog_start; /* how much at the far side, where it starts (part of the way) */
    int fog_color[3];
    float horizon;     /* the painted horizon's radius, 0: none */
    int leaves;        /* falling leaves in the arena, how many */
    float wind;        /* how hard the wind blows them */
    float clouds;      /* the painted sky's turn, degrees a second */
    float sway;        /* how far the props (trees) bend in the wind */
    int on;            /* 0: the stage is left as it is */
} StageFx;
void stagefx_draw(int ox, int oy, int x1, int y1, int x2, int y2); /* the particles, after the fighters (stagefx.c) */
int gte_camera_project(const float p[3], float out[3]); /* the battle camera's (gte.c) */
float gte_camera_scale(float z);
int gte_camera_wide(void);
const StageFx *stagefx_current(float *bright); /* the stage shown's, NULL for none (stagefx.c) */
void stagefx_stage_drawn(int polygons);       /* the stage layer's ordering table drew this many (stagefx.c) */
void stagefx_frame(void);                     /* a frame drawn */
void gl_battle_fog(int r, int g, int b); /* a battle's sky drawn, its horizon's colour (gl.c) */
extern int g_battle_shadows; /* battle_shadows = 0: the fighters cast none (gl.c, native.h) */  /* soft_shadows = 1: the field's shadows see-through and smooth (gl.c, native.h) */
extern int g_smooth_movies; /* smooth_movies = 0: movies scaled by nearest pixel (video.c) */
extern int g_deadzone;      /* deadzone = <0..32767>: the sticks' and triggers' (pad.c) */
extern int g_rumble;        /* rumble = 0: the gamepad doesn't vibrate (pad.c) */

/* The PS buttons' keys and gamepad buttons, by bit (pad.c): native.ini's
   keys_<button> = Z, Space and pad_<button> = a (SDL's names) */
#define PAD_NONE (-1)
#define PAD_LT 100 /* the triggers, as buttons */
#define PAD_RT 101
struct SDL_Gamepad;
const char *pad_button_name(int bit);   /* "cross" */
int *pad_keys(int bit);                 /* its two scancodes, 0 for none */
int *pad_source(int bit);               /* its gamepad button, PAD_LT / PAD_RT or PAD_NONE */
const char *pad_source_name(int src);   /* SDL's name: "a", "lefttrigger" */
void pad_bind_reset(void);              /* the defaults */
int pad_bind_ini(const char *key, const char *value); /* 1: a binding, read */
int pad_bind_line(int i, char *key, char *value, size_t size); /* line 0-31 for native.ini; 1: not the default */
struct SDL_Gamepad *pad_gamepad(void);  /* the one open, or NULL */

/* The options menu's Controls page and button icons (controls.c) */
enum { ICONS_AUTO, ICONS_PS1, ICONS_PS3, ICONS_PS4, ICONS_PS5, ICONS_XBOX360, ICONS_XBOXONE, ICONS_XBOXSERIES, ICONS_SWITCH, ICONS_KEYBOARD, ICONS_COUNT };
extern int g_icons;                     /* icons = ps1 | ps3 | ps4 | ps5 | xbox360 | xboxone | xboxseries | switch | keyboard | auto */
extern const char *const ICON_NAMES[ICONS_COUNT];
void controls_note(const union SDL_Event *e); /* every event: the last device used */
void controls_open(void);
int controls_active(void);
int controls_event(const union SDL_Event *e); /* 1: a binding changed */
void controls_draw(struct SDL_Renderer *r, float x, float y, float w, int rows);
float controls_hint(struct SDL_Renderer *r, float x, float y, int draw); /* the help line; its width */
void controls_tab_icons(struct SDL_Renderer *r, float left, float right, float y); /* L1 / R1, or Q / E */
int controls_glyph(int tx, int ty, int depth, int u, int v, int w, int h); /* a text sprite's face button, or -1 */
int controls_game_icon(int bit, struct SDL_Surface **img, unsigned **tex); /* 1: drawn as img instead */
extern int g_fast_text;     /* fast_text = 0: X held doesn't advance the texts (mods.c) */
extern int g_ff_speed;      /* fast forward: the game's ticks a frame shown, 1 normal (mods.c, etc.c) */
void ff_cycle(void);        /* Tab / R3: the next speed */
extern int g_map_objective; /* map_objective = 1: the map blinks the story's next area (mods.c) */
extern int g_journal;       /* journal = 1: the field menu's journal row (mods.c) */
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
