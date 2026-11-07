/* Which folder of data/mods/effects an effect plays (platform/effects.c), by stage and critical hit, and the sound library (soundlib.c), on a tree it makes beside
   the exe. Run from native/ (with SDL3.dll beside the exe):
   i686-w64-mingw32-clang -Iplatform -Iinclude -I<SDL>/include tools/test_effect_stages.c platform/effects.c platform/clips.c platform/soundlib.c platform/json.c -L<SDL>/lib -lSDL3 -o build/test_effect_stages.exe
   build/test_effect_stages.exe */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

int native_effect_images(int effect, int critical, int stage, int *ticks);
int native_fighter_stage(int fighter);
int native_sound_replace(int index);
void native_sfx_mix(int16_t *buf, int frames);

static char s_root[512];
const char *data_dir(void) { return s_root; }
int g_log;
int g_render_sounds;
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

/* a folder with a sound only: a 0.1 s mono 22.05 kHz WAV of a constant 1000 */
static void sound(const char *dir) {
    unsigned char w[44 + 2205 * 2] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x22, 0x56, 0, 0,
                                      0x44, 0xAC, 0, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0x3A, 0x11, 0, 0};
    char p[800];
    int i;

    for (i = 0; i < 2205; i++) {
        w[44 + i * 2] = 1000 & 0xFF;
        w[45 + i * 2] = 1000 >> 8;
    }
    SDL_snprintf(p, sizeof p, "%s/%s", s_root, dir);
    assert(SDL_CreateDirectory(p));
    SDL_snprintf(p, sizeof p, "%s/hit.wav", dir);
    SDL_snprintf(p, sizeof p, "%s/%s/hit.wav", s_root, dir);
    assert(SDL_SaveFile(p, w, sizeof w));
    SDL_snprintf(p, sizeof p, "%s/effect.json", dir);
    put(p, "{\"sound\": {\"file\": \"hit.wav\", \"volume\": 0.5}}");
}

/* the mix of one vsync's worth of frames: the left sample of its frame 1000 */
static int mixed(void) {
    static int16_t buf[1470 * 2];

    memset(buf, 0, sizeof buf);
    native_sfx_mix(buf, 1470);
    return buf[1000 * 2];
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
    sound("mods/effects/6");
    {
        char d[800];

        SDL_snprintf(d, sizeof d, "%s/mods/effects/7", s_root);
        assert(SDL_CreateDirectory(d));
        SDL_snprintf(d, sizeof d, "%s/mods/effects/8", s_root);
        assert(SDL_CreateDirectory(d));
    }
    put("mods/effects/7/effect.json", "{\"sound\": {\"file\": \"nope.wav\"}}"); /* no such file */
    put("mods/effects/8/effect.json", "{\"sound\": {\"file\": \"bad.wav\"}}");
    put("mods/effects/8/bad.wav", "not a wav"); /* read when it is needed */
    sound("mods/sounds"); /* hit.wav, 0.1 s of 1000 */
    put("mods/sounds/sounds.json",
        "{\"sounds\": {\"punch\": {\"file\": \"hit.wav\", \"volume\": 0.5}, \"Bad\": {\"file\": \"hit.wav\"}, \"gone\": {\"file\": \"nope.wav\"}, "
        "\"esc\": {\"file\": \"../x.wav\"}}, \"replace\": {\"57\": \"punch\", \"0\": \"punch\", \"999\": \"punch\", \"58\": \"gone\", \"59\": \"nobody\", \"60\": \"Bad\"}}");
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

    /* the options: effects and sounds switch while the exe runs, each at its own start */
    assert(played(6, 0, 0) == 0 && abs(mixed() - 500) < 20); /* a sound only: no images, a sound at half volume (22.05 -> 44.1 kHz) */
    assert(abs(mixed() - 500) < 20 && abs(mixed() - 500) < 20 && mixed() == 0); /* 0.1 s is 3 vsyncs of 1470 frames, then it ends */
    assert(native_effect_has_sound(6, 0, 0) && !native_effect_has_sound(2, 0, 0) && !native_effect_has_sound(7, 0, 0));
    assert(native_effect_has_sound(8, 0, 0)); /* until it is read */
    played(8, 0, 0);
    assert(!native_effect_has_sound(8, 0, 0) && mixed() == 0); /* unreadable: the game's sound plays */
    g_custom_sounds = 0;
    assert(!native_effect_has_sound(6, 0, 0)); /* off: the game's hit sound plays */
    assert(played(6, 0, 0) == 0 && mixed() == 0);
    g_custom_sounds = 1;
    g_custom_effects = 0;
    assert(played(2, 0, 0) == 0 && played(3, 0, 2) == 0); /* the game's own images */
    g_custom_effects = 1;
    assert(played(2, 0, 0) == 10);

    /* the sound library: a replaced battle sound plays the library's WAV at its volume, the rest are the game's */
    assert(native_sound_replace(57) == 1 && abs(mixed() - 500) < 20);
    assert(abs(mixed() - 500) < 20 && abs(mixed() - 500) < 20 && mixed() == 0);
    assert(native_sound_replace(56) == 0 && native_sound_replace(0) == 0 && native_sound_replace(200) == 0); /* not replaced, nor a sound of the game */
    /* a script mod names a library sound by id (0x100 and up): it plays, and is only silent with Custom sounds off */
    assert(native_sound_id("punch") == 0x100 && native_sound_id("gone") == -1);
    assert(native_sound_replace(58) == 0 && native_sound_replace(59) == 0 && native_sound_replace(60) == 0); /* no file, no such name, a bad name */
    g_custom_sounds = 0;
    assert(native_sound_replace(57) == 0 && mixed() == 0); /* off: the game's sound */
    g_custom_sounds = 1;
    g_render_sounds = 1;
    assert(native_sound_replace(57) == 0); /* --render-sounds writes the game's own */
    g_render_sounds = 0;
    assert(native_sound_replace(57) == 1);
    g_custom_sounds = 0;
    assert(native_sound_replace(0x100) == 1); /* a script mod's sound is silent, the game has none in its place */
    g_custom_sounds = 1;
    puts("ok");
    return 0;
}
