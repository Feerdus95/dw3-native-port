/* Script mods (native.h): data/mods/scripts/<fighter id>/<script index>.json is the whole battle script of one fighter, as a list of
   commands that are compiled to the 16-bit words the game runs (battle_script.c):
     {"schema_version": 1, "commands": [{"op": "sound", "sound": "punch", "time": 0, "hit": true}, {"op": "wait", "ticks": 10}, {"op": "end"}]}
   Every file is read and checked at the start (native_scripts_scan); a file with anything wrong in it is rejected as a whole, with the
   reason, and the game's own script plays. The numbers a command carries are the words the game stores (it negates some of them
   when it runs the command), so what the model viewer lists is what a file says. g_custom_scripts is looked at each time a script
   starts. A sound is a name of the sound library (soundlib.c), a number 1 to 89 (the game's), or "hit" / "hit_last" (the technique's
   hit sound); "hit": true on a sound makes a hit of result 3 play the game's own sound instead (bit 0x4000 of the word). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "json.h"

#define MAX_FILES 1024
#define MAX_COMMANDS 500
#define MAX_WORDS 4096
#define MAX_TICKS 600
#define SOUND_HIT_AWARE 0x4000

typedef struct {
    int fighter, index;
    short *words;
    int count;
    char path[200];
    char why[200]; /* empty: loaded */
} ScriptFile;

int g_custom_scripts = 1;
static ScriptFile s_files[MAX_FILES];
static int s_count;

typedef struct {
    short *out;
    int n;
    char *err;
    int errsz;
    int cmd; /* the command's number, from 1, for the message */
} Comp;

static int fail(Comp *c, const char *fmt, ...) {
    char msg[160];
    va_list ap;

    va_start(ap, fmt);
    SDL_vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SDL_snprintf(c->err, c->errsz, "command %d: %s", c->cmd, msg);
    return 0;
}

static int emit(Comp *c, int v) {
    if (c->n >= MAX_WORDS) {
        return fail(c, "the script is longer than %d words", MAX_WORDS);
    }
    c->out[c->n++] = (short)v;
    return 1;
}

/* member KEY of O as an integer in LO..HI; absent is DEF when HAS_DEF, else an error */
static int field(Comp *c, Json *o, const char *key, int lo, int hi, int def, int has_def, int *v) {
    Json *j = json_get(o, key);

    if (j == NULL) {
        if (has_def) {
            *v = def;
            return 1;
        }
        return fail(c, "no \"%s\"", key);
    }
    if (j->type != 'n' || j->num != (double)(int)j->num || j->num < lo || j->num > hi) {
        return fail(c, "\"%s\" is not a whole number from %d to %d", key, lo, hi);
    }
    *v = (int)j->num;
    return 1;
}
#define NEED(key, lo, hi, v) field(c, o, key, lo, hi, 0, 0, &(v))
#define OPT(key, lo, hi, def, v) field(c, o, key, lo, hi, def, 1, &(v))

static int str_of(Comp *c, Json *o, const char *key, const char *const *names, int count, int *which) {
    Json *j = json_get(o, key);
    int i;

    if (j == NULL || j->type != 's') {
        return fail(c, "no \"%s\" (a word)", key);
    }
    for (i = 0; i < count; i++) {
        if (strcmp(j->str, names[i]) == 0) {
            *which = i;
            return 1;
        }
    }
    return fail(c, "\"%s\": \"%s\" is not one of the known words", key, j->str);
}

/* the numbers of an array member KEY of O, exactly N of them, in LO..HI; absent is zeros when OPTIONAL */
static int vec(Comp *c, Json *o, const char *key, int n, int optional, int *v) {
    Json *j = json_get(o, key);
    int i;

    if (j == NULL && optional) {
        memset(v, 0, n * sizeof *v);
        return 1;
    }
    if (j == NULL || j->type != '[' || json_len(j) != n) {
        return fail(c, "\"%s\" is a list of %d numbers", key, n);
    }
    for (i = 0; i < n; i++) {
        Json *e = json_at(j, i);

        if (e->type != 'n' || e->num != (double)(int)e->num || e->num < -32768 || e->num > 32767) {
            return fail(c, "\"%s\" has a number that is not a whole number from -32768 to 32767", key);
        }
        v[i] = (int)e->num;
    }
    return 1;
}

/* the sound word of a sound command: a library name (0x100 and up), a number 1 to 89, hit 0x62, hit_last 0x63 */
static int sound_word(Comp *c, Json *o, int *word) {
    Json *j = json_get(o, "sound");
    int id, aware = 0;

    if (j == NULL) {
        return fail(c, "no \"sound\"");
    }
    if (j->type == 'n') {
        if (j->num != (double)(int)j->num || j->num < 1 || j->num > 0x59) {
            return fail(c, "\"sound\" is a library name or a number from 1 to 89");
        }
        id = (int)j->num;
    } else if (j->type == 's' && strcmp(j->str, "hit") == 0) {
        id = 0x62;
    } else if (j->type == 's' && strcmp(j->str, "hit_last") == 0) {
        id = 0x63;
    } else if (j->type == 's') {
        if ((id = native_sound_id(j->str)) < 0) {
            return fail(c, "no sound named \"%s\" in data/mods/sounds", j->str);
        }
    } else {
        return fail(c, "\"sound\" is a library name or a number from 1 to 89");
    }
    if ((j = json_get(o, "hit")) != NULL) {
        if (j->type != 'b') {
            return fail(c, "\"hit\" is true or false");
        }
        aware = j->num != 0;
    }
    if (aware && (id == 0x62 || id == 0x63)) {
        return fail(c, "\"hit\": true is for a sound of your own, \"hit\" already takes the hit's result into account");
    }
    *word = id | (aware ? SOUND_HIT_AWARE : 0);
    return 1;
}

/* How many words the command that starts at W holds, as the game reads it; 0 for one it doesn't know. N is how many words there are */
static int command_len(const short *w, int n) {
    int len = 0;

    switch (w[0]) {
    case 0:
    case 8:
    case 9:
    case 0xFF:
        return 1;
    case 1:
    case 11:
        return 2;
    case 10:
    case 7:
        return 3;
    }
    if (n < 2) {
        return 0;
    }
    switch (w[0]) {
    case 2:
        if (w[1] < 0 || w[1] > 9) {
            return 0;
        }
        len = (int[]){4, 3, 3, 7, 7, 4, 4, 4, 4, 3}[w[1]];
        if (w[1] == 5 && n > 3 && w[3] == 4) {
            len = 5;
        }
        return len;
    case 3:
        return w[1] == 0 ? 6 : 3;
    case 4:
        return w[1] == 0 ? 5 : 3;
    case 5:
        return n < 3 ? 0 : w[2] == 1 ? 17 : 4;
    case 6:
        return w[1] == 0 ? 9 : 3;
    }
    return 0;
}

static int compile_command(Comp *c, Json *o) {
    static const char *const OPS[] = {"hit", "model", "effect", "stage", "camera", "effect_model", "flash", "sound", "wait", "end", "raw"};
    static const char *const WHO[] = {"own", "partner1", "partner2", "partner3", "enemy1", "enemy2", "enemy3"};
    static const char *const DO[] = {"wait_motion", "idle", "motion", "show", "hide", "move", "turn", "jump", "wait_jump", "add_fighter", "home", "home_offset", "remove"};
    static const char *const MODE2[] = {"play", "load"}, *const MODE_STAGE[] = {"change", "wait"}, *const MODE_EM[] = {"play", "wait"}, *const MODE_FLASH[] = {"start", "end"};
    int op, who, what, a, t, v[14], i, w;
    Json *j;

    if (o->type != '{') {
        return fail(c, "is not an object");
    }
    if (!str_of(c, o, "op", OPS, 11, &op)) {
        return 0;
    }
    switch (op) {
    case 0: /* hit */
        if (!str_of(c, o, "when", (const char *const[]){"last", "next"}, 2, &a)) {
            return 0;
        }
        return emit(c, 1) && emit(c, a == 0 ? 0 : 5);
    case 1: /* model */
        if (!str_of(c, o, "who", WHO, 7, &who) || !str_of(c, o, "do", DO, 13, &what) || !emit(c, 2)) {
            return 0;
        }
        switch (what) {
        case 0: return emit(c, 0) && emit(c, who) && emit(c, 0);
        case 1: return emit(c, 0) && emit(c, who) && emit(c, 1);
        case 2: return NEED("n", 2, 32767, a) && emit(c, 0) && emit(c, who) && emit(c, a);
        case 3: return emit(c, 1) && emit(c, who);
        case 4: return emit(c, 2) && emit(c, who);
        case 5:
            return vec(c, o, "to", 3, 0, v) && NEED("time", 0, MAX_TICKS, t) && emit(c, 3) && emit(c, who) && emit(c, v[0]) && emit(c, v[1]) &&
                   emit(c, v[2]) && emit(c, t);
        case 6: return vec(c, o, "to", 3, 0, v) && emit(c, 4) && emit(c, who) && emit(c, v[0]) && emit(c, v[1]) && emit(c, v[2]) && emit(c, 0);
        case 7:
            if (!NEED("n", 1, 6, a) || !emit(c, 5) || !emit(c, who) || !emit(c, a)) {
                return 0;
            }
            return a != 4 || (NEED("distance", -32768, 32767, t) && emit(c, t));
        case 8: return emit(c, 5) && emit(c, who) && emit(c, 0);
        case 9: return NEED("fighter", 0, 9999, a) && emit(c, 6) && emit(c, who) && emit(c, a);
        case 10: return NEED("time", 0, MAX_TICKS, t) && emit(c, 7) && emit(c, who) && emit(c, t);
        case 11: return NEED("time", 0, MAX_TICKS, t) && emit(c, 8) && emit(c, who) && emit(c, t);
        default: return emit(c, 9) && emit(c, who);
        }
    case 2: /* effect */
        if (!str_of(c, o, "mode", MODE2, 2, &what) || !emit(c, 3) || !emit(c, what == 0 ? 0 : 1)) {
            return 0;
        }
        if ((j = json_get(o, "effect")) != NULL && j->type == 's' && strcmp(j->str, "own") == 0) {
            a = 9999;
        } else if (!NEED("effect", 0, 9999, a)) {
            return 0;
        }
        return emit(c, a) && (what == 1 || (vec(c, o, "at", 3, 0, v) && emit(c, v[0]) && emit(c, v[1]) && emit(c, v[2])));
    case 3: /* stage */
        if (!str_of(c, o, "mode", MODE_STAGE, 2, &what) || !emit(c, 4) || !emit(c, what)) {
            return 0;
        }
        if ((j = json_get(o, "stage")) != NULL && j->type == 's' && strcmp(j->str, "own") == 0) {
            a = 0x38;
        } else if (!NEED("stage", -1, 999, a)) {
            return 0;
        }
        return emit(c, a) && (what == 1 || (NEED("fade_out", 0, MAX_TICKS, t) && emit(c, t) && NEED("fade_in", 0, MAX_TICKS, t) && emit(c, t)));
    case 4: /* camera */
        if (!NEED("time", 0, MAX_TICKS, t) || !emit(c, 5) || !emit(c, t)) {
            return 0;
        }
        if (json_get(o, "shot") != NULL) {
            if (!NEED("shot", 1, 8, a)) {
                return 0;
            }
            return emit(c, 0) && emit(c, a == 8 ? 0 : a);
        }
        if (!emit(c, 1) || !vec(c, o, "at", 3, 0, v) || !vec(c, o, "look", 3, 0, v + 3) || !vec(c, o, "aim", 3, 1, v + 6) ||
            !vec(c, o, "turn", 3, 1, v + 9) || !OPT("rz", -32768, 32767, 0, v[12]) || !NEED("projection", 1, 32767, v[13])) {
            return 0;
        }
        for (i = 0; i < 14; i++) {
            if (!emit(c, v[i])) {
                return 0;
            }
        }
        return 1;
    case 5: /* effect_model */
        if (!str_of(c, o, "mode", MODE_EM, 2, &what) || !NEED("model", 0, 9999, a) || !emit(c, 6) || !emit(c, what == 0 ? 0 : 1) || !emit(c, a)) {
            return 0;
        }
        if (what == 1) {
            return 1;
        }
        if (!vec(c, o, "at", 3, 0, v) || !vec(c, o, "turn", 3, 1, v + 3)) {
            return 0;
        }
        for (i = 0; i < 6; i++) {
            if (!emit(c, v[i])) {
                return 0;
            }
        }
        return 1;
    case 6: /* flash */
        return str_of(c, o, "mode", MODE_FLASH, 2, &what) && NEED("time", 0, MAX_TICKS, t) && emit(c, 7) && emit(c, what) && emit(c, t);
    case 7: /* sound */
        return sound_word(c, o, &w) && OPT("time", 0, 3000, 0, t) && emit(c, 10) && emit(c, w) && emit(c, t);
    case 8: /* wait */
        return NEED("ticks", 0, MAX_TICKS, t) && emit(c, 11) && emit(c, t);
    case 9: /* end */
        return emit(c, 0);
    default: { /* raw */
        short w16[20];
        int start = c->n, len;

        if ((j = json_get(o, "words")) == NULL || j->type != '[' || json_len(j) < 1 || json_len(j) > 20) {
            return fail(c, "\"words\" is a list of 1 to 20 numbers");
        }
        for (i = 0; i < json_len(j); i++) {
            Json *e = json_at(j, i);

            if (e->type != 'n' || e->num != (double)(int)e->num || e->num < -32768 || e->num > 32767) {
                return fail(c, "\"words\" has a number that is not a whole number from -32768 to 32767");
            }
            w16[i] = (short)(int)e->num;
        }
        len = command_len(w16, json_len(j));
        if (len == 0 || len != json_len(j)) {
            return fail(c, "\"words\" is not one command of the game's (command %d, %d words)", w16[0], json_len(j));
        }
        for (i = 0; i < len; i++) {
            if (!emit(c, w16[i])) {
                return 0;
            }
        }
        return c->n - start == len;
    }
    }
}

/* ROOT (the file's JSON) compiled to OUT (room for MAX_WORDS): its length in words, or 0 with ERR set */
int native_script_compile(Json *root, short *out, char *err, int errsz) {
    Comp c = {out, 0, err, errsz, 0};
    Json *cmds, *schema, *o;
    int i, n;

    err[0] = 0;
    if (root == NULL || root->type != '{') {
        SDL_snprintf(err, errsz, "not a JSON object");
        return 0;
    }
    if ((schema = json_get(root, "schema_version")) == NULL || schema->type != 'n' || schema->num != 1) {
        SDL_snprintf(err, errsz, "\"schema_version\" is not 1");
        return 0;
    }
    if ((cmds = json_get(root, "commands")) == NULL || cmds->type != '[' || (n = json_len(cmds)) < 1 || n > MAX_COMMANDS) {
        SDL_snprintf(err, errsz, "\"commands\" is a list of 1 to %d commands", MAX_COMMANDS);
        return 0;
    }
    for (i = 0, o = cmds->child; o != NULL; i++, o = o->next) {
        c.cmd = i + 1;
        if (!compile_command(&c, o)) {
            return 0;
        }
    }
    /* the last command is the end; a script without one would run into whatever follows it */
    {
        Json *op = json_get(json_at(cmds, n - 1), "op");

        if (op == NULL || op->type != 's' || strcmp(op->str, "end") != 0) {
            SDL_snprintf(err, errsz, "the last command is not \"end\"");
            return 0;
        }
    }
    return c.n;
}

static void add_file(int fighter, int index, const char *path) {
    ScriptFile *f;
    JsonInfo ji;
    Json *root;
    short words[MAX_WORDS];
    int n, i;

    if (s_count == MAX_FILES) {
        LOG("scripts: more than %d script files, %s is left out", MAX_FILES, path);
        return;
    }
    for (i = 0; i < s_count; i++) {
        if (s_files[i].fighter == fighter && s_files[i].index == index) {
            LOG("scripts: %s is the same script as %s", path, s_files[i].path);
            return;
        }
    }
    f = &s_files[s_count++];
    f->fighter = fighter;
    f->index = index;
    SDL_strlcpy(f->path, path, sizeof f->path);
    if ((root = json_load(path, JSON_LIMITS_MANIFEST, &ji)) == NULL) {
        SDL_snprintf(f->why, sizeof f->why, "%s (line %d)", ji.msg, ji.line);
    } else {
        if ((n = native_script_compile(root, words, f->why, sizeof f->why)) > 0) {
            f->words = SDL_malloc(n * sizeof *words);
            if (f->words != NULL) {
                memcpy(f->words, words, n * sizeof *words);
                f->count = n;
            } else {
                SDL_strlcpy(f->why, "out of memory", sizeof f->why);
            }
        }
        json_free(root);
    }
    if (f->words == NULL) {
        LOG("scripts: %s: %s", path, f->why);
    }
}

static int number_of(const char *s, int max) {
    char *end;
    long v = strtol(s, &end, 10);

    return *s != 0 && *end == 0 && v >= 0 && v <= max ? (int)v : -1;
}

/* Reads data/mods/scripts once: each fighter's folder, each script's file */
void native_scripts_scan(void) {
    static int s_scanned;
    char dir[1200], sub[1300], path[1400];
    char **fighters, **files;
    int nf, n, i, j, fighter, index;

    if (s_scanned) {
        return;
    }
    s_scanned = 1;
    SDL_snprintf(dir, sizeof dir, "%s/mods/scripts", data_dir());
    if ((fighters = SDL_GlobDirectory(dir, "*", 0, &nf)) == NULL) {
        return;
    }
    for (i = 0; i < nf; i++) {
        SDL_snprintf(sub, sizeof sub, "%s/%s", dir, fighters[i]);
        if ((fighter = number_of(fighters[i], 9999)) < 0 || strchr(fighters[i], '/') != NULL) {
            if (strchr(fighters[i], '/') == NULL) {
                LOG("scripts: %s is not a fighter's number (0 to 9999)", sub);
            }
            continue;
        }
        if ((files = SDL_GlobDirectory(sub, "*.json", SDL_GLOB_CASEINSENSITIVE, &n)) == NULL) {
            continue;
        }
        for (j = 0; j < n; j++) {
            char stem[32];

            SDL_snprintf(stem, sizeof stem, "%.*s", (int)strcspn(files[j], "."), files[j]);
            SDL_snprintf(path, sizeof path, "%s/%s", sub, files[j]);
            if ((index = number_of(stem, 63)) < 0) {
                LOG("scripts: %s is not a script's number (0 to 63)", path);
            } else {
                add_file(fighter, index, path);
            }
        }
        SDL_free(files);
    }
    SDL_free(fighters);
}

/* A script starts (battle_script.c): the words of the fighter's script INDEX when a mod has a valid one and Custom scripts is on, else NULL */
const short *native_script_mod(int fighter, int index) {
    int i;

    if (!g_custom_scripts || g_render_sounds) {
        return NULL;
    }
    native_scripts_scan();
    for (i = 0; i < s_count; i++) {
        if (s_files[i].fighter == fighter && s_files[i].index == index && s_files[i].words != NULL) {
            return s_files[i].words;
        }
    }
    return NULL;
}

/* --check-mods: each script file, loaded or REJECTED with the reason; how many were rejected */
int native_scripts_check(void) {
    int i, bad = 0;

    native_scripts_scan();
    for (i = 0; i < s_count; i++) {
        bad += s_files[i].words == NULL;
        printf("%-9s script %d.%d (%d words)\n", s_files[i].words != NULL ? "loaded" : "REJECTED", s_files[i].fighter, s_files[i].index, s_files[i].count);
        if (s_files[i].words == NULL) {
            printf("          why: %s\n", s_files[i].why);
        }
    }
    printf("%d scripts, %d rejected\n", s_count, bad);
    return bad;
}
