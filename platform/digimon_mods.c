/* Digimon mods (native.h): data/mods/digimon/<name>/digimon.json, one more Digimon a partner can digivolve into each, read once at start.
   The fields (numbers, strings and arrays; the reader has no true/false):
     id           472 on, one a mod: the number of its form in a save, and the base of everything it takes (native.h)
     fid          its model, the file data/mods/<fid>.bin (a mod without it is left out)
     name         its name, ASCII
     like         a model whose partner record gives it cameras and a face (259, ExVeemon's)
     subs         [skeleton, motions, effects] in its file
     template     the DIGIMON_DATA entry it starts from (40, MetalGarurumon's): the growth tables of that Digimon
     hp_permille, mp_permille, stats [str, def, spirit, wisdom, speed], resists [fire, water, ice, wind, thunder, machine, dark],
     status [5], learn [6 levels], known [5 levels], needs [DIGIMON_DATA entry, level, entry, level] (1-based, as the growth lists)
     techs        six of {name, help {en, fr, it, de, es}, from, mp, power, element_power, element, script}
     signatures   how many of its last skills are signatures (1)
     tired, portrait_dz, face [part, frames, ...]   see NativeMod
     pair         {a, b, element, element_power, shift, scripts [3], motions [3]}: the two partners (ids) that fuse into it, the fusion's finale
     effects      [[effect id, fid], ...]: models of its techniques
     enabled      0 leaves it out (1) */
#include <SDL3/SDL.h>
#include <string.h>
#include "platform.h"
#include "../include/native.h"

static NativeMod s_mods[NATIVE_MAX_MODS];
static unsigned char s_on[NATIVE_MAX_MODS];

static int num(Json *o, const char *key, int def) {
    Json *j = json_get(o, key);
    return j != NULL && j->type == 'n' ? (int)j->num : def;
}

static void list(Json *o, const char *key, int *out, int n) {
    Json *a = json_get(o, key);
    for (int i = 0; i < n && a != NULL && i < json_len(a); i++) {
        out[i] = json_int(json_at(a, i));
    }
}

static void text(Json *o, const char *key, char *out, int size) {
    Json *j = json_get(o, key);
    if (j != NULL && j->type == 's') {
        SDL_strlcpy(out, j->str, size);
    }
}

static void read_techs(NativeMod *m, Json *techs) {
    for (int i = 0; i < 6 && techs != NULL && i < json_len(techs); i++) {
        Json *t = json_at(techs, i), *help = json_get(t, "help");
        NativeModTech *x = &m->techs[i];
        static const char *const LANGS[5] = {"en", "fr", "it", "de", "es"};

        x->from = num(t, "from", 61), x->mp = num(t, "mp", 10), x->power = num(t, "power", 100);
        x->element_power = num(t, "element_power", 1), x->script = num(t, "script", 0);
        x->element = num(t, "element", x->element_power > 1 ? 2 : 1);
        text(t, "name", x->name, sizeof x->name);
        for (int l = 0; l < 5; l++) { /* a language without its own line takes the English */
            text(help, LANGS[l], x->help[l], sizeof x->help[l]);
            if (x->help[l][0] == 0) {
                SDL_strlcpy(x->help[l], x->help[0], sizeof x->help[l]);
            }
        }
    }
}

static void read_mod(const char *folder, Json *o) {
    int id = num(o, "id", 0), k = id - NATIVE_MOD_FIRST_ID, fid = num(o, "fid", 0);
    char path[1100];
    SDL_PathInfo info;
    NativeMod *m;
    Json *pair = json_get(o, "pair"), *fx = json_get(o, "effects"), *face = json_get(o, "face");

    SDL_snprintf(path, sizeof path, "%s/mods/%04d.bin", data_dir(), fid);
    if (k < 0 || k >= NATIVE_MAX_MODS || s_on[k] || fid <= 0 || num(o, "enabled", 1) == 0 || !SDL_GetPathInfo(path, &info)) {
        LOG("digimon mod %s: left out (id %d, model %d)", folder, id, fid);
        return;
    }
    m = &s_mods[k];
    memset(m, 0, sizeof *m);
    m->id = id, m->fid = fid, m->slot = NATIVE_MOD_DATA + k, m->name_id = NATIVE_MOD_NAME + k, m->tech = NATIVE_MOD_TECH + 7 * k;
    m->like = num(o, "like", 259), m->template = num(o, "template", 40);
    m->hp_permille = num(o, "hp_permille", 1000), m->mp_permille = num(o, "mp_permille", 1000);
    m->signatures = SDL_clamp(num(o, "signatures", 1), 1, 6);
    m->tired = num(o, "tired", 0), m->portrait_dz = num(o, "portrait_dz", 0);
    list(o, "subs", m->subs, 3), list(o, "stats", m->stats, 5), list(o, "resists", m->resists, 7), list(o, "status", m->status, 5);
    list(o, "learn", m->learn, 6), list(o, "known", m->known, 5), list(o, "needs", m->needs, 4);
    read_techs(m, json_get(o, "techs"));
    SDL_strlcpy(m->folder, folder, sizeof m->folder);
    SDL_strlcpy(m->name, folder, sizeof m->name);
    text(o, "name", m->name, sizeof m->name);
    if (pair != NULL) {
        int ids[2] = {0, 0};

        list(pair, "ids", ids, 2);
        m->pair[0] = ids[0], m->pair[1] = ids[1];
        m->pair_element = num(pair, "element", 2), m->pair_element_power = num(pair, "element_power", 48), m->pair_shift = num(pair, "shift", 0);
        list(pair, "scripts", m->dna_scripts, 3), list(pair, "motions", m->dna_motions, 3);
    }
    for (int i = 0; i < 16 && fx != NULL && i < json_len(fx); i++) {
        m->fx[m->fx_n][0] = json_int(json_at(json_at(fx, i), 0)), m->fx[m->fx_n][1] = json_int(json_at(json_at(fx, i), 1));
        m->fx_n++;
    }
    for (int i = 0; i < 19 && face != NULL && i < json_len(face); i++) {
        m->face[i] = (unsigned char)json_int(json_at(face, i)), m->face_n = i + 1;
    }
    s_on[k] = 1;
    LOG("digimon mod %s: %s (id %d, model %d)", folder, m->name, id, fid);
}

/* every folder of data/mods/digimon with a digimon.json, once */
void native_mods_load(void) {
    char dir[1100], path[1200];
    char **names;
    int n = 0;

    SDL_snprintf(dir, sizeof dir, "%s/mods/digimon", data_dir());
    names = SDL_GlobDirectory(dir, "*", 0, &n);
    for (int i = 0; names != NULL && i < n; i++) {
        char *file;
        const char *p;
        Json *o;

        SDL_snprintf(path, sizeof path, "%s/%s/digimon.json", dir, names[i]);
        if ((file = SDL_LoadFile(path, NULL)) == NULL) {
            continue;
        }
        p = file;
        if ((o = json_value(&p)) != NULL) {
            read_mod(names[i], o);
            json_free(o);
        } else {
            LOG("digimon mod %s: digimon.json isn't JSON", names[i]);
        }
        SDL_free(file);
    }
    SDL_free(names);
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
    uint32_t subs, a, c, from, to;

    if (size[i] == 0) {
        size[i] = native_read_raw(m->fid, &f[i]);
        size[i] = size[i] == 0 ? -1 : size[i];
    }
    if (size[i] < 4 || (subs = rd32(f[i]) / 4) < 42 || rd32(f[i] + 4 * 41) >= size[i]) {
        return NULL;
    }
    a = rd32(f[i] + 4 * 41); /* the scripts' archive: a table of offsets, then the scripts */
    c = rd32(f[i] + a) / 4;
    if (k >= c) {
        return NULL;
    }
    from = a + rd32(f[i] + a + 4 * k);
    to = k + 1 < c ? a + rd32(f[i] + a + 4 * (k + 1)) : (uint32_t)size[i];
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
    static const short bridge[] = {7, 0, 15, 11, 15, 5, 0, 0, 5, 2, 1, 4, 2, 2, 3, 7, 1, 30, 11, 30};
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
