/* The port's options that change how the game plays: what were mods of the
   PSX game (byte patches), as options of its own (native.ini, the options menu) */
#include <SDL3/SDL.h>
#include "platform.h"

int g_fast_text = 1; /* fast_text = 0: X held doesn't advance the texts (native.h) */
int g_ff_speed = 1;  /* fast forward: the game's ticks a frame shown (etc.c); not saved */
int g_bladegarurumon = 1; /* bladegarurumon = 0: partners don't learn it (stfgtrep's growth.c) */

/* The files options make anew (files.c): the lobby's Veemon (packs.c),
   BladeGarurumon's model and the fighters' file (bladegaru.c) */
const uint8_t *made_file(int fid, long *size) {
    const uint8_t *made = packs_file(fid, size);

    return made != NULL ? made : bladegaru_file(fid, size);
}

/* Tab or R3: x1, x2, x3, x5 and round */
void ff_cycle(void) {
    static const int SPEEDS[] = {1, 2, 3, 5};
    int i = 0;

    while (i < 3 && SPEEDS[i] != g_ff_speed) {
        i++;
    }
    g_ff_speed = SPEEDS[(i + 1) % 4];
}
