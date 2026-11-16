/* DLSS through NVIDIA Streamline (the headers MIT, the DLLs NVIDIA's), for the Direct3D 12 renderer: sl.interposer.dll is loaded at run time from beside
   the exe (the launcher downloads it and its three companions from NVIDIA's Streamline release), so the port carries none of NVIDIA's
   binaries and works without them. Streamline is C++: the few structures used are laid out here as sl_core_types.h, sl_consts.h and
   sl_dlss.h have them (a 32 byte base, then the members), and tools/sl_check.cpp, kept out of the build, compares them with the real headers.
   "Manual hooking": the device, the factory and with it the swap chain are the proxies Streamline gives back, nothing else is hooked. */
#include "dx.h"
#include "dlss_api.h"

static SlInitFn s_sl_init;
static SlVoidFn s_sl_shutdown;
static SlSupportedFn s_sl_supported;
static SlPtrFn s_sl_set_device;
static SlUpgradeFn s_sl_upgrade;
static SlTokenFn s_sl_token;
static SlConstantsFn s_sl_constants;
static SlTagFn s_sl_tag;
static SlEvaluateFn s_sl_evaluate;
static SlSetOptionsFn s_sl_options;
static SlOptimalFn s_sl_optimal;
static SlFeatureFnFn s_sl_feature;
static int s_sl_state; /* 0 not tried, 1 initialised, -1 not there or refused */
static int s_sl_rw, s_sl_rh, s_sl_uw, s_sl_uh, s_sl_mode = -1;

static SlBase sl_base(const SlGuid *g, size_t ver) {
    SlBase b = {NULL, *g, ver};

    return b;
}

static void sl_log(uint32_t type, const char *msg) {
    if (type > 0) { /* (its chatter, 200 lines at start-up, is not worth the log: warnings and errors are) */
        SDL_Log("dlss: (%u) %s", type, msg);
    }
}

/* The four DLLs beside the exe (what the launcher puts there): the menu offers DLSS when they are */
int sl_files_here(void) {
    static const char *const NAMES[] = {"sl.interposer.dll", "sl.common.dll", "sl.dlss.dll", "nvngx_dlss.dll"};
    char path[600];
    int i;

    for (i = 0; i < 4; i++) {
        SDL_snprintf(path, sizeof path, "%s%s", SDL_GetBasePath(), NAMES[i]);
        if (!SDL_GetPathInfo(path, NULL)) {
            return 0;
        }
    }
    return 1;
}

/* Streamline up, before the device: 1 when it is. Without the DLLs, or on a GPU that is not NVIDIA's, it stays off and the game goes on */
int sl_start(void) {
    static const uint32_t FEATURES[1] = {SL_FEATURE_DLSS};
    wchar_t dir[600];
    const wchar_t *paths[1] = {dir};
    char path[600];
    SDL_SharedObject *so;
    SlPrefs p = {0};
    uint32_t r;

    if (s_sl_state != 0) {
        return s_sl_state > 0;
    }
    s_sl_state = -1;
    if (!sl_files_here()) {
        SDL_Log("dlss: the Streamline DLLs are not next to the exe, DLSS stays off");
        return 0;
    }
    SDL_snprintf(path, sizeof path, "%ssl.interposer.dll", SDL_GetBasePath());
    MultiByteToWideChar(CP_UTF8, 0, SDL_GetBasePath(), -1, dir, 600);
    if ((so = SDL_LoadObject(path)) == NULL) {
        SDL_Log("dlss: sl.interposer.dll did not load: %s", SDL_GetError());
        return 0;
    }
    s_sl_init = (SlInitFn)SDL_LoadFunction(so, "slInit");
    s_sl_shutdown = (SlVoidFn)SDL_LoadFunction(so, "slShutdown");
    s_sl_supported = (SlSupportedFn)SDL_LoadFunction(so, "slIsFeatureSupported");
    s_sl_set_device = (SlPtrFn)SDL_LoadFunction(so, "slSetD3DDevice");
    s_sl_upgrade = (SlUpgradeFn)SDL_LoadFunction(so, "slUpgradeInterface");
    s_sl_token = (SlTokenFn)SDL_LoadFunction(so, "slGetNewFrameToken");
    s_sl_constants = (SlConstantsFn)SDL_LoadFunction(so, "slSetConstants");
    s_sl_tag = (SlTagFn)SDL_LoadFunction(so, "slSetTagForFrame");
    s_sl_evaluate = (SlEvaluateFn)SDL_LoadFunction(so, "slEvaluateFeature");
    s_sl_feature = (SlFeatureFnFn)SDL_LoadFunction(so, "slGetFeatureFunction");
    if (s_sl_init == NULL || s_sl_shutdown == NULL || s_sl_supported == NULL || s_sl_set_device == NULL || s_sl_upgrade == NULL || s_sl_token == NULL ||
        s_sl_constants == NULL || s_sl_tag == NULL || s_sl_evaluate == NULL || s_sl_feature == NULL) {
        SDL_Log("dlss: sl.interposer.dll lacks a function, DLSS stays off");
        return 0;
    }
    p.b = sl_base(&SL_G_PREFS, 1);
    p.log_level = 1;
    p.plugin_paths = paths, p.n_plugin_paths = 1;
    p.log = sl_log;
    p.flags = SL_MANUAL_HOOKING | SL_NO_CL_TRACKING | SL_NO_DEBUG_TEXT | SL_FRAME_TAGGING; /* (no over-the-air updates: nothing is downloaded) */
    p.features = FEATURES, p.n_features = 1;
    p.engine_version = "0.20", p.project_id = "ccd5aa70-1dff-4845-acc3-18052f18d018"; /* (no id from NVIDIA: a custom engine names its project; without it NGX won't start) */
    p.render_api = SL_API_D3D12;
    if ((r = s_sl_init(&p, SL_VERSION)) != 0) {
        SDL_Log("dlss: slInit failed (%u), DLSS stays off", r);
        return 0;
    }
    s_sl_state = 1;
    return 1;
}

/* The device made (the native one): the adapter must be one DLSS runs on; then Streamline is told of it and the device becomes its proxy.
   0 and DLSS off when not, with *dev as it was */
int sl_device(ID3D12Device **dev) {
    LUID luid = ID3D12Device_GetAdapterLuid(*dev);
    SlAdapter a = {0};
    void *px = *dev;
    uint32_t r;

    a.b = sl_base(&SL_G_ADAPTER, 1);
    a.luid = (uint8_t *)&luid, a.luid_size = sizeof luid;
    if ((r = s_sl_supported(SL_FEATURE_DLSS, &a)) != 0) {
        SDL_Log("dlss: this GPU can't run it (%u: 2 driver, 3 Windows, 6 and 7 not an NVIDIA RTX), DLSS stays off", r);
        s_sl_state = -1;
        return 0;
    }
    if ((r = s_sl_set_device(*dev)) != 0 || (r = s_sl_feature(SL_FEATURE_DLSS, "slDLSSSetOptions", (void **)&s_sl_options)) != 0 ||
        (r = s_sl_feature(SL_FEATURE_DLSS, "slDLSSGetOptimalSettings", (void **)&s_sl_optimal)) != 0 || (r = s_sl_upgrade(&px)) != 0) {
        SDL_Log("dlss: Streamline refused the device (%u), DLSS stays off", r);
        s_sl_shutdown(); /* (before the swap chain is made: nothing of its is left behind) */
        s_sl_state = -1;
        return 0;
    }
    *dev = px;
    return 1;
}

/* The factory made: its proxy makes the swap chain Streamline needs to see present */
int sl_factory(IDXGIFactory4 **fac) {
    void *px = *fac;
    uint32_t r = s_sl_upgrade(&px);

    if (r != 0) {
        SDL_Log("dlss: the factory didn't upgrade (%u)", r);
        return 0;
    }
    *fac = px;
    return 1;
}

/* The DLSS mode whose ratio is the nearest to the render's (RH to UH): the player's Upscaler mode fixed the render size, and DLSS refuses a size
   that is not within a mode's range (the integer scales only come near the nominal ratios) */
static uint32_t sl_mode(int rh, int uh) {
    static const struct { uint32_t mode; float ratio; } M[] = {{SL_MODE_DLAA, 1.0f}, {SL_MODE_QUALITY, 1.5f}, {SL_MODE_BALANCED, 1.724f}, {SL_MODE_PERFORMANCE, 2.0f}, {SL_MODE_ULTRA_PERFORMANCE, 3.0f}};
    float k = (float)uh / (float)SDL_max(rh, 1), d = 1e9f;
    uint32_t best = SL_MODE_DLAA;

    for (int i = 0; i < 5; i++) {
        if (SDL_fabsf(k - M[i].ratio) < d) {
            d = SDL_fabsf(k - M[i].ratio), best = M[i].mode;
        }
    }
    return best;
}

static SlRes sl_res(ID3D12Resource *r, uint32_t state) {
    SlRes x = {0};

    x.b = sl_base(&SL_G_RES, 1);
    x.native = r, x.state = state; /* (size and format are the resource's own on D3D12) */
    return x;
}

/* One frame: color, depth and motion (all the render size, in the state of a pixel shader's view) into out (the output size, a UAV). The command
   list's heaps and bindings are the caller's to set again after it. 1 when it ran */
int sl_run(ID3D12GraphicsCommandList *cl, ID3D12Resource *color, ID3D12Resource *depth, ID3D12Resource *mv, ID3D12Resource *out, int rw, int rh, int uw, int uh,
                  float jx, float jy, int reset, float fov) {
    SlViewport vp = {sl_base(&SL_G_VIEWPORT, 1), 0};
    SlDlssOptions o = {0};
    SlConstants c = {0};
    SlRes rc = sl_res(color, SL_ST_PIXEL), rd = sl_res(depth, SL_ST_PIXEL), rm = sl_res(mv, SL_ST_PIXEL), ro = sl_res(out, SL_ST_UAV);
    SlTag tags[4] = {{sl_base(&SL_G_TAG, 1), &rc, SL_TAG_COLOR_IN, SL_LIFE_NOW, {0}}, {sl_base(&SL_G_TAG, 1), &ro, SL_TAG_COLOR_OUT, SL_LIFE_NOW, {0}},
                     {sl_base(&SL_G_TAG, 1), &rd, SL_TAG_DEPTH, SL_LIFE_NOW, {0}}, {sl_base(&SL_G_TAG, 1), &rm, SL_TAG_MVEC, SL_LIFE_NOW, {0}}};
    const SlBase *inputs[1] = {&vp.b};
    void *frame = NULL;
    uint32_t r;
    int i;

    if (s_sl_state <= 0) {
        return 0;
    }
    if (rw != s_sl_rw || rh != s_sl_rh || uw != s_sl_uw || uh != s_sl_uh || g_up_mode != s_sl_mode) {
        SlDlssOptimal best = {0};

        o.b = sl_base(&SL_G_DLSS_OPTIONS, 3);
        o.mode = sl_mode(rh, uh), o.out_w = (uint32_t)uw, o.out_h = (uint32_t)uh;
        o.pre_exposure = o.exposure_scale = 1;
        o.auto_exposure = 1; /* the picture has no exposure of its own; it is 8 bits, not HDR */
        best.b = sl_base(&SL_G_DLSS_OPTIMAL, 1);
        if ((r = s_sl_optimal(&o, &best)) == 0) {
            SDL_Log("dlss: mode %u, %dx%d -> %dx%d (it would render %ux%d, from %ux%d to %ux%d)", o.mode, rw, rh, uw, uh, best.render_w, best.render_h, best.min_w, best.min_h, best.max_w, best.max_h);
        }
        if ((r = s_sl_options(&vp, &o)) != 0) {
            SDL_Log("dlss: slDLSSSetOptions failed (%u), DLSS stays off", r);
            s_sl_state = -1;
            return 0;
        }
        s_sl_rw = rw, s_sl_rh = rh, s_sl_uw = uw, s_sl_uh = uh, s_sl_mode = g_up_mode;
    }
    c.b = sl_base(&SL_G_CONSTANTS, 2);
    for (i = 0; i < 4; i++) { /* the game has no camera matrices to give: identity, which the DLSS plugin doesn't use */
        c.view_to_clip.row[i] = c.clip_to_view.row[i] = c.clip_to_lens.row[i] = c.clip_to_prev.row[i] = c.prev_to_clip.row[i] = (SlF4){0};
        (&c.view_to_clip.row[i].x)[i] = (&c.clip_to_view.row[i].x)[i] = (&c.clip_to_lens.row[i].x)[i] = (&c.clip_to_prev.row[i].x)[i] = (&c.prev_to_clip.row[i].x)[i] = 1;
    }
    c.jitter[0] = jx, c.jitter[1] = jy;
    c.mvec_scale[0] = 1.0f / (float)rw, c.mvec_scale[1] = 1.0f / (float)rh; /* the motion is in pixels of the render size */
    c.up[1] = 1, c.right[0] = 1, c.fwd[2] = 1;
    c.near_ = 64, c.far_ = 65536, c.fov = fov, c.aspect = (float)rw / (float)rh; /* the game's depth: 1 - 64 / z, near 64 */
    c.mv_invalid = 3.40282347e38f;
    c.depth_inverted = 0, c.cam_motion = 1, c.mv_3d = 0, c.reset = reset != 0, c.ortho = 0, c.mv_dilated = 0, c.mv_jittered = 0;
    c.min_separation = 40.0f;
    if ((r = s_sl_token(&frame, NULL)) != 0 || (r = s_sl_constants(&c, frame, &vp)) != 0 || (r = s_sl_tag(frame, &vp, tags, 4, cl)) != 0 ||
        (r = s_sl_evaluate(SL_FEATURE_DLSS, frame, inputs, 1, cl)) != 0) {
        SDL_Log("dlss: a frame failed (%u), DLSS stays off", r);
        s_sl_state = -1;
        return 0;
    }
    return 1;
}

/* How long the jitter's sequence is: 8 steps at the same size, more as the picture grows over the render (8 x ratio squared, as NVIDIA advises) */
unsigned sl_phases(void) {
    float k = s_sl_rw > 0 ? (float)s_sl_uw / (float)s_sl_rw : 1.0f;

    return (unsigned)SDL_clamp(8.0f * k * k + 0.5f, 8.0f, 64.0f);
}

/* Streamline shut down, before the proxies it gave are released (dx_release: the device's start failed) */
void sl_stop(void) {
    if (s_sl_state > 0) {
        s_sl_shutdown();
        s_sl_state = -1;
    }
}

/* 0 not tried, 1 initialised, -1 not there or refused */
int sl_state(void) {
    return s_sl_state;
}
