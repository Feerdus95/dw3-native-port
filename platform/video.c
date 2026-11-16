/* The window, where the renderer (gfx.c) draws the display area (the last
   PutDispEnv) and the options menu over it. */
#include <SDL3/SDL.h>
#include "platform.h"
#include "menu_skin.h"

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

/* The game's boot image in the window while Direct3D 12 starts (with DLSS, NVIDIA's Streamline takes seconds there): the
   320x480 TIM that main.c's load_boot_image put at STAGE_VRAM, the one the game's main() shows first. Drawn by SDL in
   software where the renderer will show it (frame.c's picture: 4:3, whole rows with integer_scale; main() shows 640 rows
   from (0, 0), the VRAM's 512 of them, so the image is its top 480/512 and a black band is left under it), unsmoothed as
   there, with what is starting written in that band. Let go before the swap chain takes the window; nothing is presented
   (present_frame) until the game shows the image itself, so it stays up to then */
static int s_boot_shown;
static SDL_Surface *s_boot_pic;

SDL_Surface *video_boot_picture(void) {
    return s_boot_pic;
}

static void boot_image(SDL_Window *w) {
    extern uint8_t STAGE_VRAM[];
    const uint8_t *t = STAGE_VRAM + 4; /* after the TIM's ID word, as main() reads it: flags, block size, x, y, w, h, pixels */
    const int iw = t[12] | t[13] << 8, ih = t[14] | t[15] << 8;
    SDL_Surface *img, *win;
    int x, y;

    if ((t[0] & 7) != 2 || iw != 320 || ih != 480 || (img = SDL_CreateSurface(iw, ih, SDL_PIXELFORMAT_XRGB8888)) == NULL) {
        return; /* 16-bit, no CLUT, 320x480: anything else is not it */
    }
    for (y = 0; y < ih; y++) {
        for (x = 0; x < iw; x++) {
            const uint8_t *p = t + 16 + ((size_t)y * iw + x) * 2;
            const unsigned c = p[0] | p[1] << 8;

            ((uint32_t *)((uint8_t *)img->pixels + (size_t)y * img->pitch))[x] = (c & 31) << 19 | (c >> 5 & 31) << 11 | (c >> 10 & 31) << 3;
        }
    }
    if ((win = SDL_GetWindowSurface(w)) != NULL) {
        const char *text = g_fsr == 3 ? "Starting DLSS (NVIDIA), it takes a few seconds..." : "Starting Direct3D 12...";
        SDL_Rect vp = {0, 0, win->w * 3 > win->h * 4 ? win->h * 4 / 3 : win->w, win->w * 3 > win->h * 4 ? win->h : win->w * 3 / 4}, r;
        SDL_Renderer *sr;

        if (g_integer_scale && vp.h >= 240) {
            vp.h = vp.h / 240 * 240, vp.w = vp.h * 4 / 3;
        }
        vp.x = (win->w - vp.w) / 2, vp.y = (win->h - vp.h) / 2;
        r = (SDL_Rect){vp.x, vp.y, vp.w, vp.h * 480 / 512};
        SDL_FillSurfaceRect(win, NULL, 0);
        SDL_BlitSurfaceScaled(img, NULL, win, &r, SDL_SCALEMODE_NEAREST);
        if ((sr = SDL_CreateSoftwareRenderer(win)) != NULL) { /* the text, centred in the band */
            const float k = (float)SDL_max(1, vp.h / 480), band = vp.h - r.h;

            SDL_SetRenderScale(sr, k, k);
            SDL_SetRenderDrawColor(sr, 255, 255, 255, 255);
            SDL_RenderDebugText(sr, (vp.x + (vp.w - SDL_strlen(text) * 8 * k) / 2) / k, (r.y + r.h + (band - 8 * k) / 2) / k, text);
            SDL_RenderPresent(sr);
            SDL_DestroyRenderer(sr);
        }
        SDL_UpdateWindowSurface(w);
        s_boot_pic = SDL_ConvertSurface(win, SDL_PIXELFORMAT_RGBA32); /* for D3D12's first present (dx_init) */
        s_boot_shown = 1;
        SDL_PumpEvents();
        SDL_DestroyWindowSurface(w);
    }
    SDL_DestroySurface(img);
}

int video_init(int scale, int fullscreen) {
    int ok;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        LOG("SDL_Init: %s", SDL_GetError());
        return 0;
    }
    for (;;) { /* the renderer picked, or OpenGL when it can't start: a new window, as its flags differ */
        s_window = SDL_CreateWindow("Digimon World 2003", video_window_width(scale), 240 * scale,
                                    gfx_window_flags() | SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0) | (g_render_sounds ? SDL_WINDOW_HIDDEN : 0));
        if (s_window == NULL) {
            LOG("window: %s", SDL_GetError());
            return 0;
        }
        if (!g_render_sounds && SDL_strcmp(gfx_name(), "D3D12") == 0) {
            boot_image(s_window);
        }
        ok = gfx_init(s_window);
        SDL_DestroySurface(s_boot_pic), s_boot_pic = NULL;
        if (ok) {
            break;
        }
        SDL_DestroyWindow(s_window);
        if (!gfx_fallback()) {
            LOG("this needs OpenGL 3.3");
            return 0;
        }
        LOG("renderer: failed, OpenGL instead");
    }
    /* the menu in 320x240 units, four times as fine: its icons sharp */
    s_menu = SDL_CreateSurface(1280, 960, SDL_PIXELFORMAT_ABGR8888);
    s_menu_renderer = SDL_CreateSoftwareRenderer(s_menu);
    SDL_SetRenderScale(s_menu_renderer, 4, 4);
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
        /* the window lost its focus: sound muted, or the game held until it is back */
        if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST && g_inactive_window) {
            audio_mute(1);
            while (g_inactive_window == 2 && SDL_WaitEvent(&e) && e.type != SDL_EVENT_WINDOW_FOCUS_GAINED) {
                if (e.type == SDL_EVENT_QUIT) {
                    return 0;
                }
            }
        }
        if (e.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
            audio_mute(0);
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
        /* F10: the battle's performance meter on and off */
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_F10) {
            g_perf = !g_perf;
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

    soft_all();
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

/* skip_scenes' cue (mods.c), bottom left: the skip button's icon in a ring that fills while it is
   held, and the word in the game's language (LANGUAGE: 0-2 English, French, Italian, German,
   Spanish), in the game's font; the black over the picture while a skip runs, and its fade back */
extern int LANGUAGE;

static void skip_ring(SDL_Renderer *r, float cx, float cy, float rad, float share, SDL_FColor c) {
    SDL_Vertex v[2 * 49];
    int i, n = (int)(48 * share + 0.5f);

    for (i = 0; i <= n; i++) {
        float a = -SDL_PI_F / 2 + 2 * SDL_PI_F * i / 48;

        v[2 * i] = (SDL_Vertex){{cx + SDL_cosf(a) * (rad - 1), cy + SDL_sinf(a) * (rad - 1)}, c, {0, 0}};
        v[2 * i + 1] = (SDL_Vertex){{cx + SDL_cosf(a) * (rad + 1), cy + SDL_sinf(a) * (rad + 1)}, c, {0, 0}};
    }
    for (i = 0; i < n; i++) {
        int idx[6] = {2 * i, 2 * i + 1, 2 * i + 2, 2 * i + 1, 2 * i + 3, 2 * i + 2};

        SDL_RenderGeometry(r, NULL, v, 2 * (n + 1), idx, 6);
    }
}

static void skip_draw(SDL_Renderer *r, int cue, float ring) {
    static const char *const WORDS[7] = {"Skip", "Skip", "Skip", "Passer", "Salta", "Auslassen", "Saltar"};
    static SDL_Surface *last;
    static SDL_Texture *tex;
    const char *word = WORDS[LANGUAGE >= 0 && LANGUAGE < 7 ? LANGUAGE : 0];
    SDL_Surface *img = NULL;
    unsigned *unused;
    int blank = native_skip_blank();
    SDL_FRect box;
    float cx, cy;

    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    if (blank > 0) {
        SDL_SetRenderDrawColor(r, 0, 0, 0, (Uint8)blank);
        SDL_RenderFillRect(r, NULL);
    }
    if (cue == 0) {
        return;
    }
    /* a panel of the field menu's, bottom left: the button in the hold's ring, the word in its font */
    skin_font(1);
    box = (SDL_FRect){6, 240 - 8 - 24, 30 + skin_text_width(word), 24};
    skin_panel_shadow(r, &box);
    cx = box.x + 12, cy = box.y + 12;
    skip_ring(r, cx, cy, 9, 1, (SDL_FColor){0x32 / 255.0f, 0x32 / 255.0f, 0x8B / 255.0f, 1}); /* the panel's rule */
    skip_ring(r, cx, cy, 9, ring, (SDL_FColor){0x34 / 255.0f, 0xC5 / 255.0f, 0xF4 / 255.0f, 1}); /* its accent */
    if (controls_game_icon(15 /* square */, &img, &unused) && img != last) {
        SDL_DestroyTexture(tex);
        tex = SDL_CreateTextureFromSurface(r, img);
        last = img;
    }
    if (tex != NULL && img != NULL) {
        SDL_FRect d = {cx - 6, cy - 6, 12, 12};

        SDL_RenderTexture(r, tex, NULL, &d);
    }
    skin_ink(r, INK_LABEL);
    skin_text(r, box.x + 25, box.y + 6, word);
    skin_font(0);
}

/* Fast forward's speed, top right: a panel of the field menu's, two arrows and the speed in its font */
static void ff_draw(SDL_Renderer *r) {
    const SDL_FColor white = {1, 1, 1, 1};
    char text[8];
    SDL_FRect box;
    float x, cy;
    int i;

    SDL_snprintf(text, sizeof text, "x%d", g_ff_speed);
    skin_font(1);
    box = (SDL_FRect){0, 6, 25 + skin_text_width(text), 20};
    box.x = 320 - 8 - box.w;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    skin_panel_shadow(r, &box);
    x = box.x + 5, cy = box.y + 10;
    for (i = 0; i < 2; i++, x += 6) {
        SDL_Vertex v[3] = {{{x, cy - 4}, white, {0, 0}}, {{x + 6, cy}, white, {0, 0}}, {{x, cy + 4}, white, {0, 0}}};

        SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
    }
    skin_ink(r, INK_LABEL);
    skin_text(r, box.x + 20, box.y + 4, text);
    skin_font(0);
}

void present_frame(void) {
    SDL_Surface *shot = NULL;
    int n = dump_due(), screenshot = s_screenshot && !menu_active();

    int ff = !menu_active() && g_ff_speed > 1, pf = !menu_active() && perf_on();
    float ring;
    int cue = menu_active() ? 0 : native_skip_cue(&ring), sk = !menu_active() && (cue || native_skip_blank() > 0);

    gfx_sync(0);
    if (s_boot_shown && !g_disp_on && !menu_active() && n == 0) { /* main()'s black frames before its SetDispMask(1): boot_image's stays */
        return;
    }
    s_boot_shown = 0;
    if (menu_active() || ff || pf || sk) {
        SDL_SetRenderDrawColor(s_menu_renderer, 0, 0, 0, 0);
        SDL_RenderClear(s_menu_renderer);
        menu_draw(s_menu_renderer);
        if (ff) {
            ff_draw(s_menu_renderer);
        }
        if (pf) {
            perf_draw(s_menu_renderer);
        }
        if (sk) {
            skip_draw(s_menu_renderer, cue, ring);
        }
        SDL_RenderPresent(s_menu_renderer);
    }
    if (screenshot) {
        s_screenshot = 0;
        gfx_present(NULL, &shot); /* without the fast forward's corner */
        if (shot != NULL) {
            save_screenshot(shot);
        }
        shot = NULL;
    }
    gfx_present(menu_active() || ff || pf || sk ? s_menu : NULL, n ? &shot : NULL);
    if (n) {
        dump_vram(n, shot);
    }
}
