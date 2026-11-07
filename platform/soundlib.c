/* The sound library (native.h): data/mods/sounds/sounds.json names WAVs of the same folder and says which of the game's battle
   sounds each replaces:
     {"sounds": {"punch": {"file": "punch.wav", "volume": 1}}, "replace": {"57": "punch"}}
   A name is 1 to 32 characters of a-z 0-9 _ -; a file is a plain file name of the folder; the volume is 0 to 4. A replaced sound
   (FIGHTSTG_battleSounds' index, 1 to 0x59) plays the WAV wherever the game would have played it (battle_sound.c). The folder is
   read the first time anything asks; each WAV is read when it first plays, or in native_soundlib_warm, a frame at a time, for
   the ones that replace a sound. g_custom_sounds is looked at each time a sound plays, so it switches while playing. The sounds
   --render-sounds writes are the game's own, so it never replaces. */
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

#define MAX_LIB 128
#define MAX_INDEX 0x5A

static struct {
    char name[40];
    NativeClip clip;
} s_lib[MAX_LIB];
static int s_lib_count;
static short s_replace[MAX_INDEX]; /* 1 + the library entry that replaces the sound, 0 for none */
static char s_used[MAX_LIB];       /* a sound that replaces one, or that a script mod names: read ahead (native_soundlib_warm) */

static int valid_name(const char *name) {
    size_t i, n = strlen(name);

    for (i = 0; i < n; i++) {
        if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= '0' && name[i] <= '9') || name[i] == '_' || name[i] == '-')) {
            return 0;
        }
    }
    return n >= 1 && n <= 32;
}

static int find_name(const char *name) {
    int i;

    for (i = 0; i < s_lib_count; i++) {
        if (strcmp(s_lib[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void read_library(void) {
    char path[1200];
    JsonInfo ji;
    Json *root, *m;

    SDL_snprintf(path, sizeof path, "%s/mods/sounds/sounds.json", data_dir());
    if ((root = json_load(path, JSON_LIMITS_STAGE, &ji)) == NULL) {
        if (!ji.unopened) {
            LOG("sounds: %s: %s (line %d)", path, ji.msg, ji.line);
        }
        return;
    }
    for (m = json_get(root, "sounds") != NULL ? json_get(root, "sounds")->child : NULL; m != NULL; m = m->next) {
        Json *file = json_get(m, "file"), *vol = json_get(m, "volume");
        char wav[1300];

        if (!valid_name(m->key)) {
            LOG("sounds: %s: \"%s\" is not a name (1 to 32 of a-z 0-9 _ -)", path, m->key);
        } else if (file == NULL || file->type != 's' || strchr(file->str, '/') != NULL || strchr(file->str, '\\') != NULL ||
                   strstr(file->str, "..") != NULL) {
            LOG("sounds: %s: %s has no file of the folder", path, m->key);
        } else if (s_lib_count == MAX_LIB) {
            LOG("sounds: more than %d sounds, %s is left out", MAX_LIB, m->key);
        } else {
            SDL_snprintf(wav, sizeof wav, "%s/mods/sounds/%s", data_dir(), file->str);
            if (!SDL_GetPathInfo(wav, NULL)) {
                LOG("sounds: %s: no such sound", wav);
                continue;
            }
            SDL_strlcpy(s_lib[s_lib_count].name, m->key, sizeof s_lib[0].name);
            native_clip_set(&s_lib[s_lib_count].clip, wav, vol != NULL && vol->type == 'n' && vol->num >= 0 && vol->num <= 4 ? (int)(vol->num * 256) : 256);
            s_lib_count++;
        }
    }
    for (m = json_get(root, "replace") != NULL ? json_get(root, "replace")->child : NULL; m != NULL; m = m->next) {
        char *end;
        long index = strtol(m->key, &end, 10);
        int lib = m->type == 's' ? find_name(m->str) : -1;

        if (*end != 0 || m->key == end || index < 1 || index >= MAX_INDEX) {
            LOG("sounds: %s: \"%s\" is not a sound of the game (1 to %d)", path, m->key, MAX_INDEX - 1);
        } else if (lib < 0) {
            LOG("sounds: %s: sound %ld: no sound named in the library", path, index);
        } else {
            s_replace[index] = (short)(lib + 1);
            s_used[lib] = 1;
        }
    }
    json_free(root);
}

static void scan(void) {
    static int s_scanned;

    if (!s_scanned) {
        s_scanned = 1;
        read_library();
    }
}

/* The id a script mod gives the library's sound NAME (0x100 and up, as FIGHTSTG_playBattleSound takes it), -1 when there is none */
int native_sound_id(const char *name) {
    int i;

    scan();
    if ((i = find_name(name)) < 0) {
        return -1;
    }
    s_used[i] = 1;
    return 0x100 + i;
}

/* The game plays battle sound INDEX: 1 when the library's sound plays in its place, 0 and the game's goes on. A library sound a script
   mod names (0x100 and up) is always 1: with Custom sounds off it is silent, there is no game sound in its place */
int native_sound_replace(int index) {
    if (g_render_sounds) {
        return index >= 0x100;
    }
    if (index >= 0x100) {
        index -= 0x100;
        scan();
        if (g_custom_sounds && index < s_lib_count && native_clip_ready(&s_lib[index].clip)) {
            native_clip_play(&s_lib[index].clip);
        }
        return 1;
    }
    if (!g_custom_sounds || index < 1 || index >= MAX_INDEX) {
        return 0;
    }
    scan();
    if (s_replace[index] == 0 || !native_clip_ready(&s_lib[s_replace[index] - 1].clip)) {
        return 0; /* one that can't be read stops counting, and the game's plays */
    }
    native_clip_play(&s_lib[s_replace[index] - 1].clip);
    return 1;
}

/* Once a frame from the start, after the effects': the sounds that replace one of the game's, or that a script mod names, are read,
   one a frame */
void native_soundlib_warm(void) {
    static int s_index;

    native_scripts_scan();
    scan();
    for (; s_index < s_lib_count; s_index++) {
        if (s_used[s_index] && s_lib[s_index].clip.data == NULL) {
            native_clip_ready(&s_lib[s_index].clip);
            s_index++;
            return;
        }
    }
}
