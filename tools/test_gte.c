/* GsSetRefView2 with a super and a roll against the plain view. Run:
   clang -m32 -I platform -include stdint.h tools/test_gte.c -o test_gte.exe && ./test_gte.exe */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../platform/gte.c"
static void view_of(MATRIX *w, const int32_t q[3], int32_t out[3]) {
    apply_lv(w->m, q, out);
    for (int i = 0; i < 3; i++) out[i] += w->t[i];
}
int main(void) {
    GsCOORDINATE2 c = {0};
    SVECTOR r = {300, -700, 150};
    GsRVIEW2 v = {100, -400, -3000, 0, -200, 0, 10 << 12, NULL};
    MATRIX a, b;
    int32_t q[3] = {500, -300, 800}, local[3], d[3], o1[3], o2[3];
    MATRIX inv;
    RotMatrixYXZ_gte(&r, &c.coord);
    c.coord.t[0] = 1200; c.coord.t[1] = -50; c.coord.t[2] = 900;
    v.super = &c; GsSetRefView2(&v); a = GsWSMATRIX;
    v.super = NULL; GsSetRefView2(&v); b = GsWSMATRIX;
    for (int i = 0; i < 3; i++) d[i] = q[i] - c.coord.t[i];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) inv.m[i][j] = c.coord.m[j][i];
    apply_lv(inv.m, d, local);
    view_of(&a, q, o1); view_of(&b, local, o2);
    printf("with super %d %d %d | local %d %d %d\n", o1[0], o1[1], o1[2], o2[0], o2[1], o2[2]);
    for (int i = 0; i < 3; i++) if (abs(o1[i] - o2[i]) > 8) { puts("FAIL"); return 1; }
    /* rz = 10 degrees: the camera's x axis turns by 10 degrees */
    printf("roll row0 %d %d %d\n", b.m[0][0], b.m[0][1], b.m[0][2]);
    if (abs(b.m[0][0] - 4034) > 20) { puts("FAIL roll"); return 1; }
    puts("OK");
    return 0;
}
