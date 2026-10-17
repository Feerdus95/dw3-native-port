/* The fight stages' profiles for the battle sky and fog (g_battle_sky):
   stages.ini by the exe (assets/stages.ini), kinds of stage and the stages
   that take them, read at the first battle. FIGHTSTG's patch (stage.c)
   tells which stage is shown and how far its fade is, each frame. And what
   the stages get on top: falling leaves in the wind. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL3/SDL.h>
#include "platform.h"

#define STAGES 57
#define KINDS 16

typedef struct {
    char name[16];
    StageFx fx;
} Kind;

static StageFx s_stage[STAGES];
static int s_loaded, s_shown = -1, s_bright, s_frame = -10;
static unsigned s_now;

/* "r, g, b" into rgb, -1s when it isn't one */
static void color(const char *v, int rgb[3]) {
    if (SDL_sscanf(v, "%d , %d , %d", &rgb[0], &rgb[1], &rgb[2]) != 3) {
        rgb[0] = rgb[1] = rgb[2] = -1;
    }
}

static void set(StageFx *fx, const char *key, const char *v) {
    if (SDL_strcasecmp(key, "sky") == 0) {
        fx->sky = atoi(v) != 0;
    } else if (SDL_strcasecmp(key, "sky_top") == 0) {
        color(v, fx->sky_top);
    } else if (SDL_strcasecmp(key, "fog") == 0) {
        fx->fog = (float)SDL_atof(v);
    } else if (SDL_strcasecmp(key, "fog_start") == 0) {
        fx->fog_start = (float)SDL_atof(v);
    } else if (SDL_strcasecmp(key, "fog_color") == 0) {
        color(v, fx->fog_color);
    } else if (SDL_strcasecmp(key, "horizon") == 0) {
        fx->horizon = (float)SDL_atof(v);
    } else if (SDL_strcasecmp(key, "leaves") == 0) {
        fx->leaves = SDL_clamp(atoi(v), 0, 200);
    } else if (SDL_strcasecmp(key, "wind") == 0) {
        fx->wind = (float)SDL_atof(v);
    } else if (SDL_strcasecmp(key, "clouds") == 0) {
        fx->clouds = (float)SDL_atof(v);
    } else if (SDL_strcasecmp(key, "sway") == 0) {
        fx->sway = (float)SDL_atof(v);
    }
}

/* stages.ini: kinds first, each stage a copy of its kind with its own keys
   on top (the keys before a stage's "kind" line are lost to it) */
static void load(void) {
    static const StageFx NONE = {0, {-1, -1, -1}, 0, 0.45f, {-1, -1, -1}, 20000, 0, 1, 0, 0, 0};
    static Kind kinds[KINDS];
    int nkinds = 0, i;
    StageFx *cur = NULL;
    char path[1200], line[256], key[64], val[128];
    FILE *f;

    s_loaded = 1;
    for (i = 0; i < STAGES; i++) {
        s_stage[i] = NONE;
    }
    SDL_snprintf(path, sizeof path, "%sstages.ini", SDL_GetBasePath());
    if ((f = fopen(path, "r")) == NULL) {
        SDL_snprintf(path, sizeof path, "%s../assets/stages.ini", SDL_GetBasePath());
        f = fopen(path, "r");
    }
    if (f == NULL) {
        LOG("stagefx: no stages.ini, the stages are left as they are");
        return;
    }
    while (fgets(line, sizeof line, f) != NULL) {
        int n;

        if (line[0] == ';' || line[0] == '#') {
            continue;
        }
        if (SDL_sscanf(line, " [stage %d]", &n) == 1) {
            cur = n >= 0 && n < STAGES ? &s_stage[n] : NULL;
        } else if (SDL_sscanf(line, " [%15[^]]]", key) == 1) {
            cur = NULL;
            if (nkinds < KINDS) {
                SDL_strlcpy(kinds[nkinds].name, key, sizeof kinds[nkinds].name);
                kinds[nkinds].fx = NONE;
                kinds[nkinds].fx.on = 1;
                cur = &kinds[nkinds++].fx;
            }
        } else if (cur != NULL && SDL_sscanf(line, " %63[^= ] = %127[^\r\n;]", key, val) == 2) {
            if (SDL_strcasecmp(key, "kind") == 0) {
                for (i = 0; i < nkinds && SDL_strncasecmp(kinds[i].name, val, SDL_strlen(kinds[i].name)) != 0; i++) {
                }
                if (i < nkinds) {
                    *cur = kinds[i].fx;
                }
            } else {
                set(cur, key, val);
            }
        }
    }
    fclose(f);
}

/* FIGHTSTG's patch (stage.c), each frame: the stage shown and its model's
   brightness, 0-128 as it fades in and out */
void native_stage_shown(int id, int bright) {
    s_shown = id, s_bright = bright, s_frame = (int)s_now;
}

/* A frame drawn (gl.c): a stage not told of since is gone */
/* DW3_LOG=gpu, once a second in a battle: what moved */
static int s_swayed, s_drifted;
static void stagefx_log(void);

void stagefx_frame(void) {
    s_now++;
    if (s_now % 60 == 0) {
        stagefx_log();
    }
}

/* The stage shown's profile, with its brightness; NULL out of a battle, with
   the option off or for a stage left as it is */
const StageFx *stagefx_current(float *bright) {
    if (!g_battle_sky || !gte_in_battle() || (int)s_now - s_frame > 2 || s_shown < 0 || s_shown >= STAGES) {
        return NULL;
    }
    if (!s_loaded) {
        load();
    }
    *bright = s_bright / 128.0f;
    return s_stage[s_shown].on ? &s_stage[s_shown] : NULL;
}

/* The painted horizon's radius for FIGHTSTG's patch (fightstg.c), 0: none */
float native_stage_horizon(void) {
    float b;
    const StageFx *fx = stagefx_current(&b);

    return fx != NULL ? fx->horizon : 0;
}

/* --- falling leaves ---

   World units: y points down, the ground at 0, the arena's middle at x, z 0
   (the fighters stand within ~5000 of it, the trees from ~10000 out). Each
   leaf falls slowly, swings side to side and tumbles (turns, and flips as
   its width), carried by a wind that turns and gusts; one that lands or
   blows out of the arena comes back at the top, upwind. Drawn as a diamond
   in two greens / browns facing the camera, by the depth buffer among the
   fighters, after them. ponytail: a fixed palette and shape; a per-stage
   leaf_color when another forest wants autumn */
#define LEAVES 200
#define ARENA 9000.0f
#define TOP -7500.0f

typedef struct {
    float p[3];
    float size, phase, spin, angle, fall;
    int shade;
} Leaf;

static Leaf s_leaves[LEAVES];
static int s_leaf_count = -1, s_leaf_stage = -1;
static unsigned s_rng = 0x2003;

static float rnd(void) { /* 0-1, its own: the game's RNG is left alone */
    s_rng = s_rng * 1664525u + 1013904223u;
    return (s_rng >> 8) / 16777216.0f;
}

static void wind_at(float t, float w, float out[2]) {
    float gust = 0.6f + 0.4f * sinf(t * 0.011f) * sinf(t * 0.0037f + 1.0f);
    float dir = 0.6f + 0.5f * sinf(t * 0.0013f);

    out[0] = cosf(dir) * 18.0f * w * gust;
    out[1] = sinf(dir) * 18.0f * w * gust;
}

static void spawn(Leaf *l, int anywhere, const float wind[2]) {
    float a = rnd() * 6.2831853f, r = sqrtf(rnd()) * ARENA;

    l->p[0] = cosf(a) * r, l->p[2] = sinf(a) * r;
    l->p[1] = anywhere ? TOP + rnd() * -TOP * 0.95f : TOP - rnd() * 1500.0f;
    if (!anywhere) { /* upwind, so it crosses the arena; kept inside it */
        float d;

        l->p[0] -= wind[0] * 120.0f, l->p[2] -= wind[1] * 120.0f;
        d = sqrtf(l->p[0] * l->p[0] + l->p[2] * l->p[2]);
        if (d > ARENA) {
            l->p[0] *= ARENA / d, l->p[2] *= ARENA / d;
        }
    }
    l->size = 70.0f + rnd() * 70.0f;
    l->phase = rnd() * 6.2831853f;
    l->spin = (rnd() - 0.5f) * 0.12f;
    l->angle = rnd() * 6.2831853f;
    l->fall = 5.0f + rnd() * 6.0f;
    l->shade = (int)(rnd() * 5) % 5;
}

static void leaves_step(int n, float w) {
    float wind[2], t = (float)s_now; /* the frames' clock, as the trees' (native_stage_sway) */
    int i;

    wind_at(t, w, wind);
    for (i = 0; i < n; i++) {
        Leaf *l = &s_leaves[i];
        float sway = sinf(t * 0.045f + l->phase);

        l->p[0] += wind[0] + sway * 9.0f;
        l->p[2] += wind[1] + cosf(t * 0.037f + l->phase) * 6.0f;
        l->p[1] += l->fall * (0.7f + 0.3f * fabsf(sway)); /* slower at the ends of the swing */
        l->angle += l->spin;
        if (l->p[1] > -40.0f || l->p[0] * l->p[0] + l->p[2] * l->p[2] > ARENA * ARENA * 1.6f) {
            spawn(l, 0, wind);
        }
    }
}

static void leaves_draw(int n, float bright, int ox, int oy, int x1, int y1, int x2, int y2) {
    static const uint8_t PALETTE[5][2][3] = {
        {{58, 102, 34}, {92, 140, 52}}, {{74, 118, 38}, {120, 156, 62}}, {{110, 120, 40}, {150, 160, 70}},
        {{128, 86, 36}, {170, 124, 60}}, {{96, 70, 34}, {136, 104, 52}},
    };
    static GVtx v[LEAVES * 6];
    float t = (float)s_now;
    int i, k = 0;

    for (i = 0; i < n; i++) {
        Leaf *l = &s_leaves[i];
        float s[3], half, flip, c, sn, dx[4], dy[4];
        int j;
        static const int TRI[6] = {0, 1, 2, 0, 2, 3};

        if (!gte_camera_project(l->p, s)) {
            continue;
        }
        half = l->size * gte_camera_scale(s[2]) * 0.5f;
        if (half < 0.35f) {
            continue;
        }
        flip = 0.25f + 0.75f * fabsf(cosf(t * 0.06f + l->phase * 2.0f)); /* its width as it turns over */
        c = cosf(l->angle), sn = sinf(l->angle);
        /* a diamond along its length, narrow across */
        {
            float lx[4] = {0, 0.45f * flip, 0, -0.45f * flip}, ly[4] = {-1, 0, 1, 0};

            for (j = 0; j < 4; j++) {
                dx[j] = (lx[j] * c - ly[j] * sn) * half * (gte_camera_wide() ? 0.75f : 1.0f);
                dy[j] = (lx[j] * sn + ly[j] * c) * half;
            }
        }
        for (j = 0; j < 6; j++) {
            GVtx *o = &v[k++];
            int corner = TRI[j];
            const uint8_t *col = PALETTE[l->shade][corner == 1 ? 1 : 0]; /* one side lit */

            SDL_memset(o, 0, sizeof *o);
            o->x = s[0] + dx[corner] + ox;
            o->y = s[1] + dy[corner] + oy;
            o->r = (uint8_t)(col[0] * bright), o->g = (uint8_t)(col[1] * bright), o->b = (uint8_t)(col[2] * bright);
            o->umax = o->vmax = 255;
            o->z = g_depth ? s[2] : 0;
            o->w = s[2];
        }
    }
    if (k > 0) {
        gl_triangles(v, k, x1, y1, x2, y2, 0);
    }
}

/* --- the stage's list of props and emitters ---
   data/mods/fightstages/<id>/props.json (the viewer's Stages section writes it):
   upright billboards and particle emitters of the mod's own PNGs, placed in the
   world (the game's units, y up, 0 the ground). Drawn over the models with the
   stage's depth (gl_props), so the stage's trees and the fighters hide them. */
#define MAX_BB 200
#define MAX_EM 8
#define MAX_PART 300
#define MAX_IMG 48

typedef struct {
    int img, fixed;
    float pos[3], size[2], scale, sway, tint[3], rot, fog;
} Billboard;

typedef struct {
    int img, count;
    float center[3], radius, top, fall, wind, size[2], tint[3], fog;
    Leaf *p;
} Emitter;

static struct {
    char path[1100];
    SDL_Surface *surf;
    unsigned tex;
} s_img[MAX_IMG]; /* by path, kept across stages */
static int s_nimg;
static Billboard s_bb[MAX_BB];
static Emitter s_em[MAX_EM];
static Leaf s_pool[MAX_EM * MAX_PART];
static int s_nbb, s_nem, s_props_stage = -2;
static float s_pwind = 1; /* the list's wind, a factor of the stage's */
static float s_pfog[5];    /* the list's own fog: r, g, b (0-255), how much, where it starts */
static int s_pfog_on;
static PropVtx s_pv[(MAX_BB + MAX_EM * MAX_PART) * 6];

static float jnum(Json *o, const char *k, float d) {
    Json *v = json_get(o, k);

    return v != NULL && v->type == 'n' ? (float)v->num : d;
}

static void jvec(Json *o, const char *k, float *out, int n) {
    Json *a = json_get(o, k);
    int i;

    for (i = 0; a != NULL && a->type == '[' && i < n; i++) {
        Json *v = json_at(a, i);

        if (v != NULL && v->type == 'n') {
            out[i] = (float)v->num;
        }
    }
}

/* The image's index, loaded the first time (-1: none) */
static int image_of(const char *dir, Json *o) {
    Json *v = json_get(o, "image");
    char path[1100];
    int i;

    if (v == NULL || v->type != 's' || v->str == NULL || SDL_strchr(v->str, '/') || SDL_strchr(v->str, '\\') || SDL_strstr(v->str, "..")) {
        return -1;
    }
    SDL_snprintf(path, sizeof path, "%s/%s", dir, v->str);
    for (i = 0; i < s_nimg; i++) {
        if (SDL_strcmp(s_img[i].path, path) == 0) {
            return s_img[i].surf != NULL ? i : -1;
        }
    }
    if (s_nimg == MAX_IMG) {
        return -1;
    }
    SDL_strlcpy(s_img[s_nimg].path, path, sizeof s_img[s_nimg].path);
    {
        SDL_Surface *png = SDL_LoadPNG(path);

        if (png != NULL) {
            s_img[s_nimg].surf = SDL_ConvertSurface(png, SDL_PIXELFORMAT_RGBA32);
            SDL_DestroySurface(png);
        } else {
            LOG("props: %s: %s", path, SDL_GetError());
        }
    }
    return s_img[s_nimg++].surf != NULL ? s_nimg - 1 : -1;
}

static void spawn_part(const Emitter *e, Leaf *l, int anywhere, const float wind[2]) {
    float a = rnd() * 6.2831853f, r = sqrtf(rnd()) * e->radius;

    l->p[0] = e->center[0] + cosf(a) * r, l->p[2] = e->center[2] + sinf(a) * r;
    l->p[1] = -(e->center[1] + (anywhere ? rnd() * e->top : e->top + rnd() * 300.0f));
    if (!anywhere) { /* upwind, so it crosses the area */
        l->p[0] -= wind[0] * 120.0f, l->p[2] -= wind[1] * 120.0f;
    }
    l->size = e->size[0] + rnd() * (e->size[1] - e->size[0]);
    l->phase = rnd() * 6.2831853f;
    l->spin = (rnd() - 0.5f) * 0.12f;
    l->angle = rnd() * 6.2831853f;
    l->fall = e->fall * (5.0f + rnd() * 6.0f);
}

/* --- 3D models: an OBJ with a PNG texture, posed in the world ---
   The OBJ's triangles (quads and bigger faces fanned) with their texture
   coordinates and normals (a face's own where it has none); drawn flat-lit
   from one side, bent by the wind from its foot to its top */
#define MAX_MESH 24
#define MAX_MD 64

typedef struct {
    char path[1100];
    float *v; /* per vertex: x, y, z, u, v, nx, ny, nz */
    int n;    /* vertices, a multiple of 3 */
    float miny, h;
    int tried;
} Mesh;

typedef struct {
    int mesh, img;
    float pos[3], scale, rot, sway, tint[3], fog;
} ModelProp;

static Mesh s_mesh[MAX_MESH]; /* by path, kept across stages */
static int s_nmesh, s_nmd;
static ModelProp s_md[MAX_MD];
static PropVtx *s_mv;
static int s_mv_cap;

/* The next integer of an OBJ face's "v/t/n" token, 0 when it has none */
static int obj_int(const char **s) {
    char *e;
    long v = SDL_strtol(*s, &e, 10);

    if (e == *s) {
        return 0;
    }
    *s = e;
    return (int)v;
}

/* An OBJ's vertices, 8 floats each; NULL if it has no triangles */
static float *obj_parse(const char *text, int *count) {
    float *pos = NULL, *uv = NULL, *nrm = NULL, *out = NULL;
    int np = 0, nu = 0, nn = 0, no = 0, cap = 0;
    const char *p = text;

    while (*p) {
        const char *line = p;

        while (*p && *p != '\n') {
            p++;
        }
        if (line[0] == 'v' && (line[1] == ' ' || line[1] == 't' || line[1] == 'n')) {
            int kind = line[1] == ' ' ? 0 : line[1] == 't' ? 1 : 2, need = kind == 1 ? 2 : 3, i;
            const char *q = line + (kind == 0 ? 1 : 2);
            float f[3] = {0, 0, 0};
            float **arr = kind == 0 ? &pos : kind == 1 ? &uv : &nrm;
            int *cnt = kind == 0 ? &np : kind == 1 ? &nu : &nn;

            for (i = 0; i < need; i++) {
                char *e;

                f[i] = (float)SDL_strtod(q, &e);
                q = e;
            }
            *arr = SDL_realloc(*arr, (size_t)(*cnt + 1) * need * sizeof(float));
            if (*arr == NULL) {
                goto fail;
            }
            for (i = 0; i < need; i++) {
                (*arr)[*cnt * need + i] = f[i];
            }
            (*cnt)++;
        } else if (line[0] == 'f' && line[1] == ' ') {
            int vi[64], ti[64], ni[64], k = 0, i;
            const char *q = line + 1;

            while (k < 64) {
                while (*q == ' ' || *q == '\t') {
                    q++;
                }
                if (q >= p || *q == '\r') {
                    break;
                }
                vi[k] = obj_int(&q), ti[k] = ni[k] = 0;
                if (*q == '/') {
                    q++;
                    ti[k] = obj_int(&q);
                    if (*q == '/') {
                        q++;
                        ni[k] = obj_int(&q);
                    }
                }
                if (vi[k] == 0) {
                    break;
                }
                vi[k] = vi[k] < 0 ? np + vi[k] : vi[k] - 1;
                ti[k] = ti[k] < 0 ? nu + ti[k] : ti[k] - 1;
                ni[k] = ni[k] < 0 ? nn + ni[k] : ni[k] - 1;
                if (vi[k] < 0 || vi[k] >= np) {
                    break;
                }
                k++;
            }
            for (i = 1; i + 1 < k; i++) {
                int idx[3] = {0, i, i + 1}, j;
                float fn[3] = {0, 1, 0};

                if (no + 3 > cap) {
                    cap = cap ? cap * 2 : 3000;
                    out = SDL_realloc(out, (size_t)cap * 8 * sizeof(float));
                    if (out == NULL) {
                        goto fail;
                    }
                }
                {
                    const float *a = &pos[vi[0] * 3], *b = &pos[vi[i] * 3], *c = &pos[vi[i + 1] * 3];
                    float u0 = b[0] - a[0], u1 = b[1] - a[1], u2 = b[2] - a[2], w0 = c[0] - a[0], w1 = c[1] - a[1], w2 = c[2] - a[2];
                    float x = u1 * w2 - u2 * w1, y = u2 * w0 - u0 * w2, z = u0 * w1 - u1 * w0, l = SDL_sqrtf(x * x + y * y + z * z);

                    if (l > 1e-9f) {
                        fn[0] = x / l, fn[1] = y / l, fn[2] = z / l;
                    }
                }
                for (j = 0; j < 3; j++) {
                    float *o = &out[no * 8];
                    int a = idx[j];

                    o[0] = pos[vi[a] * 3], o[1] = pos[vi[a] * 3 + 1], o[2] = pos[vi[a] * 3 + 2];
                    o[3] = ti[a] >= 0 && ti[a] < nu ? uv[ti[a] * 2] : 0, o[4] = ti[a] >= 0 && ti[a] < nu ? 1.0f - uv[ti[a] * 2 + 1] : 0;
                    if (ni[a] >= 0 && ni[a] < nn) {
                        o[5] = nrm[ni[a] * 3], o[6] = nrm[ni[a] * 3 + 1], o[7] = nrm[ni[a] * 3 + 2];
                    } else {
                        o[5] = fn[0], o[6] = fn[1], o[7] = fn[2];
                    }
                    no++;
                }
            }
        }
        if (*p) {
            p++;
        }
    }
    SDL_free(pos), SDL_free(uv), SDL_free(nrm);
    *count = no;
    if (no == 0) {
        SDL_free(out);
        return NULL;
    }
    return out;
fail:
    SDL_free(pos), SDL_free(uv), SDL_free(nrm), SDL_free(out);
    return NULL;
}

/* The mesh of the OBJ named by "model" (-1: none) */
static int mesh_of(const char *dir, Json *o) {
    Json *v = json_get(o, "model");
    char path[1100];
    int i;
    Mesh *m;
    char *text;

    if (v == NULL || v->type != 's' || v->str == NULL || SDL_strchr(v->str, '/') || SDL_strchr(v->str, '\\') || SDL_strstr(v->str, "..")) {
        return -1;
    }
    SDL_snprintf(path, sizeof path, "%s/%s", dir, v->str);
    for (i = 0; i < s_nmesh; i++) {
        if (SDL_strcmp(s_mesh[i].path, path) == 0) {
            return s_mesh[i].v != NULL ? i : -1;
        }
    }
    if (s_nmesh == MAX_MESH) {
        return -1;
    }
    m = &s_mesh[s_nmesh];
    SDL_strlcpy(m->path, path, sizeof m->path);
    if ((text = SDL_LoadFile(path, NULL)) != NULL) {
        m->v = obj_parse(text, &m->n);
        SDL_free(text);
    }
    if (m->v == NULL) {
        LOG("props: %s: no triangles", path);
    } else {
        float lo = 1e30f, hi = -1e30f;

        for (i = 0; i < m->n; i++) {
            lo = SDL_min(lo, m->v[i * 8 + 1]), hi = SDL_max(hi, m->v[i * 8 + 1]);
        }
        m->miny = lo, m->h = SDL_max(hi - lo, 1e-3f);
    }
    s_nmesh++;
    return m->v != NULL ? s_nmesh - 1 : -1;
}

/* Every model, one draw each: turned about the vertical, bent by the wind
   (the square of the height, as a tree), flat-lit */
static void models_draw(float bright, int ox, int oy, int x1, int y1, int x2, int y2) {
    float t = (float)s_now, ww[2];
    const StageFx *fx;
    float fb;
    int i;

    fx = stagefx_current(&fb);
    wind_at(t, (fx != NULL ? fx->wind : 1.0f) * s_pwind, ww);
    for (i = 0; i < s_nmd; i++) {
        const ModelProp *md = &s_md[i];
        const Mesh *m = &s_mesh[md->mesh];
        float a = md->rot * 0.017453293f, ca = cosf(a), sa = sinf(a), height = m->h * md->scale;
        float move = 0.5f + sinf(t * 0.042f + (md->pos[0] + md->pos[2]) * 0.0004f), bend = md->sway / 18.0f * height * 0.3f * move;
        int k, n = 0;

        if (m->n > s_mv_cap) {
            PropVtx *g = SDL_realloc(s_mv, (size_t)m->n * sizeof *g);

            if (g == NULL) {
                continue;
            }
            s_mv = g, s_mv_cap = m->n;
        }
        for (k = 0; k + 2 < m->n; k += 3) {
            PropVtx tri[3];
            int j, ok = 1;

            for (j = 0; j < 3 && ok; j++) {
                const float *v = &m->v[(k + j) * 8];
                float lx = v[0] * md->scale, ly = (v[1] - m->miny) * md->scale, lz = v[2] * md->scale, rel = SDL_clamp(ly / height, 0.0f, 1.0f);
                float P[3], s[3], nx = v[5] * ca + v[7] * sa, nz = -v[5] * sa + v[7] * ca, d, shade;

                P[0] = md->pos[0] + lx * ca + lz * sa + ww[0] * bend * rel * rel;
                P[1] = -(md->pos[1] + ly);
                P[2] = md->pos[2] - lx * sa + lz * ca + ww[1] * bend * rel * rel;
                if (!gte_camera_project(P, s)) {
                    ok = 0;
                    break;
                }
                d = nx * 0.35f + v[6] * 0.8f + nz * 0.45f; /* from above and a side, unit-ish */
                shade = 0.55f + 0.45f * SDL_max(d / 0.98f, 0.0f);
                tri[j].x = s[0] + ox, tri[j].y = s[1] + oy, tri[j].z = s[2], tri[j].u = v[3], tri[j].v = v[4];
                tri[j].r = (uint8_t)SDL_clamp(md->tint[0] * bright * shade, 0, 255);
                tri[j].g = (uint8_t)SDL_clamp(md->tint[1] * bright * shade, 0, 255);
                tri[j].b = (uint8_t)SDL_clamp(md->tint[2] * bright * shade, 0, 255);
                tri[j].a = (uint8_t)SDL_clamp(md->fog * 255.0f, 0, 255);
            }
            if (ok) {
                s_mv[n++] = tri[0], s_mv[n++] = tri[1], s_mv[n++] = tri[2];
            }
        }
        if (n > 0 && md->img >= 0) {
            gl_props(s_img[md->img].surf, &s_img[md->img].tex, s_mv, n, x1, y1, x2, y2, s_pfog_on ? s_pfog : NULL);
        }
    }
}

static void props_load(int id) {
    char dir[1024], path[1100];
    char *text;
    const char *p;
    Json *root, *list, *o;
    int i;

    s_nbb = s_nem = s_nmd = 0, s_props_stage = id, s_pwind = 1, s_pfog_on = 0;
    SDL_snprintf(dir, sizeof dir, "%s/mods/fightstages/%d", data_dir(), id);
    SDL_snprintf(path, sizeof path, "%s/props.json", dir);
    if ((text = SDL_LoadFile(path, NULL)) == NULL) {
        return;
    }
    p = text;
    root = json_value(&p);
    SDL_free(text);
    if (root == NULL) {
        LOG("props: %s isn't JSON", path);
        return;
    }
    s_pwind = jnum(root, "wind", 1.0f);
    if ((list = json_get(root, "fog")) != NULL && list->type == '{') {
        s_pfog[0] = s_pfog[1] = s_pfog[2] = 128.0f, s_pfog[3] = 0.6f, s_pfog[4] = 0.3f;
        jvec(list, "color", s_pfog, 3);
        s_pfog[3] = SDL_clamp(jnum(list, "amount", s_pfog[3]), 0.0f, 1.0f), s_pfog[4] = SDL_clamp(jnum(list, "from", s_pfog[4]), 0.0f, 0.99f);
        s_pfog_on = 1;
    }
    list = json_get(root, "props");
    for (i = 0; i < json_len(list) && s_nbb < MAX_BB; i++) {
        Billboard *b = &s_bb[s_nbb];
        Json *f;

        o = json_at(list, i);
        b->img = image_of(dir, o);
        if (b->img < 0) {
            continue;
        }
        b->size[0] = b->size[1] = 100, b->tint[0] = b->tint[1] = b->tint[2] = 255;
        jvec(o, "pos", b->pos, 3), jvec(o, "size", b->size, 2), jvec(o, "tint", b->tint, 3);
        b->scale = jnum(o, "scale", 1.0f), b->sway = jnum(o, "sway", 0), b->rot = jnum(o, "rot", 0), b->fog = jnum(o, "fog", 1.0f);
        f = json_get(o, "facing");
        b->fixed = f != NULL && f->type == 's' && SDL_strcmp(f->str, "fixed") == 0;
        s_nbb++;
    }
    list = json_get(root, "emitters");
    for (i = 0; i < json_len(list) && s_nem < MAX_EM; i++) {
        Emitter *e = &s_em[s_nem];
        Json *area;
        int k;
        float wind[2];

        o = json_at(list, i);
        e->img = image_of(dir, o); /* an emitter needs its image; the stage's leaves are stages.ini's */
        if (e->img < 0) {
            continue;
        }
        e->count = SDL_clamp((int)jnum(o, "count", 20), 1, MAX_PART);
        e->radius = 3000, e->top = 4000, e->center[0] = e->center[1] = e->center[2] = 0;
        area = json_get(o, "area");
        if (area != NULL) {
            jvec(area, "center", e->center, 3);
            e->radius = jnum(area, "radius", e->radius), e->top = jnum(area, "top", e->top);
        }
        e->fall = jnum(o, "fall", 1.0f), e->wind = jnum(o, "wind", 1.0f), e->fog = jnum(o, "fog", 1.0f);
        e->size[0] = e->size[1] = 100;
        jvec(o, "size", e->size, 2);
        e->tint[0] = e->tint[1] = e->tint[2] = 255;
        jvec(o, "tint", e->tint, 3);
        e->p = &s_pool[s_nem * MAX_PART];
        wind_at((float)s_now, e->wind, wind);
        for (k = 0; k < e->count; k++) {
            spawn_part(e, &e->p[k], 1, wind);
        }
        s_nem++;
    }
    list = json_get(root, "models");
    for (i = 0; i < json_len(list) && s_nmd < MAX_MD; i++) {
        ModelProp *md = &s_md[s_nmd];

        o = json_at(list, i);
        md->mesh = mesh_of(dir, o);
        md->img = image_of(dir, o);
        if (md->mesh < 0 || md->img < 0) {
            continue;
        }
        md->tint[0] = md->tint[1] = md->tint[2] = 255, md->pos[0] = md->pos[1] = md->pos[2] = 0;
        jvec(o, "pos", md->pos, 3), jvec(o, "tint", md->tint, 3);
        md->scale = jnum(o, "scale", 1.0f), md->rot = jnum(o, "rot", 0), md->sway = jnum(o, "sway", 0), md->fog = jnum(o, "fog", 1.0f);
        s_nmd++;
    }
    json_free(root);
    LOG("props: stage %d: %d billboards, %d emitters, %d models", id, s_nbb, s_nem, s_nmd);
}

static void emit_quad(PropVtx *v, const float sx[4], const float sy[4], const float sz[4], const float col[3], float fog) {
    static const int TRI[6] = {0, 1, 2, 1, 3, 2}; /* 0 1 / 2 3: top left, top right, bottom left, bottom right */
    static const float U[4] = {0, 1, 0, 1}, V[4] = {0, 0, 1, 1};
    int j;

    for (j = 0; j < 6; j++) {
        int c = TRI[j];

        v[j].x = sx[c], v[j].y = sy[c], v[j].z = sz[c], v[j].u = U[c], v[j].v = V[c];
        v[j].r = (uint8_t)SDL_clamp(col[0], 0, 255), v[j].g = (uint8_t)SDL_clamp(col[1], 0, 255), v[j].b = (uint8_t)SDL_clamp(col[2], 0, 255);
        v[j].a = (uint8_t)SDL_clamp(fog * 255.0f, 0, 255);
    }
}

/* The billboards and the emitters' particles with image I, in one call */
static void props_image(int im, float bright, int ox, int oy, int x1, int y1, int x2, int y2) {
    int n = 0, i, k;
    float wide = gte_camera_wide() ? 0.75f : 1.0f, t = (float)s_now;

    for (i = 0; i < s_nbb; i++) {
        const Billboard *b = &s_bb[i];
        float w = b->size[0] * b->scale, h = b->size[1] * b->scale, col[3], sx[4], sy[4], sz[4], s0[3], s1[3], ww[2];
        float base[3] = {b->pos[0], -b->pos[1], b->pos[2]}, top[3];
        const StageFx *fx;
        float fb;

        if (b->img != im) {
            continue;
        }
        for (k = 0; k < 3; k++) {
            col[k] = b->tint[k] * bright;
        }
        fx = stagefx_current(&fb);
        wind_at(t, (fx != NULL ? fx->wind : 1.0f) * s_pwind, ww);
        top[0] = base[0], top[1] = base[1] - h, top[2] = base[2];
        if (b->sway > 0) { /* the tip leans and swings in the wind, as the trees (native_stage_sway) */
            float move = 0.5f + sinf(t * 0.042f + (base[0] + base[2]) * 0.0004f);

            top[0] += ww[0] / 18.0f * b->sway * h * 0.3f * move;
            top[2] += ww[1] / 18.0f * b->sway * h * 0.3f * move;
        }
        if (!b->fixed) { /* faces the camera: upright on the screen, its tip where the sway puts it */
            float hw;

            if (!gte_camera_project(base, s0) || !gte_camera_project(top, s1)) {
                continue;
            }
            hw = w * 0.5f * gte_camera_scale(s0[2]) * wide;
            { /* seen from above its height shrinks to nothing: kept at 60% of its size, as if it leaned toward the camera */
                float full = h * gte_camera_scale(s0[2]), shown = s0[1] - s1[1];

                if (shown < full * 0.6f) {
                    s1[1] = s0[1] - full * 0.6f;
                }
            }
            sx[0] = s1[0] - hw * (s0[2] / s1[2]), sx[1] = s1[0] + hw * (s0[2] / s1[2]), sx[2] = s0[0] - hw, sx[3] = s0[0] + hw;
            sy[0] = sy[1] = s1[1], sy[2] = sy[3] = s0[1];
            sz[0] = sz[1] = s1[2], sz[2] = sz[3] = s0[2];
        } else { /* flat in the world, turned rot degrees about the vertical */
            float a = b->rot * 0.017453293f, rx = cosf(a) * w * 0.5f, rz = sinf(a) * w * 0.5f, c[4][3], pr[3];
            int ok = 1;

            for (k = 0; k < 4; k++) { /* top left, top right, bottom left, bottom right */
                float side = k & 1 ? 1.0f : -1.0f, up = k < 2 ? h : 0;

                c[k][0] = base[0] + rx * side + (k < 2 ? top[0] - base[0] : 0), c[k][1] = base[1] - up, c[k][2] = base[2] + rz * side + (k < 2 ? top[2] - base[2] : 0);
                ok &= gte_camera_project(c[k], pr);
                sx[k] = pr[0], sy[k] = pr[1], sz[k] = pr[2];
            }
            if (!ok) {
                continue;
            }
        }
        for (k = 0; k < 4; k++) {
            sx[k] += ox, sy[k] += oy;
        }
        emit_quad(&s_pv[n], sx, sy, sz, col, b->fog);
        n += 6;
    }
    for (i = 0; i < s_nem; i++) {
        Emitter *e = &s_em[i];

        if (e->img != im) {
            continue;
        }
        for (k = 0; k < e->count; k++) {
            Leaf *l = &e->p[k];
            float s[3], half, flip, c = cosf(l->angle), sn = sinf(l->angle), px[4], py[4], pz[4], col[3];
            int j;

            if (!gte_camera_project(l->p, s)) {
                continue;
            }
            half = l->size * gte_camera_scale(s[2]) * 0.5f;
            if (half < 0.35f) {
                continue;
            }
            flip = 0.25f + 0.75f * fabsf(cosf(t * 0.06f + l->phase * 2.0f));
            for (j = 0; j < 4; j++) {
                float lx = (j & 1 ? 0.5f : -0.5f) * flip * 2.0f, ly = j < 2 ? -1.0f : 1.0f;

                px[j] = s[0] + (lx * c - ly * sn) * half * wide + ox, py[j] = s[1] + (lx * sn + ly * c) * half + oy, pz[j] = s[2];
            }
            for (j = 0; j < 3; j++) {
                col[j] = e->tint[j] * bright;
            }
            emit_quad(&s_pv[n], px, py, pz, col, e->fog);
            n += 6;
        }
    }
    if (n > 0) {
        gl_props(s_img[im].surf, &s_img[im].tex, s_pv, n, x1, y1, x2, y2, s_pfog_on ? s_pfog : NULL);
    }
}

static void emitters_step(void) {
    float t = (float)s_now;
    int i, k;

    for (i = 0; i < s_nem; i++) {
        Emitter *e = &s_em[i];
        float wind[2];

        wind_at(t, e->wind, wind);
        for (k = 0; k < e->count; k++) {
            Leaf *l = &e->p[k];
            float sway = sinf(t * 0.045f + l->phase), dx = l->p[0] - e->center[0], dz = l->p[2] - e->center[2];

            l->p[0] += wind[0] + sway * 9.0f;
            l->p[2] += wind[1] + cosf(t * 0.037f + l->phase) * 6.0f;
            l->p[1] += l->fall * (0.7f + 0.3f * fabsf(sway));
            l->angle += l->spin;
            if (l->p[1] > -e->center[1] - 20.0f || dx * dx + dz * dz > e->radius * e->radius * 2.5f) {
                spawn_part(e, l, 0, wind);
            }
        }
    }
}

/* Once a frame, after the fighters: the stage's list, if the mod has one */
static void props_draw(int ox, int oy, int x1, int y1, int x2, int y2) {
    int i;

    if (!gte_in_battle() || (int)s_now - s_frame > 2 || s_shown < 0 || s_shown >= STAGES) {
        s_props_stage = -2;
        return;
    }
    if (s_shown != s_props_stage) {
        props_load(s_shown);
    }
    if (s_nbb == 0 && s_nem == 0 && s_nmd == 0) {
        return;
    }
    emitters_step();
    for (i = 0; i < s_nimg; i++) {
        props_image(i, s_bright / 128.0f, ox, oy, x1, y1, x2, y2);
    }
    models_draw(s_bright / 128.0f, ox, oy, x1, y1, x2, y2);
}

/* After the fighters' table (gpu_draw.c), once a frame */
void stagefx_draw(int ox, int oy, int x1, int y1, int x2, int y2) {
    float bright, wind[2];
    const StageFx *fx = stagefx_current(&bright);
    int i, n = fx != NULL ? SDL_min(fx->leaves, LEAVES) : 0;

    props_draw(ox, oy, x1, y1, x2, y2);
    if (n == 0) {
        s_leaf_count = -1;
        return;
    }
    if (n != s_leaf_count || s_shown != s_leaf_stage) { /* a new stage: leaves all through the air */
        wind_at((float)s_now, fx->wind, wind);
        for (i = 0; i < n; i++) {
            spawn(&s_leaves[i], 1, wind);
        }
        s_leaf_count = n, s_leaf_stage = s_shown;
    }
    leaves_step(n, fx->wind);
    leaves_draw(n, bright, ox, oy, x1, y1, x2, y2);
}

/* --- the stage's own meshes: drifting clouds, swaying trees ---
   FIGHTSTG's patch (fightstg.c) asks for each stage mesh as it is drawn */

/* The painted sky's turn now: its cosine and sine, 0 when it doesn't */
int native_stage_drift(float *c, float *s) {
    float bright, a;
    const StageFx *fx = stagefx_current(&bright);

    if (fx == NULL || fx->clouds == 0) {
        return 0;
    }
    a = fmodf(s_now / 60.0f * fx->clouds, 360.0f) * 0.017453293f;
    *c = cosf(a), *s = sinf(a);
    s_drifted++;
    return 1;
}

/* How far a prop's point at x, y, z moves in the wind now, by the leaves'
   wind (wind_at: the stage's wind and its gusts): a sway every ~2.5 s, each
   place in its own time, and a lean downwind, both as strong as the gust
   blowing the leaves (a lull: barely; a full gust of wind 1 at sway 1: +-200
   units of sway and 100 of lean at a tree's top 6000 up), as the square of
   the height (none at the ground) */
static float s_top; /* the most a point moved this second, for DW3_LOG=gpu */

int native_stage_sway(float x, float y, float z, float *dx, float *dz) {
    float bright, w[2], h, amp, move, t = (float)s_now;
    const StageFx *fx = stagefx_current(&bright);

    if (fx == NULL || fx->sway <= 0 || y > -400.0f) {
        return 0;
    }
    h = SDL_min(-y / 6000.0f, 1.5f);
    wind_at(t, fx->wind, w); /* the leaves' drift a frame: 18 at a full gust of wind 1 */
    amp = fx->sway * 200.0f * h * h / 18.0f;
    move = 0.5f + sinf(t * 0.042f + (x + z) * 0.0004f);
    *dx = w[0] * amp * move;
    *dz = w[1] * amp * move;
    s_top = SDL_max(s_top, sqrtf(*dx * *dx + *dz * *dz));
    s_swayed++;
    return 1;
}

static void stagefx_log(void) {
    float b;
    const StageFx *fx = stagefx_current(&b);

    if ((g_log & LOG_GPU) && fx != NULL) {
        float w[2];

        wind_at((float)s_now, fx->wind, w);
        LOG("stagefx: stage %d, %d leaves, wind %.1f a frame; %d prop points swayed (at most %.0f units) and %d sky "
            "meshes turned this second (sky at %.1f deg)",
            s_shown, s_leaf_count, sqrtf(w[0] * w[0] + w[1] * w[1]), s_swayed, s_top, s_drifted,
            fmodf(s_now / 60.0f * fx->clouds, 360.0f));
    }
    s_swayed = s_drifted = 0;
    s_top = 0;
}
