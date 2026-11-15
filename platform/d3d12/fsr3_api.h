/* The layout of the structures and calls fsr3.c uses, declared by hand (tools/fsr3_check.c compares them with the real headers) */
#ifndef DX_FSR3_API_H
#define DX_FSR3_API_H

#include <stdbool.h>
#include <stdint.h>

typedef struct FxHdr {
    uint64_t type;
    struct FxHdr *next;
} FxHdr;
typedef struct {
    uint32_t type, format, w, h, d, mips, flags, usage;
} FxDesc;
typedef struct {
    void *res;
    FxDesc desc;
    uint32_t state;
} FxRes;
typedef struct {
    FxHdr h;
    uint32_t flags, rw, rh, uw, uh;
    void *msg;
} FxCreate;
typedef struct {
    FxHdr h;
    ID3D12Device *dev;
} FxBackend;
typedef struct {
    FxHdr h;
    uint32_t version;
} FxVersion;
typedef struct {
    FxHdr h;
    void *cl;
    FxRes color, depth, mv, exposure, reactive, comp, out;
    float jx, jy, msx, msy;
    uint32_t rw, rh, uw, uh;
    bool sharp;
    float sharpness, dt, preexp;
    bool reset;
    float near_, far_, fov, meters;
    uint32_t flags;
} FxDispatch;
typedef struct {
    FxHdr h;
    uint32_t render_w, display_w;
    int32_t *out;
} FxPhases;
typedef struct {
    FxHdr h;
    int32_t index, phases;
    float *x, *y;
} FxOffset;

enum {
    FX_CREATE = 0x10000, FX_DISPATCH = 0x10001, FX_BACKEND_DX12 = 2, FX_VERSION = 0x1000b, FX_PHASES = 0x10004, FX_OFFSET = 0x10005, FX_KEYVALUE = 0x10007, FX_KEY_SHADING = 2, FX_KEY_ACCUMULATION = 3,
    FX_VERSION_NUMBER = (4 << 22) | (1 << 12) | 2,
    FX_DEPTH_INFINITE = 1 << 4, FX_NON_LINEAR = 1 << 8, FX_AUTO_EXPOSURE = 1 << 5,
    FX_SRGB = 1 << 1,
    FX_R8 = 25, FX_R8G8B8A8 = 10, FX_R16G16F = 18, FX_R32F = 28, FX_TEX2D = 2, FX_USAGE_UAV = 2,
    FX_ST_UAV = 1 << 1, FX_ST_PIXEL = 1 << 3
};

typedef uint32_t (*FxCreateFn)(void **ctx, FxHdr *desc, const void *mem);
typedef uint32_t (*FxDestroyFn)(void **ctx, const void *mem);
typedef uint32_t (*FxDispatchFn)(void **ctx, const FxHdr *desc);
typedef uint32_t (*FxQueryFn)(void **ctx, FxHdr *desc);
typedef uint32_t (*FxConfigureFn)(void **ctx, const FxHdr *desc);
typedef struct {
    FxHdr h;
    uint64_t key, u64;
    void *ptr;
} FxKeyValue;

#endif
