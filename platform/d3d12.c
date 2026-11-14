/* The renderer on Direct3D 12 (x64, Windows), written by hand: the other backend behind renderer.h, chosen at build time
   (build.py --d3d12; gl.c is left out). Step 1: it shows the display area of the CPU's VRAM (gpu_draw.c draws every
   primitive there in software, at 1x), with the options menu over it and the screenshots. What the card draws finer
   (render_scale, the 3D with depth, props, post-process) comes after, one piece at a time, by gl.c's side. */
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
#define PIC_W VRAM_W
#define PIC_H VRAM_H
#define MENU_W 1280
#define MENU_H 960

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

static const char *HLSL =
    "cbuffer C : register(b0) { float4 uv; };\n"
    "Texture2D tx : register(t0);\n"
    "SamplerState sm : register(s0);\n"
    "struct VO { float4 p : SV_Position; float2 t : TEXCOORD0; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    o.t = uv.xy + c * uv.zw;\n"
    "    return o;\n"
    "}\n"
    "float4 ps(VO i) : SV_Target { return tx.Sample(sm, i.t); }\n";

static SDL_Window *s_window;
static ID3D12Device *s_dev;
static ID3D12CommandQueue *s_queue;
static IDXGISwapChain3 *s_swap;
static ID3D12DescriptorHeap *s_rtv_heap, *s_srv_heap;
static ID3D12Resource *s_back[FRAMES], *s_pic, *s_menu, *s_up_pic, *s_up_menu, *s_read;
static ID3D12CommandAllocator *s_alloc;
static ID3D12GraphicsCommandList *s_list;
static ID3D12RootSignature *s_root;
static ID3D12PipelineState *s_pso_opaque, *s_pso_alpha;
static ID3D12Fence *s_fence;
static HANDLE s_event;
static UINT64 s_fence_value;
static UINT s_rtv_inc, s_srv_inc;
static int s_w, s_h, s_ready;
static UINT8 *s_up_pic_ptr, *s_up_menu_ptr, *s_read_ptr;
static UINT s_read_pitch;

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

static ID3D12Resource *texture(int w, int h) {
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC rd = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)w, (UINT)h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, 0};
    ID3D12Resource *r = NULL;

    ID3D12Device_CreateCommittedResource(s_dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL, &IID_ID3D12Resource, (void **)&r);
    return r;
}

static void barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = {0};

    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(s_list, 1, &b);
}

/* w x h pixels from the upload buffer (rows of w * 4 bytes, already padded to 256) into the texture */
static void upload(ID3D12Resource *tex, ID3D12Resource *up, int w, int h) {
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};

    dst.pResource = tex, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = (UINT)w, src.PlacedFootprint.Footprint.Height = (UINT)h, src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = (UINT)((w * 4 + 255) & ~255);
    barrier(tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(s_list, &dst, 0, 0, 0, &src, NULL);
    barrier(tex, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

static void backbuffers(void) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_rtv_heap);
    int i;

    for (i = 0; i < FRAMES; i++) {
        IDXGISwapChain3_GetBuffer(s_swap, (UINT)i, &IID_ID3D12Resource, (void **)&s_back[i]);
        ID3D12Device_CreateRenderTargetView(s_dev, s_back[i], NULL, h);
        h.ptr += s_rtv_inc;
    }
    if (s_read != NULL) {
        ID3D12Resource_Release(s_read);
    }
    s_read_pitch = (UINT)((s_w * 4 + 255) & ~255);
    s_read = buffer(D3D12_HEAP_TYPE_READBACK, (UINT64)s_read_pitch * (UINT)s_h, D3D12_RESOURCE_STATE_COPY_DEST, &s_read_ptr);
}

static void srv(ID3D12Resource *r, int slot) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_srv_heap);
    D3D12_SHADER_RESOURCE_VIEW_DESC d = {0};

    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM, d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, d.Texture2D.MipLevels = 1;
    h.ptr += (SIZE_T)slot * s_srv_inc;
    ID3D12Device_CreateShaderResourceView(s_dev, r, &d, h);
}

static ID3D12PipelineState *pipeline(ID3DBlob *vs, ID3DBlob *ps, int alpha) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {0};
    ID3D12PipelineState *p = NULL;

    d.pRootSignature = s_root;
    d.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs), d.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    d.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps), d.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID, d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE, d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].BlendEnable = alpha;
    d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA, d.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    d.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE, d.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    d.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = 0xFFFFFFFFu, d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1, d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM, d.SampleDesc.Count = 1;
    ID3D12Device_CreateGraphicsPipelineState(s_dev, &d, &IID_ID3D12PipelineState, (void **)&p);
    return p;
}

uint64_t gfx_window_flags(void) {
    return 0; /* no GL: the window is plain */
}

int gfx_init(SDL_Window *window) {
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FRAMES, 0, 0}, sd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    IDXGIFactory4 *fac = NULL;
    IDXGISwapChain1 *sc = NULL;
    DXGI_SWAP_CHAIN_DESC1 scd = {0};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {0};
    D3D12_STATIC_SAMPLER_DESC ss = {0};
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    ID3DBlob *rs = NULL, *err = NULL, *vs = NULL, *ps = NULL;

    s_window = window;
    SDL_GetWindowSizeInPixels(window, &s_w, &s_h);
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
    s_rtv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(s_dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    s_srv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(s_dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Device_CreateCommandAllocator(s_dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&s_alloc);
    ID3D12Device_CreateCommandList(s_dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_alloc, NULL, &IID_ID3D12GraphicsCommandList, (void **)&s_list);
    ID3D12GraphicsCommandList_Close(s_list);
    ID3D12Device_CreateFence(s_dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&s_fence);
    s_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    /* the root: a table of one SRV, 4 constants (the texture rectangle), a linear clamped sampler */
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range, rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, rp[1].Constants.Num32BitValues = 4, rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    ss.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR, ss.AddressU = ss.AddressV = ss.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    ss.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL, ss.MaxLOD = D3D12_FLOAT32_MAX;
    rsd.NumParameters = 2, rsd.pParameters = rp, rsd.NumStaticSamplers = 1, rsd.pStaticSamplers = &ss;
    if (!OK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &err)) ||
        !OK(ID3D12Device_CreateRootSignature(s_dev, 0, ID3D10Blob_GetBufferPointer(rs), ID3D10Blob_GetBufferSize(rs), &IID_ID3D12RootSignature, (void **)&s_root)) ||
        !OK(D3DCompile(HLSL, SDL_strlen(HLSL), NULL, NULL, NULL, "vs", "vs_5_0", 0, 0, &vs, &err)) ||
        !OK(D3DCompile(HLSL, SDL_strlen(HLSL), NULL, NULL, NULL, "ps", "ps_5_0", 0, 0, &ps, &err))) {
        SDL_Log("d3d12: shaders: %s", err != NULL ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        return 0;
    }
    s_pso_opaque = pipeline(vs, ps, 0);
    s_pso_alpha = pipeline(vs, ps, 1);
    s_pic = texture(PIC_W, PIC_H);
    s_menu = texture(MENU_W, MENU_H);
    s_up_pic = buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)PIC_W * 4 * PIC_H, D3D12_RESOURCE_STATE_GENERIC_READ, &s_up_pic_ptr);
    s_up_menu = buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)((MENU_W * 4 + 255) & ~255) * MENU_H, D3D12_RESOURCE_STATE_GENERIC_READ, &s_up_menu_ptr);
    if (s_pso_opaque == NULL || s_pso_alpha == NULL || s_pic == NULL || s_menu == NULL || s_up_pic == NULL || s_up_menu == NULL) {
        SDL_Log("d3d12: resources");
        return 0;
    }
    srv(s_pic, 0);
    srv(s_menu, 1);
    backbuffers();
    s_ready = 1;
    SDL_Log("d3d12: ready, %dx%d", s_w, s_h);
    return 1;
}

static void resize(int w, int h) {
    int i;

    wait_idle();
    for (i = 0; i < FRAMES; i++) {
        ID3D12Resource_Release(s_back[i]);
    }
    s_w = w, s_h = h;
    IDXGISwapChain3_ResizeBuffers(s_swap, FRAMES, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    backbuffers();
}

int gfx_scale(void) {
    return 1;
}

void gfx_sync(int force) {
    (void)force;
    soft_all();
}

/* what the card draws finer comes later: the CPU's VRAM is what is shown */
void gfx_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) { (void)v, (void)n, (void)x1, (void)y1, (void)x2, (void)y2, (void)semi; }
void gfx_vram_dirty(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
void gfx_shadows_resolve(void) {}
void gfx_depth_clear(void) {}
void gfx_refresh(int x, int y, int w, int h) { (void)x, (void)y, (void)w, (void)h; }
void gfx_copy(int sx, int sy, int w, int h, int dx, int dy) { (void)sx, (void)sy, (void)w, (void)h, (void)dx, (void)dy; }
void gfx_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) { (void)sx, (void)sy, (void)w, (void)h, (void)dx, (void)dy, (void)dw; }
void gfx_battle_fog(int r, int g, int b) { (void)r, (void)g, (void)b; }
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

/* The display area of VRAM as RGBA in the upload buffer (rows padded to 256 bytes): 15-bit, or 24-bit for the movies */
static void picture_rgba(const PSX_RECT *d) {
    int x, y, pitch = (d->w * 4 + 255) & ~255;

    soft_read(d->x, d->y, d->w, d->h);
    for (y = 0; y < d->h; y++) {
        uint32_t *o = (uint32_t *)(s_up_pic_ptr + (size_t)y * pitch);

        if (g_disp.isrgb24) {
            const uint8_t *b = (const uint8_t *)&g_vram[(d->y + y) * VRAM_W + d->x];

            for (x = 0; x < d->w && (d->x * 2 + x * 3 + 3) <= VRAM_W * 2; x++) {
                o[x] = (uint32_t)(b[x * 3] | b[x * 3 + 1] << 8 | b[x * 3 + 2] << 16 | 0xFF000000u);
            }
        } else {
            const uint16_t *p = &g_vram[(d->y + y) * VRAM_W + d->x];

            for (x = 0; x < d->w; x++) {
                uint32_t v = p[x], r = (v & 31) << 3, g = (v >> 5 & 31) << 3, b = (v >> 10 & 31) << 3;

                o[x] = (r | r >> 5) | (g | g >> 5) << 8 | (b | b >> 5) << 16 | 0xFF000000u;
            }
        }
    }
}

static void quad(int slot, const int vp[4], const float uv[4], int alpha) {
    D3D12_VIEWPORT v = {(float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3], 0, 1};
    D3D12_RECT sc = {vp[0], vp[1], vp[0] + vp[2], vp[1] + vp[3]};
    D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(s_srv_heap);

    g.ptr += (UINT64)slot * s_srv_inc;
    ID3D12GraphicsCommandList_SetPipelineState(s_list, alpha ? s_pso_alpha : s_pso_opaque);
    ID3D12GraphicsCommandList_RSSetViewports(s_list, 1, &v);
    ID3D12GraphicsCommandList_RSSetScissorRects(s_list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(s_list, 0, g);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(s_list, 1, 4, uv, 0);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(s_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawInstanced(s_list, 4, 1, 0, 0);
}

void gfx_present(const SDL_Surface *menu, SDL_Surface **shot) {
    PSX_RECT d = g_disp.disp;
    int w, h, vp[4], menu_vp[4], game, i;
    int aw = gte_widescreen() ? 16 : 4, ah = gte_widescreen() ? 9 : 3;
    UINT bi;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    ID3D12DescriptorHeap *heaps[1];
    const float black[4] = {0, 0, 0, 1};

    if (!s_ready) {
        return;
    }
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
    game = g_disp_on && d.w > 0 && d.h > 0;

    wait_idle(); /* one frame in flight: the upload buffers and the allocator are free */
    ID3D12CommandAllocator_Reset(s_alloc);
    ID3D12GraphicsCommandList_Reset(s_list, s_alloc, NULL);
    if (game) {
        picture_rgba(&d);
        upload(s_pic, s_up_pic, d.w, d.h);
    }
    if (menu != NULL) {
        for (i = 0; i < MENU_H; i++) {
            SDL_memcpy(s_up_menu_ptr + (size_t)i * ((MENU_W * 4 + 255) & ~255), (const uint8_t *)menu->pixels + (size_t)i * menu->pitch, MENU_W * 4);
        }
        upload(s_menu, s_up_menu, MENU_W, MENU_H);
    }
    bi = IDXGISwapChain3_GetCurrentBackBufferIndex(s_swap);
    barrier(s_back[bi], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(s_rtv_heap);
    rtv.ptr += (SIZE_T)bi * s_rtv_inc;
    ID3D12GraphicsCommandList_OMSetRenderTargets(s_list, 1, &rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(s_list, rtv, black, 0, NULL);
    heaps[0] = s_srv_heap;
    ID3D12GraphicsCommandList_SetDescriptorHeaps(s_list, 1, heaps);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(s_list, s_root);
    if (game) {
        float uv[4] = {0, 0, d.w / (float)PIC_W, d.h / (float)PIC_H};

        quad(0, vp, uv, 0);
    }
    if (menu != NULL) {
        float uv[4] = {0, 0, 1, 1};

        quad(1, menu_vp, uv, 1);
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
    ID3D12GraphicsCommandList_Close(s_list);
    {
        ID3D12CommandList *l[1] = {(ID3D12CommandList *)s_list};

        ID3D12CommandQueue_ExecuteCommandLists(s_queue, 1, l);
    }
    if (shot != NULL) {
        wait_idle();
        if ((*shot = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888)) != NULL) {
            for (i = 0; i < h; i++) {
                SDL_memcpy((uint8_t *)(*shot)->pixels + (size_t)i * (*shot)->pitch, s_read_ptr + (size_t)i * s_read_pitch, (size_t)w * 4);
            }
        }
    }
    IDXGISwapChain3_Present(s_swap, g_vsync ? 1 : 0, 0);
}
