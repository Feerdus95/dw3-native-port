/* BladeGarurumon as a partner's Digimon (the 45th: patches/src/main/game,
   stfgtrep): the DNA form, whose model (M469GARM) has no animations of its
   own, gets MetalGarurumon's (M196MGAR), retargeted node by node, and its
   signature Solar Laser: a model 471 of its own, with a record of its own in
   the fighters' file. Both are made at start from the player's files; the
   DNA keeps its 469 as it is.

   Retargeting (kb: battle_models): MetalGarurumon's rotation tracks on the
   nodes that match (both skeletons rest at rotation 0), BladeGarurumon's
   translations (its bones' lengths), the roots with MetalGarurumon's moves
   scaled by height; nodes without a match hold their rest pose. The timeline
   keeps BladeGarurumon's idle (to t 110), then MetalGarurumon's, plus the
   DNA's attack (t 230-469) moved past it to replace the signature's moves. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

#define FIGHTERS_FILE 0x1CC /* SMDLDATA.PRO */
#define NEW_FID 1           /* the Japanese MSAMTMAP, which the European game never reads */
#define NEW_MODEL 471
#define OLD_MODEL 469 /* BladeGarurumon's DNA form */
#define MG_MODEL 196  /* MetalGarurumon */
#define RECORD 196    /* a partner's FighterInfo, in bytes */
#define BG_SKEL 73
#define BG_ANIM 36
#define MG_SKEL 51
#define MG_ANIM 39
#define END 30000
#define IDLE_END 110 /* BladeGarurumon's idle, kept: motion 0, t 10-109 */
#define REF_T 10     /* both models' pose of reference: their idle's start */
#define ATK_FROM 230 /* the DNA's attack (motion 56), its nodes 29-34 the beam and blades */
#define ATK_TO 469
#define ATK_SHIFT 2000

/* BladeGarurumon's node -> MetalGarurumon's, by shape and side */
static const int NODE_MAP[][2] = {{0, 0},   {2, 7},   {3, 8},   {4, 9},   {6, 1},   {7, 2},   {8, 3},
                                  {11, 4},  {12, 5},  {13, 6},  {16, 10}, {17, 14}, {18, 15}, {19, 16},
                                  {22, 11}, {23, 12}, {24, 13}, {27, 17}, {28, 18}};
/* the start of the motions' segments (sub 0): MetalGarurumon's signature's, the DNA attack's */
static const int16_t MG_SIG_SEGS[8] = {0, 10, 0, 0, 10, 51, 1450, 1500};
static const int16_t BG_ATK_SEGS[8] = {0, 15, 0, 0, 15, 50, 230, 279};
/* the record's cameras the DNA forms have at 0 (6, 7, 10, 11: the portrait,
   the views out of battle), in bytes: MetalGarurumon's, whose others match */
static const int CAM_FILL[][2] = {{62, 74}, {86, 98}, {136, 146}, {158, 170}, {182, 186}, {190, 194}};

/* Action script 11, the signature: Solar Laser. The signatures' common start
   (the attacker close, sprites 1020 and 1009, the dark stage 55), then the
   DNA's attack (Growlmon's script 12: BladeGarurumon's motion 56, its camera
   cuts, sounds and hits). Its cameras are the DNA's, in the stage's
   coordinates: the script puts the attacker at the origin as the DNA does,
   and the common end puts it back. Opcodes (FIGHTSTG's battle scripts): 1
   damage, 2 actor, 3 effect, 4 stage, 5 camera, 7 fade, 10 sound, 11 wait */
#define CAM(ex, ey, ez, ax, ay, az, fov) 5, 0, 1, ex, ey, ez, ax, ay, az, 0, 0, 0, 0, 0, 0, 0, fov
static const int16_t SOLAR_LASER[] = {
    3, 1, 1020, 3, 1, 1009, 3, 1, 2, 3, 1, 65, 3, 1, 17, 4, 1, 55, /* preload */
    2, 2, 4,                                                         /* the target hidden */
    2, 3, 0, 0, 0, 0, 0,                                             /* the attacker at the origin */
    CAM(-2816, 1408, -5824, 448, 1728, -1600, 226),
    3, 0, 1020, 0, 0, -1, 11, 10, 3, 0, 1009, 0, 0, 32767,
    10, 28, 0, 10, 89, 0, 4, 0, 55, 1, 1, 11, 10,
    2, 0, 0, 50, /* motion 50: BladeGarurumon's 56 */
    CAM(-2816, 1408, -5824, 448, 1728, -1600, 226), 11, 50, 10, 15, 0, 11, 5,
    CAM(-3904, 576, 3264, 1344, 1920, -384, 226), 11, 15, 10, 62, 0, 11, 45,
    CAM(-2816, 1408, -5824, 448, 1728, -1600, 226), 11, 30,
    CAM(-3712, 1344, -5184, -192, 1408, -2240, 448), 10, 24, 0, 11, 55,
    CAM(-4480, 1472, -4160, -128, 1472, -1600, 226), 10, 53, 120, 11, 30,
    CAM(3712, 1024, -8192, -1600, 2048, -3520, 226), 11, 30,
    CAM(-4480, 2816, 3136, 448, 1472, -1024, 226), 11, 15,
    7, 0, 15, 11, 15, /* the hit, as the DNA's */
    5, 0, 0, 5, 2, 1, 4, 2, 2, 0, 7, 1, 30, 11, 30,
    3, 0, 2, 0, 0, 32767, 3, 0, 65, 0, 0, 32767, 3, 0, 17, 0, 0, 32767, 10, 99, 0, 1, 0, 11, 20,
    3, 0, 65, 0, 0, 32767, 3, 0, 17, 0, 0, 32767, 10, 64, 0, 11, 60,
    5, 0, 0, 0, 10, 91, 0, 2, 1, 0, 2, 7, 0, 0, 2, 0, 0, 1, 4, 0, 0, 15, 15, 11, 60, 255, /* the common end */
};

static uint32_t rd32(const uint8_t *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24);
}

/* --- a file of subs: u32 offsets, then the subs one after another --- */

typedef struct {
    uint8_t *p;
    uint32_t n;
} Blob;

static int split(const uint8_t *b, long size, Blob **out) {
    uint32_t n = size >= 4 ? rd32(b) / 4 : 0, i, from, to;

    *out = calloc(n ? n : 1, sizeof(Blob));
    for (i = 0; i < n; i++) {
        from = rd32(b + 4 * i);
        to = i + 1 < n ? rd32(b + 4 * (i + 1)) : (uint32_t)size;
        if (from > to || to > (uint32_t)size) {
            return -1;
        }
        (*out)[i].p = (uint8_t *)b + from;
        (*out)[i].n = to - from;
    }
    return (int)n;
}

static uint8_t *join(const Blob *subs, int n, long *size) {
    uint32_t at = 4 * n;
    int i;
    uint8_t *out;

    for (i = 0; i < n; i++) {
        at += subs[i].n;
    }
    out = malloc(at);
    *size = at;
    at = 4 * n;
    for (i = 0; i < n; i++) {
        wr32(out + 4 * i, at);
        memcpy(out + at, subs[i].p, subs[i].n);
        at += subs[i].n;
    }
    return out;
}

/* --- keyframe tracks: 4 sections (translation, rotation, scale, extra) of {x, y, z, t} --- */

typedef struct {
    int v[4];
} Key;

typedef struct {
    Key *k;
    int n, cap;
} Keys;

static void push(Keys *s, int x, int y, int z, int t) {
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 16;
        s->k = realloc(s->k, s->cap * sizeof(Key));
    }
    s->k[s->n].v[0] = x, s->k[s->n].v[1] = y, s->k[s->n].v[2] = z, s->k[s->n].v[3] = t;
    s->n++;
}

static int parse_track(Blob b, Keys out[4]) {
    uint32_t h[5], p;
    int k;

    memset(out, 0, 4 * sizeof(Keys));
    if (b.n < 16) {
        return 0;
    }
    for (k = 0; k < 4; k++) {
        h[k] = rd32(b.p + 4 * k);
    }
    h[4] = b.n;
    for (k = 0; k < 4; k++) {
        for (p = h[k]; p + 8 <= h[k + 1] && h[k + 1] <= b.n; p += 8) {
            push(&out[k], (int16_t)(b.p[p] | b.p[p + 1] << 8), (int16_t)(b.p[p + 2] | b.p[p + 3] << 8),
                 (int16_t)(b.p[p + 4] | b.p[p + 5] << 8), (int16_t)(b.p[p + 6] | b.p[p + 7] << 8));
        }
    }
    return out[0].n > 0;
}

static Blob build_track(Keys secs[4]) {
    Blob b;
    uint32_t at = 16;
    int k, i, j;

    b.n = 16;
    for (k = 0; k < 4; k++) {
        b.n += 8 * secs[k].n;
    }
    b.p = malloc(b.n);
    for (k = 0; k < 4; k++) {
        wr32(b.p + 4 * k, at);
        for (i = 0; i < secs[k].n; i++, at += 8) {
            for (j = 0; j < 4; j++) {
                b.p[at + 2 * j] = (uint8_t)secs[k].k[i].v[j];
                b.p[at + 2 * j + 1] = (uint8_t)(secs[k].k[i].v[j] >> 8);
            }
        }
    }
    return b;
}

/* round(a + d * f) as the Python that made the PSX mod: each step a double
   (volatile: x87's longer ones rounded), the half to even */
static int to_int(int a, int d, double f) {
    volatile double prod = d * f, sum = a + prod;

    return (int)nearbyint(sum);
}

/* a section's value at t, between its keys */
static void sample(const Keys *s, int t, int out[3]) {
    const Key *prev = &s->k[0];
    int i, j;

    for (i = 0; i < s->n; i++) {
        const Key *k = &s->k[i];

        if (k->v[3] == t) {
            memcpy(out, k->v, 3 * sizeof(int));
            return;
        }
        if (k->v[3] > t) {
            volatile double f = (double)(t - prev->v[3]) / (k->v[3] - prev->v[3] > 1 ? k->v[3] - prev->v[3] : 1);

            for (j = 0; j < 3; j++) {
                out[j] = to_int(prev->v[j], k->v[j] - prev->v[j], f);
            }
            return;
        }
        prev = k;
    }
    memcpy(out, s->k[s->n - 1].v, 3 * sizeof(int));
}

/* BladeGarurumon's idle, then MetalGarurumon's keys as a change from its pose of reference */
static Keys splice(const Keys *bg, const Keys *mg, double scale) {
    Keys out = {0};
    int bref[3], mref[3], i;

    sample(bg, REF_T, bref);
    sample(mg, REF_T, mref);
    for (i = 0; i < bg->n; i++) {
        if (bg->k[i].v[3] < IDLE_END) {
            push(&out, bg->k[i].v[0], bg->k[i].v[1], bg->k[i].v[2], bg->k[i].v[3]);
        }
    }
    for (i = 0; i < mg->n; i++) {
        const int *m = mg->k[i].v;

        if (m[3] >= IDLE_END) {
            push(&out, to_int(bref[0], m[0] - mref[0], scale), to_int(bref[1], m[1] - mref[1], scale),
                 to_int(bref[2], m[2] - mref[2], scale), m[3]);
        }
    }
    return out;
}

/* a node without a match: its idle, then its pose of reference */
static Keys hold(const Keys *bg) {
    Keys out = {0};
    int ref[3], i;

    sample(bg, REF_T, ref);
    for (i = 0; i < bg->n; i++) {
        if (bg->k[i].v[3] < IDLE_END) {
            push(&out, bg->k[i].v[0], bg->k[i].v[1], bg->k[i].v[2], bg->k[i].v[3]);
        }
    }
    push(&out, ref[0], ref[1], ref[2], IDLE_END);
    push(&out, ref[0], ref[1], ref[2], END);
    return out;
}

/* the DNA attack's own keys, moved past the timeline, before its end */
static Keys graft(const Keys *keys, const Keys *orig) {
    Keys out = {0};
    int v[3], i;

    for (i = 0; i < keys->n; i++) {
        if (keys->k[i].v[3] < END) {
            push(&out, keys->k[i].v[0], keys->k[i].v[1], keys->k[i].v[2], keys->k[i].v[3]);
        }
    }
    sample(orig, ATK_FROM, v);
    push(&out, v[0], v[1], v[2], ATK_FROM + ATK_SHIFT);
    for (i = 0; i < orig->n; i++) {
        if (orig->k[i].v[3] > ATK_FROM && orig->k[i].v[3] < ATK_TO) {
            push(&out, orig->k[i].v[0], orig->k[i].v[1], orig->k[i].v[2], orig->k[i].v[3] + ATK_SHIFT);
        }
    }
    sample(orig, ATK_TO, v);
    push(&out, v[0], v[1], v[2], ATK_TO + ATK_SHIFT);
    for (i = 0; i < keys->n; i++) {
        if (keys->k[i].v[3] >= END) {
            push(&out, keys->k[i].v[0], keys->k[i].v[1], keys->k[i].v[2], keys->k[i].v[3]);
        }
    }
    return out;
}

/* a skeleton's node: {parent + 1, geometry sub, track sub} */
static int skeleton(Blob b, uint32_t node, int what) {
    uint32_t i = 2 + 3 * node + what;

    return b.n >= 8 && node < rd32(b.p + 4) && 4 * i + 4 <= b.n ? (int)rd32(b.p + 4 * i) : -1;
}

/* the motion table's 10 segments (4 s16 each) of the motion that starts with head */
static long segments(Blob b, const int16_t head[8]) {
    uint8_t pat[16];
    long at = -1, i;
    int j;

    for (j = 0; j < 8; j++) {
        pat[2 * j] = (uint8_t)head[j], pat[2 * j + 1] = (uint8_t)(head[j] >> 8);
    }
    for (i = 0; i + 80 <= (long)b.n; i += 2) {
        if (memcmp(b.p + i, pat, 16) == 0) {
            if (at >= 0) {
                return -1; /* not one: the files aren't the ones this was made for */
            }
            at = i;
        }
    }
    return at;
}

static void free_keys(Keys k[4]) {
    int i;

    for (i = 0; i < 4; i++) {
        free(k[i].k);
    }
}

/* The model 471: M469GARM's subs with the tracks, motions and scripts made anew */
static uint8_t *build_model(const uint8_t *bgf, long bgsize, const uint8_t *mgf, long mgsize, long *size) {
    Blob *bg = NULL, *mg = NULL, *out = NULL, ev, scripts;
    int nb = split(bgf, bgsize, &bg), nm = split(mgf, mgsize, &mg), node, i, k, mapped;
    uint8_t *made = NULL;
    long mi, bi;
    double scale;
    volatile double height;
    Keys t0[4], m0[4];

    if (nb <= BG_SKEL || nm <= MG_SKEL || nb <= BG_ANIM || nm <= MG_ANIM) {
        goto done;
    }
    if (!parse_track(bg[skeleton(bg[BG_SKEL], 0, 2)], t0) || !parse_track(mg[skeleton(mg[MG_SKEL], 0, 2)], m0) ||
        m0[0].k[0].v[1] == 0) {
        goto done;
    }
    height = (double)t0[0].k[0].v[1] / m0[0].k[0].v[1]; /* the roots' heights, a double */
    scale = height;
    free_keys(t0), free_keys(m0);
    out = calloc(nb, sizeof(Blob));
    memcpy(out, bg, nb * sizeof(Blob));
    for (node = 0; skeleton(bg[BG_SKEL], node, 2) >= 0; node++) {
        int trk = skeleton(bg[BG_SKEL], node, 2);
        Keys orig[4], now[4], m[4], g[4];

        if (trk <= 0 || trk >= nb || !parse_track(bg[trk], orig)) {
            goto done;
        }
        parse_track(bg[trk], now);
        for (mapped = -1, i = 0; i < (int)(sizeof NODE_MAP / sizeof NODE_MAP[0]); i++) {
            if (NODE_MAP[i][0] == node) {
                mapped = NODE_MAP[i][1];
            }
        }
        if (mapped >= 0) {
            int mtrk = skeleton(mg[MG_SKEL], mapped, 2);
            Keys spliced;

            if (mtrk <= 0 || mtrk >= nm || !parse_track(mg[mtrk], m)) {
                goto done;
            }
            if (node == 0 || node == 16) { /* the roots */
                spliced = splice(&now[0], &m[0], scale);
                free(now[0].k), now[0] = spliced;
            }
            spliced = splice(&now[1], &m[1], 1.0);
            free(now[1].k), now[1] = spliced;
            free_keys(m);
        } else {
            /* scale too: the DNA's effect nodes (29-34) are hidden at t 10 and only show in its attack */
            for (k = 0; k < 3; k++) {
                Keys h = hold(&now[k]);

                free(now[k].k), now[k] = h;
            }
        }
        for (k = 0; k < 3; k++) {
            g[k] = graft(&now[k], &orig[k]);
        }
        g[3] = now[3];
        out[trk] = build_track(g);
        for (k = 0; k < 3; k++) {
            free(g[k].k), free(now[k].k);
        }
        free(now[3].k);
        free_keys(orig);
    }
    /* the motions: MetalGarurumon's table, with BladeGarurumon's idle (motion 0)
       and its DNA attack, moved, in place of the signature's segments */
    ev.n = mg[0].n;
    ev.p = malloc(ev.n);
    memcpy(ev.p, mg[0].p, ev.n);
    if (rd32(ev.p) + 24 > ev.n || rd32(bg[0].p) + 24 > bg[0].n) {
        goto done;
    }
    memcpy(ev.p + rd32(ev.p), bg[0].p + rd32(bg[0].p), 24);
    mi = segments(ev, MG_SIG_SEGS);
    bi = segments(bg[0], BG_ATK_SEGS);
    if (mi < 0 || bi < 0) {
        free(ev.p);
        goto done;
    }
    for (i = 0; i < 40; i += 4) {
        int16_t s[4];

        memcpy(s, bg[0].p + bi + 2 * i, 8);
        if (s[2] > 0) { /* a real segment's t0 and t1 (not a loop, the end or a pause) */
            s[2] += ATK_SHIFT, s[3] += ATK_SHIFT;
        }
        memcpy(ev.p + mi + 2 * i, s, 8);
    }
    out[0] = ev;
    /* MetalGarurumon's 18 action scripts, with Solar Laser as 11 */
    {
        Blob parts[18];
        uint32_t at = 18 * 4, code = sizeof SOLAR_LASER, pad = (4 - code % 4) % 4;
        Blob src = mg[MG_ANIM];

        if (src.n < 72) {
            goto done;
        }
        for (i = 0; i < 18; i++) {
            uint32_t from = rd32(src.p + 4 * i), to = i < 17 ? rd32(src.p + 4 * (i + 1)) : src.n;

            parts[i].p = src.p + from, parts[i].n = to - from;
        }
        scripts.n = at;
        for (i = 0; i < 18; i++) {
            scripts.n += i == 11 ? code + pad : parts[i].n;
        }
        scripts.p = calloc(1, scripts.n);
        for (i = 0; i < 18; i++) {
            wr32(scripts.p + 4 * i, at);
            if (i == 11) {
                memcpy(scripts.p + at, SOLAR_LASER, code); /* little-endian s16, as the file's */
                at += code + pad;
            } else {
                memcpy(scripts.p + at, parts[i].p, parts[i].n);
                at += parts[i].n;
            }
        }
        out[BG_ANIM] = scripts;
    }
    made = join(out, nb, size);
done:
    if (made == NULL) {
        LOG("bladegarurumon: its model can't be made from M469GARM and M196MGAR");
    }
    free(bg);
    free(mg);
    free(out); /* ponytail: the made tracks leak once, at start; nothing to free them for */
    return made;
}

/* --- the fighters' file: an entry and a partner's record for 471 --- */

/* the key {s16 model, u8 index, u8 kind} of a model, its offset, or -1; *end: the list's end */
static long find_key(const uint8_t *f, long size, int model, uint32_t *end) {
    uint32_t at = rd32(f + 4);
    long found = -1;

    for (; at + 4 <= (uint32_t)size && (f[at] | f[at + 1] << 8) != 0; at += 4) {
        if ((int16_t)(f[at] | f[at + 1] << 8) == model) {
            found = at;
        }
    }
    *end = at;
    return found;
}

/* SMDLDATA with model 471: a key past the list's last and a partner's record
   past the partners' table (469's, its files 1's, and MetalGarurumon's
   cameras where the DNA forms have none). The records' face pointers past
   the keys move with the 4 bytes inserted */
static uint8_t *build_fighters(const uint8_t *f, long size, long *out_size, int *bg_fid, int *mg_fid) {
    uint32_t base, o1, o2, o3, end, off, ptr, n_player;
    long old, donor;
    uint8_t rec[RECORD], *out;
    int i;

    if (size < 16) {
        return NULL;
    }
    base = rd32(f), o1 = rd32(f + 4), o2 = rd32(f + 8), o3 = rd32(f + 12);
    old = find_key(f, size, OLD_MODEL, &end);
    donor = find_key(f, size, MG_MODEL, &end);
    if (o1 >= o2 || o2 >= o3 || o3 > (uint32_t)size || old < 0 || donor < 0 || find_key(f, size, NEW_MODEL, &end) >= 0) {
        return NULL;
    }
    if (o2 + (f[old + 2] + 1) * RECORD > o3 || o2 + (f[donor + 2] + 1) * RECORD > o3) {
        return NULL;
    }
    n_player = (o3 - o2) / RECORD;
    memcpy(rec, f + o2 + f[old + 2] * RECORD, RECORD);
    for (i = 0; i < (int)(sizeof CAM_FILL / sizeof CAM_FILL[0]); i++) {
        memcpy(rec + CAM_FILL[i][0], f + o2 + f[donor + 2] * RECORD + CAM_FILL[i][0], CAM_FILL[i][1] - CAM_FILL[i][0]);
    }
    *bg_fid = (int)(rd32(rec) >> 16);
    *mg_fid = (int)(rd32(f + o2 + f[donor + 2] * RECORD) >> 16);
    for (i = 0; i < 3; i++) { /* its skeleton, motions and effects: (fid, sub) */
        wr32(rec + 4 * i, NEW_FID << 16 | (rd32(rec + 4 * i) & 0xFFFF));
    }
    *out_size = size + 4 + RECORD;
    out = malloc(*out_size);
    memcpy(out, f, end);
    out[end] = (uint8_t)NEW_MODEL, out[end + 1] = NEW_MODEL >> 8, out[end + 2] = (uint8_t)n_player, out[end + 3] = f[old + 3];
    memcpy(out + end + 4, f + end, o3 - end);
    memcpy(out + o3 + 4, rec, RECORD);
    memcpy(out + o3 + 4 + RECORD, f + o3, size - o3);
    wr32(out + 8, o2 + 4);
    wr32(out + 12, o3 + 4 + RECORD);
    for (i = 0; i < 2; i++) { /* the partners' records, then the enemies' */
        uint32_t from = i ? o3 + 4 + RECORD : o2 + 4, to = i ? (uint32_t)*out_size : o3 + 4 + RECORD;

        for (off = from; off < to && off + 16 <= (uint32_t)*out_size; off += i ? 72 : RECORD) {
            ptr = rd32(out + off + 12);
            if (ptr >= base && ptr - base >= end && ptr - base < o2) {
                wr32(out + off + 12, ptr + 4);
            }
        }
    }
    return out;
}

/* The fighters' file and model 471, made the first time either is read;
   NULL for the game's own (or when they can't be made: logged) */
const uint8_t *bladegaru_file(int fid, long *size) {
    static uint8_t *fighters, *model;
    static long fighters_size, model_size;
    static int tried;

    if (fid != FIGHTERS_FILE && fid != NEW_FID) {
        return NULL;
    }
    if (!tried) {
        uint8_t *f, *bg, *mg;
        long fsize = native_read_raw(FIGHTERS_FILE, &f), bgsize, mgsize;
        int bg_fid = 0, mg_fid = 0;

        tried = 1;
        fighters = build_fighters(f, fsize, &fighters_size, &bg_fid, &mg_fid);
        if (fighters != NULL) {
            bgsize = native_read_raw(bg_fid, &bg);
            mgsize = native_read_raw(mg_fid, &mg);
            model = build_model(bg, bgsize, mg, mgsize, &model_size);
            free(bg);
            free(mg);
        }
        if (model == NULL) { /* the record without its model would crash a battle */
            free(fighters);
            fighters = NULL;
            LOG("bladegarurumon: the fighters' file stays the game's");
        }
        free(f);
    }
    if (fid == FIGHTERS_FILE) {
        *size = fighters_size;
        return fighters;
    }
    *size = model_size;
    return model;
}
