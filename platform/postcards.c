/* The journal's pictures of the fields (native.h): data/postcards/XXX.png, a
   field's map seen from afar, XXX its mode in hexadecimal. They are made
   from the player's own disc by tools/postcards.py, so none ship; a field
   without one keeps the journal's cross. Drawn by the graphics card, from
   the journal's own primitive (gpu_draw.c) */
#include <stdint.h>
#include <SDL3/SDL.h>
#include "platform.h"

#define FIRST 0x200 /* the fields' modes */
#define COUNT 0x100

static SDL_Surface *s_img[COUNT];
static unsigned s_tex[COUNT];
static uint8_t s_tried[COUNT];

static SDL_Surface *postcard(int mode) {
    char path[1200];
    SDL_Surface *s;
    int i = mode - FIRST;

    if (i < 0 || i >= COUNT) {
        return NULL;
    }
    if (!s_tried[i]) {
        s_tried[i] = 1;
        SDL_snprintf(path, sizeof path, "%sdata/postcards/%03X.png", SDL_GetBasePath(), mode);
        if ((s = SDL_LoadPNG(path)) != NULL) {
            s_img[i] = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(s);
        }
    }
    return s_img[i];
}

int native_postcard(int mode) {
    return postcard(mode) != NULL;
}

void postcard_draw(int mode, float x, float y, float w, float h, int cx1, int cy1, int cx2, int cy2) {
    if (postcard(mode) != NULL) {
        gl_image(s_img[mode - FIRST], &s_tex[mode - FIRST], x, y, x + w, y + h, 0, cx1, cy1, cx2, cy2);
    }
}
