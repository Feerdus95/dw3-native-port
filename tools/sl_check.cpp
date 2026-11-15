// Compares the structures of platform/d3d12/dlss_api.h with NVIDIA's own headers (Streamline, in third_party/streamline). Not part of the build:
//   clang++ --target=x86_64-w64-mingw32 -std=c++17 -Wno-invalid-offsetof -Ithird_party/SDL3-3.4.16/include -Ithird_party/streamline/include -fsyntax-only tools/sl_check.cpp
#define COBJMACROS
#include <stddef.h>
#include <SDL3/SDL.h>
#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <sl.h>
#include <sl_consts.h>
#include <sl_dlss.h>
#include "../platform/d3d12/dlss_api.h"

#define SAME(a, b) static_assert(sizeof(a) == sizeof(b), #a " size")
#define AT(a, am, b, bm) static_assert(offsetof(a, am) == offsetof(b, bm), #a "." #am)

SAME(SlBase, sl::BaseStructure);
SAME(SlGuid, sl::StructType);
SAME(SlExtent, sl::Extent);
SAME(SlM4, sl::float4x4);
SAME(SlPrefs, sl::Preferences);
AT(SlPrefs, show_console, sl::Preferences, showConsole);
AT(SlPrefs, log_level, sl::Preferences, logLevel);
AT(SlPrefs, plugin_paths, sl::Preferences, pathsToPlugins);
AT(SlPrefs, n_plugin_paths, sl::Preferences, numPathsToPlugins);
AT(SlPrefs, logs_path, sl::Preferences, pathToLogsAndData);
AT(SlPrefs, allocate, sl::Preferences, allocateCallback);
AT(SlPrefs, release, sl::Preferences, releaseCallback);
AT(SlPrefs, log, sl::Preferences, logMessageCallback);
AT(SlPrefs, flags, sl::Preferences, flags);
AT(SlPrefs, features, sl::Preferences, featuresToLoad);
AT(SlPrefs, n_features, sl::Preferences, numFeaturesToLoad);
AT(SlPrefs, app_id, sl::Preferences, applicationId);
AT(SlPrefs, engine, sl::Preferences, engine);
AT(SlPrefs, engine_version, sl::Preferences, engineVersion);
AT(SlPrefs, project_id, sl::Preferences, projectId);
AT(SlPrefs, render_api, sl::Preferences, renderAPI);
SAME(SlAdapter, sl::AdapterInfo);
AT(SlAdapter, luid, sl::AdapterInfo, deviceLUID);
AT(SlAdapter, luid_size, sl::AdapterInfo, deviceLUIDSizeInBytes);
SAME(SlViewport, sl::ViewportHandle);
SAME(SlRes, sl::Resource);
AT(SlRes, type, sl::Resource, type);
AT(SlRes, native, sl::Resource, native);
AT(SlRes, state, sl::Resource, state);
AT(SlRes, gpu_va, sl::Resource, gpuVirtualAddress);
AT(SlRes, internal, sl::Resource, internalFlags);
SAME(SlTag, sl::ResourceTag);
AT(SlTag, res, sl::ResourceTag, resource);
AT(SlTag, type, sl::ResourceTag, type);
AT(SlTag, lifecycle, sl::ResourceTag, lifecycle);
AT(SlTag, extent, sl::ResourceTag, extent);
SAME(SlConstants, sl::Constants);
AT(SlConstants, clip_to_prev, sl::Constants, clipToPrevClip);
AT(SlConstants, jitter, sl::Constants, jitterOffset);
AT(SlConstants, mvec_scale, sl::Constants, mvecScale);
AT(SlConstants, pos, sl::Constants, cameraPos);
AT(SlConstants, fwd, sl::Constants, cameraFwd);
AT(SlConstants, near_, sl::Constants, cameraNear);
AT(SlConstants, mv_invalid, sl::Constants, motionVectorsInvalidValue);
AT(SlConstants, depth_inverted, sl::Constants, depthInverted);
AT(SlConstants, mv_jittered, sl::Constants, motionVectorsJittered);
AT(SlConstants, min_separation, sl::Constants, minRelativeLinearDepthObjectSeparation);
SAME(SlDlssOptions, sl::DLSSOptions);
AT(SlDlssOptions, mode, sl::DLSSOptions, mode);
AT(SlDlssOptions, out_w, sl::DLSSOptions, outputWidth);
AT(SlDlssOptions, pre_exposure, sl::DLSSOptions, preExposure);
AT(SlDlssOptions, hdr, sl::DLSSOptions, colorBuffersHDR);
AT(SlDlssOptions, preset_dlaa, sl::DLSSOptions, dlaaPreset);
AT(SlDlssOptions, preset_ultra_quality, sl::DLSSOptions, ultraQualityPreset);
AT(SlDlssOptions, auto_exposure, sl::DLSSOptions, useAutoExposure);
AT(SlDlssOptions, alpha_upscaling, sl::DLSSOptions, alphaUpscalingEnabled);
SAME(SlDlssOptimal, sl::DLSSOptimalSettings);
AT(SlDlssOptimal, min_w, sl::DLSSOptimalSettings, renderWidthMin);
AT(SlDlssOptimal, max_h, sl::DLSSOptimalSettings, renderHeightMax);

static_assert(SL_VERSION == sl::kSDKVersion, "version");
static_assert(SL_MODE_DLAA == (int)sl::DLSSMode::eDLAA && SL_MODE_QUALITY == (int)sl::DLSSMode::eMaxQuality && SL_MODE_PERFORMANCE == (int)sl::DLSSMode::eMaxPerformance &&
              SL_MODE_BALANCED == (int)sl::DLSSMode::eBalanced && SL_MODE_ULTRA_PERFORMANCE == (int)sl::DLSSMode::eUltraPerformance, "modes");
static_assert(SL_API_D3D12 == (int)sl::RenderAPI::eD3D12 && SL_FEATURE_DLSS == sl::kFeatureDLSS, "ids");
static_assert(SL_TAG_DEPTH == sl::kBufferTypeDepth && SL_TAG_MVEC == sl::kBufferTypeMotionVectors && SL_TAG_COLOR_IN == sl::kBufferTypeScalingInputColor &&
              SL_TAG_COLOR_OUT == sl::kBufferTypeScalingOutputColor, "tags");
static_assert(SL_MANUAL_HOOKING == (int)sl::PreferenceFlags::eUseManualHooking && SL_NO_CL_TRACKING == (int)sl::PreferenceFlags::eDisableCLStateTracking &&
              SL_NO_DEBUG_TEXT == (int)sl::PreferenceFlags::eDisableDebugText && SL_FRAME_TAGGING == (int)sl::PreferenceFlags::eUseFrameBasedResourceTagging, "flags");
static_assert(SL_LIFE_NOW == (int)sl::eOnlyValidNow, "life");
int main() { return 0; }
