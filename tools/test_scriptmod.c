/* Script mods (platform/scriptmod.c): each command compiles to the words the game reads (battle_script.c), what is wrong is rejected with
   a reason, and data/mods/scripts is read into native_script_mod, with Custom scripts switching it off. Run by tools/run_tests.py. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "json.h"

static char s_root[512];
const char *data_dir(void) { return s_root; }
int g_log;
int g_render_sounds;
int g_custom_sounds = 1;
void native_log(const char *fmt, ...) {}
int native_sound_id(const char *name) { return strcmp(name, "punch") == 0 ? 0x100 : strcmp(name, "kick") == 0 ? 0x101 : -1; }
const short *native_script_mod(int fighter, int index);

/* the JSON of one script made of COMMANDS, compiled: its length (0 on a rejection, WHY then says why), the words in W */
static int compile(const char *commands, short *w, char *why) {
    char *text = malloc(strlen(commands) + 64);
    JsonInfo ji;
    Json *root;
    int n;

    sprintf(text, "{\"schema_version\": 1, \"commands\": [%s]}", commands);
    root = json_parse(text, (long)strlen(text), JSON_LIMITS_MANIFEST, &ji);
    if (root == NULL) {
        fprintf(stderr, "%s: %s\n", ji.msg, commands);
        abort();
    }
    n = native_script_compile(root, w, why, 200);
    json_free(root);
    free(text);
    return n;
}

static void words(const char *commands, int n, const short *want) {
    short w[4096];
    char why[200];

    assert(compile(commands, w, why) == n);
    assert(memcmp(w, want, n * sizeof *w) == 0);
}

static void rejected(const char *commands, const char *needle) {
    short w[4096];
    char why[200];

    assert(compile(commands, w, why) == 0);
    if (strstr(why, needle) == NULL) {
        fprintf(stderr, "wanted \"%s\" in \"%s\" for %s\n", needle, why, commands);
        abort();
    }
}

static void put(const char *file, const char *text) {
    char p[800];

    SDL_snprintf(p, sizeof p, "%s/%s", s_root, file);
    assert(SDL_SaveFile(p, text, strlen(text)));
}

int main(void) {
    char p[600];
    const short *s;

    /* each command, as the game's loop reads it */
    words("{\"op\":\"end\"}", 1, (short[]){0});
    words("{\"op\":\"hit\",\"when\":\"last\"},{\"op\":\"hit\",\"when\":\"next\"},{\"op\":\"end\"}", 5, (short[]){1, 0, 1, 5, 0});
    words("{\"op\":\"model\",\"who\":\"own\",\"do\":\"motion\",\"n\":3},{\"op\":\"model\",\"who\":\"enemy2\",\"do\":\"wait_motion\"},{\"op\":\"end\"}", 9,
          (short[]){2, 0, 0, 3, 2, 0, 5, 0, 0});
    words("{\"op\":\"model\",\"who\":\"partner1\",\"do\":\"jump\",\"n\":4,\"distance\":-300},{\"op\":\"model\",\"who\":\"own\",\"do\":\"wait_jump\"},{\"op\":\"end\"}", 10,
          (short[]){2, 5, 1, 4, -300, 2, 5, 0, 0, 0});
    words("{\"op\":\"model\",\"who\":\"own\",\"do\":\"move\",\"to\":[1,2,3],\"time\":9},{\"op\":\"model\",\"who\":\"own\",\"do\":\"hide\"},{\"op\":\"end\"}", 11,
          (short[]){2, 3, 0, 1, 2, 3, 9, 2, 2, 0, 0});
    words("{\"op\":\"effect\",\"mode\":\"load\",\"effect\":\"own\"},{\"op\":\"effect\",\"mode\":\"play\",\"effect\":2,\"at\":[0,-384,5120]},{\"op\":\"end\"}", 10,
          (short[]){3, 1, 9999, 3, 0, 2, 0, -384, 5120, 0});
    words("{\"op\":\"stage\",\"mode\":\"change\",\"stage\":\"own\",\"fade_out\":4,\"fade_in\":5},{\"op\":\"end\"}", 6, (short[]){4, 0, 0x38, 4, 5, 0});
    words("{\"op\":\"camera\",\"time\":10,\"shot\":8},{\"op\":\"camera\",\"time\":0,\"shot\":3},{\"op\":\"end\"}", 9, (short[]){5, 10, 0, 0, 5, 0, 0, 3, 0});
    words("{\"op\":\"camera\",\"time\":0,\"at\":[1,2,3],\"look\":[4,5,6],\"projection\":330},{\"op\":\"end\"}", 18,
          (short[]){5, 0, 1, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0, 330, 0});
    words("{\"op\":\"effect_model\",\"mode\":\"play\",\"model\":7,\"at\":[1,2,3],\"turn\":[4,5,6]},{\"op\":\"effect_model\",\"mode\":\"wait\",\"model\":7},{\"op\":\"end\"}", 13,
          (short[]){6, 0, 7, 1, 2, 3, 4, 5, 6, 6, 1, 7, 0});
    words("{\"op\":\"flash\",\"mode\":\"start\",\"time\":3},{\"op\":\"wait\",\"ticks\":10},{\"op\":\"end\"}", 6, (short[]){7, 0, 3, 11, 10, 0});
    /* sounds: a library name is 0x100 and up, hit-aware sets bit 0x4000, hit and hit_last are the game's 0x62 and 0x63 */
    words("{\"op\":\"sound\",\"sound\":\"punch\"},{\"op\":\"sound\",\"sound\":\"kick\",\"time\":5,\"hit\":true},{\"op\":\"sound\",\"sound\":57,\"time\":0},"
          "{\"op\":\"sound\",\"sound\":\"hit\"},{\"op\":\"sound\",\"sound\":\"hit_last\",\"time\":2},{\"op\":\"end\"}", 16,
          (short[]){10, 0x100, 0, 10, 0x4101, 5, 10, 57, 0, 10, 0x62, 0, 10, 0x63, 2, 0});
    /* raw: one command's words, checked for the length the game reads */
    words("{\"op\":\"raw\",\"words\":[10,57,0]},{\"op\":\"raw\",\"words\":[2,5,0,4,100]},{\"op\":\"end\"}", 9, (short[]){10, 57, 0, 2, 5, 0, 4, 100, 0});

    rejected("{\"op\":\"wait\",\"ticks\":10}", "not \"end\"");
    rejected("{\"op\":\"nope\"},{\"op\":\"end\"}", "command 1");
    rejected("{\"op\":\"wait\",\"ticks\":601},{\"op\":\"end\"}", "ticks");
    rejected("{\"op\":\"wait\"},{\"op\":\"end\"}", "ticks");
    rejected("{\"op\":\"end\"},{\"op\":\"sound\",\"sound\":\"ghost\"},{\"op\":\"end\"}", "command 2: no sound named \"ghost\"");
    rejected("{\"op\":\"sound\",\"sound\":90},{\"op\":\"end\"}", "1 to 89");
    rejected("{\"op\":\"sound\",\"sound\":\"hit\",\"hit\":true},{\"op\":\"end\"}", "own");
    rejected("{\"op\":\"model\",\"who\":\"nobody\",\"do\":\"idle\"},{\"op\":\"end\"}", "known words");
    rejected("{\"op\":\"model\",\"who\":\"own\",\"do\":\"move\",\"to\":[1,2],\"time\":1},{\"op\":\"end\"}", "3 numbers");
    rejected("{\"op\":\"effect\",\"mode\":\"play\",\"effect\":10000,\"at\":[0,0,0]},{\"op\":\"end\"}", "effect");
    rejected("{\"op\":\"raw\",\"words\":[10,57]},{\"op\":\"end\"}", "not one command");
    rejected("{\"op\":\"raw\",\"words\":[99]},{\"op\":\"end\"}", "not one command");
    rejected("{\"op\":\"model\",\"who\":\"own\",\"do\":\"motion\",\"n\":1.5},{\"op\":\"end\"}", "whole number");
    {
        short w[4096];
        char why[200];
        JsonInfo ji;
        const char *text = "{\"schema_version\": 2, \"commands\": [{\"op\":\"end\"}]}";
        Json *root = json_parse(text, (long)strlen(text), NULL, &ji);

        assert(root != NULL && native_script_compile(root, w, why, sizeof why) == 0 && strstr(why, "schema_version") != NULL);
        json_free(root);
    }
    /* a script longer than the game's room is refused */
    {
        char *big = malloc(500 * 30 + 64);
        short w[4096];
        char why[200];

        strcpy(big, "");
        for (int i = 0; i < 501; i++) {
            strcat(big, "{\"op\":\"wait\",\"ticks\":1},");
        }
        strcat(big, "{\"op\":\"end\"}");
        assert(compile(big, w, why) == 0 && strstr(why, "1 to 500") != NULL);
        free(big);
    }

    /* the folders: data/mods/scripts/<fighter>/<index>.json */
    SDL_snprintf(s_root, sizeof s_root, "%s/dw3_scriptmod_test", SDL_GetCurrentDirectory());
    SDL_RemovePath(s_root);
    SDL_snprintf(p, sizeof p, "%s/mods/scripts/3", s_root);
    assert(SDL_CreateDirectory(p));
    SDL_snprintf(p, sizeof p, "%s/mods/scripts/x", s_root);
    assert(SDL_CreateDirectory(p));
    put("mods/scripts/3/5.json", "{\"schema_version\":1,\"commands\":[{\"op\":\"sound\",\"sound\":\"punch\"},{\"op\":\"end\"}]}");
    put("mods/scripts/3/6.json", "{\"schema_version\":1,\"commands\":[{\"op\":\"wait\"}]}");
    put("mods/scripts/3/notes.json", "{}");
    put("mods/scripts/x/1.json", "{}");
    assert(g_custom_scripts == 1);
    s = native_script_mod(3, 5);
    assert(s != NULL && s[0] == 10 && s[1] == 0x100 && s[3] == 0);
    assert(native_script_mod(3, 6) == NULL);   /* rejected */
    assert(native_script_mod(3, 7) == NULL);   /* none */
    assert(native_script_mod(4, 5) == NULL);
    g_custom_scripts = 0;
    assert(native_script_mod(3, 5) == NULL);   /* switched off in game */
    g_custom_scripts = 1;
    assert(native_script_mod(3, 5) != NULL);
    SDL_RemovePath(s_root);
    printf("scriptmod ok\n");
    return 0;
}
