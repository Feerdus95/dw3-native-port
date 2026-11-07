/* The Digimon mods' loader (platform/digimon_mods.c): what it makes of every kind of folder, on a data tree it makes beside the exe.
   Run from native/ (with SDL3.dll beside the exe):
   i686-w64-mingw32-clang -Iplatform -Iinclude -I<SDL>/include tools/test_mods.c platform/digimon_mods.c platform/json.c -L<SDL>/lib -lSDL3 -o build/test_mods.exe
   build/test_mods.exe */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"
#include "../include/native.h"

static char s_root[512];
static int s_checks;
int g_log;
const char *data_dir(void) { return s_root; }
/* the game's fighters' file: absent, or a stub of 16 bytes in which 259 and 300 are partners (bladegaru.c holds the real reader) */
static int s_fighters;
long native_read_raw(int fid, uint8_t **out) {
    *out = s_fighters ? calloc(16, 1) : NULL;
    return s_fighters ? 16 : 0;
}
int native_fighter_has_record(const uint8_t *f, long size, int model) { return f != NULL && size == 16 && (model == 259 || model == 300); }

static int has(const char *sub, int fid) {
    char p[800];
    SDL_PathInfo i;

    SDL_snprintf(p, sizeof p, "%s/%s/%04d.bin", s_root, sub, fid);
    return SDL_GetPathInfo(p, &i);
}
int native_disc_file(int fid) { return has("files", fid); }
int native_disk_file(int fid) { return has("mods", fid) || has("files", fid); }

static void put(const char *rel, const void *data, size_t size) {
    char p[800];

    SDL_snprintf(p, sizeof p, "%s/%s", s_root, rel);
    assert(SDL_SaveFile(p, data, size));
}

/* a model file with ENTRIES entries in its table: the first offset says how many */
static void model(const char *sub, int fid, int entries) {
    unsigned char b[400] = {0};
    char rel[64];

    for (int i = 0; i < entries; i++) { /* every entry starts where the data does, after the table */
        b[4 * i] = (unsigned char)(entries * 4);
    }
    SDL_snprintf(rel, sizeof rel, "%s/%04d.bin", sub, fid);
    put(rel, b, sizeof b);
}

/* a model file of the bytes given, as is (a malformed one) */
static void raw_model(const void *b, size_t n) {
    put("mods/7000.bin", b, n);
}

/* a model with the scripts' archive (entry 41): a table of 42 offsets, the archive's own table of s_scripts scripts at 200 */
static int s_scripts, s_off1, s_off2; /* the scripts' count; where scripts 1 and 2 start in the archive when not the table's end */
static void model_scripts(void) {
    unsigned char b[400] = {0};

    for (int i = 0; i < 42; i++) {
        b[4 * i] = 42 * 4;
    }
    b[4 * 41] = 200;
    for (int i = 0; i < s_scripts; i++) { /* empty scripts, each at the table's end */
        b[200 + 4 * i] = (unsigned char)(s_scripts * 4);
    }
    if (s_off1) {
        b[204] = (unsigned char)s_off1;
    }
    if (s_off2) {
        b[208] = (unsigned char)s_off2;
    }
    raw_model(b, sizeof b);
}

/* a model of 42 entries with entry 41 at A, and the 4 bytes at 200 as given (the archive's first offset when A is 200) */
static int s_a, s_first_word;
static void model_41(void) {
    unsigned char b[400] = {0};

    for (int i = 0; i < 42; i++) {
        b[4 * i] = 42 * 4;
    }
    b[4 * 41] = (unsigned char)s_a;
    b[200] = (unsigned char)s_first_word;
    raw_model(b, sizeof b);
}

static void (*s_model)(void); /* what makes the model of the next one(), when not the plain one */

static void folder(const char *name, const char *json) {
    char rel[200];

    SDL_snprintf(rel, sizeof rel, "%s/mods/digimon/%s", s_root, name);
    assert(SDL_CreateDirectory(rel));
    if (json != NULL) {
        SDL_snprintf(rel, sizeof rel, "mods/digimon/%s/digimon.json", name);
        put(rel, json, strlen(json));
    }
}

/* a folder and all in it (SDL_RemovePath takes only empty ones) */
static void wipe(const char *dir) {
    int n = 0;
    char **names = SDL_GlobDirectory(dir, "*", 0, &n);

    for (int i = 0; names != NULL && i < n; i++) {
        char p[800];
        SDL_PathInfo info;

        SDL_snprintf(p, sizeof p, "%s/%s", dir, names[i]);
        if (SDL_GetPathInfo(p, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            wipe(p);
        } else {
            SDL_RemovePath(p);
        }
    }
    SDL_free(names);
    SDL_RemovePath(dir);
}

static void fresh(void) {
    char p[800];

    wipe(s_root);
    SDL_snprintf(p, sizeof p, "%s/mods/digimon", s_root), assert(SDL_CreateDirectory(p));
    SDL_snprintf(p, sizeof p, "%s/files", s_root), assert(SDL_CreateDirectory(p));
}

#define TECH(n) "{\"name\": \"T" #n "\", \"help\": {\"en\": \"help " #n "\"}, \"from\": 61, \"mp\": 10, \"power\": 100}"
#define TECHS "\"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) "," TECH(6) "]"

/* a manifest that is right, with EXTRA fields after (starting with a comma) */
static char *manifest(int id, int fid, const char *extra) {
    static char buf[4096];

    SDL_snprintf(buf, sizeof buf, "{\"id\": %d, \"fid\": %d, \"name\": \"Mon%d\", \"subs\": [0, 1, 2], %s%s}", id, fid, id, TECHS, extra ? extra : "");
    return buf;
}

static const NativeModReport *report(const char *name) {
    int n, dropped;
    const NativeModReport *r = native_mods_report(&n, &dropped);

    for (int i = 0; i < n; i++) {
        if (strcmp(r[i].folder, name) == 0) {
            return &r[i];
        }
    }
    return NULL;
}

static int loads(void) {
    int k, n = 0;

    native_mods_load();
    for (k = 0; k < NATIVE_MAX_MODS; k++) {
        n += native_mod_at(k) != NULL;
    }
    return n;
}

/* one folder with JSON, its model there: the status and a part of its reason (or of its warnings) */
static void one(const char *json, int status, const char *part, const char *warning) {
    const NativeModReport *r;

    fresh();
    s_model ? s_model() : model("mods", 7000, 5);
    folder("m", json);
    loads();
    r = report("m");
    s_checks++;
    if (r == NULL || r->status != status || (part && !strstr(r->reason, part)) || (warning && !strstr(r->warnings, warning)) || (!warning && status == NATIVE_MOD_LOADED && r->warnings[0])) {
        printf("%s\n  status %d (want %d), reason \"%s\" (want \"%s\"), warnings \"%s\" (want \"%s\")\n", json, r ? r->status : -1, status, r ? r->reason : "", part ? part : "", r ? r->warnings : "", warning ? warning : "");
    }
    assert(r != NULL && r->status == status);
    assert(!part || strstr(r->reason, part));
    assert(!warning || strstr(r->warnings, warning));
    assert(warning || status != NATIVE_MOD_LOADED || r->warnings[0] == 0);
}
#define GOOD(extra) one(manifest(472, 7000, extra), NATIVE_MOD_LOADED, NULL, NULL)
#define WARNS(extra, w) one(manifest(472, 7000, extra), NATIVE_MOD_LOADED, NULL, w)
#define REJECTS(json, why) one(json, NATIVE_MOD_REJECTED, why, NULL)

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* what a failed check prints stays when the assert aborts */
    SDL_snprintf(s_root, sizeof s_root, "%sdw3_test_mods", SDL_GetBasePath());

    /* a manifest that is right, and what it makes */
    GOOD("");
    {
        const NativeMod *m = native_mod_at(0);

        assert(m != NULL && m->id == 472 && m->slot == NATIVE_MOD_DATA && m->name_id == NATIVE_MOD_NAME && m->tech == NATIVE_MOD_TECH);
        assert(strcmp(m->name, "Mon472") == 0 && strcmp(m->folder, "m") == 0 && native_mod_by_id(472) == m && native_mod_by_id(473) == NULL);
        assert(m->like == 259 && m->template == 40 && m->hp_permille == 1000 && m->signatures == 1 && m->techs[0].element == 1 && m->techs[3].from == 61);
        assert(strcmp(m->techs[5].help[3], "help 6") == 0); /* a language without a line takes the English */
        s_checks += 7;
    }
    GOOD(", \"schema_version\": 1, \"author\": \"someone\", \"version\": \"1.0.0\", \"enabled\": true, \"tired\": true");
    GOOD(", \"enabled\": 1, \"tired\": 1, \"signatures\": 3, \"template\": 53, \"portrait_dz\": -380, \"face\": [2, 3, 8, 17]");
    s_model = model_scripts, s_scripts = 40; /* the scripts it names (Flamedramon's 21, 28, 29) are in an archive of 40 */
    GOOD(", \"pair\": {\"ids\": [259, 5], \"element\": 2, \"element_power\": 48, \"shift\": 3000, \"scripts\": [21, 28, 29], \"motions\": [50, 62, 63]}");
    s_model = NULL;
    GOOD(", \"effects\": [[450, 7000], [451, 7000]], \"needs\": [13, 40, 12, 40], \"stats\": [1, 2, 3, 4, 5], \"resists\": [1, 2, 3, 4, 5, 6, 7]");
    one("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], " TECHS ",}", NATIVE_MOD_LOADED, NULL, "1 trailing comma"); /* a hand edit is let by, with a word */
    /* a manifest from before there were schema versions, without the fields added since: the folder's name is its name */
    one("{\"id\": 472, \"fid\": 7000, " TECHS "}", NATIVE_MOD_LOADED, NULL, "subs is missing");

    /* what is only unusual loads, with its word */
    WARNS(", \"min_port_version\": \"0.18\", \"colour\": 3", "unknown keys: min_port_version, colour");
    WARNS(", \"template\": 52", "placeholder");
    WARNS(", \"signatures\": 9", "brought to 6");
    WARNS(", \"learn\": [10, 20, 30, 40, 50, 120]", "99");
    WARNS(", \"stats\": [1, 2]", "stats has 2 values of 5");
    WARNS(", \"needs\": [13, 40, 0, 0]", "does nothing");
    WARNS(", \"face\": [2, 3, 8]", "odd number");
    WARNS(", \"pair\": {\"ids\": [259, 5], \"scripts\": [1, 2]}", "scripts has 2 values of 3");
    WARNS(", \"effects\": [[450, 7999]]", "7999.bin aren't there");
    WARNS(", \"author\": 5", "author should be a text");
    one("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",{\"help\": {\"en\": \"h\"}, \"script\": 12}]}", NATIVE_MOD_LOADED, NULL, "has no name");
    WARNS(", \"pair\": {\"ids\": [259, 5]}", "no scripts' archive"); /* the model of 5 entries has none */

    /* what would make the game read what isn't there, or a number its field can't hold, is refused with the reason */
    /* "like" against the partners of the fighters' file: another that is there, one that isn't, and no file to look in */
    s_fighters = 1;
    GOOD(", \"like\": 300");
    REJECTS(manifest(472, 7000, ", \"like\": 301"), "like 301 isn't a partner");
    s_fighters = 0;
    GOOD(", \"like\": 301");
    REJECTS("{", "ends");
    REJECTS("[1]", "must hold an object");
    REJECTS("{\"id\": 472, \"fid\": 7000,, " TECHS "}", "member's name");
    REJECTS("{\"fid\": 7000, \"subs\": [0, 1, 2], " TECHS "}", "id is missing");
    REJECTS("{\"id\": 472, \"subs\": [0, 1, 2], " TECHS "}", "fid is missing");
    REJECTS(manifest(471, 7000, NULL), "id is 471, but it can be 472 to 475");
    REJECTS(manifest(476, 7000, NULL), "id is 476, but it can be 472 to 475");
    REJECTS("{\"id\": \"472\", \"fid\": 7000, " TECHS "}", "id must be a number");
    REJECTS("{\"id\": 472.5, \"fid\": 7000, " TECHS "}", "whole number");
    REJECTS(manifest(472, 8192, NULL), "fid is 8192, but it can be 1 to 8191");
    REJECTS(manifest(472, 0, NULL), "fid is 0");
    REJECTS(manifest(472, 7001, NULL), "data/mods/7001.bin isn't there");
    REJECTS(manifest(472, 7000, ", \"enabled\": \"yes\""), "enabled must be true");
    REJECTS(manifest(472, 7000, ", \"schema_version\": 2"), "newer port");
    REJECTS(manifest(472, 7000, ", \"schema_version\": \"1\""), "schema_version must be a number");
    REJECTS(manifest(472, 7000, ", \"schema_version\": 0"), "schema_version");
    REJECTS(manifest(472, 7000, ", \"hp_permille\": 0"), "hp_permille is 0");
    REJECTS(manifest(472, 7000, ", \"hp_permille\": 2001"), "hp_permille is 2001");
    REJECTS(manifest(472, 7000, ", \"mp_permille\": 100000"), "mp_permille is 100000");
    GOOD(", \"hp_permille\": 2000, \"mp_permille\": 1");
    REJECTS(manifest(472, 7000, ", \"template\": 55"), "template is 55");
    REJECTS(manifest(472, 7000, ", \"template\": -1"), "template is -1");
    REJECTS(manifest(472, 7000, ", \"stats\": [1, 2, 3, 4, 70000]"), "stats[4]");
    REJECTS(manifest(472, 7000, ", \"stats\": [1, \"2\", 3, 4, 5]"), "stats[1]");
    REJECTS(manifest(472, 7000, ", \"stats\": 5"), "stats must be a list");
    REJECTS(manifest(472, 7000, ", \"learn\": [1, 2, 3, 4, 5, 300]"), "learn[5]");
    REJECTS(manifest(472, 7000, ", \"needs\": [13, 40, 12]"), "needs has 3 values");
    REJECTS(manifest(472, 7000, ", \"needs\": [56, 40, 12, 40]"), "needs[0] is 56");
    GOOD(", \"needs\": [55, 99, 1, 1]");
    REJECTS(manifest(472, 7000, ", \"needs\": [13, 100, 12, 40]"), "needs[1]");
    REJECTS(manifest(472, 7000, ", \"needs\": [13, 40, 12, 0]"), "needs[3] is the level of Digimon 12");
    REJECTS(manifest(472, 7000, ", \"needs\": [13, 0, 12, 40]"), "needs[1] is the level of Digimon 13");
    REJECTS(manifest(472, 7000, ", \"needs\": [-1, 40, 12, 40]"), "needs[0]");
    /* the texts: plain ASCII, English help present */
    REJECTS("{\"id\": 472, \"fid\": 7000, \"name\": \"Caf\u00e9\", \"subs\": [0, 1, 2], " TECHS "}", "plain ASCII");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",""{\"name\": \"Fl\u00e8che\", \"help\": {\"en\": \"x\"}}]}", "plain ASCII");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",""{\"help\": {}}]}", "help.en is missing");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",""{\"help\": {\"en\": \"\", \"fr\": \"x\"}}]}", "help.en is missing");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",""{\"name\": \"x\"}]}", "help.en is missing");
    REJECTS(manifest(472, 7000, ", \"id\": 473"), "twice"); /* a duplicated key */
    /* a model that isn't one: its table is read by the game, so what it says has to fit the file */
    {
        static const unsigned char small[4] = {100, 0, 0, 0}, odd[40] = {6}, huge[8] = {0, 0, 0, 0x80}, zero[16] = {0}, past[16] = {8, 0, 0, 0, 0, 1, 0, 0}, eof[20] = {20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20, 0, 0, 0, 20}, low[16] = {8, 0, 0, 0, 0, 0, 0, 0},
                                    short_end[16] = {8, 0, 0, 0, 14}, three[3] = {4};
        const char *J = manifest(472, 7000, NULL);
        s_model = NULL;
        #define MODEL(bytes, why) do { fresh(); raw_model(bytes, sizeof bytes); folder("m", J); loads(); { const NativeModReport *r = report("m"); s_checks++; \
            if (r == NULL || r->status != NATIVE_MOD_REJECTED || strstr(r->reason, why) == NULL) printf("model %s: %s\n", why, r ? r->reason : "no report"); \
            assert(r != NULL && r->status == NATIVE_MOD_REJECTED && strstr(r->reason, why) != NULL); } } while (0)
        MODEL(small, "starts at 100, in a file of 4 bytes"); /* 25 entries claimed in 4 bytes */
        MODEL(odd, "starts at 6");
        MODEL(huge, "starts at 2147483648"); /* the bit that overflowed a signed shift */
        MODEL(zero, "starts at 0");
        MODEL(three, "isn't a model file");
        MODEL(eof, "subs[0] (entry 0) starts at 20"); /* every entry at the end of a 20-byte file: nothing there to read */
        MODEL(low, "subs[1] (entry 1) starts at 0"); /* an entry inside the table */
        MODEL(short_end, "subs[1] (entry 1) starts at 14"); /* 2 bytes left, a word is 4 */
        MODEL(past, "subs[1]"); /* 2 entries: the table is 8 bytes, subs [0, 1, 2] asks for the third; and the second one points past the file */
        #undef MODEL
    }
    /* the reader finale() uses, on files cut to their exact size (the sanitizers' run sees any read past it) */
    {
        extern int native_mod_script_at(const unsigned char *, size_t, int, unsigned *, unsigned *, int *, const char **);
        static const struct { const char *name; size_t size; int a, first_word, k, want; } T[] = {
            /* a table of 42 entries (168 bytes) in a file smaller than it: finale()'s own case, never read at entry 41 */
            {"42 entries in 4 bytes", 4, 0, 0, 0, 2},
            {"42 entries in 100 bytes", 100, 0, 0, 0, 2},
            {"42 entries in 167 bytes", 167, 0, 0, 0, 2},
            {"entry 41 at the end of 168", 168, 168, 0, 0, 2},
            {"entry 41 inside the table", 400, 100, 12, 0, 2},
            {"entry 41 at the table's last word", 400, 164, 12, 0, 2},
            {"archive's word cut by the end", 202, 200, 12, 0, 2},
            {"archive's table past the end", 400, 200, 252, 0, 2},
            {"archive's first offset 0", 400, 200, 0, 0, 2},
            {"archive's first offset 6", 400, 200, 6, 0, 2},
            {"no archive", 400, 0, 0, 0, 1},
            {"script 3 of 3", 400, 200, 12, 3, 2},
            {"script -1", 400, 200, 12, -1, 2},
            {"a script that exists", 400, 200, 12, 2, 0},
            {"script 0 (its end) at an odd offset", 400, 200, 12, 0, 2}, /* 213 */
            {"script 1 at an odd offset", 400, 200, 12, 1, 2},          /* 213: a pointer a short can't sit at */
            {"script 1 ends at an odd offset", 400, 200, 12, 1, 2},
            {"the last script's odd end", 401, 200, 12, 2, 0}, /* only drops a byte */
        };
        for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
            unsigned char full[400] = {0};
            unsigned char *f = malloc(T[i].size ? T[i].size : 1);
            unsigned from = 0, to = 0;
            int count = 0, r;
            const char *why = "";

            for (int e = 0; e < 42; e++) {
                full[4 * e] = 42 * 4;
            }
            if (T[i].size < 168 && T[i].size >= 4) { /* the table the file has is shorter than the 42 entries it claims */
                full[0] = 168;
            }
            full[4 * 41] = (unsigned char)T[i].a;
            full[200] = (unsigned char)T[i].first_word;
            if (T[i].first_word == 12) { /* its three scripts, empty, at its table's end */
                full[204] = full[208] = 12;
            }
            if (strstr(T[i].name, " at an odd")) {
                full[204] = 13, full[208] = 14;
            } else if (strstr(T[i].name, "script 1 ends at an odd")) {
                full[204] = 12, full[208] = 13;
            }
            memcpy(f, full, T[i].size);
            r = native_mod_script_at(f, T[i].size, T[i].k, &from, &to, &count, &why);
            s_checks++;
            if (r != T[i].want) {
                printf("script reader, %s: %d (want %d), %s\n", T[i].name, r, T[i].want, why);
            }
            assert(r == T[i].want);
            if (strstr(T[i].name, "odd offset")) { /* refused for the alignment, not for something else */
                assert(strstr(why, "aligned") != NULL);
            }
            free(f);
        }
        assert(native_mod_script_at(NULL, 0, 0, &(unsigned){0}, &(unsigned){0}, &(int){0}, &(const char *){0}) == 2);
        s_checks++;
    }
    /* pair.scripts against the scripts of the model's archive */
    s_model = model_scripts, s_scripts = 3;
    s_off1 = 0, s_off2 = 255; /* script 2 starts past the file */
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 0, 2]}"), "pair.scripts[2] (script 2)");
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 1, 1]}"), "pair.scripts[1] (script 1)"); /* a script ends where the next starts */
    s_off1 = 200, s_off2 = 0; /* script 1 starts after script 2 does */
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 1, 2]}"), "pair.scripts[1] (script 1)");
    s_off1 = 4, s_off2 = 0; /* script 1 starts inside the archive's own table, so script 0 ends there */
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 1, 2]}"), "pair.scripts[0] (script 0)"); /* script 0 would end inside the table */
    s_off1 = s_off2 = 0;
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 1, 3]}"), "pair.scripts[2] (script 3): the archive has fewer scripts");
    GOOD(", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 1, 2]}");
    GOOD(", \"pair\": {\"ids\": [259, 5]}");
    /* the archive's place and table, malformed (the same reader runs in finale() on whatever the file holds then) */
    s_model = model_41;
    s_a = 100, s_first_word = 12; /* the archive starts inside the table of entries */
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 0, 0]}"), "starts inside the model's table of entries");
    s_a = 200, s_first_word = 10; /* its first offset isn't a multiple of 4 */
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 0, 0]}"), "table of offsets isn't inside the file");
    s_a = 200, s_first_word = 252; /* its table reaches past the file */
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 0, 0]}"), "table of offsets isn't inside the file");
    s_a = 200, s_first_word = 0; /* an empty table */
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 0, 0]}"), "table of offsets isn't inside the file");
    s_a = 0, s_first_word = 0; /* no archive */
    WARNS(", \"pair\": {\"ids\": [259, 5], \"scripts\": [0, 0, 0]}", "no scripts' archive");
    s_model = NULL;
    REJECTS(manifest(472, 7000, ", \"face\": [2, 255]"), "face[1]");
    REJECTS(manifest(472, 7000, ", \"face\": [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]"), "face has 20 values");
    REJECTS(manifest(472, 7000, ", \"portrait_dz\": 40000"), "portrait_dz");
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 259]}"), "names the same Digimon (259) twice");
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [472, 5]}"), "names the mod itself (472)");
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [5, 472]}"), "names the mod itself (472)");
    REJECTS(manifest(472, 7000, ", \"pair\": 3"), "pair must be an object");
    REJECTS(manifest(472, 7000, ", \"pair\": {\"element\": 2}"), "pair.ids is missing");
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259]}"), "pair.ids has 1 values");
    REJECTS(manifest(472, 7000, ", \"pair\": {\"ids\": [259, 5], \"shift\": 40000}"), "pair.shift");
    REJECTS(manifest(472, 7000, ", \"effects\": 3"), "effects must be");
    REJECTS(manifest(472, 7000, ", \"effects\": [[450, 7000], [450, 7000]]"), "effect id 450 is in effects twice");
    REJECTS(manifest(472, 7000, ", \"effects\": [[450]]"), "effects[0]");
    REJECTS(manifest(472, 7000, ", \"effects\": [[0, 7000]]"), "effects[0]");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2]}", "techs needs a list of six");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) "]}", "techs needs a list of six");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",3]}", "techs[5] must be an object");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",{\"from\": 0}]}", "techs[5].from is 0");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",{\"from\": 445}]}", "techs[5].from is 445");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",{\"mp\": 70000}]}", "techs[5].mp");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",{\"script\": 256}]}", "techs[5].script");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 2], \"techs\": [" TECH(1) "," TECH(2) "," TECH(3) "," TECH(4) "," TECH(5) ",{\"name\": \"a name that is far too long\"}]}", "techs[5].name has 27 characters");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"name\": \"a name that is far too long\", " TECHS "}", "name has 27 characters");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1], " TECHS "}", "subs has 2 values");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 5], " TECHS "}", "subs[2] is 5, but the model has 5 entries");
    REJECTS("{\"id\": 472, \"fid\": 7000, \"subs\": [0, 1, 70000], " TECHS "}", "subs[2]");

    /* a model of the game's disc isn't replaced */
    fresh();
    model("mods", 123, 5), model("files", 123, 5);
    folder("m", manifest(472, 123, NULL));
    assert(loads() == 0 && strstr(report("m")->reason, "file of the game's disc") != NULL);
    s_checks++;

    /* four at once, and the fifth */
    fresh();
    for (int k = 0; k < 5; k++) {
        char name[16], extra[200];

        model("mods", 7000 + k, 5);
        SDL_snprintf(name, sizeof name, "mod%d", k);
        SDL_snprintf(extra, sizeof extra, ", \"pair\": {\"ids\": [%d, 5]}, \"effects\": [[%d, 7000]]", 100 + k, 450 + k);
        folder(name, manifest(472 + k, 7000 + k, extra));
    }
    assert(loads() == 4);
    for (int k = 0; k < 4; k++) {
        assert(native_mod_at(k) != NULL && native_mod_at(k)->id == 472 + k && native_mod_at(k)->slot == NATIVE_MOD_DATA + k);
    }
    assert(report("mod4")->status == NATIVE_MOD_REJECTED && strstr(report("mod4")->reason, "id is 476") != NULL);
    {
        int fid;

        assert(native_mod_for_effect(452, &fid) == native_mod_at(2) && fid == 7000 && native_mod_for_effect(454, &fid) == NULL);
        assert(native_pair_tech(102, 5) == native_mod_at(2)->tech + 6 && native_pair_tech(5, 102) == native_mod_at(2)->tech + 6 && native_pair_tech(5, 5) == 0);
    }
    s_checks += 12;

    /* clashes: all of them lose, naming the others, whichever the order of the folders */
    for (int order = 0; order < 2; order++) {
        const char *a = order ? "bbb" : "aaa", *b = order ? "aaa" : "bbb";

        fresh();
        model("mods", 7000, 5), model("mods", 7001, 5);
        folder(a, manifest(472, 7000, NULL)), folder(b, manifest(472, 7001, NULL));
        folder("ccc", manifest(473, 7000, ", \"pair\": {\"ids\": [1, 2]}, \"effects\": [[500, 7000], [501, 7000]]"));
        folder("ddd", manifest(474, 7001, ", \"pair\": {\"ids\": [2, 1]}, \"effects\": [[501, 7000]]"));
        folder("eee", manifest(475, 7000, ", \"pair\": {\"ids\": [7, 8]}, \"effects\": [[502, 7000]]"));
        assert(loads() == 1 && native_mod_at(3) != NULL && strcmp(native_mod_at(3)->folder, "eee") == 0);
        assert(report("aaa")->status == NATIVE_MOD_REJECTED && strstr(report("aaa")->reason, "id 472 is also asked for by 'bbb'") != NULL);
        assert(strstr(report("bbb")->reason, "id 472 is also asked for by 'aaa'") != NULL);
        assert(strstr(report("ccc")->reason, "its fusion (1 and 2) is also made by 'ddd'") != NULL && strstr(report("ccc")->reason, "effect id 500") == NULL);
        assert(strstr(report("ddd")->reason, "effect id 501 is also used by 'ccc'") != NULL);
        assert(report("eee")->status == NATIVE_MOD_LOADED);
        s_checks += 7;
    }
    /* the same model and name are only warned about */
    fresh();
    model("mods", 7000, 5);
    folder("a", manifest(472, 7000, NULL)), folder("b", manifest(473, 7000, NULL));
    assert(loads() == 2 && strstr(report("a")->warnings, "model file is also that of 'b'") != NULL && strstr(report("b")->warnings, "model file is also that of 'a'") != NULL);
    s_checks++;

    /* turned off: no slot, no clash; an invalid one is refused all the same */
    fresh();
    model("mods", 7000, 5);
    folder("on", manifest(472, 7000, NULL));
    folder("off", manifest(472, 7000, ", \"enabled\": false"));
    folder("zero", manifest(473, 7999, ", \"enabled\": 0")); /* its model isn't there: nothing asked of a mod that is off */
    folder("badoff", manifest(480, 7000, ", \"enabled\": false"));
    assert(loads() == 1 && report("on")->status == NATIVE_MOD_LOADED && report("on")->reason[0] == 0);
    assert(report("off")->status == NATIVE_MOD_DISABLED && report("zero")->status == NATIVE_MOD_DISABLED);
    assert(report("badoff")->status == NATIVE_MOD_REJECTED && strstr(report("badoff")->reason, "id is 480") != NULL);
    s_checks += 4;

    /* folders that aren't mods: without a manifest, a file */
    fresh();
    model("mods", 7000, 5);
    folder("empty", NULL), folder("good", manifest(472, 7000, NULL));
    put("mods/digimon/readme.txt", "hi", 2);
    assert(loads() == 1 && report("empty")->status == NATIVE_MOD_SKIPPED && report("readme.txt") == NULL);
    {
        int n, dropped;

        native_mods_report(&n, &dropped);
        assert(n == 2 && dropped == 0);
    }
    s_checks += 3;

    /* no digimon folder at all */
    wipe(s_root);
    assert(loads() == 0);
    s_checks++;

    /* more folders than the report has room for: the report stops, the mods don't (the good one is the last of 301) */
    fresh();
    model("mods", 7000, 5);
    for (int i = 0; i < 300; i++) {
        char name[16];

        SDL_snprintf(name, sizeof name, "bad%03d", i);
        folder(name, "{");
    }
    folder("zzz", manifest(472, 7000, NULL));
    assert(loads() == 1 && native_mod_at(0) != NULL && strcmp(native_mod_at(0)->folder, "zzz") == 0);
    {
        int n, dropped;
        const NativeModReport *r = native_mods_report(&n, &dropped);

        assert(n == NATIVE_MOD_REPORTS && dropped == 301 - NATIVE_MOD_REPORTS && strcmp(r[0].folder, "bad000") == 0 && r[0].status == NATIVE_MOD_REJECTED);
        assert(report("zzz") == NULL); /* past the report's room */
    }
    s_checks += 4;

    wipe(s_root);
    printf("ok, %d checks\n", s_checks);
    return 0;
}
