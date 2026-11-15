/* The layout of the structures and calls dlss.c uses, declared by hand (tools/sl_check.cpp compares them with the real headers) */
#ifndef DX_DLSS_API_H
#define DX_DLSS_API_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t a;
    uint16_t b, c;
    uint8_t d[8];
} SlGuid;
typedef struct {
    void *next;
    SlGuid type;
    size_t ver;
} SlBase;
typedef struct { float x, y, z, w; } SlF4;
typedef struct { SlF4 row[4]; } SlM4;
typedef struct { uint32_t top, left, w, h; } SlExtent;

typedef struct {
    SlBase b;
    uint8_t show_console;
    uint32_t log_level;
    const wchar_t **plugin_paths;
    uint32_t n_plugin_paths;
    const wchar_t *logs_path;
    void *allocate, *release;
    void (*log)(uint32_t type, const char *msg);
    uint64_t flags;
    const uint32_t *features;
    uint32_t n_features, app_id, engine;
    const char *engine_version, *project_id;
    uint32_t render_api;
} SlPrefs;
typedef struct {
    SlBase b;
    uint8_t *luid;
    uint32_t luid_size;
    void *vk;
} SlAdapter;
typedef struct {
    SlBase b;
    uint32_t value;
} SlViewport;
typedef struct {
    SlBase b;
    char type;
    void *native, *memory, *view;
    uint32_t state, w, h, format, mips, layers;
    uint64_t gpu_va;
    uint32_t flags, usage;
    uint16_t internal, reserved;
} SlRes;
typedef struct {
    SlBase b;
    SlRes *res;
    uint32_t type;
    int lifecycle;
    SlExtent extent;
} SlTag;
typedef struct {
    SlBase b;
    SlM4 view_to_clip, clip_to_view, clip_to_lens, clip_to_prev, prev_to_clip;
    float jitter[2], mvec_scale[2], pinhole[2], pos[3], up[3], right[3], fwd[3];
    float near_, far_, fov, aspect, mv_invalid;
    char depth_inverted, cam_motion, mv_3d, reset, ortho, mv_dilated, mv_jittered;
    float min_separation;
} SlConstants;
typedef struct {
    SlBase b;
    uint32_t mode, out_w, out_h;
    float sharpness, pre_exposure, exposure_scale;
    char hdr, invert_x, invert_y;
    uint32_t preset_dlaa, preset_quality, preset_balanced, preset_performance, preset_ultra_performance, preset_ultra_quality;
    char auto_exposure, alpha_upscaling;
} SlDlssOptions;
typedef struct {
    SlBase b;
    uint32_t render_w, render_h;
    float sharpness;
    uint32_t min_w, min_h, max_w, max_h;
} SlDlssOptimal;

enum {
    SL_MANUAL_HOOKING = 1 << 2, SL_NO_CL_TRACKING = 1 << 0, SL_NO_DEBUG_TEXT = 1 << 1, SL_FRAME_TAGGING = 1 << 7,
    SL_FEATURE_DLSS = 0, SL_TAG_DEPTH = 0, SL_TAG_MVEC = 1, SL_TAG_COLOR_IN = 3, SL_TAG_COLOR_OUT = 4,
    SL_MODE_PERFORMANCE = 1, SL_MODE_BALANCED = 2, SL_MODE_QUALITY = 3, SL_MODE_ULTRA_PERFORMANCE = 4, SL_MODE_DLAA = 6,
    SL_API_D3D12 = 1, SL_LIFE_NOW = 0,
    SL_ST_PIXEL = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, SL_ST_UAV = D3D12_RESOURCE_STATE_UNORDERED_ACCESS
};
#define SL_VERSION ((2ull << 48) | (14ull << 32) | (16ull << 16) | 0xfedc) /* the Streamline the structures are of (2.14.16) */

static const SlGuid SL_G_PREFS = {0x1ca10965, 0xbf8e, 0x432b, {0x8d, 0xa1, 0x67, 0x16, 0xd8, 0x79, 0xfb, 0x14}};
static const SlGuid SL_G_ADAPTER = {0x677315f, 0xa746, 0x4492, {0x9f, 0x42, 0xcb, 0x61, 0x42, 0xc9, 0xc3, 0xd4}};
static const SlGuid SL_G_VIEWPORT = {0x171b6435, 0x9b3c, 0x4fc8, {0x99, 0x94, 0xfb, 0xe5, 0x25, 0x69, 0xaa, 0xa4}};
static const SlGuid SL_G_RES = {0x3a9d70cf, 0x2418, 0x4b72, {0x83, 0x91, 0x13, 0xf8, 0x72, 0x1c, 0x72, 0x61}};
static const SlGuid SL_G_TAG = {0x4c6a5aad, 0xb445, 0x496c, {0x87, 0xff, 0x1a, 0xf3, 0x84, 0x5b, 0xe6, 0x53}};
static const SlGuid SL_G_CONSTANTS = {0xdcd35ad7, 0x4e4a, 0x4bad, {0xa9, 0xc, 0xe0, 0xc4, 0x9e, 0xb2, 0x3a, 0xfe}};
static const SlGuid SL_G_DLSS_OPTIONS = {0x6ac826e4, 0x4c61, 0x4101, {0xa9, 0x2d, 0x63, 0x8d, 0x42, 0x10, 0x57, 0xb8}};
static const SlGuid SL_G_DLSS_OPTIMAL = {0xef1d0957, 0xfd58, 0x4df7, {0xb5, 0x4, 0x8b, 0x69, 0xd8, 0xaa, 0x6b, 0x76}};

/* every call returns a Result, 0 when it went well */
typedef uint32_t (*SlInitFn)(const SlPrefs *, uint64_t);
typedef uint32_t (*SlVoidFn)(void);
typedef uint32_t (*SlSupportedFn)(uint32_t, const SlAdapter *);
typedef uint32_t (*SlPtrFn)(void *);
typedef uint32_t (*SlUpgradeFn)(void **);
typedef uint32_t (*SlTokenFn)(void **, const uint32_t *);
typedef uint32_t (*SlConstantsFn)(const SlConstants *, const void *, const SlViewport *);
typedef uint32_t (*SlTagFn)(const void *, const SlViewport *, const SlTag *, uint32_t, void *);
typedef uint32_t (*SlEvaluateFn)(uint32_t, const void *, const SlBase **, uint32_t, void *);
typedef uint32_t (*SlFeatureFnFn)(uint32_t, const char *, void **);
typedef uint32_t (*SlSetOptionsFn)(const SlViewport *, const SlDlssOptions *);
typedef uint32_t (*SlOptimalFn)(const SlDlssOptions *, SlDlssOptimal *);

#endif
