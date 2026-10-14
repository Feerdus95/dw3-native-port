/* The fight stages' profiles for the battle sky and fog (g_battle_sky):
   stages.ini by the exe (assets/stages.ini), kinds of stage and the stages
   that take them, read at the first battle. FIGHTSTG's patch (stage.c)
   tells which stage is shown and how far its fade is, each frame. */
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
    }
}

/* stages.ini: kinds first, each stage a copy of its kind with its own keys
   on top (the keys before a stage's "kind" line are lost to it) */
static void load(void) {
    static const StageFx NONE = {0, {-1, -1, -1}, 0, 0.45f, {-1, -1, -1}, 20000, 0};
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
void stagefx_frame(void) {
    s_now++;
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
