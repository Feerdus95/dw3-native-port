/* FSR 3.1's temporal upscaler (AMD FidelityFX, MIT), for the Direct3D 12 renderer: loaded at run time from the two DLLs the player puts next to the exe
   (amd_fidelityfx_loader_dx12.dll and amd_fidelityfx_upscaler_dx12.dll, from AMD's FidelityFX SDK), so the port carries none of
   AMD's binaries and works without them.
   The structures are laid out as AMD's ffx_api.h, ffx_upscale.h and ffx_api_dx12.h have them (only what is used is declared here;
   tools/fsr3_check.c, kept out of the build, compares them with the real headers). */
#include "dx.h"
#include "fsr3_api.h"

static FxCreateFn s_fx_create;
static FxDestroyFn s_fx_destroy;
static FxDispatchFn s_fx_dispatch;
static FxQueryFn s_fx_query;
static FxConfigureFn s_fx_configure;
static void *s_fx_ctx;
static int s_fx_rw, s_fx_rh, s_fx_uw, s_fx_uh, s_fx_tried;

static void fx_message(uint32_t type, const wchar_t *msg) {
    char s[256];
    int i;

    for (i = 0; i < 255 && msg != NULL && msg[i] != 0; i++) {
        s[i] = msg[i] < 127 ? (char)msg[i] : '?';
    }
    s[i] = 0;
    SDL_Log("fsr3: (%u) %s", type, s);
}

/* The DLLs next to the exe, once: 1 when the API is there */
int fx_load(void) {
    char path[600];
    SDL_SharedObject *so;

    if (!s_fx_tried) {
        s_fx_tried = 1;
        SDL_snprintf(path, sizeof path, "%samd_fidelityfx_loader_dx12.dll", SDL_GetBasePath());
        if ((so = SDL_LoadObject(path)) != NULL) {
            s_fx_create = (FxCreateFn)SDL_LoadFunction(so, "ffxCreateContext");
            s_fx_destroy = (FxDestroyFn)SDL_LoadFunction(so, "ffxDestroyContext");
            s_fx_dispatch = (FxDispatchFn)SDL_LoadFunction(so, "ffxDispatch");
            s_fx_query = (FxQueryFn)SDL_LoadFunction(so, "ffxQuery");
            s_fx_configure = (FxConfigureFn)SDL_LoadFunction(so, "ffxConfigure");
        }
        if (s_fx_create == NULL || s_fx_destroy == NULL || s_fx_dispatch == NULL || s_fx_query == NULL) {
            s_fx_create = NULL;
            SDL_Log("fsr3: the FidelityFX DLLs are not next to the exe, FSR 3.1 stays off");
        }
    }
    return s_fx_create != NULL;
}

static void fx_free(void) {
    if (s_fx_ctx != NULL) {
        s_fx_destroy(&s_fx_ctx, NULL);
        s_fx_ctx = NULL;
    }
    s_fx_rw = s_fx_rh = s_fx_uw = s_fx_uh = 0;
}

/* The context for a render size and an output size (made again when either changes). 1 when it is there */
static int fx_context(ID3D12Device *dev, int rw, int rh, int uw, int uh) {
    FxCreate c = {{FX_CREATE, NULL}, FX_DEPTH_INFINITE | FX_NON_LINEAR | FX_AUTO_EXPOSURE, (uint32_t)rw, (uint32_t)rh, (uint32_t)uw, (uint32_t)uh, (void *)fx_message};
    FxBackend b = {{FX_BACKEND_DX12, NULL}, dev};
    FxVersion v = {{FX_VERSION, NULL}, FX_VERSION_NUMBER};
    uint32_t r;

    if (s_fx_ctx != NULL && rw == s_fx_rw && rh == s_fx_rh && uw == s_fx_uw && uh == s_fx_uh) {
        return 1;
    }
    fx_free();
    c.h.next = &v.h, v.h.next = &b.h;
    if ((r = s_fx_create(&s_fx_ctx, &c.h, NULL)) != 0) {
        SDL_Log("fsr3: ffxCreateContext failed (%u), FSR 3.1 stays off", r);
        s_fx_ctx = NULL;
        s_fx_create = NULL; /* (not tried again) */
        return 0;
    }
    s_fx_rw = rw, s_fx_rh = rh, s_fx_uw = uw, s_fx_uh = uh;
    SDL_Log("fsr3: context %dx%d -> %dx%d", rw, rh, uw, uh);
    if (s_fx_configure != NULL) { /* it forgets its history sooner: less ghosting, a little more flicker (AMD's defaults: 0.333 and 1) */
        float acc = 0.2f, shade = 2.0f;
        FxKeyValue a = {{FX_KEYVALUE, NULL}, FX_KEY_ACCUMULATION, 0, &acc}, b = {{FX_KEYVALUE, NULL}, FX_KEY_SHADING, 0, &shade};

        SDL_Log("fsr3: configure %u %u (accumulation %.2f, shading change %.2f)", s_fx_configure(&s_fx_ctx, &a.h), s_fx_configure(&s_fx_ctx, &b.h), acc, shade);
    }
    return 1;
}

/* The jitter of step index of the sequence that suits the sizes (pixels of the render size, +x right, +y down) */
static void fx_jitter(unsigned index, int rw, int uw, float *x, float *y) {
    int32_t n = 8;
    FxPhases p = {{FX_PHASES, NULL}, (uint32_t)rw, (uint32_t)uw, &n};
    FxOffset o;

    *x = *y = 0;
    if (s_fx_query == NULL || s_fx_query(NULL, &p.h) != 0 || n < 1) {
        n = 8;
    }
    o = (FxOffset){{FX_OFFSET, NULL}, (int32_t)(index % (unsigned)n), n, x, y};
    if (s_fx_query == NULL || s_fx_query(NULL, &o.h) != 0) {
        *x = *y = 0;
    }
}

static FxRes fx_res(ID3D12Resource *r, int w, int h, uint32_t fmt, uint32_t state, uint32_t usage) {
    FxRes f = {r, {FX_TEX2D, fmt, (uint32_t)w, (uint32_t)h, 1, 1, 0, usage}, state};

    return f;
}

/* One frame: color, depth and motion (all the render size, in the state of a pixel shader's view) into out (the output size, a UAV).
   The command list's heaps and bindings are the caller's to set again after it. 1 when it ran */
int fx_run(ID3D12Device *dev, ID3D12GraphicsCommandList *cl, ID3D12Resource *color, ID3D12Resource *depth, ID3D12Resource *mv, ID3D12Resource *out,
                  int rw, int rh, int uw, int uh, float jx, float jy, int reset, float fov, float ms) {
    FxDispatch d;
    uint32_t r;

    if (!fx_context(dev, rw, rh, uw, uh)) {
        return 0;
    }
    SDL_memset(&d, 0, sizeof d);
    d.h.type = FX_DISPATCH, d.cl = cl;
    d.color = fx_res(color, rw, rh, FX_R8G8B8A8, FX_ST_PIXEL, 0);
    d.depth = fx_res(depth, rw, rh, FX_R32F, FX_ST_PIXEL, 0);
    d.mv = fx_res(mv, rw, rh, FX_R16G16F, FX_ST_PIXEL, 0);
    d.out = fx_res(out, uw, uh, FX_R8G8B8A8, FX_ST_UAV, FX_USAGE_UAV);
    d.jx = jx, d.jy = jy, d.msx = d.msy = 1; /* the motion is in pixels of the render size already */
    d.rw = (uint32_t)rw, d.rh = (uint32_t)rh, d.uw = (uint32_t)uw, d.uh = (uint32_t)uh;
    d.dt = ms, d.preexp = 1, d.reset = reset != 0;
    d.near_ = 64, d.far_ = 3.4e38f, d.fov = fov, d.meters = 1; /* the game's depth: 1 - 64 / z, no far plane */
    d.flags = FX_SRGB;
    if ((r = s_fx_dispatch(&s_fx_ctx, &d.h)) != 0) {
        SDL_Log("fsr3: ffxDispatch failed (%u), FSR 3.1 stays off", r);
        fx_free();
        s_fx_create = NULL;
        return 0;
    }
    return 1;
}

/* 1 once AMD's DLLs are loaded (fx_load) */
int fx_loaded(void) {
    return s_fx_create != NULL;
}

/* Frame INDEX's jitter in render pixels, for the context there is (one of 640 to 1280 before it is made) */
void fx_jitter_now(unsigned index, float *x, float *y) {
    fx_jitter(index, s_fx_rw > 0 ? s_fx_rw : 640, s_fx_uw > 0 ? s_fx_uw : 1280, x, y);
}
