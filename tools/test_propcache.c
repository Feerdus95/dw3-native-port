/* The baked props list (platform/stagefx.c): props.json read, the cache written, then read back from the cache,
   the two compared. Run from a scratch folder:
   i686-w64-mingw32-clang -I<SDL>/include -I../platform ../tools/test_propcache.c -L<SDL>/lib -lSDL3 -o test_propcache.exe
   with json_excerpt.inc made by: sed -n 34,150p ../platform/stagemods.c > json_excerpt.inc  (-I. for it)
   (it includes stagefx.c and that JSON reader, with stubs for the card and the game) */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "platform.h"

int g_battle_sky = 1, g_depth = 1, g_log, g_perf;
int gte_in_battle(void) { return 1; }
int gte_camera_project(const float p[3], float out[3]) { return 0; }
int gte_camera_get(PCam *c) { return 0; }
float gte_camera_scale(float z) { return 1; }
int gte_camera_wide(void) { return 0; }
void gl_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {}
unsigned gl_inst_make(const PInst *v, int n) { return 1; }
void gl_inst_free(unsigned b) {}
void gl_props_inst(const SDL_Surface *i, unsigned *t, unsigned b, int f, int n, const PCam *c, int x1, int y1, int x2, int y2, const float *fo) {}
void gl_props_part(const SDL_Surface *i, unsigned *t, const PPart *p, const PCam *c, int x1, int y1, int x2, int y2, const float *fo) {}
void gl_props(const SDL_Surface *i, unsigned *t, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fo) {}
int perf_on(void) { return 0; }
void perf_begin(int s) {}
void perf_end(int s) {}
void perf_gpu(int s, double ms) {}
static char s_data[1024];
const char *data_dir(void) { return s_data; }

/* the JSON reader, lines 34-150 of stagemods.c */
#include "json_excerpt.inc"

#include "stagefx.c"

static void write_png(const char *path) {
    SDL_Surface *s = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);

    SDL_FillSurfaceRect(s, NULL, SDL_MapSurfaceRGBA(s, 10, 200, 30, 255));
    assert(SDL_SavePNG(s, path));
    SDL_DestroySurface(s);
}

int main(void) {
    char dir[1100], path[1200];
    FILE *f;
    PInst *a;
    Emitter ea[MAX_EM];
    ModelProp ma[MAX_MD];
    int na, nea, nma, ibeg[MAX_IMG], icnt[MAX_IMG];
    float wind, fog[5];
    int fogon, leaves, i;

    SDL_snprintf(s_data, sizeof s_data, "tdata");
    SDL_snprintf(dir, sizeof dir, "tdata/mods/fightstages/7");
    SDL_CreateDirectory("tdata");
    SDL_CreateDirectory("tdata/mods");
    SDL_CreateDirectory("tdata/mods/fightstages");
    SDL_CreateDirectory(dir);
    SDL_snprintf(path, sizeof path, "%s/grass.png", dir), write_png(path);
    SDL_snprintf(path, sizeof path, "%s/flower.png", dir), write_png(path);
    SDL_snprintf(path, sizeof path, "%s/petal.png", dir), write_png(path);
    SDL_snprintf(path, sizeof path, "%s/rock.obj", dir);
    f = fopen(path, "w");
    fputs("v 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 0 1\nvn 0 0 1\nf 1/1/1 2/2/1 3/3/1\n", f);
    fclose(f);
    SDL_snprintf(path, sizeof path, "%s/props.json", dir);
    f = fopen(path, "w");
    fputs("{\"wind\": 1.5, \"leaves\": 12, \"fog\": {\"color\": [10, 20, 30], \"amount\": 0.4, \"from\": 0.2},\n \"props\": [", f);
    for (i = 0; i < 500; i++) {
        fprintf(f, "%s{\"type\":\"billboard\",\"image\":\"%s\",\"pos\":[%d,0,%d],\"size\":[100,%d],\"sway\":0.5,\"tint\":[200,210,220],\"rot\":%d%s}", i ? "," : "", i % 3 ? "grass.png" : "flower.png", i * 7 - 900, 300 - i * 3, 80 + i % 40, i % 90, i % 5 == 0 ? ",\"facing\":\"fixed\"" : "");
    }
    fputs("],\n \"emitters\": [{\"type\":\"emitter\",\"image\":\"petal.png\",\"count\":40,\"area\":{\"center\":[1,2,3],\"radius\":2500,\"top\":3500},\"fall\":1.5,\"wind\":0.8,\"size\":[30,60],\"tint\":[255,200,210]}],\n"
          " \"models\": [{\"type\":\"model\",\"model\":\"rock.obj\",\"image\":\"grass.png\",\"pos\":[5,6,7],\"scale\":2,\"rot\":30,\"sway\":0.1,\"tint\":[100,110,120]}]}\n", f);
    fclose(f);

    /* from the JSON: this also writes the cache */
    props_load(7);
    assert(s_nbb == 500 && s_nem == 1 && s_nmd == 1);
    na = s_nbb, nea = s_nem, nma = s_nmd, wind = s_pwind, leaves = s_pleaves, fogon = s_pfog_on;
    memcpy(fog, s_pfog, sizeof fog), memcpy(ea, s_em, sizeof ea), memcpy(ma, s_md, sizeof ma);
    memcpy(ibeg, s_ibeg, sizeof ibeg), memcpy(icnt, s_icnt, sizeof icnt);
    a = SDL_malloc(sizeof(PInst) * na), memcpy(a, s_inst, sizeof(PInst) * na);
    SDL_snprintf(path, sizeof path, "tdata/cache/props_7.bin");
    {
        SDL_PathInfo info;

        assert(SDL_GetPathInfo(path, &info) && info.size > 500 * sizeof(PInst));
    }

    /* from the cache: the same */
    s_props_stage = -2;
    props_load(7);
    assert(s_nbb == na && s_nem == nea && s_nmd == nma && s_pwind == wind && s_pleaves == leaves && s_pfog_on == fogon);
    assert(leaves == 12 && wind == 1.5f && fogon == 1);
    assert(!memcmp(fog, s_pfog, sizeof fog) && !memcmp(a, s_inst, sizeof(PInst) * na));
    for (i = 0; i < MAX_IMG; i++) {
        assert(ibeg[i] == s_ibeg[i] || icnt[i] == 0);
        assert(icnt[i] == s_icnt[i]);
    }
    assert(!memcmp(&ea[0], &s_em[0], sizeof(Emitter)) && !memcmp(&ma[0], &s_md[0], sizeof(ModelProp)));

    /* the JSON changes: the cache is not its */
    SDL_Delay(1100);
    SDL_snprintf(path, sizeof path, "%s/props.json", dir);
    f = fopen(path, "w");
    fputs("{\"props\": [{\"type\":\"billboard\",\"image\":\"grass.png\",\"pos\":[1,2,3]}]}\n", f);
    fclose(f);
    s_props_stage = -2;
    props_load(7);
    assert(s_nbb == 1 && s_nem == 0 && s_nmd == 0 && s_pleaves == -1);
    puts("propcache ok");
    return 0;
}
