/* The debug keys (native.ini: debug = 1), run at the frame's end:
     F1  the stage select (STAGSLCT), the developers' menu: stages, scenes,
         test battles
     F3  the sound test (SOUNDTST), F4 the vibration test (SHOCKTST): the
         European stage select has no lines for them
     F2  GAME (the save data and the mode) to game_<vsync>.bin, to compare
         with the recomp on the same save
     F5  savestate to saves/quick.state, F9 load it
   A savestate is the game's variables (main's and the overlays', one range:
   memory.c), the heap, the overlay areas, VRAM and the platform's console
   state (SAVED: GPU, GTE, SPU, sequencer). It belongs to the exe that wrote
   it: another build has other addresses, so it is refused. Not during a
   movie, whose stream is a host file. */
#include <SDL3/SDL.h>
#include <string.h>
#include "platform.h"
#include "../include/native.h"

extern uint8_t game_vars_begin[], ovl_sub_end[];
extern uint32_t HEAP_BASE[];
extern uint8_t OVERLAY_VRAM[], STAGE_VRAM[];
extern uint8_t GAME[];  /* the game's GameState */
int getMode(void);      /* main/game/state.c */
void requestMode(int mode, int arg);
int VSync(int mode);    /* etc.c */
void loadFont(void);    /* main/text/font.c */

#define GAME_DUMP_SIZE 0x26F0 /* up to its methods (EU) */
#define MODE_STAGE_SELECT 0x1500
#define MODE_SOUND_TEST 0x800     /* SOUNDTST */
#define MODE_VIBRATION_TEST 0x900 /* SHOCKTST */

typedef struct {
    char magic[4];
    uint32_t begin, size, heap; /* where and how big: the exe's fingerprint */
} Header;

static int s_key;

/* --field MODE [--progress N] (main.c): the field to jump to once the game
   reaches the language select or the title, as the stage select does, at
   that story point (GAME.progress) if given; for the field editor's Test */
int g_field_mode, g_field_progress = -1, g_field_event;
#define GAME_PROGRESS 0x263C
#define GAME_MODE 0x26C4 /* EU */
#define MODE_TITLE 0xE00
#define MODE_LANGUAGE 0x1600

void state_key(int key) {
    if (key == SDLK_F1 || key == SDLK_F2 || key == SDLK_F3 || key == SDLK_F4 || key == SDLK_F5 || key == SDLK_F9) {
        s_key = key;
    }
}

typedef struct {
    void *p;
    size_t n;
} Part;

/* The parts, in file order */
static int parts(Part list[5]) {
    list[0] = (Part){game_vars_begin, (size_t)(ovl_sub_end - game_vars_begin)};
    list[1] = (Part){HEAP_BASE, NATIVE_HEAP_SIZE};
    list[2] = (Part){OVERLAY_VRAM, 512 * 1024};
    list[3] = (Part){STAGE_VRAM, 512 * 1024};
    list[4] = (Part){g_vram, VRAM_W * VRAM_H * 2};
    return 5;
}

static void state_path(char *path, size_t n) {
    snprintf(path, n, "%s/../saves/quick.state", data_dir());
}

static Header header(void) {
    Header h = {{'D', 'W', '3', 'S'}, (uint32_t)(uintptr_t)game_vars_begin,
                (uint32_t)(ovl_sub_end - game_vars_begin), NATIVE_HEAP_SIZE};

    return h;
}

static void save_state(void) {
    char path[1200], tmp[1210];
    Header h = header();
    Part list[5];
    int i, n = parts(list);
    FILE *f;
    int ok;

    state_path(path, sizeof path);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if ((f = fopen(tmp, "wb")) == NULL) {
        LOG("state: can't write %s", tmp);
        return;
    }
    ok = fwrite(&h, sizeof h, 1, f) == 1;
    for (i = 0; i < n && ok; i++) {
        ok = fwrite(list[i].p, list[i].n, 1, f) == 1;
    }
    ok = fclose(f) == 0 && ok;
    if (ok && SDL_RenamePath(tmp, path)) { /* never a half-written state over a good one */
        LOG("state: saved %s", path);
    } else {
        LOG("state: saving %s failed", path);
        SDL_RemovePath(tmp);
    }
}

static void load_state(void) {
    char path[1200];
    Header h, want = header();
    Part list[5];
    int i, n = parts(list);
    FILE *f;
    long size = sizeof h;

    for (i = 0; i < n; i++) {
        size += (long)list[i].n;
    }
    state_path(path, sizeof path);
    if ((f = fopen(path, "rb")) == NULL) {
        LOG("state: no %s (F5 saves one)", path);
        return;
    }
    fseek(f, 0, SEEK_END);
    if (ftell(f) != size || fseek(f, 0, SEEK_SET) != 0 || fread(&h, sizeof h, 1, f) != 1 || memcmp(&h, &want, sizeof h) != 0) {
        LOG("state: %s is from another build, not loaded", path);
        fclose(f);
        return;
    }
    for (i = 0; i < n; i++) {
        if (fread(list[i].p, list[i].n, 1, f) != 1) {
            LOG("state: %s cut short", path); /* can't happen: the size was checked */
        }
    }
    fclose(f);
    spu_cd_clear();
    gl_sync(1);
    LOG("state: loaded %s", path);
}

static void dump_game(void) {
    char path[64];
    FILE *f;

    snprintf(path, sizeof path, "game_%05d.bin", VSync(-1));
    if ((f = fopen(path, "wb")) == NULL || fwrite(GAME, GAME_DUMP_SIZE, 1, f) != 1) {
        LOG("dump: can't write %s", path);
    } else {
        LOG("dump: %s, mode %04X", path, getMode());
    }
    if (f != NULL) {
        fclose(f);
    }
}

void state_frame(void) {
    int key = s_key;

    s_key = 0;
    if (g_field_mode != 0 && (getMode() >> 8 == MODE_TITLE >> 8 || getMode() >> 8 == MODE_LANGUAGE >> 8)) {
        int title = MODE_TITLE;

        LOG("field: mode %04X%s", g_field_mode, g_field_progress >= 0 ? ", with a story point" : "");
        if (getMode() >> 8 == MODE_LANGUAGE >> 8) {
            loadFont(); /* as F1: the language select wipes the font */
        }
        if (g_field_progress >= 0) {
            memcpy(GAME + GAME_PROGRESS, &g_field_progress, 4);
        }
        /* as if from the title: from another mode the field puts the player
           back where a field was left (GAME.fieldPos, 0, 0 here), not at
           its start (fieldstg/field.c startField) */
        memcpy(GAME + GAME_MODE, &title, 4);
        requestMode(g_field_mode, 0);
        g_field_mode = 0;
    }
    if (key == SDLK_F1 || key == SDLK_F3 || key == SDLK_F4) {
        int mode = key == SDLK_F1 ? MODE_STAGE_SELECT : key == SDLK_F3 ? MODE_SOUND_TEST : MODE_VIBRATION_TEST;

        LOG("debug: mode %04X (vsync %d, from mode %04X)", mode, VSync(-1), getMode());
        if (getMode() >> 8 == 0x16) {
            /* the language select wipes the font, which the next movie's end
               loads again (stdwtitl/movie.c): the same here */
            loadFont();
        }
        requestMode(mode, 0);
    } else if (key == SDLK_F2) {
        dump_game();
    } else if ((key == SDLK_F5 || key == SDLK_F9) && movie_playing()) {
        LOG("state: not during a movie");
    } else if (key == SDLK_F5) {
        save_state();
    } else if (key == SDLK_F9) {
        load_state();
    }
}
