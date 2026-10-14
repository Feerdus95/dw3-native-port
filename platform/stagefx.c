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

/* After the fighters' table (gpu_draw.c), once a frame */
void stagefx_draw(int ox, int oy, int x1, int y1, int x2, int y2) {
    float bright, wind[2];
    const StageFx *fx = stagefx_current(&bright);
    int i, n = fx != NULL ? SDL_min(fx->leaves, LEAVES) : 0;

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

/* How far a prop's point at x, y, z moves in the wind now: the leaves'
   wind (wind_at, the stage's wind and its gusts), so the trees lean
   downwind as much as the leaves are blown and ease back as a gust dies,
   with a small flutter on top, each place in its own time; as the square of
   its height (none at the ground; ~180 units at a tree's top 6000 up for
   sway 1 in a full gust of wind 1) */
int native_stage_sway(float x, float y, float z, float *dx, float *dz) {
    float bright, w[2], h, amp, flutter, t = (float)s_now;
    const StageFx *fx = stagefx_current(&bright);

    if (fx == NULL || fx->sway <= 0 || y > -400.0f) {
        return 0;
    }
    h = SDL_min(-y / 6000.0f, 1.5f);
    wind_at(t, fx->wind, w); /* the leaves' drift a frame: 18 at a full gust of wind 1 */
    amp = fx->sway * 180.0f * h * h / 18.0f;
    flutter = 1.0f + 0.25f * sinf(t * 0.05f + (x + z) * 0.0004f);
    *dx = w[0] * amp * flutter;
    *dz = w[1] * amp * flutter;
    s_swayed++;
    return 1;
}

static void stagefx_log(void) {
    float b;
    const StageFx *fx = stagefx_current(&b);

    if ((g_log & LOG_GPU) && fx != NULL) {
        LOG("stagefx: stage %d, %d leaves; %d prop points swayed and %d sky meshes turned this second (sky at %.1f deg)",
            s_shown, s_leaf_count, s_swayed, s_drifted, fmodf(s_now / 60.0f * fx->clouds, 360.0f));
    }
    s_swayed = s_drifted = 0;
}
