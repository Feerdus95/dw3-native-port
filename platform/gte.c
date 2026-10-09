/* The GTE (the PSX's geometry coprocessor) in C, and the libgte/libgs
   functions built on it. Fixed point as the hardware does it (psx-spx,
   "Geometry Transformation Engine"), so results match closely. */
#include <math.h>
#include <string.h>
#include "platform.h"

#define ANGLE (6.283185307179586 / 4096) /* radians per PSX angle unit */

typedef struct {
    int16_t m[3][3];
    int16_t pad;
    int32_t t[3];
} MATRIX;

typedef struct {
    int16_t vx, vy, vz, pad;
} SVECTOR;

typedef struct {
    int32_t vx, vy, vz, pad;
} VECTOR;

/* --- registers --- */
static int16_t V0[3];
static int32_t IR[4];  /* IR0-IR3 */
static int32_t MAC[4]; /* MAC0-MAC3 */
static int32_t SX[3], SY[3]; /* SXY0-SXY2 */
static int32_t SZ[4];  /* SZ0-SZ3 */
static uint32_t RGB2, RGBC;
static uint32_t OTZ, FLAG;

static int16_t RT[3][3], LM[3][3], LCM[3][3];
static int32_t TR[3], BK[3];
static int32_t OFX, OFY, H = 1000, DQA, DQB, ZSF3 = 0x155, ZSF4 = 0x100;

static int32_t clamp(int64_t v, int32_t lo, int32_t hi) {
    return v < lo ? lo : v > hi ? hi : (int32_t)v;
}

/* MAC1-3 = (T * 0x1000 + M * v) >> 12; IR1-3 saturated (lm: 0 or -0x8000) */
static void mvmva(int16_t m[3][3], const int32_t v[3], const int32_t *t, int lm) {
    int i;

    for (i = 0; i < 3; i++) {
        int64_t s = t ? (int64_t)t[i] << 12 : 0;

        s += (int64_t)m[i][0] * v[0] + (int64_t)m[i][1] * v[1] + (int64_t)m[i][2] * v[2];
        MAC[i + 1] = (int32_t)(s >> 12);
        IR[i + 1] = clamp(MAC[i + 1], lm ? 0 : -0x8000, 0x7FFF);
    }
}

static void v0_as_int(int32_t out[3]) {
    out[0] = V0[0];
    out[1] = V0[1];
    out[2] = V0[2];
}

/* --- the macros of the game's gte.h --- */
void psx_gte_lddp(int32_t r) { IR[0] = (int16_t)r; }

void psx_gte_ldsv(const int16_t *p) {
    IR[1] = p[0];
    IR[2] = p[1];
    IR[3] = p[2];
}

void psx_gte_stsv(int16_t *p) {
    p[0] = (int16_t)IR[1];
    p[1] = (int16_t)IR[2];
    p[2] = (int16_t)IR[3];
}

void psx_gte_gpf12(void) {
    int i;

    for (i = 1; i <= 3; i++) {
        MAC[i] = (int32_t)(((int64_t)IR[0] * IR[i]) >> 12);
        IR[i] = clamp(MAC[i], -0x8000, 0x7FFF);
    }
}

void psx_gte_SetRotMatrix(const MATRIX *m) { memcpy(RT, m->m, sizeof RT); }
void psx_gte_SetTransMatrix(const MATRIX *m) { memcpy(TR, m->t, sizeof TR); }
void psx_gte_SetLightMatrix(const MATRIX *m) { memcpy(LM, m->m, sizeof LM); }

void psx_gte_ldv0(const int16_t *p) {
    V0[0] = p[0];
    V0[1] = p[1];
    V0[2] = p[2];
}

void psx_gte_ldv0_unaligned(const void *p) {
    int16_t v[3];

    memcpy(v, p, sizeof v);
    psx_gte_ldv0(v);
}

void psx_gte_ldlv0(const int32_t *p) {
    V0[0] = (int16_t)p[0];
    V0[1] = (int16_t)p[1];
    V0[2] = (int16_t)p[2];
}

/* V0 through RT and TR, then the perspective into SXY2/SZ3 and the depth cue into IR0 */
void psx_gte_rtps(void) {
    int32_t v[3];
    int64_t h_sz, mac0;
    int i;

    FLAG = 0; /* each command starts it over */
    v0_as_int(v);
    mvmva(RT, v, TR, 0);
    for (i = 0; i < 3; i++) {
        SZ[i] = SZ[i + 1];
    }
    SZ[3] = clamp(MAC[3], 0, 0xFFFF);
    if (SZ[3] > H / 2) {
        h_sz = (((int64_t)H * 0x20000 / SZ[3]) + 1) / 2;
        if (h_sz > 0x1FFFF) h_sz = 0x1FFFF;
    } else {
        h_sz = 0x1FFFF;
        FLAG |= 1u << 17;
    }
    SX[0] = SX[1]; SY[0] = SY[1];
    SX[1] = SX[2]; SY[1] = SY[2];
    mac0 = h_sz * IR[1] + OFX;
    SX[2] = clamp(mac0 >> 16, -0x400, 0x3FF);
    mac0 = h_sz * IR[2] + OFY;
    SY[2] = clamp(mac0 >> 16, -0x400, 0x3FF);
    mac0 = h_sz * DQA + DQB;
    MAC[0] = (int32_t)mac0;
    IR[0] = clamp(mac0 >> 12, 0, 0x1000);
}

void psx_gte_rtv0(void) {
    int32_t v[3];

    v0_as_int(v);
    mvmva(RT, v, NULL, 0);
}

void psx_gte_rt(void) {
    int32_t v[3];

    v0_as_int(v);
    mvmva(RT, v, TR, 0);
}

void psx_gte_rtir(void) {
    int32_t v[3] = {IR[1], IR[2], IR[3]};

    mvmva(RT, v, NULL, 0);
}

void psx_gte_ldclmv(const int16_t *p) {
    IR[1] = p[0];
    IR[2] = p[3];
    IR[3] = p[6];
}

void psx_gte_stclmv(int16_t *p) {
    p[0] = (int16_t)IR[1];
    p[3] = (int16_t)IR[2];
    p[6] = (int16_t)IR[3];
}

void psx_gte_stsxy(uint32_t *p) { *p = (uint16_t)SX[2] | (uint32_t)(uint16_t)SY[2] << 16; }
void psx_gte_stflg(uint32_t *p) { *p = FLAG; }

void psx_gte_stlvnl(int32_t *p) {
    p[0] = MAC[1];
    p[1] = MAC[2];
    p[2] = MAC[3];
}

void psx_gte_stszotz(int32_t *p) { *p = SZ[3] >> 2; }

/* Normal color single: lights on V0 (L), then the color matrix and back color */
void psx_gte_ncs(void) {
    int32_t v[3];
    int32_t ir[3];
    int i;

    v0_as_int(v);
    mvmva(LM, v, NULL, 1);
    ir[0] = IR[1];
    ir[1] = IR[2];
    ir[2] = IR[3];
    mvmva(LCM, ir, BK, 1);
    RGB2 = RGBC & 0xFF000000u;
    for (i = 0; i < 3; i++) {
        RGB2 |= (uint32_t)clamp(MAC[i + 1] >> 4, 0, 255) << (i * 8);
    }
}

void psx_gte_strgb(uint32_t *p) { *p = RGB2; }

void psx_gte_ldsxy3(uint32_t a, uint32_t b, uint32_t c) {
    SX[0] = (int16_t)a; SY[0] = (int16_t)(a >> 16);
    SX[1] = (int16_t)b; SY[1] = (int16_t)(b >> 16);
    SX[2] = (int16_t)c; SY[2] = (int16_t)(c >> 16);
}

void psx_gte_nclip(void) {
    MAC[0] = SX[0] * SY[1] + SX[1] * SY[2] + SX[2] * SY[0] - SX[0] * SY[2] - SX[1] * SY[0] - SX[2] * SY[1];
}

void psx_gte_stopz(int32_t *p) { *p = MAC[0]; }

void psx_gte_ldsz3(uint32_t a, uint32_t b, uint32_t c) {
    SZ[1] = (uint16_t)a;
    SZ[2] = (uint16_t)b;
    SZ[3] = (uint16_t)c;
}

void psx_gte_ldsz4(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    SZ[0] = (uint16_t)a;
    psx_gte_ldsz3(b, c, d);
}

void psx_gte_avsz3(void) {
    MAC[0] = ZSF3 * (SZ[1] + SZ[2] + SZ[3]);
    OTZ = (uint32_t)clamp(MAC[0] >> 12, 0, 0xFFFF);
}

void psx_gte_avsz4(void) {
    MAC[0] = ZSF4 * (SZ[0] + SZ[1] + SZ[2] + SZ[3]);
    OTZ = (uint32_t)clamp(MAC[0] >> 12, 0, 0xFFFF);
}

void psx_gte_stotz(uint32_t *p) { *p = OTZ; }

/* --- libgte --- */

void InitGeom(void) {
    OFX = OFY = 0;
    H = 1000;
    DQA = -0x155;
    DQB = 0x1400000;
    ZSF3 = 0x155;
    ZSF4 = 0x100;
}

void SetGeomOffset(int ofx, int ofy) {
    OFX = ofx << 16;
    OFY = ofy << 16;
}

void SetBackColor(int r, int g, int b) {
    BK[0] = r << 4;
    BK[1] = g << 4;
    BK[2] = b << 4;
}

/* sin and cos of a 4096-per-turn angle, times 4096 */
int rsin(int a) {
    return (int)lround(sin(a * ANGLE) * 4096);
}

int rcos(int a) {
    return (int)lround(cos(a * ANGLE) * 4096);
}

/* a = a * b, 4.12 */
static void mul33(int16_t a[3][3], int16_t b[3][3]) {
    int16_t r[3][3];
    int i, j;

    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            r[i][j] = (int16_t)(((int64_t)a[i][0] * b[0][j] + (int64_t)a[i][1] * b[1][j] + (int64_t)a[i][2] * b[2][j]) >> 12);
    memcpy(a, r, sizeof r);
}

/* m * v >> 12 for a long vector (ApplyMatrixLV) */
static void apply_lv(int16_t m[3][3], const int32_t v[3], int32_t out[3]) {
    int i;

    for (i = 0; i < 3; i++) {
        out[i] = (int32_t)(((int64_t)m[i][0] * v[0] + (int64_t)m[i][1] * v[1] + (int64_t)m[i][2] * v[2]) >> 12);
    }
}

/* b = a * b, rotation and translation (GsMulCoord2) */
static void mul_coord(MATRIX *a, MATRIX *b) {
    int32_t t[3];
    int i;

    apply_lv(a->m, b->t, t);
    for (i = 0; i < 3; i++) {
        b->t[i] = t[i] + a->t[i];
    }
    {
        int16_t r[3][3];

        memcpy(r, a->m, sizeof r);
        mul33(r, b->m);
        memcpy(b->m, r, sizeof r);
    }
}

static void rot_x(int16_t m[3][3], int a) {
    int s = rsin(a), c = rcos(a);
    int16_t r[3][3] = {{4096, 0, 0}, {0, (int16_t)c, (int16_t)-s}, {0, (int16_t)s, (int16_t)c}};
    memcpy(m, r, sizeof r);
}

static void rot_y(int16_t m[3][3], int a) {
    int s = rsin(a), c = rcos(a);
    int16_t r[3][3] = {{(int16_t)c, 0, (int16_t)s}, {0, 4096, 0}, {(int16_t)-s, 0, (int16_t)c}};
    memcpy(m, r, sizeof r);
}

static void rot_z(int16_t m[3][3], int a) {
    int s = rsin(a), c = rcos(a);
    int16_t r[3][3] = {{(int16_t)c, (int16_t)-s, 0}, {(int16_t)s, (int16_t)c, 0}, {0, 0, 4096}};
    memcpy(m, r, sizeof r);
}

/* m = Ry * Rx * Rz */
MATRIX *RotMatrixYXZ_gte(SVECTOR *r, MATRIX *m) {
    int16_t x[3][3], z[3][3];

    rot_y(m->m, r->vy);
    rot_x(x, r->vx);
    rot_z(z, r->vz);
    mul33(m->m, x);
    mul33(m->m, z);
    return m;
}

/* m = Rz * Ry * Rx */
MATRIX *RotMatrixZYX_gte(SVECTOR *r, MATRIX *m) {
    int16_t y[3][3], x[3][3];

    rot_z(m->m, r->vz);
    rot_y(y, r->vy);
    rot_x(x, r->vx);
    mul33(m->m, y);
    mul33(m->m, x);
    return m;
}

/* Each column j times v[j] */
MATRIX *ScaleMatrix(MATRIX *m, VECTOR *v) {
    int i;

    for (i = 0; i < 3; i++) {
        m->m[i][0] = (int16_t)(((int64_t)m->m[i][0] * v->vx) >> 12);
        m->m[i][1] = (int16_t)(((int64_t)m->m[i][1] * v->vy) >> 12);
        m->m[i][2] = (int16_t)(((int64_t)m->m[i][2] * v->vz) >> 12);
    }
    return m;
}

SVECTOR *ApplyMatrixSV(MATRIX *m, SVECTOR *v0, SVECTOR *v1) {
    int i;
    int16_t in[3] = {v0->vx, v0->vy, v0->vz}, out[3];

    for (i = 0; i < 3; i++) {
        out[i] = (int16_t)((m->m[i][0] * in[0] + m->m[i][1] * in[1] + m->m[i][2] * in[2]) >> 12);
    }
    v1->vx = out[0];
    v1->vy = out[1];
    v1->vz = out[2];
    return v1;
}

/* --- libgs --- */

typedef struct GsCOORDINATE2 {
    uint32_t flg;
    MATRIX coord;
    MATRIX workm;
    void *param;
    struct GsCOORDINATE2 *super;
    struct GsCOORDINATE2 *sub;
} GsCOORDINATE2;

typedef struct {
    int32_t vpx, vpy, vpz, vrx, vry, vrz;
    int32_t rz; /* degrees * 4096 */
    GsCOORDINATE2 *super;
} GsRVIEW2;

/* A coordinate system's local-to-world matrix, through its supers (GsGetLw) */
static void get_lw(GsCOORDINATE2 *c, MATRIX *out) {
    if (c->super != NULL) {
        get_lw(c->super, out);
        {
            MATRIX local = c->coord;

            mul_coord(out, &local);
            *out = local;
        }
    } else {
        *out = c->coord;
    }
}

typedef struct {
    int vx, vy, vz;
    uint8_t r, g, b;
} GsF_LIGHT;

MATRIX GsWSMATRIX;      /* world to screen, from GsSetRefView2 */
MATRIX GsLIGHTWSMATRIX; /* the flat lights' directions, one per row */

void GsInit3D(void) {}
void GsSetLightMode(int mode) {}

void GsSetProjection(int h) {
    H = h;
}

/* A light: its direction into a row of the light matrix, its color into a
   column of the color matrix */
void GsSetFlatLight(int id, GsF_LIGHT *lt) {
    double len = sqrt((double)lt->vx * lt->vx + (double)lt->vy * lt->vy + (double)lt->vz * lt->vz);

    /* as libgs: a zero direction changes nothing; the color is c * 4096 / 255 */
    if (id < 0 || id > 2 || (int)len == 0) {
        return;
    }
    GsLIGHTWSMATRIX.m[id][0] = (int16_t)((-lt->vx << 12) / (int)len);
    GsLIGHTWSMATRIX.m[id][1] = (int16_t)((-lt->vy << 12) / (int)len);
    GsLIGHTWSMATRIX.m[id][2] = (int16_t)((-lt->vz << 12) / (int)len);
    LCM[0][id] = (int16_t)((lt->r << 12) / 255);
    LCM[1][id] = (int16_t)((lt->g << 12) / 255);
    LCM[2][id] = (int16_t)((lt->b << 12) / 255);
}

/* The camera at vp looking at vr, rolled by rz: rows x (right), y (down), z
   (forward), and t = -M * vp. The world's y points down, as the screen's.
   vp and vr are in super's coordinates: as libgs, the view is then composed
   with super's world matrix inverted (transposed, t = -R^T t). */
int GsSetRefView2(GsRVIEW2 *pv) {
    double f[3] = {pv->vrx - pv->vpx, pv->vry - pv->vpy, pv->vrz - pv->vpz};
    double x[3], y[3], len, c, s, rx[3], ry[3];
    int i, roll = pv->rz / 360; /* degrees * 4096 to 4096 per turn, truncated as libgs */

    len = sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len == 0) {
        return 0;
    }
    for (i = 0; i < 3; i++) f[i] /= len;
    /* x = down x f, y = f x x */
    x[0] = f[2]; x[1] = 0; x[2] = -f[0];
    len = sqrt(x[0] * x[0] + x[2] * x[2]);
    if (len == 0) { x[0] = 1; x[2] = 0; len = 1; }
    x[0] /= len; x[2] /= len;
    y[0] = f[1] * x[2] - f[2] * x[1];
    y[1] = f[2] * x[0] - f[0] * x[2];
    y[2] = f[0] * x[1] - f[1] * x[0];
    c = rcos(roll) / 4096.0;
    s = rsin(roll) / 4096.0;
    for (i = 0; i < 3; i++) {
        rx[i] = c * x[i] + s * y[i];
        ry[i] = -s * x[i] + c * y[i];
    }
    for (i = 0; i < 3; i++) {
        GsWSMATRIX.m[0][i] = (int16_t)lround(rx[i] * 4096);
        GsWSMATRIX.m[1][i] = (int16_t)lround(ry[i] * 4096);
        GsWSMATRIX.m[2][i] = (int16_t)lround(f[i] * 4096);
    }
    for (i = 0; i < 3; i++) {
        GsWSMATRIX.t[i] = -(int32_t)((GsWSMATRIX.m[i][0] * (int64_t)pv->vpx + GsWSMATRIX.m[i][1] * (int64_t)pv->vpy +
                                       GsWSMATRIX.m[i][2] * (int64_t)pv->vpz) >> 12);
    }
    if (pv->super != NULL) {
        MATRIX lw, inv;
        int j;

        get_lw(pv->super, &lw);
        for (i = 0; i < 3; i++)
            for (j = 0; j < 3; j++)
                inv.m[i][j] = lw.m[j][i];
        apply_lv(inv.m, lw.t, inv.t);
        for (i = 0; i < 3; i++) {
            inv.t[i] = -inv.t[i];
        }
        mul_coord(&GsWSMATRIX, &inv);
        GsWSMATRIX = inv;
    }
    return 0;
}
