/* The port's options that change how the game plays: what were mods of the
   PSX game (byte patches), as options of its own (native.ini, the options menu) */
#include <SDL3/SDL.h>
#include "platform.h"
#include "stage_tables.h"
#include "../include/native.h"

int g_fast_text = 1; /* fast_text = 0: X held doesn't advance the texts (native.h) */
int g_fast_travel = 0; /* fast_travel = 1: X on the map screen travels (ststatus/map_screen.c) */
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
