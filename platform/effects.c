/* Effects' own images (native.h): data/mods/effects/<id>/ (PNGs) drawn in a
   battle's 2D effect's place (fightstg/sprite_effect.c), as the model viewer
   exports an effect: the 320x240 screen around the effect's point, at any
   size of the same shape (bigger is finer). A critical hit's are in critical/
   there, else it has the others. The folder's effect.json, which the model
   viewer's Effects tab writes, says how they play:
     {"frames": [["00.png", 2], ...], "x": 0, "y": 0, "scale": 1, "blend": "alpha"}
   each frame a PNG and its ticks (1/60 s, as the battle counts), the images
   moved by (x, y) screen pixels and scaled, blended by their alpha or added
   ("add"). Without it, every PNG in their names' order, 2 ticks each. Loaded
   once, the first time the effect plays. */
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

#define MAX_SETS 64
#define MAX_FRAMES 512
static struct {
    int effect, critical;
    int images; /* PNGs, in their names' order */
    char **names;
    SDL_Surface **img;
    unsigned *tex;
    int frames; /* what plays: an image each, until its tick */
    short image[MAX_FRAMES], end[MAX_FRAMES];
    float x, y, scale;
    int add;
} s_sets[MAX_SETS];
static int s_count;

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The folder's effect.json, into SET's frames; 0 without one */
static int read_json(int set, const char *dir) {
    char path[1200];
    char *text;
    const char *p;
    Json *root, *frames, *v;
    int i, k, tick = 0;

    SDL_snprintf(path, sizeof path, "%s/effect.json", dir);
    if ((text = SDL_LoadFile(path, NULL)) == NULL) {
        return 0;
    }
    p = text;
    root = json_value(&p);
    SDL_free(text);
    if (root == NULL) {
        LOG("effects: %s isn't JSON", path);
        return 0;
    }
    frames = json_get(root, "frames");
    for (i = 0; i < json_len(frames) && s_sets[set].frames < MAX_FRAMES; i++) {
        Json *name = json_at(json_at(frames, i), 0);
        int ticks = json_int(json_at(json_at(frames, i), 1));

        for (k = 0; name != NULL && name->type == 's' && k < s_sets[set].images; k++) {
            if (SDL_strcasecmp(s_sets[set].names[k], name->str) == 0) {
                break;
            }
        }
        if (name == NULL || k == s_sets[set].images || ticks <= 0) {
            LOG("effects: %s: frame %d has no PNG or ticks", path, i);
            continue;
        }
        tick += ticks;
        s_sets[set].image[s_sets[set].frames] = (short)k;
        s_sets[set].end[s_sets[set].frames++] = (short)SDL_min(tick, 0xFFF);
    }
    if ((v = json_get(root, "x")) != NULL && v->type == 'n') s_sets[set].x = (float)v->num;
    if ((v = json_get(root, "y")) != NULL && v->type == 'n') s_sets[set].y = (float)v->num;
    if ((v = json_get(root, "scale")) != NULL && v->type == 'n' && v->num > 0) s_sets[set].scale = (float)v->num;
    if ((v = json_get(root, "blend")) != NULL && v->type == 's') s_sets[set].add = SDL_strcmp(v->str, "add") == 0;
    json_free(root);
    return 1;
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
    s_sets[set].scale = 1;
    SDL_snprintf(dir, sizeof dir, "%s/mods/effects/%d%s", data_dir(), effect, critical ? "/critical" : "");
    names = SDL_GlobDirectory(dir, "*.png", SDL_GLOB_CASEINSENSITIVE, &n);
    if (names == NULL || n == 0) {
        SDL_free(names);
        return set;
    }
    qsort(names, n, sizeof *names, by_name);
    s_sets[set].names = names; /* kept, with the images */
    s_sets[set].img = SDL_calloc(n, sizeof *s_sets[set].img);
    s_sets[set].tex = SDL_calloc(n, sizeof *s_sets[set].tex);
    for (i = 0; i < n; i++) {
        SDL_Surface *png;

        SDL_snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if ((png = SDL_LoadPNG(path)) == NULL) {
            LOG("effects: %s: %s", path, SDL_GetError());
            break;
        }
        s_sets[set].img[i] = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
        SDL_DestroySurface(png);
        if (s_sets[set].img[i] == NULL) {
            break;
        }
    }
    s_sets[set].images = i; /* up to the first that failed */
    if (!read_json(set, dir)) {
        for (i = 0; i < s_sets[set].images && i < MAX_FRAMES; i++) {
            s_sets[set].image[i] = (short)i;
            s_sets[set].end[i] = (short)(i * 2 + 2);
        }
        s_sets[set].frames = i;
    }
    return set;
}

int native_effect_images(int effect, int critical, int *ticks) {
    int set = critical ? load_set(effect, 1) : -1;

    if (set < 0 || s_sets[set].frames == 0) {
        set = load_set(effect, 0);
    }
    if (set < 0 || s_sets[set].frames == 0) {
        return -1;
    }
    *ticks = s_sets[set].end[s_sets[set].frames - 1];
    return set;
}

/* Once a frame from the start: every effect folder of the mod is read the first time (the game's start, behind its
   logos) and its pictures put on the card one a frame. Decoding a big PNG, premultiplying it and uploading it take
   milliseconds, and were done in the frame an effect first showed: a hitch in each of the first frames of a signature
   move. An effect that plays before it is warm is made on the spot, as before. */
void native_effects_warm(void) {
    static int s_scanned, s_set, s_img;
    char dir[1100], sub[1200];

    if (!s_scanned) {
        char **names;
        int n = 0, i;

        s_scanned = 1;
        SDL_snprintf(dir, sizeof dir, "%s/mods/effects", data_dir());
        names = SDL_GlobDirectory(dir, "*", 0, &n);
        for (i = 0; names != NULL && i < n; i++) {
            SDL_PathInfo info;

            SDL_snprintf(sub, sizeof sub, "%s/%s", dir, names[i]);
            if (names[i][0] < '0' || names[i][0] > '9' || !SDL_GetPathInfo(sub, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
                continue;
            }
            load_set(atoi(names[i]), 0);
            SDL_snprintf(sub, sizeof sub, "%s/%s/critical", dir, names[i]);
            if (SDL_GetPathInfo(sub, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
                load_set(atoi(names[i]), 1);
            }
        }
        SDL_free(names);
        return;
    }
    for (; s_set < s_count; s_set++, s_img = 0) {
        for (; s_sets[s_set].img != NULL && s_img < s_sets[s_set].images; s_img++) {
            if (s_sets[s_set].tex[s_img] == 0 && s_sets[s_set].img[s_img] != NULL) {
                if (gl_image_prepare(s_sets[s_set].img[s_img], &s_sets[s_set].tex[s_img])) {
                    s_img++;
                }
                return; /* not up yet: the same one next frame */
            }
        }
    }
}

void native_effect_draw(unsigned key, float x, float y, float xs, int cx1, int cy1, int cx2, int cy2) {
    int set = key >> 12, tick = key & 0xFFF, f = 0;
    SDL_Surface *img;
    float w, h;

    if (set >= s_count || s_sets[set].frames == 0) {
        return;
    }
    while (f < s_sets[set].frames - 1 && tick >= s_sets[set].end[f]) {
        f++;
    }
    img = s_sets[set].img[s_sets[set].image[f]];
    if (gte_widescreen() && xs == 1) {
        xs = 0.75f; /* the 16:9 picture stretches the 2D: narrowed like the 3D, they keep their shape */
    }
    h = 240.0f * s_sets[set].scale;
    w = h * img->w / img->h * xs;
    x += s_sets[set].x * xs;
    y += s_sets[set].y;
    gl_image(img, &s_sets[set].tex[s_sets[set].image[f]], x - w / 2, y - h / 2, x + w / 2, y + h / 2, s_sets[set].add, cx1, cy1, cx2,
             cy2);
}
