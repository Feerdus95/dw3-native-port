/* Compares the structures of platform/d3d12/fsr3_api.h with AMD's own headers (the FidelityFX SDK, in third_party/fidelityfx-sdk). Not part of the build:
   clang --target=x86_64-w64-mingw32 -Ithird_party/SDL3-3.4.16/include -Ithird_party/fidelityfx-sdk/src/Kits/FidelityFX/api/include
         -Ithird_party/fidelityfx-sdk/src/Kits/FidelityFX/upscalers/include -fsyntax-only tools/fsr3_check.c */
#define COBJMACROS
#include <stddef.h>
#include <SDL3/SDL.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_5.h>
#include "ffx_api.h"
#include "ffx_upscale.h"
#include "dx12/ffx_api_dx12.h"
#include "../platform/d3d12/fsr3_api.h"

#define SAME(a, b) _Static_assert(sizeof(a) == sizeof(b), #a " size")
#define AT(a, am, b, bm) _Static_assert(offsetof(a, am) == offsetof(b, bm), #am)

SAME(FxHdr, ffxApiHeader);
SAME(FxRes, struct FfxApiResource);
SAME(FxCreate, struct ffxCreateContextDescUpscale);
SAME(FxBackend, struct ffxCreateBackendDX12Desc);
SAME(FxVersion, struct ffxCreateContextDescUpscaleVersion);
SAME(FxDispatch, struct ffxDispatchDescUpscale);
SAME(FxPhases, struct ffxQueryDescUpscaleGetJitterPhaseCount);
SAME(FxOffset, struct ffxQueryDescUpscaleGetJitterOffset);
AT(FxRes, desc.format, struct FfxApiResource, description.format);
AT(FxRes, desc.usage, struct FfxApiResource, description.usage);
AT(FxRes, state, struct FfxApiResource, state);
AT(FxCreate, rw, struct ffxCreateContextDescUpscale, maxRenderSize);
AT(FxCreate, uw, struct ffxCreateContextDescUpscale, maxUpscaleSize);
AT(FxCreate, msg, struct ffxCreateContextDescUpscale, fpMessage);
AT(FxDispatch, cl, struct ffxDispatchDescUpscale, commandList);
AT(FxDispatch, color, struct ffxDispatchDescUpscale, color);
AT(FxDispatch, mv, struct ffxDispatchDescUpscale, motionVectors);
AT(FxDispatch, out, struct ffxDispatchDescUpscale, output);
AT(FxDispatch, jx, struct ffxDispatchDescUpscale, jitterOffset);
AT(FxDispatch, msx, struct ffxDispatchDescUpscale, motionVectorScale);
AT(FxDispatch, rw, struct ffxDispatchDescUpscale, renderSize);
AT(FxDispatch, uw, struct ffxDispatchDescUpscale, upscaleSize);
AT(FxDispatch, dt, struct ffxDispatchDescUpscale, frameTimeDelta);
AT(FxDispatch, reset, struct ffxDispatchDescUpscale, reset);
AT(FxDispatch, near_, struct ffxDispatchDescUpscale, cameraNear);
AT(FxDispatch, flags, struct ffxDispatchDescUpscale, flags);
AT(FxPhases, out, struct ffxQueryDescUpscaleGetJitterPhaseCount, pOutPhaseCount);
AT(FxOffset, x, struct ffxQueryDescUpscaleGetJitterOffset, pOutX);

_Static_assert(FX_CREATE == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE, "create");
_Static_assert(FX_DISPATCH == FFX_API_DISPATCH_DESC_TYPE_UPSCALE, "dispatch");
_Static_assert(FX_BACKEND_DX12 == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12, "backend");
_Static_assert(FX_VERSION == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION, "version");
_Static_assert(FX_VERSION_NUMBER == FFX_UPSCALER_VERSION, "version number");
_Static_assert(FX_PHASES == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT, "phases");
_Static_assert(FX_OFFSET == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET, "offset");
_Static_assert(FX_DEPTH_INFINITE == FFX_UPSCALE_ENABLE_DEPTH_INFINITE, "infinite");
_Static_assert(FX_NON_LINEAR == FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE, "nonlinear");
_Static_assert(FX_AUTO_EXPOSURE == FFX_UPSCALE_ENABLE_AUTO_EXPOSURE, "exposure");
_Static_assert(FX_SRGB == FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB, "srgb");
_Static_assert(FX_R8G8B8A8 == FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM, "rgba8");
_Static_assert(FX_R16G16F == FFX_API_SURFACE_FORMAT_R16G16_FLOAT, "rg16");
_Static_assert(FX_R32F == FFX_API_SURFACE_FORMAT_R32_FLOAT, "r32");
_Static_assert(FX_TEX2D == FFX_API_RESOURCE_TYPE_TEXTURE2D, "tex2d");
_Static_assert(FX_USAGE_UAV == FFX_API_RESOURCE_USAGE_UAV, "uav");
_Static_assert(FX_ST_UAV == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS, "st uav");
_Static_assert(FX_ST_PIXEL == FFX_API_RESOURCE_STATE_PIXEL_READ, "st pixel");
_Static_assert(FX_R8 == FFX_API_SURFACE_FORMAT_R8_UNORM, "r8");
AT(FxDispatch, reactive, struct ffxDispatchDescUpscale, reactive);
