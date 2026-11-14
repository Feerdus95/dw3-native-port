/* The renderer on Direct3D 12 (x64, Windows), written by hand: the other backend behind renderer.h, chosen at build time
   (build.py --d3d12; gl.c is left out). As gl.c's: gpu_draw.c draws every primitive into the CPU's VRAM at 1x; the card
   draws the same ones finer (render_scale) into its own copy of VRAM, the picture shown on screen. The VRAM goes up as a
   R16_UINT texture, the CLUTs looked up in the pixel shader.
   Done: the copy at render_scale, the batches (semi-transparency, depth buffer), refreshes and VRAM-to-VRAM copies, the
   menu, screenshots. Not yet (gl.c has them): the battle shadows' mask, fog, props, post-process, SMAA, FSR. */
#define RECT PSX_RECT /* windows.h has its own */
#include "platform.h"
#undef RECT
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS /* mingw's d3d12.h: the getters that return a struct */
#include <SDL3/SDL.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#define FRAMES 2
#define MENU_W 1280
#define MENU_H 960
#define BATCH 6144
#define RING (48u << 20) /* the frame's uploads: VRAM rects and vertices */
#define MAX_SCALE 8

int g_res_scale = 1;
int g_depth = 1;
int g_true_color = 1, g_perspective = 1, g_smooth_3d = 1;
int g_sharpen, g_brightness, g_gamma, g_saturation;
int g_scanlines, g_integer_scale;
int g_smaa, g_fsr;
int g_soft_shadows;
int g_vsync = 1;
int g_battle_shadows = 1;
int g_battle_sky;
uint16_t g_vram_dirty[8];

/* --- shaders (the port of gl.c's MAIN_VS / MAIN_FS; see there for what each mode bit means) --- */

static const char *MAIN_HLSL =
    "Texture2D<uint> vram : register(t0);\n"
    "cbuffer K : register(b0) {\n"
    "    float2 size; float2 jit; float4 fog; float2 fogz; float scale; int persp; int pas; int truecolor; int smooth3d; int pad;\n"
    "};\n"
    "struct VI { float4 pos_uv : POSITION; uint4 colr : COLOR; uint4 tex : TEXCOORD0; uint4 limits : TEXCOORD1;\n"
    "            uint4 window : TEXCOORD2; uint mode : TEXCOORD3; float z : TEXCOORD4; float w : TEXCOORD5; };\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; float3 col : TEXCOORD1; float vw : TEXCOORD2;\n"
    "            nointerpolation uint4 t : TEXCOORD3; nointerpolation uint4 lim : TEXCOORD4; nointerpolation uint4 tw : TEXCOORD5;\n"
    "            nointerpolation uint md : TEXCOORD6; nointerpolation uint deep : TEXCOORD7; };\n"
    "VO vs(VI i) {\n"
    "    VO o;\n"
    "    float cz = i.z > 0.0 ? clamp(1.0 - 64.0 / i.z, 0.0, 1.0) : 0.0;\n"
    "    o.p = float4(i.pos_uv.x / size.x * 2.0 - 1.0, 1.0 - i.pos_uv.y / size.y * 2.0, cz, 1.0);\n"
    "    o.p *= (persp != 0 && i.w > 0.0) ? i.w : 1.0;\n"
    "    if (i.w > 0.0) o.p.xy += jit * o.p.w;\n"
    "    o.uv = i.pos_uv.zw; o.col = float3(i.colr.xyz); o.t = i.tex; o.lim = i.limits; o.tw = i.window; o.md = i.mode;\n"
    "    o.deep = i.w > 0.0 ? 1u : 0u; o.vw = i.w;\n"
    "    return o;\n"
    "}\n"
    "static const float DITHER[16] = {-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0};\n"
    "static uint4 t, lim, tw;\n"
    "static uint md;\n"
    "uint fetch(int x, int y) { return vram.Load(int3(x & 1023, y & 511, 0)); }\n"
    "float3 rgb5(uint p) { return float3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u); }\n"
    "float nz(uint v) { return v != 0u ? 1.0 : 0.0; }\n"
    "uint texel(int u, int v) {\n"
    "    u = clamp(u, (int)lim.x, (int)lim.z) & 255;\n"
    "    v = clamp(v, (int)lim.y, (int)lim.w) & 255;\n"
    "    u = (u & ~(int)tw.x) | ((int)tw.z & (int)tw.x);\n"
    "    v = (v & ~(int)tw.y) | ((int)tw.w & (int)tw.y);\n"
    "    uint depth = (md >> 4) & 3u;\n"
    "    if (depth == 0u) {\n"
    "        uint w = fetch((int)t.z + u / 4, (int)t.w + v);\n"
    "        return fetch((int)t.x + (int)((w >> (uint)((u & 3) * 4)) & 15u), (int)t.y);\n"
    "    } else if (depth == 1u) {\n"
    "        uint w = fetch((int)t.z + u / 2, (int)t.w + v);\n"
    "        return fetch((int)t.x + (int)((w >> (uint)((u & 1) * 8)) & 255u), (int)t.y);\n"
    "    }\n"
    "    return fetch((int)t.z + u, (int)t.w + v);\n"
    "}\n"
    "float4 ps(VO i, float4 fc : SV_Position) : SV_Target {\n"
    "    t = i.t; lim = i.lim; tw = i.tw; md = i.md;\n"
    "    float2 uv = i.uv; float3 col = i.col; uint deep = i.deep; float vw = i.vw;\n"
    "    uint pix = 0u;\n"
    "    float3 tex = float3(0.0, 0.0, 0.0);\n"
    "    bool semi = (md & 8u) != 0u;\n"
    "    float3 c8 = col, c5;\n"
    "    float cover = 1.0;\n"
    "    if ((md & 1024u) != 0u) {\n"
    "        if (pas == 1) discard;\n"
    "        return float4(1.0, 1.0, 1.0, 1.0);\n"
    "    }\n"
    "    if ((md & 128u) != 0u) {\n"
    "        pix = fetch((int)(fc.x / scale), (int)(fc.y / scale));\n"
    "        return float4(rgb5(pix) / 31.0, (float)(pix >> 15));\n"
    "    }\n"
    "    if ((md & 2u) != 0u) {\n"
    "        float2 at = uv - 0.5 * ddx(uv) - 0.5 * ddy(uv);\n"
    "        if ((md & 512u) != 0u) {\n"
    "            float2 sz = float2(lim.zw - lim.xy) + 1.0, mid = float2(lim.xy) + sz * float2(0.484, 0.469);\n"
    "            cover = 1.0 - smoothstep(0.6, 1.0, length((uv - mid) / (sz * float2(0.42, 0.4))));\n"
    "            if (cover <= 0.0) discard;\n"
    "            at = mid;\n"
    "        }\n"
    "        pix = texel((int)floor(at.x + 0.002), (int)floor(at.y + 0.002));\n"
    "        tex = rgb5(pix);\n"
    "        if (smooth3d != 0 && deep != 0u && (md & 512u) == 0u) {\n"
    "            float2 p = uv - 0.5;\n"
    "            int2 ii = (int2)floor(p);\n"
    "            float2 f = p - float2(ii);\n"
    "            uint a = texel(ii.x, ii.y), b = texel(ii.x + 1, ii.y), c = texel(ii.x, ii.y + 1), e = texel(ii.x + 1, ii.y + 1);\n"
    "            float4 k = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y) *\n"
    "                       float4(nz(a), nz(b), nz(c), nz(e));\n"
    "            float sum = k.x + k.y + k.z + k.w;\n"
    "            if (sum < 0.5) discard;\n"
    "            tex = (rgb5(a) * k.x + rgb5(b) * k.y + rgb5(c) * k.z + rgb5(e) * k.w) / sum;\n"
    "            if (pix == 0u) pix = a != 0u ? a : b != 0u ? b : c != 0u ? c : e;\n"
    "        }\n"
    "        if (pix == 0u) discard;\n"
    "        semi = semi && (pix & 0x8000u) != 0u;\n"
    "        c8 = floor(tex * col / 16.0);\n"
    "    }\n"
    "    if ((pas == 1 && semi) || (pas == 2 && !semi)) discard;\n"
    "    if ((md & 6u) == 6u) {\n"
    "        c5 = tex;\n"
    "    } else {\n"
    "        float d = (md & 64u) != 0u ? DITHER[((int)fc.y & 3) * 4 + ((int)fc.x & 3)] : 0.0;\n"
    "        c5 = truecolor != 0 ? clamp(c8 / 8.0, 0.0, 31.0) : clamp(floor((c8 + d) / 8.0), 0.0, 31.0);\n"
    "    }\n"
    "    if ((md & 512u) != 0u) c5 = float3(18.6 * cover, 18.6 * cover, 18.6 * cover);\n"
    "    else if (fog.a > 0.0 && deep != 0u && !semi && (md & 2048u) == 0u) c5 = lerp(c5, fog.rgb, fog.a * smoothstep(fogz.x, fogz.y, vw));\n"
    "    return float4(c5 / 31.0, ((md & 256u) != 0u || (pix & 0x8000u) != 0u) ? 1.0 : 0.0);\n"
    "}\n";

/* A texture on a rectangle: where it goes is the viewport, which part of the texture the constants */
static const char *BLIT_HLSL =
    "cbuffer C : register(b0) { float4 uv; };\n"
    "Texture2D tx : register(t0);\n"
    "SamplerState lin : register(s0);\n"
    "SamplerState pt : register(s1);\n"
    "struct VO { float4 p : SV_Position; float2 t : TEXCOORD0; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    o.t = uv.xy + c * uv.zw;\n"
    "    return o;\n"
    "}\n"
    "float4 ps(VO i) : SV_Target { return tx.Sample(lin, i.t); }\n"
    "float4 psn(VO i) : SV_Target { return tx.Sample(pt, i.t); }\n";

/* --- the device --- */

typedef struct {
    ID3D12Resource *r;
    D3D12_RESOURCE_STATES st;
} Tex;

enum { S_PIC, S_MENU, S_VRAM, S_RT, S_TMP, S_COUNT }; /* the SRV heap's slots */

static SDL_Window *s_window;
static ID3D12Device *s_dev;
static ID3D12CommandQueue *s_queue;
static IDXGISwapChain3 *s_swap;
static ID3D12DescriptorHeap *s_rtv_heap, *s_srv_heap, *s_dsv_heap;
static ID3D12Resource *s_back[FRAMES], *s_up_pic, *s_up_menu, *s_read, *s_ring_res;
static Tex s_pic, s_menu, s_vram, s_rt, s_tmp, s_depth;
static ID3D12CommandAllocator *s_alloc;
static ID3D12GraphicsCommandList *s_list;
static ID3D12RootSignature *s_main_root, *s_blit_root;
static ID3D12PipelineState *s_pso_present, *s_pso_copy, *s_pso_copyn;
static ID3D12PipelineState *s_pso_main[6 * 3]; /* made on first use: (blend + 1) * 3 + depth mode */
static ID3DBlob *s_main_vs, *s_main_ps;
static D3D12_INPUT_ELEMENT_DESC s_layout[8];
static ID3D12Fence *s_fence;
static HANDLE s_event;
static UINT64 s_fence_value;
static UINT s_rtv_inc, s_srv_inc;
static int s_w, s_h, s_ready, s_open, s_scale;
static UINT8 *s_up_pic_ptr, *s_up_menu_ptr, *s_read_ptr, *s_ring;
static UINT s_read_pitch, s_ring_off;

static GVtx *s_batch;
static int s_count, s_semi = -1, s_deep, s_clip[4] = {0, 0, 1023, 511};

#define OK(x) ((x) >= 0)

static void wait_idle(void) {
    if (s_queue == NULL || s_fence == NULL) {
        return;
    }
    ID3D12CommandQueue_Signal(s_queue, s_fence, ++s_fence_value);
    if (ID3D12Fence_GetCompletedValue(s_fence) < s_fence_value) {
        ID3D12Fence_SetEventOnCompletion(s_fence, s_fence_value, s_event);
        WaitForSingleObject(s_event, INFINITE);
    }
}

static ID3D12Resource *buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state, UINT8 **map) {
    D3D12_HEAP_PROPERTIES hp = {type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC rd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 0};
    ID3D12Resource *r = NULL;

    if (!OK(ID3D12Device_CreateCommittedResource(s_dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL, &IID_ID3D12Resource, (void **)&r))) {
        return NULL;
    }
    if (map != NULL) {
        D3D12_RANGE none = {0, 0};

        ID3D12Resource_Map(r, 0, type == D3D12_HEAP_TYPE_READBACK ? NULL : &none, (void **)map);
    }
    return r;
}

static Tex texture(int w, int h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear) {
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC rd = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)w, (UINT)h, 1, 1, fmt, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags};
    Tex t = {NULL, state};

    ID3D12Device_CreateCommittedResource(s_dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, clear, &IID_ID3D12Resource, (void **)&t.r);
    return t;
}

static void list(void) {
    ID3D12DescriptorHeap *heaps[1] = {s_srv_heap};

    if (s_open) {
        return;
    }
    ID3D12CommandAllocator_Reset(s_alloc);
    ID3D12GraphicsCommandList_Reset(s_list, s_alloc, NULL);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(s_list, 1, heaps);
    s_open = 1;
}

/* the open list run and waited for: the uploads are free again */
static void submit(void) {
    ID3D12CommandList *l[1] = {(ID3D12CommandList *)s_list};

    if (!s_open) {
        return;
    }
    ID3D12GraphicsCommandList_Close(s_list);
    ID3D12CommandQueue_ExecuteCommandLists(s_queue, 1, l);
    wait_idle();
    s_open = 0;
    s_ring_off = 0;
}

static void trans(Tex *t, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = {0};

    if (t->st == to) {
        return;
    }
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t->r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t->st;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(s_list, 1, &b);
    t->st = to;
}

static void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    Tex t = {r, from};

    trans(&t, to);
}

/* size bytes of the frame's upload ring, 512-aligned; its offset in *off */
static UINT8 *ring_alloc(UINT size, UINT *off) {
    s_ring_off = (s_ring_off + 511u) & ~511u;
    if (s_ring_off + size > RING) {
        submit(); /* full: what is recorded runs, the ring starts over */
        list();
    }
    *off = s_ring_off;
    s_ring_off += size;
    return s_ring + *off;
}

static void srv(const Tex *t, int slot, DXGI_FORMAT fmt) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_srv_heap);
    D3D12_SHADER_RESOURCE_VIEW_DESC d = {0};

    d.Format = fmt, d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, d.Texture2D.MipLevels = 1;
    h.ptr += (SIZE_T)slot * s_srv_inc;
    ID3D12Device_CreateShaderResourceView(s_dev, t->r, &d, h);
}

static D3D12_CPU_DESCRIPTOR_HANDLE rtv_at(int i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_rtv_heap);

    h.ptr += (SIZE_T)i * s_rtv_inc;
    return h;
}

/* w x h pixels from the upload buffer (rows padded to 256) into the texture */
static void upload_tex(Tex *tex, ID3D12Resource *up, int w, int h) {
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};

    dst.pResource = tex->r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = (UINT)w, src.PlacedFootprint.Footprint.Height = (UINT)h, src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = (UINT)((w * 4 + 255) & ~255);
    trans(tex, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(s_list, &dst, 0, 0, 0, &src, NULL);
    trans(tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

static void backbuffers(void) {
    int i;

    for (i = 0; i < FRAMES; i++) {
        IDXGISwapChain3_GetBuffer(s_swap, (UINT)i, &IID_ID3D12Resource, (void **)&s_back[i]);
        ID3D12Device_CreateRenderTargetView(s_dev, s_back[i], NULL, rtv_at(i));
    }
    if (s_read != NULL) {
        ID3D12Resource_Release(s_read);
    }
    s_read_pitch = (UINT)((s_w * 4 + 255) & ~255);
    s_read = buffer(D3D12_HEAP_TYPE_READBACK, (UINT64)s_read_pitch * (UINT)s_h, D3D12_RESOURCE_STATE_COPY_DEST, &s_read_ptr);
}

/* --- pipelines --- */

static void blend_of(D3D12_RENDER_TARGET_BLEND_DESC *b, int semi) {
    b->BlendEnable = semi >= 0;
    b->BlendOp = b->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b->SrcBlendAlpha = D3D12_BLEND_ONE, b->DestBlendAlpha = D3D12_BLEND_ZERO;
    b->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    switch (semi) {
    case 0: /* (back + front) / 2: the blend factor is 0.5 */
        b->SrcBlend = b->DestBlend = D3D12_BLEND_BLEND_FACTOR;
        break;
    case 1: /* back + front */
        b->SrcBlend = b->DestBlend = D3D12_BLEND_ONE;
        break;
    case 2: /* back - front */
        b->SrcBlend = b->DestBlend = D3D12_BLEND_ONE;
        b->BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        b->SrcBlendAlpha = D3D12_BLEND_ZERO, b->DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    case 4: /* back * (1 - front): a soft shadow */
        b->SrcBlend = D3D12_BLEND_ZERO, b->DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        b->SrcBlendAlpha = D3D12_BLEND_ZERO, b->DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    default: /* back + front / 4: the blend factor is 0.25 */
        b->SrcBlend = D3D12_BLEND_BLEND_FACTOR, b->DestBlend = D3D12_BLEND_ONE;
        break;
    }
}

/* semi: -1 none; depth: 0 off, 1 test and write, 2 test only */
static ID3D12PipelineState *main_pso(int semi, int depth) {
    int k = (semi + 1) * 3 + depth;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {0};

    if (s_pso_main[k] != NULL) {
        return s_pso_main[k];
    }
    d.pRootSignature = s_main_root;
    d.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(s_main_vs), d.VS.BytecodeLength = ID3D10Blob_GetBufferSize(s_main_vs);
    d.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(s_main_ps), d.PS.BytecodeLength = ID3D10Blob_GetBufferSize(s_main_ps);
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID, d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE, d.RasterizerState.DepthClipEnable = TRUE;
    blend_of(&d.BlendState.RenderTarget[0], semi);
    d.DepthStencilState.DepthEnable = depth != 0;
    d.DepthStencilState.DepthWriteMask = depth == 1 ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.InputLayout.pInputElementDescs = s_layout, d.InputLayout.NumElements = 8;
    d.SampleMask = 0xFFFFFFFFu, d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1, d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM, d.DSVFormat = DXGI_FORMAT_D32_FLOAT, d.SampleDesc.Count = 1;
    if (!OK(ID3D12Device_CreateGraphicsPipelineState(s_dev, &d, &IID_ID3D12PipelineState, (void **)&s_pso_main[k]))) {
        SDL_Log("d3d12: pipeline %d", k);
    }
    return s_pso_main[k];
}

static ID3D12PipelineState *blit_pso(ID3DBlob *vs, ID3DBlob *ps, int write_alpha) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {0};
    ID3D12PipelineState *p = NULL;

    d.pRootSignature = s_blit_root;
    d.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs), d.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    d.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps), d.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID, d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE, d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE, d.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    d.BlendState.RenderTarget[0].BlendOp = d.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE, d.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    /* on screen the window stays opaque: the copy's alpha is the mask bit */
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = write_alpha ? D3D12_COLOR_WRITE_ENABLE_ALL : 7;
    d.SampleMask = 0xFFFFFFFFu, d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1, d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM, d.SampleDesc.Count = 1;
    ID3D12Device_CreateGraphicsPipelineState(s_dev, &d, &IID_ID3D12PipelineState, (void **)&p);
    return p;
}

static ID3D12RootSignature *root(const D3D12_ROOT_SIGNATURE_DESC *rsd) {
    ID3DBlob *rs = NULL, *err = NULL;
    ID3D12RootSignature *r = NULL;

    if (OK(D3D12SerializeRootSignature(rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &err))) {
        ID3D12Device_CreateRootSignature(s_dev, 0, ID3D10Blob_GetBufferPointer(rs), ID3D10Blob_GetBufferSize(rs), &IID_ID3D12RootSignature, (void **)&r);
    }
    return r;
}

static int compile(const char *src, const char *entry, const char *target, ID3DBlob **out) {
    ID3DBlob *err = NULL;

    if (!OK(D3DCompile(src, SDL_strlen(src), NULL, NULL, NULL, entry, target, 0, 0, out, &err))) {
        SDL_Log("d3d12: shader %s: %s", entry, err != NULL ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        return 0;
    }
    return 1;
}

uint64_t gfx_window_flags(void) {
    return 0; /* no GL: the window is plain */
}

int gfx_init(SDL_Window *window) {
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FRAMES + 1, 0, 0};
    D3D12_DESCRIPTOR_HEAP_DESC sd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, S_COUNT, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC dd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, 0, 0};
    IDXGIFactory4 *fac = NULL;
    IDXGISwapChain1 *sc = NULL;
    DXGI_SWAP_CHAIN_DESC1 scd = {0};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {0};
    D3D12_STATIC_SAMPLER_DESC ss[2] = {0};
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    ID3DBlob *bvs = NULL, *bps = NULL, *bpsn = NULL;
    static const struct { const char *name; UINT idx; DXGI_FORMAT fmt; UINT off; } L[8] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0}, {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UINT, 16},
        {"TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_UINT, 20}, {"TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_UINT, 28},
        {"TEXCOORD", 2, DXGI_FORMAT_R16G16B16A16_UINT, 36}, {"TEXCOORD", 3, DXGI_FORMAT_R32_UINT, 44},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 48}, {"TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 52}};
    int i;

    s_window = window;
    SDL_GetWindowSizeInPixels(window, &s_w, &s_h);
    if ((s_batch = SDL_malloc(BATCH * sizeof(GVtx))) == NULL) {
        return 0;
    }
    if (hwnd == NULL || !OK(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&s_dev)) ||
        !OK(ID3D12Device_CreateCommandQueue(s_dev, &qd, &IID_ID3D12CommandQueue, (void **)&s_queue)) ||
        !OK(CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&fac))) {
        SDL_Log("d3d12: no device");
        return 0;
    }
    scd.Width = (UINT)s_w, scd.Height = (UINT)s_h, scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM, scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, scd.BufferCount = FRAMES, scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!OK(IDXGIFactory4_CreateSwapChainForHwnd(fac, (IUnknown *)s_queue, hwnd, &scd, NULL, NULL, &sc)) ||
        !OK(IDXGISwapChain1_QueryInterface(sc, &IID_IDXGISwapChain3, (void **)&s_swap))) {
        SDL_Log("d3d12: no swap chain");
        return 0;
    }
    IDXGIFactory4_MakeWindowAssociation(fac, hwnd, DXGI_MWA_NO_ALT_ENTER);
    ID3D12Device_CreateDescriptorHeap(s_dev, &rd, &IID_ID3D12DescriptorHeap, (void **)&s_rtv_heap);
    ID3D12Device_CreateDescriptorHeap(s_dev, &sd, &IID_ID3D12DescriptorHeap, (void **)&s_srv_heap);
    ID3D12Device_CreateDescriptorHeap(s_dev, &dd, &IID_ID3D12DescriptorHeap, (void **)&s_dsv_heap);
    s_rtv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(s_dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s_srv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(s_dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Device_CreateCommandAllocator(s_dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&s_alloc);
    ID3D12Device_CreateCommandList(s_dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&s_list);
    ID3D12GraphicsCommandList_Close(s_list);
    ID3D12Device_CreateFence(s_dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&s_fence);
    s_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    /* the blit's root: a table of one SRV, 4 constants (the texture rectangle), two samplers */
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range, rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, rp[1].Constants.Num32BitValues = 4, rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    for (i = 0; i < 2; i++) {
        ss[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss[i].AddressU = ss[i].AddressV = ss[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss[i].ShaderRegister = (UINT)i, ss[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL, ss[i].MaxLOD = D3D12_FLOAT32_MAX;
    }
    rsd.NumParameters = 2, rsd.pParameters = rp, rsd.NumStaticSamplers = 2, rsd.pStaticSamplers = ss;
    s_blit_root = root(&rsd);
    /* the main shader's root: 16 constants, a table of one SRV (the VRAM) */
    rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, rp[1].Constants.Num32BitValues = 16, rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rsd.NumStaticSamplers = 0, rsd.pStaticSamplers = NULL;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    s_main_root = root(&rsd);
    if (s_blit_root == NULL || s_main_root == NULL || !compile(BLIT_HLSL, "vs", "vs_5_0", &bvs) || !compile(BLIT_HLSL, "ps", "ps_5_0", &bps) ||
        !compile(BLIT_HLSL, "psn", "ps_5_0", &bpsn) || !compile(MAIN_HLSL, "vs", "vs_5_0", &s_main_vs) || !compile(MAIN_HLSL, "ps", "ps_5_0", &s_main_ps)) {
        return 0;
    }
    for (i = 0; i < 8; i++) {
        s_layout[i].SemanticName = L[i].name, s_layout[i].SemanticIndex = L[i].idx, s_layout[i].Format = L[i].fmt;
        s_layout[i].AlignedByteOffset = L[i].off, s_layout[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    }
    s_pso_present = blit_pso(bvs, bps, 0);
    s_pso_copy = blit_pso(bvs, bps, 1);
    s_pso_copyn = blit_pso(bvs, bpsn, 1);
    s_pic = texture(VRAM_W, VRAM_H, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
    s_menu = texture(MENU_W, MENU_H, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
    s_vram = texture(VRAM_W, VRAM_H, DXGI_FORMAT_R16_UINT, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
    s_up_pic = buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)VRAM_W * 4 * VRAM_H, D3D12_RESOURCE_STATE_GENERIC_READ, &s_up_pic_ptr);
    s_up_menu = buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)((MENU_W * 4 + 255) & ~255) * MENU_H, D3D12_RESOURCE_STATE_GENERIC_READ, &s_up_menu_ptr);
    s_ring_res = buffer(D3D12_HEAP_TYPE_UPLOAD, RING, D3D12_RESOURCE_STATE_GENERIC_READ, &s_ring);
    if (s_pso_present == NULL || s_pso_copy == NULL || s_pso_copyn == NULL || s_pic.r == NULL || s_menu.r == NULL || s_vram.r == NULL ||
        s_up_pic == NULL || s_up_menu == NULL || s_ring_res == NULL) {
        SDL_Log("d3d12: resources");
        return 0;
    }
    srv(&s_pic, S_PIC, DXGI_FORMAT_R8G8B8A8_UNORM);
    srv(&s_menu, S_MENU, DXGI_FORMAT_R8G8B8A8_UNORM);
    srv(&s_vram, S_VRAM, DXGI_FORMAT_R16_UINT);
    backbuffers();
    s_ready = 1;
    SDL_Log("d3d12: ready, %dx%d", s_w, s_h);
    return 1;
}

static void resize(int w, int h) {
    int i;

    submit();
    wait_idle();
    for (i = 0; i < FRAMES; i++) {
        ID3D12Resource_Release(s_back[i]);
    }
    s_w = w, s_h = h;
    IDXGISwapChain3_ResizeBuffers(s_swap, FRAMES, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    backbuffers();
}

int gfx_scale(void) {
    return SDL_max(s_scale, 1);
}

/* --- VRAM on the card --- */

/* A rect of the 1x VRAM onto the card */
static void upload(int x, int y, int w, int h) {
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    UINT pitch = (UINT)((w * 2 + 255) & ~255), off;
    UINT8 *p;
    int r;

    list();
    p = ring_alloc(pitch * (UINT)h, &off);
    for (r = 0; r < h; r++) {
        SDL_memcpy(p + (size_t)r * pitch, &g_vram[(y + r) * VRAM_W + x], (size_t)w * 2);
    }
    dst.pResource = s_vram.r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = s_ring_res, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = off;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16_UINT;
    src.PlacedFootprint.Footprint.Width = (UINT)w, src.PlacedFootprint.Footprint.Height = (UINT)h, src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = pitch;
    trans(&s_vram, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(s_list, &dst, (UINT)x, (UINT)y, 0, &src, NULL);
}

/* The dirty blocks among `need` (a mask per block row), as runs of a row */
static void upload_blocks(const uint16_t need[8]) {
    int row, col;

    for (row = 0; row < 8; row++) {
        uint16_t bits = need[row] & g_vram_dirty[row];

        for (col = 0; col < 16; col++) {
            if (bits >> col & 1) {
                int start = col;

                while (col < 16 && (bits >> col & 1)) {
                    col++;
                }
                upload(start * 64, row * 64, (col - start) * 64, 64);
            }
        }
        g_vram_dirty[row] &= ~need[row];
    }
}

static void need_rect(uint16_t need[8], int x, int y, int w, int h) {
    int r, c;

    for (r = (y & 511) >> 6; r <= ((y + h - 1) & 511) >> 6 && r < 8; r++) {
        for (c = (x & 1023) >> 6; c <= ((x + w - 1) & 1023) >> 6 && c < 16; c++) {
            need[r] |= (uint16_t)(1u << c);
        }
    }
}

void gfx_vram_dirty(int x, int y, int w, int h) {
    need_rect(g_vram_dirty, x, y, w, h);
}

/* --- the batch --- */

typedef struct {
    float size[2], jit[2], fog[4], fogz[2], scale;
    int persp, pass, truecolor, smooth3d, pad;
} Consts;

static void draw_batch(int semi, int depth, int pass, const Consts *k0) {
    Consts k = *k0;
    static const float half[4] = {0.5f, 0.5f, 0.5f, 0.5f}, quarter[4] = {0.25f, 0.25f, 0.25f, 0.25f};

    k.pass = pass;
    ID3D12GraphicsCommandList_SetPipelineState(s_list, main_pso(semi, depth));
    ID3D12GraphicsCommandList_OMSetBlendFactor(s_list, semi == 0 ? half : quarter);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(s_list, 1, 16, &k, 0);
    ID3D12GraphicsCommandList_DrawInstanced(s_list, (UINT)s_count, 1, 0, 0);
}

/* The batch, in its two passes when some of it is semi-transparent: the opaque pixels first, then the blended ones over them */
static void flush_batch(void) {
    int s = s_scale;
    UINT off, bytes = (UINT)(s_count * sizeof(GVtx));
    UINT8 *p;
    D3D12_VERTEX_BUFFER_VIEW vb;
    D3D12_VIEWPORT vp = {0, 0, (float)(VRAM_W * s), (float)(VRAM_H * s), 0, 1};
    D3D12_RECT sc = {s_clip[0] * s, s_clip[1] * s, (s_clip[2] + 1) * s, (s_clip[3] + 1) * s};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_at(FRAMES), dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_dsv_heap);
    D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(s_srv_heap);
    Consts k = {{VRAM_W, VRAM_H}, {0, 0}, {0, 0, 0, 0}, {0, 0}, (float)s, g_perspective, 0, g_true_color, g_smooth_3d, 0};

    if (s_count == 0) {
        return;
    }
    if (s_semi == 5) { /* a battle shadow: the mask's, not here yet */
        s_count = 0, s_semi = -1;
        return;
    }
    list();
    p = ring_alloc(bytes, &off);
    SDL_memcpy(p, s_batch, bytes);
    trans(&s_vram, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    trans(&s_rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    vb.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(s_ring_res) + off, vb.SizeInBytes = bytes, vb.StrideInBytes = sizeof(GVtx);
    ID3D12GraphicsCommandList_OMSetRenderTargets(s_list, 1, &rtv, FALSE, &dsv);
    ID3D12GraphicsCommandList_RSSetViewports(s_list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(s_list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(s_list, s_main_root);
    g.ptr += (UINT64)S_VRAM * s_srv_inc;
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(s_list, 0, g);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(s_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetVertexBuffers(s_list, 0, 1, &vb);
    draw_batch(-1, s_deep ? 1 : 0, s_semi < 0 ? 0 : 1, &k);
    if (s_semi >= 0) { /* the blended pixels test the depth, they don't hide what is behind */
        draw_batch(s_semi, s_deep ? 2 : 0, 2, &k);
    }
    s_count = 0;
    s_semi = -1;
}

static void flush(void) {
    flush_batch();
}

/* Triangles (n a multiple of 3) for the copy, with gpu_draw.c's state: the drawing area, the semi-transparency mode (4: a soft shadow's) */
void gfx_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {
    int semi_prim = (v[0].mode & 8) != 0, deep = v[0].z > 0;

    if (s_scale == 0) {
        return;
    }
    if (s_count + n > BATCH || deep != s_deep || x1 != s_clip[0] || y1 != s_clip[1] || x2 != s_clip[2] || y2 != s_clip[3] ||
        (semi_prim && s_semi >= 0 && semi != s_semi) || (!semi_prim && s_semi >= 0) ||
        (semi == 5 && s_semi != 5 && s_count > 0)) { /* battle shadows go to the mask: a batch of their own */
        flush();
        s_clip[0] = x1, s_clip[1] = y1, s_clip[2] = x2, s_clip[3] = y2;
        s_deep = deep;
    }
    if (v[0].mode & 2) {
        /* the page and the CLUT as the card has them: what the CPU drew there since (a texture drawn to) goes up first */
        uint16_t need[8] = {0};
        int depth = v[0].mode >> 4 & 3;

        need_rect(need, v[0].tx, v[0].ty, 64 << depth, 256);
        need_rect(need, v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        soft_read(v[0].tx, v[0].ty, 64 << depth, 256);
        soft_read(v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        if ((need[0] & g_vram_dirty[0]) | (need[1] & g_vram_dirty[1]) | (need[2] & g_vram_dirty[2]) | (need[3] & g_vram_dirty[3]) |
            (need[4] & g_vram_dirty[4]) | (need[5] & g_vram_dirty[5]) | (need[6] & g_vram_dirty[6]) | (need[7] & g_vram_dirty[7])) {
            flush();
            upload_blocks(need);
        }
    }
    if (semi_prim) {
        s_semi = semi;
    }
    SDL_memcpy(&s_batch[s_count], v, n * sizeof(GVtx));
    s_count += n;
}

/* An ordering table drawn: the battle shadows' mask comes later */
void gfx_shadows_resolve(void) {}

void gfx_depth_clear(void) {
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_dsv_heap);

    if (s_scale == 0 || s_depth.r == NULL) {
        return;
    }
    flush();
    list();
    ID3D12GraphicsCommandList_ClearDepthStencilView(s_list, dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
}

/* A quad of the copy that shows the 1x VRAM under it: after the CPU wrote VRAM (an image, a fill) */
void gfx_refresh(int x, int y, int w, int h) {
    GVtx q[6];
    int i;

    if (s_scale == 0 || w <= 0 || h <= 0) {
        return;
    }
    flush();
    if (x + w > VRAM_W) w = VRAM_W - x;
    if (y + h > VRAM_H) h = VRAM_H - y;
    upload(x, y, w, h);
    SDL_memset(q, 0, sizeof q);
    for (i = 0; i < 6; i++) {
        static const int CX[6] = {0, 1, 0, 1, 1, 0}, CY[6] = {0, 0, 1, 0, 1, 1};

        q[i].x = (float)(x + CX[i] * w);
        q[i].y = (float)(y + CY[i] * h);
        q[i].mode = 128;
    }
    SDL_memcpy(s_batch, q, sizeof q);
    s_count = 6;
    s_clip[0] = 0, s_clip[1] = 0, s_clip[2] = 1023, s_clip[3] = 511;
    s_deep = 0;
    flush();
}

void gfx_battle_fog(int r, int g, int b) { (void)r, (void)g, (void)b; }

/* A rectangle of tex (its slot in the SRV heap) onto the viewport vp (pixels, from the top left) */
static void quad(int slot, const int vp[4], const float uv[4], ID3D12PipelineState *pso) {
    D3D12_VIEWPORT v = {(float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3], 0, 1};
    D3D12_RECT sc = {vp[0], vp[1], vp[0] + vp[2], vp[1] + vp[3]};
    D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(s_srv_heap);

    g.ptr += (UINT64)slot * s_srv_inc;
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(s_list, s_blit_root);
    ID3D12GraphicsCommandList_SetPipelineState(s_list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(s_list, 1, &v);
    ID3D12GraphicsCommandList_RSSetScissorRects(s_list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(s_list, 0, g);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(s_list, 1, 4, uv, 0);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(s_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawInstanced(s_list, 4, 1, 0, 0);
}

/* VRAM to VRAM, in the copy */
void gfx_copy(int sx, int sy, int w, int h, int dx, int dy) {
    gfx_copy_to(sx, sy, w, h, (float)dx, dy, (float)w);
}

/* The same, to dw pixels across from dx (a widescreen battle's menus) */
void gfx_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) {
    int s = s_scale, x0, x1;
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    D3D12_BOX box;

    if (s == 0 || w <= 0 || h <= 0) {
        return;
    }
    flush();
    if (sx + w > VRAM_W) w = VRAM_W - sx;
    if (dx + dw > VRAM_W) dw = VRAM_W - dx;
    if (sy + h > VRAM_H) h = VRAM_H - sy;
    if (dy + h > VRAM_H) h = VRAM_H - dy;
    x0 = (int)(dx * s + 0.5f), x1 = (int)((dx + dw) * s + 0.5f);
    list();
    trans(&s_rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
    trans(&s_tmp, D3D12_RESOURCE_STATE_COPY_DEST);
    dst.pResource = s_tmp.r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = s_rt.r, src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    box.left = (UINT)(sx * s), box.top = (UINT)(sy * s), box.right = (UINT)((sx + w) * s), box.bottom = (UINT)((sy + h) * s), box.front = 0, box.back = 1;
    ID3D12GraphicsCommandList_CopyTextureRegion(s_list, &dst, 0, 0, 0, &src, &box);
    if (x1 - x0 == w * s) { /* the same width: a plain copy back */
        trans(&s_tmp, D3D12_RESOURCE_STATE_COPY_SOURCE);
        trans(&s_rt, D3D12_RESOURCE_STATE_COPY_DEST);
        dst.pResource = s_rt.r, src.pResource = s_tmp.r;
        box.left = 0, box.top = 0, box.right = (UINT)(w * s), box.bottom = (UINT)(h * s);
        ID3D12GraphicsCommandList_CopyTextureRegion(s_list, &dst, (UINT)x0, (UINT)(dy * s), 0, &src, &box);
    } else { /* stretched across, by the nearest texel */
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtv_at(FRAMES);
        int vp[4] = {x0, dy * s, x1 - x0, h * s};
        float uv[4] = {0, 0, w * s / (float)(VRAM_W * s), h * s / (float)(VRAM_H * s)};

        trans(&s_tmp, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        trans(&s_rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12GraphicsCommandList_OMSetRenderTargets(s_list, 1, &rtv, FALSE, NULL);
        quad(S_TMP, vp, uv, s_pso_copyn);
    }
}

/* The render scale in use, made to follow render_scale: the copy, its depth, the temporary of the copies */
void gfx_sync(int force) {
    int want = SDL_clamp(g_res_scale, 1, MAX_SCALE);

    if (!s_ready || g_vram == NULL) { /* VRAM comes with ResetGraph */
        return;
    }
    if (want != s_scale) {
        D3D12_CLEAR_VALUE cc = {DXGI_FORMAT_R8G8B8A8_UNORM, {{0, 0, 0, 0}}}, cd = {DXGI_FORMAT_D32_FLOAT, {{0}}};
        const float zero[4] = {0, 0, 0, 0};

        flush();
        submit();
        if (s_rt.r != NULL) {
            ID3D12Resource_Release(s_rt.r);
            ID3D12Resource_Release(s_tmp.r);
            ID3D12Resource_Release(s_depth.r);
        }
        s_scale = want;
        cd.DepthStencil.Depth = 1.0f;
        s_rt = texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc);
        s_tmp = texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
        s_depth = texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_D32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cd);
        if (s_rt.r == NULL || s_tmp.r == NULL || s_depth.r == NULL) {
            SDL_Log("d3d12: the copy at scale %d", want);
            return;
        }
        ID3D12Device_CreateRenderTargetView(s_dev, s_rt.r, NULL, rtv_at(FRAMES));
        ID3D12Device_CreateDepthStencilView(s_dev, s_depth.r, NULL, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_dsv_heap));
        srv(&s_rt, S_RT, DXGI_FORMAT_R8G8B8A8_UNORM);
        srv(&s_tmp, S_TMP, DXGI_FORMAT_R8G8B8A8_UNORM);
        list();
        ID3D12GraphicsCommandList_ClearRenderTargetView(s_list, rtv_at(FRAMES), zero, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(s_list, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_dsv_heap),
                                                        D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    } else if (!force) {
        return;
    }
    soft_all();
    gfx_refresh(0, 0, VRAM_W, VRAM_H);
}

/* what the card does not draw yet: props, images */
void gfx_depth_save(void) {}
void gfx_depth_models(void) {}
int gfx_image_prepare(const SDL_Surface *img, unsigned *tex) { (void)img, (void)tex; return 0; }
void gfx_image(const SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2) {
    (void)img, (void)tex, (void)x0, (void)y0, (void)x1, (void)y1, (void)add, (void)cx1, (void)cy1, (void)cx2, (void)cy2;
}
void gfx_props(const SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)v, (void)n, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}
unsigned gfx_inst_make(const PInst *v, int n) { (void)v, (void)n; return 0; }
void gfx_inst_free(unsigned b) { (void)b; }
unsigned gfx_buf_make(const void *p, unsigned long bytes) { (void)p, (void)bytes; return 0; }
void gfx_mesh_make(GMesh *g, const float *v, int n) { (void)v, (void)n; g->n = 0; }
void gfx_props_model(const SDL_Surface *img, unsigned *tex, const GMesh *g, float miny, float h, unsigned inst, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)g, (void)miny, (void)h, (void)inst, (void)first, (void)n, (void)cam, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}
void gfx_props_inst(const SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)b, (void)first, (void)n, (void)cam, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}
void gfx_props_part(const SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)p, (void)cam, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}

/* --- the frame --- */

/* The 24-bit movies' display area as RGBA in the upload buffer (rows padded to 256 bytes) */
static void movie_rgba(const PSX_RECT *d) {
    int x, y, pitch = (d->w * 4 + 255) & ~255;

    soft_read(d->x, d->y, d->w, d->h);
    for (y = 0; y < d->h; y++) {
        uint32_t *o = (uint32_t *)(s_up_pic_ptr + (size_t)y * pitch);
        const uint8_t *b = (const uint8_t *)&g_vram[(d->y + y) * VRAM_W + d->x];

        for (x = 0; x < d->w && (d->x * 2 + x * 3 + 3) <= VRAM_W * 2; x++) {
            o[x] = (uint32_t)(b[x * 3] | b[x * 3 + 1] << 8 | b[x * 3 + 2] << 16 | 0xFF000000u);
        }
    }
}

/* The frame: the display area of the copy (or the 1x VRAM's 24-bit movie) letterboxed to 4:3, then the menu (a 1280x960 RGBA surface, or NULL) */
void gfx_present(const SDL_Surface *menu, SDL_Surface **shot) {
    PSX_RECT d = g_disp.disp;
    int w, h, vp[4], menu_vp[4], game, movie, i;
    int aw = gte_widescreen() ? 16 : 4, ah = gte_widescreen() ? 9 : 3;
    UINT bi;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    const float black[4] = {0, 0, 0, 1};

    if (!s_ready) {
        return;
    }
    flush();
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    if (w < 1 || h < 1) {
        return;
    }
    if (w != s_w || h != s_h) {
        resize(w, h);
    }
    vp[2] = w * ah > h * aw ? h * aw / ah : w;
    vp[3] = w * ah > h * aw ? h : w * ah / aw;
    if (g_integer_scale && vp[3] >= 240) {
        vp[3] = vp[3] / 240 * 240;
        vp[2] = vp[3] * aw / ah;
    }
    vp[0] = (w - vp[2]) / 2, vp[1] = (h - vp[3]) / 2;
    menu_vp[2] = w * 3 > h * 4 ? h * 4 / 3 : w;
    menu_vp[3] = w * 3 > h * 4 ? h : w * 3 / 4;
    menu_vp[0] = (w - menu_vp[2]) / 2, menu_vp[1] = (h - menu_vp[3]) / 2;
    if (d.x + d.w > VRAM_W) d.w = VRAM_W - d.x;
    if (d.y + d.h > VRAM_H) d.h = VRAM_H - d.y;
    movie = g_disp_on && d.w > 0 && d.h > 0 && g_disp.isrgb24;
    game = g_disp_on && d.w > 0 && d.h > 0 && !g_disp.isrgb24 && s_scale > 0;

    list();
    if (movie) {
        movie_rgba(&d);
        upload_tex(&s_pic, s_up_pic, d.w, d.h);
    }
    if (menu != NULL) {
        for (i = 0; i < MENU_H; i++) {
            SDL_memcpy(s_up_menu_ptr + (size_t)i * ((MENU_W * 4 + 255) & ~255), (const uint8_t *)menu->pixels + (size_t)i * menu->pitch, MENU_W * 4);
        }
        upload_tex(&s_menu, s_up_menu, MENU_W, MENU_H);
    }
    if (game) {
        trans(&s_rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    bi = IDXGISwapChain3_GetCurrentBackBufferIndex(s_swap);
    barrier(s_back[bi], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv = rtv_at((int)bi);
    ID3D12GraphicsCommandList_OMSetRenderTargets(s_list, 1, &rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(s_list, rtv, black, 0, NULL);
    if (movie) {
        float uv[4] = {0, 0, d.w / (float)VRAM_W, d.h / (float)VRAM_H};

        quad(S_PIC, vp, uv, s_pso_present);
    } else if (game) {
        float uv[4] = {d.x / (float)VRAM_W, d.y / (float)VRAM_H, d.w / (float)VRAM_W, d.h / (float)VRAM_H};

        quad(S_RT, vp, uv, s_pso_present);
    }
    if (menu != NULL) {
        float uv[4] = {0, 0, 1, 1};

        quad(S_MENU, menu_vp, uv, s_pso_present);
    }
    if (shot != NULL) {
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};

        barrier(s_back[bi], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dst.pResource = s_read, dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = (UINT)w, dst.PlacedFootprint.Footprint.Height = (UINT)h, dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = s_read_pitch;
        src.pResource = s_back[bi], src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ID3D12GraphicsCommandList_CopyTextureRegion(s_list, &dst, 0, 0, 0, &src, NULL);
        barrier(s_back[bi], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    } else {
        barrier(s_back[bi], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }
    submit();
    if (shot != NULL && (*shot = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888)) != NULL) {
        for (i = 0; i < h; i++) {
            SDL_memcpy((uint8_t *)(*shot)->pixels + (size_t)i * (*shot)->pitch, s_read_ptr + (size_t)i * s_read_pitch, (size_t)w * 4);
        }
    }
    IDXGISwapChain3_Present(s_swap, g_vsync ? 1 : 0, 0);
}
