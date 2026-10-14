/* Effects' own images (native.h): data/mods/effects/<id>/ (PNGs), in their
   names' order, drawn in a battle's 2D effect's place (fightstg/
   sprite_effect.c), one every other field, as the model viewer exports an
   effect: the 320x240 screen around the effect's point, at any size of the
   same shape (bigger is finer). A critical hit's are in critical/ there,
   else it has the others. Loaded once, the first time the effect plays. */
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

#define MAX_SETS 64
static struct {
    int effect, critical, count;
    SDL_Surface **frames;
    unsigned *tex;
} s_sets[MAX_SETS];
static int s_count;

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The set of EFFECT's folder (critical/ in it when CRITICAL), loaded or found */
static int load_set(int effect, int critical) {
    char dir[1024], path[1200];
    char **names;
    int i, n = 0, set;

    for (i = 0; i < s_count; i++) {
        if (s_sets[i].effect == effect && s_sets[i].critical == critical) {
            return i;
        }
    }
    if (s_count == MAX_SETS) {
        return -1;
    }
    set = s_count++;
    s_sets[set].effect = effect;
    s_sets[set].critical = critical;
    SDL_snprintf(dir, sizeof dir, "%s/mods/effects/%d%s", data_dir(), effect, critical ? "/critical" : "");
    names = SDL_GlobDirectory(dir, "*.png", SDL_GLOB_CASEINSENSITIVE, &n);
    if (names == NULL || n == 0) {
        SDL_free(names);
        return set;
    }
    qsort(names, n, sizeof *names, by_name);
    s_sets[set].frames = SDL_calloc(n, sizeof *s_sets[set].frames);
    s_sets[set].tex = SDL_calloc(n, sizeof *s_sets[set].tex);
    for (i = 0; i < n; i++) {
        SDL_Surface *png;

        SDL_snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if ((png = SDL_LoadPNG(path)) == NULL) {
            LOG("effects: %s: %s", path, SDL_GetError());
            break;
        }
        s_sets[set].frames[i] = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(png);
        if (s_sets[set].frames[i] == NULL) {
            break;
        }
    }
    s_sets[set].count = i; /* up to the first that failed */
    SDL_free(names);
    return set;
}

int native_effect_images(int effect, int critical, int *frames) {
    int set = critical ? load_set(effect, 1) : -1;

    if (set < 0 || s_sets[set].count == 0) {
        set = load_set(effect, 0);
    }
    if (set < 0 || s_sets[set].count == 0) {
        return -1;
    }
    *frames = s_sets[set].count;
    return set;
}

void native_effect_draw(unsigned key, float x, float y, float xs, int cx1, int cy1, int cx2, int cy2) {
    int set = key >> 12, frame = key & 0xFFF;
    SDL_Surface *img;
    float w, h;

    if (set >= s_count || s_sets[set].count == 0) {
        return;
    }
    if (frame >= s_sets[set].count) {
        frame = s_sets[set].count - 1;
    }
    img = s_sets[set].frames[frame];
    h = 240.0f;
    w = h * img->w / img->h * xs;
    gl_image(img, &s_sets[set].tex[frame], x - w / 2, y - h / 2, x + w / 2, y + h / 2, cx1, cy1, cx2, cy2);
}
