/* Native port: the GTE macros of dw3_decomp/include/gte.h as calls into the
   platform layer (native/platform/gte.c), in place of inline MIPS asm. */
#ifndef GTE_H
#define GTE_H

void psx_gte_lddp();
void psx_gte_ldsv();
void psx_gte_gpf12();
void psx_gte_stsv();
void psx_gte_SetRotMatrix();
void psx_gte_ldv0();
void psx_gte_ldv0_unaligned();
void psx_gte_rtps();
void psx_gte_rtv0();
void psx_gte_stsxy();
void psx_gte_stflg();
void psx_gte_stlvnl();
void psx_gte_stszotz();
void psx_gte_SetTransMatrix();
void psx_gte_ldlv0();
void psx_gte_ldclmv();
void psx_gte_rt();
void psx_gte_rtir();
void psx_gte_stclmv();
void psx_gte_SetLightMatrix();
void psx_gte_ncs();
void psx_gte_strgb();
void psx_gte_ldsxy3();
void psx_gte_nclip();
void psx_gte_stopz();
void psx_gte_ldsz3();
void psx_gte_ldsz4();
void psx_gte_avsz3();
void psx_gte_avsz4();
void psx_gte_stotz();

#define gte_lddp(r0) psx_gte_lddp(r0)
#define gte_ldsv(r0) psx_gte_ldsv(r0)
#define gte_gpf12() psx_gte_gpf12()
#define gte_stsv(r0) psx_gte_stsv(r0)
#define gte_SetRotMatrix(r0) psx_gte_SetRotMatrix(r0)
#define gte_ldv0(r0) psx_gte_ldv0(r0)
#define gte_ldv0_unaligned(r0) psx_gte_ldv0_unaligned(r0)
#define gte_rtps() psx_gte_rtps()
#define gte_rtv0() psx_gte_rtv0()
#define gte_stsxy(r0) psx_gte_stsxy(r0)
#define gte_stflg(r0) psx_gte_stflg(r0)
#define gte_stlvnl(r0) psx_gte_stlvnl(r0)
#define gte_stszotz(r0) psx_gte_stszotz(r0)
#define gte_SetTransMatrix(r0) psx_gte_SetTransMatrix(r0)
#define gte_ldlv0(r0) psx_gte_ldlv0(r0)
#define gte_ldclmv(r0) psx_gte_ldclmv(r0)
#define gte_rt() psx_gte_rt()
#define gte_rtir() psx_gte_rtir()
#define gte_stclmv(r0) psx_gte_stclmv(r0)
#define gte_SetLightMatrix(r0) psx_gte_SetLightMatrix(r0)
#define gte_ncs() psx_gte_ncs()
#define gte_strgb(r0) psx_gte_strgb(r0)
#define gte_ldsxy3(r0, r1, r2) psx_gte_ldsxy3(r0, r1, r2)
#define gte_nclip() psx_gte_nclip()
#define gte_stopz(r0) psx_gte_stopz(r0)
#define gte_ldsz3(r0, r1, r2) psx_gte_ldsz3(r0, r1, r2)
#define gte_ldsz4(r0, r1, r2, r3) psx_gte_ldsz4(r0, r1, r2, r3)
#define gte_avsz3() psx_gte_avsz3()
#define gte_avsz4() psx_gte_avsz4()
#define gte_stotz(r0) psx_gte_stotz(r0)

/* the SDK's larger macros (gte_CompMatrix, gte_AverageZ3...) built from these */
#include <gtemac.h>

#endif /* GTE_H */
