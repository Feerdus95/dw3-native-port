/* Digimon mods (native.h): data/mods/digimon/<name>/digimon.json, one more Digimon a partner can digivolve into each, read once at start.
   The fields (JSON, json.h: numbers, strings, booleans and arrays; unlisted fields are ignored with a warning):
     schema_version  1 (and what a manifest without one is); a bigger one belongs to a newer port and is refused
     author, version texts for whoever shares it, not read
     id           472 on, one a mod: the number of its form in a save, and the base of everything it takes (native.h)
     fid          its model, the file data/mods/<fid>.bin (a mod without it is left out)
     name         its name, ASCII (the folder's name without it)
     like         a model whose partner record gives it cameras and a face (259, ExVeemon's)
     subs         [skeleton, motions, effects] in its file
     template     the DIGIMON_DATA entry it starts from (40, MetalGarurumon's): the growth tables of that Digimon
     hp_permille, mp_permille, stats [str, def, spirit, wisdom, speed], resists [fire, water, ice, wind, thunder, machine, dark],
     status [5], learn [6 levels], known [5 levels], needs [DIGIMON_DATA entry, level, entry, level] (1-based, as the growth lists)
     techs        six of {name, help {en, fr, it, de, es}, from, mp, power, element_power, element, script}
     signatures   how many of its last skills are signatures (1)
     tired, portrait_dz, face [part, frames, ...]   see NativeMod
     pair         {ids [a, b], element, element_power, shift, scripts [3], motions [3]}: the two partners (ids) that fuse into it, the fusion's finale
     effects      [[effect id, fid], ...]: models of its techniques
     enabled      false (or 0) leaves it out: it takes no slot and clashes with nobody (true, or 1, when it is missing)

   Three steps, none of them trusting the file. The JSON is read (json.c); each manifest is checked on its own (check_mod: a mistake that
   would make the game read what isn't there, or a number its field can't hold, rejects the mod and says why; what is only unusual is
   a warning and the mod loads); then the mods that clash with one another are all rejected (settle): one id is one slot, one pair is one
   Tag menu fusion, one effect id is one model. Only then are the mods that are left made (native.h's NativeMod) and the game's tables
   take them. Folders are read in the order of their names, so the same folders give the same mods on every machine. */
#include <SDL3/SDL.h>
#include <math.h>
#include <string.h>
#include "platform.h"
#include "../include/native.h"

static NativeMod s_mods[NATIVE_MAX_MODS];
static unsigned char s_on[NATIVE_MAX_MODS];

/* --- one manifest, checked --- */

/* What is known of a folder while it goes through the steps (all of them, whatever the report has room for) */
typedef struct Entry {
    char *folder;
    char name[24];
    int status, id, fid;
    char reason[192], warn[192];
    NativeMod *mod; /* the made mod, while it is a candidate */
} Entry;

typedef struct Check {
    Entry *e;
    NativeMod *m;
    int enabled;
    char ctx[24]; /* "techs[2]." in front of a key, for the messages */
} Check;

static const char *what(Check *c, const char *key) {
    static char buf[64];

    SDL_snprintf(buf, sizeof buf, "%s%s", c->ctx, key);
    return buf;
}

static void reject(Check *c, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
static void reject(Check *c, const char *fmt, ...) {
    va_list ap;

    if (c->e->reason[0] == 0) {
        va_start(ap, fmt);
        SDL_vsnprintf(c->e->reason, sizeof c->e->reason, fmt, ap);
        va_end(ap);
    }
}

static void warn(Check *c, const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(2);
static void warn(Check *c, const char *fmt, ...) {
    size_t n = SDL_strlen(c->e->warn);
    va_list ap;

    if (n > 0 && n + 2 < sizeof c->e->warn) {
        SDL_memcpy(c->e->warn + n, "; ", 3), n += 2;
    }
    va_start(ap, fmt);
    SDL_vsnprintf(c->e->warn + n, sizeof c->e->warn - n, fmt, ap);
    va_end(ap);
}

#define BAD(c) ((c)->e->reason[0] != 0)

/* A whole number of KEY in LO..HI into *out (DEF when it isn't there, which a REQUIRED one doesn't allow) */
static int whole(Check *c, Json *o, const char *key, long lo, long hi, long def, int required, int *out) {
    Json *j = json_get(o, key);

    *out = (int)def;
    if (BAD(c)) {
        return 0;
    }
    if (j == NULL) {
        if (required) {
            reject(c, "%s is missing", what(c, key));
        }
        return 0;
    }
    if (j->type != 'n') {
        reject(c, "%s must be a number", what(c, key));
    } else if (j->num != floor(j->num)) {
        reject(c, "%s must be a whole number, it is %g", what(c, key), j->num);
    } else if (j->num < lo || j->num > hi) {
        reject(c, "%s is %.0f, but it can be %ld to %ld", what(c, key), j->num, lo, hi);
    } else {
        *out = (int)j->num;
        return 1;
    }
    return 0;
}

enum { EXACT, ABOUT, UP_TO }; /* a list's length: this many; this many or the rest filled in (warned); this many at most */

/* The list KEY of whole numbers in LO..HI into out[0..n-1]; how many it had */
static int list(Check *c, Json *o, const char *key, int *out, int n, long lo, long hi, int length) {
    Json *a = json_get(o, key);
    int len = 0;

    if (a == NULL || BAD(c)) {
        return 0;
    }
    if (a->type != '[') {
        reject(c, "%s must be a list", what(c, key));
        return 0;
    }
    len = json_len(a);
    if (len != n && (length == EXACT || (length == UP_TO && len > n))) {
        reject(c, "%s has %d values, it needs %s%d", what(c, key), len, length == UP_TO ? "up to " : "", n);
        return len;
    }
    if (len != n && length == ABOUT) {
        warn(c, "%s has %d values of %d (%s)", what(c, key), len, n, len < n ? "the rest are 0" : "the rest are ignored");
    }
    for (int i = 0; i < len && i < n; i++) {
        Json *v = json_at(a, i);

        if (v->type != 'n' || v->num != floor(v->num) || v->num < lo || v->num > hi) {
            reject(c, "%s[%d] must be a whole number from %ld to %ld", what(c, key), i, lo, hi);
            return len;
        }
        out[i] = (int)v->num;
    }
    return len;
}

/* A text of KEY into out[size]: ASCII, which is all the game's font has */
static void text(Check *c, Json *o, const char *key, char *out, int size, int required) {
    Json *j = json_get(o, key);

    if (BAD(c)) {
        return;
    }
    if (j == NULL) {
        if (required) {
            reject(c, "%s is missing", what(c, key));
        }
        return;
    }
    if (j->type != 's') {
        reject(c, "%s must be a text", what(c, key));
    } else if ((int)SDL_strlen(j->str) >= size) {
        reject(c, "%s has %d characters, it can have %d", what(c, key), (int)SDL_strlen(j->str), size - 1);
    } else {
        for (const char *s = j->str; *s; s++) {
            if ((unsigned char)*s < 0x20 || (unsigned char)*s > 0x7E) {
                reject(c, "%s has characters the game's font doesn't have, it can use only plain ASCII (0x20 to 0x7E)", what(c, key));
                return;
            }
        }
        SDL_strlcpy(out, j->str, size);
    }
}

/* true, false, 0 or 1 */
static int flag(Check *c, Json *o, const char *key, int def) {
    Json *j = json_get(o, key);

    if (j == NULL || BAD(c)) {
        return def;
    }
    if (j->type == 'b' || (j->type == 'n' && (j->num == 0 || j->num == 1))) {
        return j->num != 0;
    }
    reject(c, "%s must be true, false, 0 or 1", what(c, key));
    return def;
}

/* the keys an object has that are not KNOWN: one warning */
static void unknown_keys(Check *c, Json *o, const char *const *known, int n) {
    char names[96] = "";
    int count = 0;

    for (Json *k = o != NULL ? o->child : NULL; k != NULL; k = k->next) {
        int i = 0;

        while (i < n && SDL_strcmp(k->key, known[i]) != 0) {
            i++;
        }
        if (i == n && count++ < 3) {
            SDL_snprintf(names + SDL_strlen(names), sizeof names - SDL_strlen(names), "%s%s", count > 1 ? ", " : "", k->key);
        }
    }
    if (count > 0) {
        warn(c, "%sunknown key%s: %s%s", c->ctx, count > 1 ? "s" : "", names, count > 3 ? ", ..." : "");
    }
}

static void check_techs(Check *c, Json *techs) {
    static const char *const LANGS[5] = {"en", "fr", "it", "de", "es"};
    static const char *const KEYS[] = {"name", "help", "from", "mp", "power", "element_power", "element", "script"};

    if (techs == NULL || techs->type != '[' || json_len(techs) != 6) {
        reject(c, "techs needs a list of six techniques%s", techs != NULL && techs->type == '[' ? "" : ", it is missing or isn't a list");
        return;
    }
    for (int i = 0; i < 6 && !BAD(c); i++) {
        Json *t = json_at(techs, i), *help;
        NativeModTech *x = &c->m->techs[i];

        SDL_snprintf(c->ctx, sizeof c->ctx, "techs[%d].", i);
        if (t->type != '{') {
            reject(c, "techs[%d] must be an object", i);
            break;
        }
        whole(c, t, "from", 1, NATIVE_MOD_TECH - 1, 61, 0, &x->from); /* a technique of the game's: the mods' own aren't made yet */
        whole(c, t, "mp", 0, 65535, 10, 0, &x->mp);
        whole(c, t, "power", 0, 65535, 100, 0, &x->power);
        whole(c, t, "element_power", 0, 255, 1, 0, &x->element_power);
        whole(c, t, "element", 0, 255, x->element_power > 1 ? 2 : 1, 0, &x->element);
        whole(c, t, "script", 0, 255, 0, 0, &x->script);
        if (x->script == 5 || x->script == 12) {
            warn(c, "techs[%d].script %d is one of the game's special scripts", i, x->script);
        }
        text(c, t, "name", x->name, sizeof x->name, 0);
        if (x->name[0] == 0 && !BAD(c)) {
            warn(c, "techs[%d] has no name", i);
        }
        if ((help = json_get(t, "help")) != NULL && help->type != '{') {
            reject(c, "techs[%d].help must be an object", i);
        }
        for (int l = 0; l < 5 && !BAD(c); l++) { /* a language without its own line takes the English */
            text(c, help, LANGS[l], x->help[l], sizeof x->help[l], 0);
            if (x->help[l][0] == 0) {
                SDL_strlcpy(x->help[l], x->help[0], sizeof x->help[l]);
            }
        }
        if (x->help[0][0] == 0 && !BAD(c)) {
            reject(c, "techs[%d].help.en is missing or empty (the other languages use it when they have no line)", i);
        }
        unknown_keys(c, t, KEYS, 8);
    }
    c->ctx[0] = 0;
}

static void check_pair(Check *c, Json *pair) {
    static const char *const KEYS[] = {"ids", "element", "element_power", "shift", "scripts", "motions"};
    NativeMod *m = c->m;
    int ids[2] = {0, 0};

    if (pair->type != '{') {
        reject(c, "pair must be an object");
        return;
    }
    SDL_strlcpy(c->ctx, "pair.", sizeof c->ctx);
    if (json_get(pair, "ids") == NULL) {
        reject(c, "pair.ids is missing");
    }
    list(c, pair, "ids", ids, 2, 1, 65535, EXACT);
    m->pair[0] = ids[0], m->pair[1] = ids[1];
    if (!BAD(c) && ids[0] == ids[1]) {
        reject(c, "pair.ids names the same Digimon (%d) twice", ids[0]);
    } else if (!BAD(c) && (ids[0] == m->id || ids[1] == m->id)) {
        reject(c, "pair.ids names the mod itself (%d): nothing can fuse into it", m->id);
    }
    whole(c, pair, "element", 0, 255, 2, 0, &m->pair_element);
    whole(c, pair, "element_power", 0, 255, 48, 0, &m->pair_element_power);
    whole(c, pair, "shift", -32768, 32767, 0, 0, &m->pair_shift);
    list(c, pair, "scripts", m->dna_scripts, 3, 0, 32767, ABOUT);
    list(c, pair, "motions", m->dna_motions, 3, 0, 32767, ABOUT);
    unknown_keys(c, pair, KEYS, 6);
    c->ctx[0] = 0;
}

static void check_effects(Check *c, Json *fx) {
    NativeMod *m = c->m;

    if (fx->type != '[' || json_len(fx) > 16) {
        reject(c, "effects must be a list of up to 16 pairs");
        return;
    }
    for (Json *e = fx->child; e != NULL && !BAD(c); e = e->next) {
        Json *id = json_at(e, 0), *fid = json_at(e, 1);

        if (e->type != '[' || json_len(e) != 2 || id->type != 'n' || fid->type != 'n' || id->num != floor(id->num) || fid->num != floor(fid->num) ||
            id->num < 1 || id->num > 65535 || fid->num < 1 || fid->num >= NATIVE_MAX_FIDS) {
            reject(c, "effects[%d] must be [effect id 1 to 65535, file 1 to %d]", m->fx_n, NATIVE_MAX_FIDS - 1);
            return;
        }
        for (int i = 0; i < m->fx_n; i++) {
            if (m->fx[i][0] == (int)id->num) {
                reject(c, "effect id %d is in effects twice", (int)id->num);
                return;
            }
        }
        m->fx[m->fx_n][0] = (int)id->num, m->fx[m->fx_n][1] = (int)fid->num;
        m->fx_n++;
    }
}

static uint32_t rd32(const uint8_t *p);

/* Script K of the model F of SIZE bytes (entry 41 is the scripts' archive: a table of offsets, the scripts after it; a script runs to the next one's
   start, the last to the end). check_files() and finale() both read it here, so what the loader accepted is what the runtime reads, on a file that
   isn't the one the loader saw as well. MOD_SCRIPT_OK: [*from, *to) is inside the archive's data and *count is its scripts; MOD_SCRIPT_NONE: the model
   has no archive; MOD_SCRIPT_BAD: *why says what is wrong (the table of entries, the archive's place, its table, K or the script's offsets) */
enum { MOD_SCRIPT_OK, MOD_SCRIPT_NONE, MOD_SCRIPT_BAD };
int native_mod_script_at(const uint8_t *f, size_t size, int k, uint32_t *from, uint32_t *to, int *count, const char **why);
int native_mod_script_at(const uint8_t *f, size_t size, int k, uint32_t *from, uint32_t *to, int *count, const char **why) {
    uint64_t first, a, table, lo, hi, at, start, end;

    *why = "the model's table of entries isn't inside the file";
    if (f == NULL || size < 4 || (first = rd32(f)) < 4 || first % 4 != 0 || first > size) {
        return MOD_SCRIPT_BAD;
    }
    if (first / 4 < 42 || rd32(f + 4 * 41) == 0) {
        return MOD_SCRIPT_NONE;
    }
    a = rd32(f + 4 * 41);
    if (a < first || a + 4 > size) {
        *why = a < first ? "the archive (entry 41) starts inside the model's table of entries" : "the archive (entry 41) starts past the end of the file";
        return MOD_SCRIPT_BAD;
    }
    table = rd32(f + a); /* its first offset is where its scripts start: its table has table / 4 of them */
    if (table < 4 || table % 4 != 0 || a + table > size) {
        *why = "the archive's table of offsets isn't inside the file";
        return MOD_SCRIPT_BAD;
    }
    *count = (int)(table / 4);
    if (k < 0 || k >= *count) {
        *why = "the archive has fewer scripts";
        return MOD_SCRIPT_BAD;
    }
    lo = a + table;
    hi = size;
    at = a + 4 * (uint64_t)k; /* 64 bits throughout: no product or sum wraps, whatever the file says */
    start = a + rd32(f + at);
    end = k + 1 < *count ? a + rd32(f + at + 4) : hi;
    if (start < lo || start > hi) {
        *why = "the script starts outside the archive's data";
        return MOD_SCRIPT_BAD;
    }
    if (end < start || end > hi) {
        *why = "the script ends before it starts or past the end of the file";
        return MOD_SCRIPT_BAD;
    }
    if (start % 2 != 0 || (k + 1 < *count && end % 2 != 0)) { /* finale() reads it as 16-bit words (the last script's odd end only drops a byte) */
        *why = "the script isn't aligned to 2 bytes";
        return MOD_SCRIPT_BAD;
    }
    *from = (uint32_t)start;
    *to = (uint32_t)end;
    return MOD_SCRIPT_OK;
}

/* The model's file in its place, readable as the game reads it (a table of offsets, the entries subs asks for inside the file, the scripts
   pair asks for among its scripts), and no file of the game's disc replaced by it. Enabled mods only */
static void check_files(Check *c) {
    NativeMod *m = c->m;
    char path[1200];
    uint8_t *f;
    size_t size;
    uint32_t first, off;
    char missing[96] = "";
    int count;

    if (native_disc_file(m->fid)) {
        reject(c, "fid %d is a file of the game's disc: data/mods/%04d.bin would replace it in the whole game", m->fid, m->fid);
        return;
    }
    SDL_snprintf(path, sizeof path, "%s/mods/%04d.bin", data_dir(), m->fid);
    if ((f = SDL_LoadFile(path, &size)) == NULL) {
        reject(c, "the model data/mods/%04d.bin isn't there", m->fid);
        return;
    }
    first = size >= 4 ? rd32(f) : 0; /* the first offset is where the entries start: the table before it has first / 4 of them */
    if (size < 4 || first < 4 || first % 4 != 0 || first > size) {
        reject(c, "the model data/mods/%04d.bin isn't a model file (its table of entries starts at %u, in a file of %u bytes)", m->fid, (unsigned)first,
               (unsigned)size);
        SDL_free(f);
        return;
    }
    count = (int)(first / 4);
    for (int i = 0; i < 3; i++) {
        if (m->subs[i] >= count) {
            reject(c, "subs[%d] is %d, but the model has %d entries", i, m->subs[i], count);
            break;
        }
        off = rd32(f + 4 * m->subs[i]); /* an entry starts after the table and has at least its first word (every entry the game reads is one) */
        if (off < first || (uint64_t)off + 4 > size) {
            reject(c, "subs[%d] (entry %d) starts at %u, outside the model's data (%u to %u)", i, m->subs[i], (unsigned)off, (unsigned)first, (unsigned)size);
            break;
        }
    }
    if (!BAD(c) && m->pair[0] > 0) { /* the scripts finale() reads, by the same rules it reads them with */
        const char *why = "";
        int scripts = 0;

        for (int i = 0; i < 3 && !BAD(c); i++) {
            uint32_t from, to;

            switch (native_mod_script_at(f, size, m->dna_scripts[i], &from, &to, &scripts, &why)) {
            case MOD_SCRIPT_NONE:
                warn(c, "the model has no scripts' archive (entry 41): the fusion plays only its motions");
                i = 3;
                break;
            case MOD_SCRIPT_BAD:
                reject(c, "pair.scripts[%d] (script %d): %s", i, m->dna_scripts[i], why);
                break;
            default:
                break;
            }
        }
    }
    SDL_free(f);
    if (!BAD(c)) { /* the partner whose record (cameras, face, distance) the mod's is made from; no fighters' file to look in: nothing to say */
        uint8_t *fighters = NULL;
        long n = native_read_raw(NATIVE_FIGHTERS_FILE, &fighters);

        if (n > 0 && !native_fighter_has_record(fighters, n, m->like)) {
            reject(c, "like %d isn't a partner of the game's fighters' file (259 is ExVeemon's)", m->like);
        }
        free(fighters); /* calloc'd by native_read_raw: not SDL's heap */
    }
    if (BAD(c)) {
        return;
    }
    for (int i = 0; i < m->fx_n; i++) { /* a technique's model that isn't there just plays without it */
        if (!native_disk_file(m->fx[i][1])) {
            SDL_snprintf(missing + SDL_strlen(missing), sizeof missing - SDL_strlen(missing), "%s%04d.bin", missing[0] ? ", " : "", m->fx[i][1]);
        }
    }
    if (missing[0]) {
        warn(c, "the effect models %s aren't there", missing);
    }
}

/* The manifest ROOT of folder E into a NativeMod, or the reason it can't be one */
static void check_mod(Entry *e, Json *root, NativeMod *m) {
    static const char *const KEYS[] = {"schema_version", "author", "version", "enabled", "id", "fid", "name", "like", "subs", "template",
                                       "hp_permille", "mp_permille", "stats", "resists", "status", "learn", "known", "needs", "techs",
                                       "signatures", "tired", "portrait_dz", "face", "pair", "effects"};
    Check chk = {e, m, 1, ""}, *c = &chk;
    Json *j;
    int v;

    if (root->type != '{') {
        reject(c, "digimon.json must hold an object");
        return;
    }
    if ((j = json_get(root, "schema_version")) != NULL) {
        if (whole(c, root, "schema_version", 1, 1000000, 1, 0, &v) && v > NATIVE_MOD_SCHEMA) {
            reject(c, "schema_version %d is for a newer port (this one reads up to %d)", v, NATIVE_MOD_SCHEMA);
        }
    }
    for (const char *const *k = KEYS + 1; k < KEYS + 3; k++) {
        if ((j = json_get(root, *k)) != NULL && j->type != 's') {
            warn(c, "%s should be a text", *k);
        }
    }
    c->enabled = flag(c, root, "enabled", 1);
    whole(c, root, "id", NATIVE_MOD_FIRST_ID, NATIVE_MOD_FIRST_ID + NATIVE_MAX_MODS - 1, 0, 1, &m->id);
    whole(c, root, "fid", 1, NATIVE_MAX_FIDS - 1, 0, 1, &m->fid);
    e->id = m->id, e->fid = m->fid;
    text(c, root, "name", m->name, sizeof m->name, 0);
    if (m->name[0] == 0 && !BAD(c)) { /* the folder's name, as long as the name can be */
        SDL_strlcpy(m->name, e->folder, sizeof m->name);
        if (SDL_strlen(e->folder) >= sizeof m->name) {
            warn(c, "name is missing and the folder's is cut to %d characters", (int)sizeof m->name - 1);
        }
    }
    SDL_strlcpy(e->name, m->name, sizeof e->name);
    whole(c, root, "like", 1, 65535, 259, 0, &m->like);
    whole(c, root, "template", 0, NATIVE_MOD_DATA - 1, 40, 0, &m->template); /* the game's entries: the mods' own are made after it */
    if (m->template == 52 || m->template == 54) {
        warn(c, "template %d is one of the game's empty placeholder entries", m->template);
    }
    whole(c, root, "hp_permille", 1, 2000, 1000, 0, &m->hp_permille);
    whole(c, root, "mp_permille", 1, 2000, 1000, 0, &m->mp_permille);
    if (json_get(root, "subs") == NULL) {
        warn(c, "subs is missing, [0, 0, 0] is used");
    }
    list(c, root, "subs", m->subs, 3, 0, 65535, EXACT);
    list(c, root, "stats", m->stats, 5, 0, 65535, ABOUT);
    list(c, root, "resists", m->resists, 7, 0, 65535, ABOUT);
    list(c, root, "status", m->status, 5, 0, 255, ABOUT);
    list(c, root, "learn", m->learn, 6, 0, 255, ABOUT);
    list(c, root, "known", m->known, 5, 0, 255, ABOUT);
    for (int i = 0; i < 6 && !BAD(c); i++) {
        if (m->learn[i] > 99 || (i < 5 && m->known[i] > 99)) {
            warn(c, "learn and known levels go up to 99");
            break;
        }
    }
    if (json_get(root, "needs") != NULL) {
        list(c, root, "needs", m->needs, 4, 0, 99, EXACT);
        for (int i = 0; i < 4 && !BAD(c); i += 2) {
            if (m->needs[i] > NATIVE_MOD_DATA) {
                reject(c, "needs[%d] is %d: the Digimon are the game's entries, 1 to %d (or 0 for none)", i, m->needs[i], NATIVE_MOD_DATA);
            } else if (m->needs[i] > 0 && m->needs[i + 1] < 1) {
                reject(c, "needs[%d] is the level of Digimon %d, it can be 1 to 99", i + 1, m->needs[i]);
            }
        }
        if (!BAD(c) && (m->needs[0] > 0) != (m->needs[2] > 0)) {
            warn(c, "needs does nothing unless both Digimon (1st and 3rd value) are set");
        }
    }
    check_techs(c, json_get(root, "techs"));
    whole(c, root, "signatures", -1000000, 1000000, 1, 0, &v);
    m->signatures = SDL_clamp(v, 1, 6);
    if (v != m->signatures && !BAD(c)) {
        warn(c, "signatures %d is brought to %d (1 to 6)", v, m->signatures);
    }
    m->tired = flag(c, root, "tired", 0);
    whole(c, root, "portrait_dz", -32768, 32767, 0, 0, &m->portrait_dz);
    if (json_get(root, "face") != NULL) {
        int face[19] = {0};

        v = list(c, root, "face", face, 19, 0, 254, UP_TO); /* 255 ends the table */
        for (int i = 0; i < v && i < 19; i++) {
            m->face[i] = (unsigned char)face[i], m->face_n = i + 1;
        }
        if (!BAD(c) && v % 2 != 0) {
            warn(c, "face has an odd number of values (part, frames, ...)");
        }
    }
    if ((j = json_get(root, "pair")) != NULL) {
        check_pair(c, j);
    }
    if ((j = json_get(root, "effects")) != NULL) {
        check_effects(c, j);
    }
    unknown_keys(c, root, KEYS, (int)SDL_arraysize(KEYS));
    if (!BAD(c) && c->enabled) {
        check_files(c);
    }
    e->status = BAD(c) ? NATIVE_MOD_REJECTED : c->enabled ? NATIVE_MOD_LOADED : NATIVE_MOD_DISABLED;
}

/* --- the mods together --- */

static Entry *s_entries;
static int s_entry_n;
static NativeModReport s_report[NATIVE_MOD_REPORTS];
static int s_report_n, s_dropped;

static int pair_of(const NativeMod *a, const NativeMod *b) {
    return a->pair[0] != 0 && b->pair[0] != 0 && ((a->pair[0] == b->pair[0] && a->pair[1] == b->pair[1]) || (a->pair[0] == b->pair[1] && a->pair[1] == b->pair[0]));
}

static int effect_of(const NativeMod *a, const NativeMod *b) {
    for (int i = 0; i < a->fx_n; i++) {
        for (int j = 0; j < b->fx_n; j++) {
            if (a->fx[i][0] == b->fx[j][0]) {
                return a->fx[i][0];
            }
        }
    }
    return 0;
}

/* Candidates that want what another wants are all rejected (no one gets it by the order of the folders), each told with whom it clashes */
static void settle(void) {
    for (int i = 0; i < s_entry_n; i++) {
        Entry *a = &s_entries[i];

        for (int kind = 0; a->mod != NULL && kind < 3; kind++) {
            const char *with[3];
            int n = 0, value = 0;
            char line[160];

            for (int j = 0; j < s_entry_n; j++) {
                const NativeMod *b = s_entries[j].mod;
                int hit = b == NULL || j == i ? 0 : kind == 0 ? a->mod->id == b->id : kind == 1 ? pair_of(a->mod, b) : effect_of(a->mod, b);

                if (hit) {
                    value = kind == 2 ? effect_of(a->mod, b) : value;
                    if (n < 3) {
                        with[n] = s_entries[j].folder;
                    }
                    n++;
                }
            }
            if (n == 0) {
                continue;
            }
            SDL_snprintf(line, sizeof line, kind == 0 ? "id %d is also asked for by" : kind == 1 ? "its fusion (%d and %d) is also made by" : "effect id %d is also used by",
                         kind == 1 ? a->mod->pair[0] : kind == 0 ? a->mod->id : value, a->mod->pair[1]);
            for (int k = 0; k < n && k < 3; k++) {
                SDL_snprintf(line + SDL_strlen(line), sizeof line - SDL_strlen(line), "%s '%s'", k ? "," : "", with[k]);
            }
            if (n > 3) {
                SDL_snprintf(line + SDL_strlen(line), sizeof line - SDL_strlen(line), " and %d more", n - 3);
            }
            SDL_snprintf(a->reason + SDL_strlen(a->reason), sizeof a->reason - SDL_strlen(a->reason), "%s%s", a->reason[0] ? "; " : "", line);
        }
    }
    for (int i = 0; i < s_entry_n; i++) { /* all at once: whoever clashed lost, however the others did */
        Entry *a = &s_entries[i];

        if (a->mod != NULL && a->reason[0] != 0) {
            a->status = NATIVE_MOD_REJECTED;
        }
    }
    for (int i = 0; i < s_entry_n; i++) { /* the ones left that share what harms no one, only worth a word */
        for (int j = i + 1; j < s_entry_n; j++) {
            Entry *a = &s_entries[i], *b = &s_entries[j];

            if (a->status != NATIVE_MOD_LOADED || b->status != NATIVE_MOD_LOADED || a->reason[0] || b->reason[0]) {
                continue;
            }
            if (a->fid == b->fid || SDL_strcmp(a->name, b->name) == 0) {
                const char *same = a->fid == b->fid ? "model file" : "name";

                warn(&(Check){a, NULL, 0, ""}, "the %s is also that of '%s'", same, b->folder);
                warn(&(Check){b, NULL, 0, ""}, "the %s is also that of '%s'", same, a->folder);
            }
        }
    }
}

static int by_name(const void *a, const void *b) {
    return SDL_strcmp(*(char *const *)a, *(char *const *)b);
}

static void clear(void) {
    for (int i = 0; i < s_entry_n; i++) {
        SDL_free(s_entries[i].folder);
        SDL_free(s_entries[i].mod);
    }
    SDL_free(s_entries);
    s_entries = NULL, s_entry_n = 0, s_report_n = 0, s_dropped = 0;
    SDL_memset(s_mods, 0, sizeof s_mods), SDL_memset(s_on, 0, sizeof s_on);
}

/* every folder of data/mods/digimon, once */
void native_mods_load(void) {
    char dir[1100], path[1200];
    char **names;
    int n = 0, room = 0;

    clear();
    SDL_snprintf(dir, sizeof dir, "%s/mods/digimon", data_dir());
    names = SDL_GlobDirectory(dir, "*", 0, &n);
    if (names != NULL) {
        SDL_qsort(names, n, sizeof *names, by_name);
    }
    for (int i = 0; names != NULL && i < n; i++) {
        SDL_PathInfo info;
        Entry *e;
        Json *root;
        JsonInfo ji;

        SDL_snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        if (SDL_strlen(names[i]) > 200 || !SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
            continue;
        }
        if (s_entry_n == room) {
            Entry *more = SDL_realloc(s_entries, (room = room ? room * 2 : 16) * sizeof *s_entries);

            if (more == NULL) {
                LOG("digimon mods: out of memory, the folders from %s on are left out", names[i]);
                break;
            }
            s_entries = more;
        }
        e = &s_entries[s_entry_n];
        SDL_memset(e, 0, sizeof *e);
        e->folder = SDL_strdup(names[i]);
        e->mod = SDL_calloc(1, sizeof *e->mod);
        if (e->folder == NULL || e->mod == NULL) {
            SDL_free(e->folder), SDL_free(e->mod);
            LOG("digimon mods: out of memory, the folders from %s on are left out", names[i]);
            break;
        }
        s_entry_n++;
        SDL_snprintf(path, sizeof path, "%s/%s/digimon.json", dir, names[i]);
        if (!SDL_GetPathInfo(path, &info)) {
            e->status = NATIVE_MOD_SKIPPED;
            SDL_strlcpy(e->reason, "no digimon.json in it", sizeof e->reason);
        } else if ((root = json_load(path, JSON_LIMITS_MANIFEST, &ji)) == NULL) {
            e->status = NATIVE_MOD_REJECTED;
            SDL_snprintf(e->reason, sizeof e->reason, "digimon.json: %s", ji.msg);
            if (ji.line > 0) {
                SDL_snprintf(e->reason + SDL_strlen(e->reason), sizeof e->reason - SDL_strlen(e->reason), " (line %d, column %d)", ji.line, ji.col);
            }
        } else {
            check_mod(e, root, e->mod);
            if (ji.commas > 0 && e->status != NATIVE_MOD_REJECTED) {
                warn(&(Check){e, e->mod, 0, ""}, "%d trailing comma%s", ji.commas, ji.commas > 1 ? "s" : "");
            }
            json_free(root);
        }
        if (e->status != NATIVE_MOD_LOADED) { /* only an enabled, valid manifest is a candidate */
            SDL_free(e->mod);
            e->mod = NULL;
        }
    }
    SDL_free(names);
    settle();
    for (int i = 0; i < s_entry_n; i++) {
        Entry *e = &s_entries[i];

        if (e->status == NATIVE_MOD_LOADED && e->reason[0] == 0) {
            int k = e->mod->id - NATIVE_MOD_FIRST_ID;

            s_mods[k] = *e->mod;
            s_mods[k].slot = NATIVE_MOD_DATA + k, s_mods[k].name_id = NATIVE_MOD_NAME + k, s_mods[k].tech = NATIVE_MOD_TECH + 7 * k;
            SDL_strlcpy(s_mods[k].folder, e->folder, sizeof s_mods[k].folder);
            s_on[k] = 1;
            LOG("digimon mod %s: %s (id %d, model %d)", e->folder, e->name, e->id, e->fid);
        } else if (e->status == NATIVE_MOD_DISABLED) {
            SDL_strlcpy(e->reason, "turned off (enabled is false)", sizeof e->reason);
            LOG("digimon mod %s: left out, %s", e->folder, e->reason);
        } else if (e->status != NATIVE_MOD_SKIPPED) {
            e->status = NATIVE_MOD_REJECTED;
            LOG("digimon mod %s: rejected, %s", e->folder, e->reason);
        }
        if (e->warn[0] && e->status != NATIVE_MOD_SKIPPED) {
            LOG("digimon mod %s: warning, %s", e->folder, e->warn);
        }
        if (i < NATIVE_MOD_REPORTS) {
            NativeModReport *r = &s_report[s_report_n++];

            SDL_strlcpy(r->folder, e->folder, sizeof r->folder), SDL_strlcpy(r->name, e->name, sizeof r->name);
            r->id = e->id, r->fid = e->fid, r->status = e->status;
            SDL_strlcpy(r->reason, e->status == NATIVE_MOD_LOADED ? "" : e->reason, sizeof r->reason), SDL_strlcpy(r->warnings, e->warn, sizeof r->warnings);
        } else {
            s_dropped++;
        }
    }
}

/* the report of the last load: its rows in the folders' order, and how many more folders there were than rows */
const NativeModReport *native_mods_report(int *count, int *dropped) {
    *count = s_report_n, *dropped = s_dropped;
    return s_report;
}

const NativeMod *native_mod_at(int k) {
    return k >= 0 && k < NATIVE_MAX_MODS && s_on[k] ? &s_mods[k] : NULL;
}

const NativeMod *native_mod_by_id(int id) {
    return native_mod_at(id - NATIVE_MOD_FIRST_ID);
}

const NativeMod *native_mod_for_effect(int id, int *fid) {
    for (int k = 0; k < NATIVE_MAX_MODS; k++) {
        for (int i = 0; s_on[k] && i < s_mods[k].fx_n; i++) {
            if (s_mods[k].fx[i][0] == id) {
                *fid = s_mods[k].fx[i][1];
                return &s_mods[k];
            }
        }
    }
    return NULL;
}

/* --- the fusion: the pair's DNA script with the mod as the DNA form --- */

static const NativeMod *s_fusing;

/* the mod whose fusion technique PAIR_TECH is; the other partner of the pair as ACTIVE sees it, or 0 */
int native_dna_begin(int pair_tech, int active) {
    s_fusing = NULL;
    for (int k = 0; k < NATIVE_MAX_MODS; k++) {
        const NativeMod *m = native_mod_at(k);

        if (m != NULL && m->pair[0] != 0 && pair_tech == m->tech + 6) {
            s_fusing = m;
            return active == m->pair[0] ? m->pair[1] : m->pair[0];
        }
    }
    return 0;
}

/* the tag menu's technique for ACTIVE with PARTNER: a mod's fusion, or 0 */
int native_pair_tech(int active, int partner) {
    for (int k = 0; k < NATIVE_MAX_MODS; k++) {
        const NativeMod *m = native_mod_at(k);

        if (m != NULL && m->pair[0] != 0 && ((m->pair[0] == active && m->pair[1] == partner) || (m->pair[1] == active && m->pair[0] == partner))) {
            return m->tech + 6;
        }
    }
    return 0;
}

static uint32_t rd32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

/* the finale of the fusion: the mod's attack K (a script of its file: dw2-extractor/flame_dcb.py dna_body) without its end, or NULL */
static const short *finale(const NativeMod *m, int k, int *n) {
    static uint8_t *f[NATIVE_MAX_MODS];
    static long size[NATIVE_MAX_MODS];
    int i = m->id - NATIVE_MOD_FIRST_ID;
    uint32_t from, to;
    int count;
    const char *why;

    if (size[i] == 0) {
        size[i] = native_read_raw(m->fid, &f[i]);
        size[i] = size[i] == 0 ? -1 : size[i];
    }
    if (size[i] < 4 || native_mod_script_at(f[i], (size_t)size[i], k, &from, &to, &count, &why) != MOD_SCRIPT_OK) {
        return NULL;
    }
    for (*n = (int)(to - from) / 2; *n > 0 && ((const short *)(f[i] + from))[*n - 1] == 0; (*n)--) {
    }
    if (*n < 2 || ((const short *)(f[i] + from))[*n - 1] != 255) {
        return NULL;
    }
    (*n)--; /* its own end */
    return (const short *)(f[i] + from);
}

/* script 12 of a pair's fighter (the game's DNA fusion) with the other partner and the mod as the DNA form; the form's attack
   (the commands from its motion 56 to the stage's return) is one of the mod's three, at the random number RND */
short *native_dna_script(const short *pc, int other, unsigned rnd) {
    static short v[1024], w[2048];
    /* the fusion's attack scripts start the looping sound 89 (10, 89, 0) and the model's own scripts stop it at their end (10, 91, 0), which the finale
       leaves out: stopped here, first thing, or it plays on until the next map or fight */
    static const short bridge[] = {10, 91, 0, 7, 0, 15, 11, 15, 5, 0, 0, 5, 2, 1, 4, 2, 2, 3, 7, 1, 30, 11, 30};
    const NativeMod *mod = s_fusing;
    const short *fin;
    int i, k = -1, tail = -1, end = -1, n = 0, m = 0, pick = (int)(rnd % 3);

    memcpy(v, pc, sizeof v); /* script 12 is a few hundred words and has the archive's other scripts after it */
    if (mod == NULL) {
        return v;
    }
    for (i = 0; i < 60; i++) { /* "model add" (2, 6, who, fighter): the partner is model 2, the DNA form 3 */
        if (v[i] == 2 && v[i + 1] == 6 && (v[i + 2] == 2 || v[i + 2] == 3)) {
            v[i + 3] = (short)(v[i + 2] == 2 ? other : mod->id);
        }
    }
    for (i = 0; other == mod->pair[1] && mod->pair_shift != 0 && i < 160; i++) { /* a partner bigger than the game's: its flight farther still */
        if (v[i] == 2 && v[i + 1] == 3 && v[i + 2] == 2) {
            v[i + 5] -= (short)mod->pair_shift;
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
    fin = end >= 0 ? finale(mod, mod->dna_scripts[pick], &n) : NULL;
    if (fin == NULL) { /* no finale: the old way, only its motion */
        if (k >= 0) {
            v[k + 3] = (short)mod->dna_motions[pick];
        }
        return v;
    }
    if (v[k - 2] == 11) { /* the form's entrance lasts the motion's 60 ticks, not the 180 of the game's own forms */
        v[k - 1] = 75;
    }
    memcpy(w, v, k * sizeof *w);
    memcpy(w + k, fin, n * sizeof *w);
    m = k + n;
    if (w[m - 2] == 11 && w[m - 1] > 20) { /* the attack's own pause after its last hit */
        w[m - 1] = 20;
    }
    /* the DNA form's leaving, as the game's: a white fade, the field's camera, the form hidden, the fade back in */
    memcpy(w + m, bridge, sizeof bridge);
    m += sizeof bridge / sizeof *bridge;
    memcpy(w + m, v + tail, (end + 1 - tail) * sizeof *w);
    return w;
}
