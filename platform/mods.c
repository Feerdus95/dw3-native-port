/* The port's options that change how the game plays: what were mods of the
   PSX game (byte patches), as options of its own (native.ini, the options menu) */
#include <SDL3/SDL.h>
#include "platform.h"
#include "stage_tables.h"
#include "../include/native.h"

int g_fast_text = 1; /* fast_text = 0: X held doesn't advance the texts (native.h) */
int g_fast_travel = 0; /* fast_travel = 1: X on the map screen travels (ststatus/map_screen.c) */
int g_map_objective = 0; /* map_objective = 1: the map blinks the story's next area (ststatus/map_screen.c) */
int g_journal = 0; /* journal = 1: the field menu's journal row (ststatus/item_list.c) */
int g_ff_speed = 1;  /* fast forward: the game's ticks a frame shown (etc.c); not saved */
int g_give_form; /* --give ID (main.c) */
int g_dna_other; /* native.h */
/* the finale of the fusion: Flamedramon's attack K (script 21, 28 or 29 of its file: dw2-extractor/flame_dcb.py dna_body) without its end, or NULL */
static uint32_t rd32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static const short *flame_finale(int k, int *n) {
    static uint8_t *f;
    static long size = -1;
    uint32_t subs, a, c, from, to;

    if (size < 0) {
        size = native_read_raw(NATIVE_FLAMEDRAMON_FID, &f);
    }
    if (size < 4 || (subs = rd32(f) / 4) < 42 || rd32(f + 4 * 41) >= size) {
        return NULL;
    }
    a = rd32(f + 4 * 41); /* the scripts' archive: a table of offsets, then the scripts */
    c = rd32(f + a) / 4;
    if (k >= c) {
        return NULL;
    }
    from = a + rd32(f + a + 4 * k);
    to = k + 1 < c ? a + rd32(f + a + 4 * (k + 1)) : (uint32_t)size;
    for (*n = (int)(to - from) / 2; *n > 0 && ((const short *)(f + from))[*n - 1] == 0; (*n)--) {
    }
    if (*n < 2 || ((const short *)(f + from))[*n - 1] != 255) {
        return NULL;
    }
    (*n)--; /* its own end */
    return (const short *)(f + from);
}

/* script 12 of a pair's fighter (the game's DNA fusion) with the other partner and Flamedramon as the DNA form; the form's attack
   (the commands from its motion 56 to the stage's return) is one of Flamedramon's three, at the random number RND */
short *native_dna_script(const short *pc, int other, unsigned rnd) {
    static short v[1024], w[2048];
    const short *fin;
    int i, k = -1, tail = -1, end = -1, n = 0, m = 0;

    memcpy(v, pc, sizeof v); /* script 12 is a few hundred words and has the archive's other scripts after it */
    for (i = 0; i < 60; i++) { /* "model add" (2, 6, who, fighter): the partner is model 2, the DNA form 3 */
        if (v[i] == 2 && v[i + 1] == 6 && (v[i + 2] == 2 || v[i + 2] == 3)) {
            v[i + 3] = (short)(v[i + 2] == 2 ? other : NATIVE_FLAMEDRAMON);
        }
    }
    for (i = 0; i < 1000 && k < 0; i++) { /* the DNA form's attack: motion 56 */
        k = v[i] == 2 && v[i + 1] == 0 && v[i + 2] == 3 && v[i + 3] == 56 ? i : -1;
    }
    for (i = k + 4; k >= 0 && i < 1000 && tail < 0; i++) { /* the stage's return (4, 0, 0) and the script's end (255) */
        tail = v[i] == 4 && v[i + 1] == 0 && v[i + 2] == 0 ? i : -1;
    }
    for (i = tail; tail >= 0 && i < 1020 && end < 0; i++) {
        end = v[i] == 255 ? i : -1;
    }
    fin = end >= 0 ? flame_finale(rnd % 3 == 0 ? 21 : rnd % 3 == 1 ? 28 : 29, &n) : NULL;
    if (fin == NULL) { /* no finale: the old way, only its motion */
        for (i = 0; k >= 0 && i < 1; i++) {
            v[k + 3] = (short)(rnd % 3 == 0 ? 50 : rnd % 3 == 1 ? 62 : 63);
        }
        return v;
    }
    memcpy(w, v, k * sizeof *w);
    memcpy(w + k, fin, n * sizeof *w);
    memcpy(w + k + n, v + tail, (end + 1 - tail) * sizeof *w);
    m = k + n + end + 1 - tail;
    (void)m;
    return w;
}
int g_flamedramon = 1; /* flamedramon = 0: partners don't learn it (stfgtrep's growth.c) */
int g_bladegarurumon = 1; /* bladegarurumon = 0: partners don't learn it (stfgtrep's growth.c) */

/* The files options make anew (files.c): the lobby's Veemon (packs.c),
   BladeGarurumon's model and the fighters' file (bladegaru.c) */
const uint8_t *made_file(int fid, long *size) {
    const uint8_t *made = packs_file(fid, size);

    return made != NULL ? made : bladegaru_file(fid, size);
}

int VSync(int mode);

/* Fast text's buttons (native.h): those of HELD down 20 vsyncs or more
   without a break; a gap of more than a few vsyncs since the last call
   starts the count again */
int native_long_held(int held) {
    static int last = -1, out;
    static int count[16];
    int now = VSync(-1), i;

    if (now != last) {
        int step = now - last;

        out = 0;
        for (i = 0; i < 16; i++) {
            count[i] = (held >> i & 1) && step > 0 && step <= 4 ? SDL_min(count[i] + step, 1000) : 0;
            out |= count[i] >= 20 ? 1 << i : 0;
        }
        last = now;
    }
    return out;
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

/* Fast travel's doors: every stage's exits (SLOT_EXIT), each with a mode of
   the stage it is in, and where each mode's stage starts the player. Read
   once before the game runs, from each stage's setup as the field runs it
   (the field editor's changes too), its variables put back after */
#define SLOT_EXIT 1
#define FIRST_FIELD 0x200
#define FIELDS 0xD7 /* 0x200 to 0x2D6, the map's fields */
static NativeExit *s_exits;
static int s_exit_count;
static struct {
    int set, x, y, dir;
} s_starts[FIELDS];

static const StageFuncs *stage_of(void *init) {
    for (const StageFuncs *s = native_stage_list; s->name != NULL; s++) {
        if (s->start == init) {
            return s;
        }
    }
    return NULL;
}

/* each mode of the European table (FIELDSTG_pickStage's, outside the extra
   game) set up as FIELDSTG does: its stage picked, its setup, then the field
   editor's changes (--at kept for the game) */
void native_fast_travel_setup(void) {
    FieldState *st = &FIELDSTG_state;
    int room = 0, at_x = g_field_at_x;

    for (const StageEntry *e = FIELDSTG_euStages; e->mode != 0; e++) {
        const StageFuncs *s = stage_of(e->init);
        int i = e->mode - FIRST_FIELD;

        if (s == NULL || i < 0 || i >= FIELDS) {
            continue;
        }
        native_reset_overlays(1);
        memset(st, 0, 0x64);
        memset(&FIELDSTG_map, 0, sizeof FIELDSTG_map);
        st->stageFile = e->file, st->stageInit = e->init;
        s->setup();
        g_field_at_x = -1;
        native_apply_stage_mod();
        s_starts[i].set = 1, s_starts[i].x = st->startX, s_starts[i].y = st->startY, s_starts[i].dir = st->startDir;
        for (StageSlot *slot = st->slots; slot != NULL && slot->type != 0; slot++) {
            if (slot->type != SLOT_EXIT) {
                continue;
            }
            if (s_exit_count == room) {
                room = room ? room * 2 : 256;
                s_exits = SDL_realloc(s_exits, room * sizeof *s_exits);
            }
            s_exits[s_exit_count++] = (NativeExit){e->mode, slot->arg, slot->x, slot->y, slot->dir, slot->place, slot->placeArg,
                                                   {{slot->conditions[0][0], slot->conditions[0][1]},
                                                    {slot->conditions[1][0], slot->conditions[1][1]}}};
        }
    }
    g_field_at_x = at_x;
    LOG("fast travel: %d doors", s_exit_count);
    native_reset_overlays(0);
    native_reset_overlays(1);
}

const NativeExit *native_exits(int *count) {
    *count = s_exit_count;
    return s_exits;
}

int native_stage_start(int mode, int *x, int *y, int *dir) {
    if (mode < FIRST_FIELD || mode >= FIRST_FIELD + FIELDS || !s_starts[mode - FIRST_FIELD].set) {
        return 0;
    }
    *x = s_starts[mode - FIRST_FIELD].x, *y = s_starts[mode - FIRST_FIELD].y, *dir = s_starts[mode - FIRST_FIELD].dir;
    return 1;
}
