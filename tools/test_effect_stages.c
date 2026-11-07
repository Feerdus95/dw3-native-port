/* Which folder of data/mods/effects an effect plays (platform/effects.c), by stage and critical hit, on a tree it makes beside
   the exe. Run from native/ (with SDL3.dll beside the exe):
   i686-w64-mingw32-clang -Iplatform -Iinclude -I<SDL>/include tools/test_effect_stages.c platform/effects.c platform/json.c -L<SDL>/lib -lSDL3 -o build/test_effect_stages.exe
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

/* the port's JSON reader is linked in (platform/json.c) */

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
