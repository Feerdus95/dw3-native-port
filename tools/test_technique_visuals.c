/* The technique visuals map (platform/technique_visuals.c): a valid technique_visuals.ini is read, a bad one is rejected whole
   (every value back to the original), and the shipped config/technique_visuals.ini loads. Run by tools/run_tests.py. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "native.h"

void native_log(const char *fmt, ...) {}

/* the map's text written beside the exe (where the loader reads it), then loaded */
static int load(const char *text) {
    char path[1200];
    FILE *f;

    snprintf(path, sizeof path, "%stechnique_visuals.ini", SDL_GetBasePath());
    f = fopen(path, "wb");
    assert(f != NULL);
    fputs(text, f);
    fclose(f);
    return native_visual_load();
}

int main(void) {
    char path[1200];

    assert(load("# comment\n[technique.0x89]\nlaunch_timing = aligned\nprojectile_model = 331\nlaunch_sound = none\n"
                "launch_sound_duration = 12\n[technique.5]\n") == 1);
    assert(native_visual_get(0x89, NATIVE_VIS_PROJECTILE, 7) == 331);
    assert(native_visual_get(0x89, NATIVE_VIS_LAUNCH_SOUND, 7) == -1);
    assert(native_visual_get(0x89, NATIVE_VIS_LAUNCH_SOUND_DURATION, 7) == 12);
    assert(native_visual_get(0x89, NATIVE_VIS_LAUNCH_TIMING, 0) == NATIVE_LAUNCH_ALIGNED);
    assert(native_visual_get(0x89, NATIVE_VIS_STAGE, 7) == 7); /* omitted: the original */
    assert(native_visual_get(5, NATIVE_VIS_PROJECTILE, 7) == 7); /* empty section: all original */
    assert(native_visual_get(0, NATIVE_VIS_PROJECTILE, 7) == 7 && native_visual_get(0xB8, NATIVE_VIS_PROJECTILE, 7) == 7);

    /* each of these is rejected whole: the first section's values go back to the original too */
    assert(load("[technique.1]\nstage = 3\n[technique.1]\n") == 0);                     /* a section twice */
    assert(native_visual_get(1, NATIVE_VIS_STAGE, 7) == 7);
    assert(load("[technique.1]\nstage = 3\nstage = 4\n") == 0);                        /* a key twice */
    assert(load("[technique.1]\nnope = 3\n") == 0);                                    /* an unknown key */
    assert(load("stage = 3\n") == 0);                                                  /* a key before any section */
    assert(load("[technique.0]\n") == 0 && load("[technique.0xB8]\n") == 0);           /* technique out of range */
    assert(load("[technique.1]\nstage = 32768\n") == 0 && load("[technique.1]\nstage = -1\n") == 0);
    assert(load("[technique.1]\nlaunch_timing = soon\n") == 0);
    assert(load("[technique.1]\nlaunch_sound_duration = none\n") == 0);                /* none has no meaning for a duration */
    assert(load("[technique.1]\nstage = 3 4\n") == 0);
    assert(native_visual_get(1, NATIVE_VIS_STAGE, 7) == 7);

    /* the map the build ships (run_tests.py copies it here) */
    snprintf(path, sizeof path, "%sshipped_technique_visuals.ini", SDL_GetBasePath());
    {
        FILE *f = fopen(path, "rb");
        char text[65536];
        size_t n;

        assert(f != NULL);
        n = fread(text, 1, sizeof text - 1, f);
        fclose(f);
        text[n] = 0;
        assert(n > 0 && n < sizeof text - 1);
        assert(load(text) == 1);
    }
    printf("ok\n");
    return 0;
}
