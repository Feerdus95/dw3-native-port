/* Which folder of data/mods/effects an effect plays (platform/effects.c), by stage and critical hit, on a tree it makes beside
   the exe. Run from native/ (with SDL3.dll beside the exe):
   i686-w64-mingw32-clang -Iplatform -Iinclude -I<SDL>/include tools/test_effect_stages.c platform/effects.c -L<SDL>/lib -lSDL3 -o build/test_effect_stages.exe
   build/test_effect_stages.exe */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

int native_effect_images(int effect, int critical, int stage, int *ticks);
int native_fighter_stage(int fighter);

static char s_root[512];
const char *data_dir(void) { return s_root; }
int g_log;
void native_log(const char *fmt, ...) {}
int gte_widescreen(void) { return 0; }
int gl_image_prepare(const struct SDL_Surface *s, unsigned *tex) { *tex = 1; return 1; }
void gl_image(const struct SDL_Surface *s, unsigned *tex, float x1, float y1, float x2, float y2, int add, int cx1, int cy1, int cx2, int cy2) {}

/* just what effects.c asks of the port's JSON: this test's files are written below, with no strings in the lists */
static const char *skip(const char *p) { while (*p == ' ' || *p == '\n') p++; return p; }
Json *json_value(const char **pp) {
    const char *p = skip(*pp);
    Json *j = calloc(1, sizeof *j), **tail = &j->child;

    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';

        j->type = *p++;
        for (p = skip(p); *p != close;) {
            const char *key = NULL;
            Json *item;
            size_t len = 0;

            if (j->type == '{') {
                key = ++p;
                len = strchr(p, '"') - p;
                p = skip(key + len + 1) + 1;
            }
            *pp = p;
            item = json_value(pp);
            p = skip(*pp);
            if (key) item->key = SDL_strndup(key, len);
            *tail = item, tail = &item->next;
            if (*p == ',') p = skip(p + 1);
        }
        *pp = p + 1;
    } else if (*p == '"') {
        const char *end = strchr(p + 1, '"');

        j->type = 's', j->str = SDL_strndup(p + 1, end - p - 1), *pp = end + 1;
    } else {
        char *end;

        j->type = 'n', j->num = strtod(p, &end), *pp = end;
    }
    return j;
}
void json_free(Json *j) {}
Json *json_get(Json *o, const char *k) {
    Json *c;

    for (c = o ? o->child : NULL; c; c = c->next) if (c->key && strcmp(c->key, k) == 0) return c;
    return NULL;
}
Json *json_at(Json *a, int i) {
    Json *c = a ? a->child : NULL;

    while (c && i-- > 0) c = c->next;
    return c;
}
int json_len(Json *a) {
    int n = 0;
    Json *c;

    for (c = a ? a->child : NULL; c; c = c->next) n++;
    return n;
}
int json_int(Json *j) { return j ? (int)j->num : 0; }

static void put(const char *file, const char *text) {
    char p[800];

    SDL_snprintf(p, sizeof p, "%s/%s", s_root, file);
    assert(SDL_SaveFile(p, text, strlen(text)));
}

/* a folder with one PNG that plays TICKS ticks, so the ticks tell which folder played */
static void art(const char *dir, int ticks, const char *extra) {
    char p[800], j[200];
    SDL_Surface *s = SDL_CreateSurface(4, 3, SDL_PIXELFORMAT_RGBA32);

    SDL_snprintf(p, sizeof p, "%s/%s", s_root, dir);
    assert(SDL_CreateDirectory(p));
    SDL_snprintf(p, sizeof p, "%s/%s/a.png", s_root, dir);
    assert(SDL_SavePNG(s, p));
    SDL_DestroySurface(s);
    SDL_snprintf(p, sizeof p, "%s/effect.json", dir);
    SDL_snprintf(j, sizeof j, "{\"frames\": [[\"a.png\", %d]]%s}", ticks, extra);
    put(p, j);
}

static int played(int effect, int critical, int stage) {
    int ticks = 0;

    return native_effect_images(effect, critical, stage, &ticks) < 0 ? 0 : ticks;
}

int main(void) {
    SDL_snprintf(s_root, sizeof s_root, "%sdw3_test_effect_stages", SDL_GetBasePath());
    SDL_RemovePath(s_root); /* a tree of an earlier run stays: it is rewritten */
    art("mods/effects/2", 10, "");
    art("mods/effects/2/critical", 11, "");
    art("mods/effects/2/champion", 20, "");
    art("mods/effects/2/ultimate/critical", 31, "");
    art("mods/effects/3/champion", 40, "");
    art("mods/effects/4", 50, "");
    art("mods/effects/4/champion", 60, ", \"active\": 0");
    put("mods/effects/stages.json", "{\"rookie\": [3], \"champion\": [5, 19]}");

    assert(native_fighter_stage(3) == 1 && native_fighter_stage(5) == 2 && native_fighter_stage(19) == 2);
    assert(native_fighter_stage(999) == 0);

    assert(played(2, 0, 0) == 10 && played(2, 1, 0) == 11); /* as ever */
    assert(played(2, 0, 1) == 10 && played(2, 1, 1) == 11); /* no rookie/: a rookie gets the base mod */
    assert(played(2, 0, 2) == 20);
    assert(played(2, 1, 2) == 20); /* no champion/critical/: the champion's plain ones, not a rookie's critical */
    assert(played(2, 0, 3) == 20); /* ultimate/ has only critical/: the plain ones are the champion's */
    assert(played(2, 1, 3) == 31);
    assert(played(2, 0, 4) == 20 && played(2, 1, 4) == 31); /* no mega/: the nearest lower stage */
    assert(played(3, 0, 0) == 0 && played(3, 0, 1) == 0); /* no base mod: the original, for those under its stage */
    assert(played(3, 0, 2) == 40);
    assert(played(4, 0, 2) == 50); /* "active": 0 leaves it out */
    assert(played(5, 0, 2) == 0 && played(2, 0, 99) == 10);
    puts("ok");
    return 0;
}
