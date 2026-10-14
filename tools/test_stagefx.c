/* stages.ini read as the profiles say (platform/stagefx.c), from the exe's
   folder. Run from build/ with stages.ini copied there:
   i686-w64-mingw32-clang -I<SDL>/include -I../platform ../tools/test_stagefx.c ../platform/stagefx.c -L<SDL>/lib -lSDL3 -o test_stagefx.exe
   cp ../assets/stages.ini . && ./test_stagefx.exe */
#include <assert.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include "platform.h"

int g_battle_sky = 1, g_depth = 1;
int gte_in_battle(void) { return 1; }
int gte_camera_project(const float p[3], float out[3]) { return 0; }
float gte_camera_scale(float z) { return 1; }
int gte_camera_wide(void) { return 0; }
void gl_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {}
void native_stage_shown(int id, int bright);

static const StageFx *at(int stage) {
    float b;

    native_stage_shown(stage, 64);
    return stagefx_current(&b);
}

int main(void) {
    const StageFx *fx;
    float b;

    fx = at(13); /* open air */
    assert(fx && fx->sky && fx->fog == 0.6f && fx->horizon == 20000 && fx->fog_color[0] == -1);
    fx = at(14); /* open air, its own fog colour */
    assert(fx && fx->sky && fx->fog_color[0] == 200 && fx->fog_color[2] == 230);
    fx = at(15); /* a room, its own fog */
    assert(fx && !fx->sky && fx->fog == 0.3f && fx->fog_color[1] == 184 && fx->horizon == 0);
    assert(fx->leaves == 0);
    fx = at(13);
    assert(fx->leaves == 60 && fx->wind == 1.0f && fx->clouds == 1.5f && fx->sway == 1.0f);
    fx = at(7); /* a room */
    assert(fx && !fx->sky && fx->fog_color[0] == 0 && fx->horizon == 0);
    assert(at(18) == NULL && at(33) == NULL && at(56) == NULL); /* left as they are */
    native_stage_shown(13, 64);
    assert(stagefx_current(&b) && b == 0.5f);
    stagefx_frame(), stagefx_frame(), stagefx_frame();
    assert(stagefx_current(&b) == NULL); /* not told of for 3 frames: gone */
    puts("stagefx ok");
    return 0;
}
