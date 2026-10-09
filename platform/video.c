/* The window: shows the display area of VRAM (the last PutDispEnv) through
   an SDL streaming texture. */
#include <SDL3/SDL.h>
#include "platform.h"

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static uint32_t *s_pixels;

int video_init(int scale, int fullscreen) {
    s_pixels = SDL_calloc(VRAM_W * VRAM_H, 4);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG("SDL_Init: %s", SDL_GetError());
        return 0;
    }
    if (!SDL_CreateWindowAndRenderer("Digimon World 2003", 320 * scale, 240 * scale,
                                     SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0), &s_window, &s_renderer)) {
        LOG("window: %s", SDL_GetError());
        return 0;
    }
    SDL_SetRenderLogicalPresentation(s_renderer, 4, 3, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, VRAM_W, VRAM_H);
    SDL_SetTextureScaleMode(s_texture, SDL_SCALEMODE_NEAREST);
    return 1;
}

int pump_events(void) {
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            return 0;
        }
        /* F11 or Alt+Enter: fullscreen on and off */
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
            (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
            SDL_SetWindowFullscreen(s_window, !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN));
        }
    }
    return 1;
}

/* DW3_DUMP="N[:from:to]" writes the whole VRAM to vram_<vsync>.bmp every N
   vsyncs, and what is on the screen to screen_<vsync>.bmp: a look at it
   without a screenshot of the desktop */
static void dump_vram(void) {
    static int n, every = -1, from, to = 0x7FFFFFFF;
    SDL_Surface *s;
    const char *env;
    char name[32];
    int i;

    if (every < 0) {
        env = SDL_getenv("DW3_DUMP");
        every = 0;
        if (env != NULL) {
            SDL_sscanf(env, "%d:%d:%d", &every, &from, &to);
        }
    }
    if (every <= 0 || ++n % every != 0 || n < from || n > to) {
        return;
    }
    s = SDL_CreateSurface(VRAM_W, VRAM_H, SDL_PIXELFORMAT_XRGB8888);
    for (i = 0; i < VRAM_W * VRAM_H; i++) {
        uint16_t p = g_vram[i];
        ((uint32_t *)s->pixels)[i] = (uint32_t)((p & 31) << 19 | (p >> 5 & 31) << 11 | (p >> 10 & 31) << 3);
    }
    SDL_snprintf(name, sizeof(name), "vram_%05d.bmp", n);
    SDL_SaveBMP(s, name);
    SDL_DestroySurface(s);
    if (g_disp_on && g_disp.disp.w > 0 && g_disp.disp.h > 0) {
        s = SDL_CreateSurfaceFrom(g_disp.disp.w, g_disp.disp.h, SDL_PIXELFORMAT_XRGB8888,
                                  &s_pixels[g_disp.disp.y * VRAM_W + g_disp.disp.x], VRAM_W * 4);
        SDL_snprintf(name, sizeof(name), "screen_%05d.bmp", n);
        SDL_SaveBMP(s, name);
        SDL_DestroySurface(s);
    }
}

/* 15-bit BGR VRAM to 32-bit, only the display area */
void present_frame(void) {
    RECT d = g_disp.disp;
    SDL_FRect src;
    int x, y;

    SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
    SDL_RenderClear(s_renderer);
    if (g_disp_on && d.w > 0 && d.h > 0) {
        if (d.x + d.w > VRAM_W) d.w = VRAM_W - d.x;
        if (d.y + d.h > VRAM_H) d.h = VRAM_H - d.y;
        for (y = d.y; y < d.y + d.h && g_disp.isrgb24; y++) {
            /* 24-bit (the movies): d.w pixels of 3 bytes from d.x on */
            const uint8_t *b = (const uint8_t *)&g_vram[y * VRAM_W + d.x];

            for (x = 0; x < d.w && (d.x * 2 + x * 3 + 3) <= VRAM_W * 2; x++) {
                s_pixels[y * VRAM_W + d.x + x] = (uint32_t)(b[x * 3] << 16 | b[x * 3 + 1] << 8 | b[x * 3 + 2]);
            }
        }
        for (y = d.y; y < d.y + d.h && !g_disp.isrgb24; y++) {
            for (x = d.x; x < d.x + d.w; x++) {
                uint16_t p = g_vram[y * VRAM_W + x];
                uint32_t r = (p & 31) << 3, g = (p >> 5 & 31) << 3, b = (p >> 10 & 31) << 3;
                s_pixels[y * VRAM_W + x] = r << 16 | g << 8 | b;
            }
        }
        SDL_UpdateTexture(s_texture, NULL, s_pixels, VRAM_W * 4);
        src.x = d.x;
        src.y = d.y;
        src.w = d.w;
        src.h = d.h;
        SDL_RenderTexture(s_renderer, s_texture, &src, NULL);
    }
    SDL_RenderPresent(s_renderer);
    dump_vram();
}
