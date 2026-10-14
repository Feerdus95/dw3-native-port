/* The window: an OpenGL one, where gl.c draws the display area (the last
   PutDispEnv) and the options menu over it. */
#include <SDL3/SDL.h>
#include "platform.h"

static SDL_Window *s_window;
static SDL_Surface *s_menu;            /* the menu, drawn by SDL in software */
static SDL_Renderer *s_menu_renderer;
static int s_screenshot; /* F12: the next frame to screenshots/ */

int video_window_width(int scale) {
    return g_widescreen ? (scale * 1280 + 1) / 3 : 320 * scale;
}

/* The window at a size in 320x240 steps (16:9 with widescreen), made smaller
   until it fits the screen it is on, title bar and taskbar aside, and centred
   there: a 16:9 window as tall as a 4:3 one can be wider than the screen */
void video_resize(int scale) {
    SDL_DisplayID display = SDL_GetDisplayForWindow(s_window);
    SDL_Rect usable = {0, 0, 1 << 16, 1 << 16};

    SDL_GetDisplayUsableBounds(display, &usable);
    while (scale > 1 && (video_window_width(scale) > usable.w || 240 * scale > usable.h - 40)) {
        scale--;
    }
    if (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) {
        SDL_SetWindowFullscreen(s_window, false);
    }
    SDL_SetWindowSize(s_window, video_window_width(scale), 240 * scale);
    SDL_SetWindowPosition(s_window, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

int video_init(int scale, int fullscreen) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG("SDL_Init: %s", SDL_GetError());
        return 0;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    s_window = SDL_CreateWindow("Digimon World 2003", video_window_width(scale), 240 * scale,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0));
    if (s_window == NULL) {
        LOG("window: %s", SDL_GetError());
        return 0;
    }
    if (!gl_init(s_window)) {
        LOG("this needs OpenGL 3.3");
        return 0;
    }
    /* the menu in 320x240 units, twice as fine */
    s_menu = SDL_CreateSurface(640, 480, SDL_PIXELFORMAT_ABGR8888);
    s_menu_renderer = SDL_CreateSoftwareRenderer(s_menu);
    SDL_SetRenderScale(s_menu_renderer, 2, 2);
    return 1;
}

SDL_Window *video_window(void) {
    return s_window;
}

int pump_events(void) {
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT) {
            return 0;
        }
        if (menu_event(&e)) {
            continue;
        }
        /* Tab or R3: fast forward's next speed */
        if ((e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_TAB) ||
            (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)) {
            ff_cycle();
        }
        /* F12: the game's picture as drawn, to screenshots/ by the exe */
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F12) {
            s_screenshot = 1;
        }
        /* F11 or Alt+Enter: fullscreen on and off */
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat &&
            (e.key.key == SDLK_F11 || (e.key.key == SDLK_RETURN && (e.key.mod & SDL_KMOD_ALT)))) {
            SDL_SetWindowFullscreen(s_window, !(SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN));
        } else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && g_debug) {
            state_key((int)e.key.key);
        }
    }
    return 1;
}

/* DW3_DUMP="N[:from:to]" writes the whole VRAM to vram_<vsync>.bmp every N
   vsyncs, and the window as drawn (the menu too) to window_<vsync>.bmp: a
   look at it without a screenshot of the desktop */
static int dump_due(void) {
    static int n, every = -1, from, to = 0x7FFFFFFF;
    const char *env;

    if (every < 0) {
        env = SDL_getenv("DW3_DUMP");
        every = 0;
        if (env != NULL) {
            SDL_sscanf(env, "%d:%d:%d", &every, &from, &to);
        }
    }
    return every > 0 && ++n % every == 0 && n >= from && n <= to ? n : 0;
}

static void dump_vram(int n, SDL_Surface *shot) {
    SDL_Surface *s = SDL_CreateSurface(VRAM_W, VRAM_H, SDL_PIXELFORMAT_XRGB8888);
    char name[32];
    int i;

    for (i = 0; i < VRAM_W * VRAM_H; i++) {
        uint16_t p = g_vram[i];
        ((uint32_t *)s->pixels)[i] = (uint32_t)((p & 31) << 19 | (p >> 5 & 31) << 11 | (p >> 10 & 31) << 3);
    }
    SDL_snprintf(name, sizeof(name), "vram_%05d.bmp", n);
    SDL_SaveBMP(s, name);
    SDL_DestroySurface(s);
    if (shot != NULL) {
        SDL_snprintf(name, sizeof(name), "window_%05d.bmp", n);
        SDL_SaveBMP(shot, name);
        SDL_DestroySurface(shot);
    }
}

/* The frame as drawn, the options menu aside, to screenshots/shot_<time>.bmp */
static void save_screenshot(SDL_Surface *shot) {
    char path[1200];

    SDL_snprintf(path, sizeof path, "%sscreenshots", SDL_GetBasePath());
    SDL_CreateDirectory(path);
    SDL_snprintf(path, sizeof path, "%sscreenshots/shot_%llu.bmp", SDL_GetBasePath(), (unsigned long long)SDL_GetTicks());
    if (SDL_SaveBMP(shot, path)) {
        LOG("screenshot: %s", path);
    }
    SDL_DestroySurface(shot);
}

void present_frame(void) {
    SDL_Surface *shot = NULL;
    int n = dump_due(), screenshot = s_screenshot && !menu_active();

    int ff = !menu_active() && g_ff_speed > 1;

    gl_sync(0);
    if (menu_active() || ff) {
        SDL_SetRenderDrawColor(s_menu_renderer, 0, 0, 0, 0);
        SDL_RenderClear(s_menu_renderer);
        menu_draw(s_menu_renderer);
        if (ff) { /* fast forward's speed, in a corner */
            char text[8];
            SDL_FRect bg = {320 - 54, 6, 48, 14};

            SDL_snprintf(text, sizeof text, ">> x%d", g_ff_speed);
            SDL_SetRenderDrawBlendMode(s_menu_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(s_menu_renderer, 0, 0, 0, 160);
            SDL_RenderFillRect(s_menu_renderer, &bg);
            SDL_SetRenderDrawColor(s_menu_renderer, 255, 255, 255, 255);
            SDL_RenderDebugText(s_menu_renderer, bg.x + 4, bg.y + 3, text);
        }
        SDL_RenderPresent(s_menu_renderer);
    }
    if (screenshot) {
        s_screenshot = 0;
        gl_present(NULL, &shot); /* without the fast forward's corner */
        if (shot != NULL) {
            save_screenshot(shot);
        }
        shot = NULL;
    }
    gl_present(menu_active() || ff ? s_menu : NULL, n ? &shot : NULL);
    if (n) {
        dump_vram(n, shot);
    }
}
