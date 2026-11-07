/* Effects' own images (native.h): data/mods/effects/<id>/ (PNGs) drawn in a
   battle's 2D effect's place (fightstg/sprite_effect.c), as the model viewer
   exports an effect: the 320x240 screen around the effect's point, at any
   size of the same shape (bigger is finer). A critical hit's are in critical/
   there, a stage's in rookie/, champion/, ultimate/ or mega/ (with a critical/
   of its own); stages.json beside the effects says which Digimon is what:
     {"rookie": [3, 145], "champion": [5], ...}
   Each folder's effect.json, which the model viewer's Effects tab writes, says
   how its images play:
     {"frames": [["00.png", 2], ...], "x": 0, "y": 0, "scale": 1, "blend": "alpha"}
   each frame a PNG and its ticks (1/60 s, as the battle counts), the images
   moved by (x, y) screen pixels and scaled, blended by their alpha or added
   ("add"); "active": 0 leaves the folder out. Without it, every PNG in their
   names' order, 2 ticks each. A folder may also carry a sound, played when the
   effect starts (a folder with only that has no PNGs):
     "sound": {"file": "hit.wav", "volume": 1}
   a WAV of the folder, in place of the game's hit sound of the attack (the
   attack script leaves it out, but for a hit of result 3: native_effect_has_sound).
   The WAVs are read after the images, one a frame (or at their first use). The folders are read
   once, at the start; g_custom_effects and g_custom_sounds (the options) are
   looked at each time an effect starts, so they switch while playing. */
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

#define MAX_SETS 256
#define MAX_FRAMES 512
#define MAX_STAGED 512
static const char *const STAGES[] = {"", "rookie", "champion", "ultimate", "mega"};
#define STAGE_COUNT 5
int g_custom_effects = 1;
int g_custom_sounds = 1;
static struct {
    int effect, critical, stage;
    int images; /* PNGs, in their names' order */
    char **names;
    SDL_Surface **img;
    unsigned *tex;
    int frames; /* what plays: an image each, until its tick */
    short image[MAX_FRAMES], end[MAX_FRAMES];
    float x, y, scale;
    int add;
    NativeClip snd; /* its sound, if it has one (clips.c) */
} s_sets[MAX_SETS];
static int s_count;
static struct {
    short fighter, stage;
} s_staged[MAX_STAGED];
static int s_staged_count;

static int by_name(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* FILE (a WAV in DIR) is SET's sound: only its path and volume are kept (VOL is the JSON's volume or NULL), the WAV is
   read and converted when first needed (native_clip_ready) */
static void load_sound(int set, const char *dir, const char *file, const Json *vol) {
    char path[1200];

    if (strchr(file, '/') != NULL || strchr(file, '\\') != NULL || strstr(file, "..") != NULL) {
        LOG("effects: %s: sound %s is not a file of the folder", dir, file);
        return;
    }
    SDL_snprintf(path, sizeof path, "%s/%s", dir, file);
    if (!SDL_GetPathInfo(path, NULL)) {
        LOG("effects: %s: no such sound", path);
        return;
    }
    native_clip_set(&s_sets[set].snd, path, vol != NULL && vol->type == 'n' && vol->num >= 0 && vol->num <= 4 ? (int)(vol->num * 256) : 256);
}

/* The folder's effect.json, into SET's frames; 0 without one */
static int read_json(int set, const char *dir) {
    char path[1200];
    JsonInfo ji;
    Json *root, *frames, *v;
    int i, k, tick = 0;

    SDL_snprintf(path, sizeof path, "%s/effect.json", dir);
    if ((root = json_load(path, JSON_LIMITS_STAGE, &ji)) == NULL) {
        if (!ji.unopened) {
            LOG("effects: %s: %s (line %d)", path, ji.msg, ji.line);
        }
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
    if ((v = json_get(root, "sound")) != NULL && (v = json_get(v, "file")) != NULL && v->type == 's') {
        load_sound(set, dir, v->str, json_get(json_get(root, "sound"), "volume"));
    }
    if ((v = json_get(root, "active")) != NULL && v->type == 'n' && v->num == 0) {
        s_sets[set].frames = 0;
        native_clip_clear(&s_sets[set].snd);
    }
    json_free(root);
    return 1;
}

static int find_set(int effect, int stage, int critical) {
    int i;

    for (i = 0; i < s_count; i++) {
        if (s_sets[i].effect == effect && s_sets[i].stage == stage && s_sets[i].critical == critical) {
            return i;
        }
    }
    return -1;
}

/* Loads the set of EFFECT's folder for STAGE (critical/ in it when CRITICAL), if it has PNGs */
static void load_set(int effect, int stage, int critical) {
    char dir[1024], path[1200];
    char **names;
    int i, n = 0, set;

    SDL_snprintf(dir, sizeof dir, "%s/mods/effects/%d%s%s%s", data_dir(), effect, stage ? "/" : "", STAGES[stage], critical ? "/critical" : "");
    names = SDL_GlobDirectory(dir, "*.png", SDL_GLOB_CASEINSENSITIVE, &n);
    SDL_snprintf(path, sizeof path, "%s/effect.json", dir);
    if (names == NULL || (n == 0 && !SDL_GetPathInfo(path, NULL))) { /* no PNGs, nor a sound's folder */
        SDL_free(names);
        return;
    }
    if (s_count == MAX_SETS) {
        LOG("effects: more than %d folders of images, %s is left out", MAX_SETS, dir);
        SDL_free(names);
        return;
    }
    set = s_count++;
    s_sets[set].effect = effect;
    s_sets[set].stage = stage;
    s_sets[set].critical = critical;
    s_sets[set].scale = 1;
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
}

/* stages.json, into s_staged */
static void read_stages(void) {
    char path[1100];
    JsonInfo ji;
    Json *root;
    int s, i;

    SDL_snprintf(path, sizeof path, "%s/mods/effects/stages.json", data_dir());
    if ((root = json_load(path, JSON_LIMITS_STAGE, &ji)) == NULL) {
        if (!ji.unopened) {
            LOG("effects: %s: %s (line %d)", path, ji.msg, ji.line);
        }
        return;
    }
    for (s = 1; s < STAGE_COUNT; s++) {
        Json *ids = json_get(root, STAGES[s]);

        for (i = 0; i < json_len(ids) && s_staged_count < MAX_STAGED; i++) {
            s_staged[s_staged_count].fighter = (short)json_int(json_at(ids, i));
            s_staged[s_staged_count++].stage = (short)s;
        }
    }
    json_free(root);
}

/* Every effect folder of the mod, the first time anything asks: an effect or a stage it doesn't have costs no disk after that */
static void scan_mods(void) {
    static int s_scanned;
    char dir[1100];
    char **names;
    int n = 0, i, stage;

    if (s_scanned) {
        return;
    }
    s_scanned = 1;
    SDL_snprintf(dir, sizeof dir, "%s/mods/effects", data_dir());
    names = SDL_GlobDirectory(dir, "*", 0, &n);
    for (i = 0; names != NULL && i < n; i++) {
        if (names[i][0] < '0' || names[i][0] > '9') {
            continue;
        }
        for (stage = 0; stage < STAGE_COUNT; stage++) {
            load_set(atoi(names[i]), stage, 0);
            load_set(atoi(names[i]), stage, 1);
        }
    }
    SDL_free(names);
    read_stages();
}

int native_fighter_stage(int fighter) {
    int i;

    scan_mods();
    for (i = 0; i < s_staged_count; i++) {
        if (s_staged[i].fighter == fighter) {
            return s_staged[i].stage;
        }
    }
    return 0;
}

/* STAGE's set, else (no folder, or one left out) the nearest lower stage's and at last the base one: in each, a critical hit's
   if it is there, else the plain one. A set counts when it has images, or a sound if SOUND */
static int pick_set(int effect, int critical, int stage, int sound) {
    int s, c;

    for (s = stage >= 0 && stage < STAGE_COUNT ? stage : 0; s >= 0; s--) {
        for (c = critical != 0; c >= 0; c--) {
            int set = find_set(effect, s, c);

            if (set >= 0 && (sound ? s_sets[set].snd.path != NULL : s_sets[set].frames > 0)) {
                return set;
            }
        }
    }
    return -1;
}

/* Whether the effect plays a sound of its own: the game's hit sound is then left out (battle_script.c) */
int native_effect_has_sound(int effect, int critical, int stage) {
    scan_mods();
    return g_custom_sounds && pick_set(effect, critical, stage, 1) >= 0;
}

/* An effect starts: its set's sound plays (g_custom_sounds), and its images are returned (g_custom_effects), else -1 */
int native_effect_images(int effect, int critical, int stage, int *ticks) {
    int set;

    scan_mods();
    if (g_custom_sounds && (set = pick_set(effect, critical, stage, 1)) >= 0) {
        native_clip_play(&s_sets[set].snd);
    }
    if (!g_custom_effects || (set = pick_set(effect, critical, stage, 0)) < 0) {
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
    static int s_set, s_img, s_snd;

    scan_mods();
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
    for (; s_snd < s_count; s_snd++) { /* the images are all up: the sounds, one a frame */
        if (s_sets[s_snd].snd.path != NULL && s_sets[s_snd].snd.data == NULL) {
            native_clip_ready(&s_sets[s_snd++].snd);
            return;
        }
    }
    native_soundlib_warm();
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
