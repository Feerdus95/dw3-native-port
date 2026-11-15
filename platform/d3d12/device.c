/* The Direct3D 12 renderer's device: the queue, the frames in flight, resources and their states, the GPU's timestamps, set up */
#include "dx.h"

#define DXS_INIT {.semi = -1, .clip = {0, 0, 1023, 511}, .mask_box = {VRAM_W, VRAM_H, -1, -1}, .depth_used = 1}
SDL_COMPILE_TIME_ASSERT(dx_release_lists, FRAMES == 2 && FIF == 2); /* (dx_release names back[0..1] and s_alloc[0..1]) */

static ID3D12CommandQueue *s_queue;

static ID3D12Fence *s_fence;

static HANDLE s_event;

static UINT64 s_fence_value;

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

ID3D12Resource *dx_buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state, UINT8 **map) {
    D3D12_HEAP_PROPERTIES hp = {type, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC rd = {D3D12_RESOURCE_DIMENSION_BUFFER, 0, size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, 0};
    ID3D12Resource *r = NULL;

    if (!OK(ID3D12Device_CreateCommittedResource(DXS.dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, NULL, &IID_ID3D12Resource, (void **)&r))) {
        return NULL;
    }
    if (map != NULL) {
        D3D12_RANGE none = {0, 0};

        ID3D12Resource_Map(r, 0, type == D3D12_HEAP_TYPE_READBACK ? NULL : &none, (void **)map);
    }
    return r;
}

Tex dx_texture(int w, int h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear) {
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC rd = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, (UINT64)w, (UINT)h, 1, 1, fmt, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, flags};
    Tex t = {NULL, state};

    ID3D12Device_CreateCommittedResource(DXS.dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, state, clear, &IID_ID3D12Resource, (void **)&t.r);
    return t;
}

static UINT8 *s_qptr;

static UINT64 s_qfreq;

static unsigned char s_qc[FIF][QMAX];

/* Two frames in flight: the CPU records the next while the GPU draws the last. Each has its command allocator, its half of
   the upload ring and its fence; the slot in use is reused once its fence says the GPU is done with it */
static ID3D12CommandAllocator *s_alloc[FIF];

static UINT64 s_slot_fence[FIF];

void dx_wait_slot(int slot) {
    if (s_fence != NULL && ID3D12Fence_GetCompletedValue(s_fence) < s_slot_fence[slot]) {
        ID3D12Fence_SetEventOnCompletion(s_fence, s_slot_fence[slot], s_event);
        WaitForSingleObject(s_event, INFINITE);
    }
}

/* the timestamps of the frame that last ran in SLOT (its fence is passed): what its GPU time went to, logged when it was long */
static void harvest(int slot) {
    const UINT64 *t = (const UINT64 *)(s_qptr + (size_t)slot * QMAX * 8);
    double ms[C_N] = {0}, total = 0;
    int i, n = DXS.qn_slot[slot];

    DXS.qn_slot[slot] = 0;
    if (n < 2 || s_qfreq == 0) {
        return;
    }
    for (i = 0; i + 1 < n; i++) {
        double d = (double)(t[i + 1] - t[i]) * 1000.0 / (double)s_qfreq;

        ms[s_qc[slot][i]] += d, total += d;
    }
    if (total > 10.0) {
        SDL_Log("d3d12: gpu %.1f ms: 3d %.1f, 2d %.1f, shadow mask %.1f, shadow blur %.1f, vram refresh %.1f, copies %.1f, depth clear %.1f, images %.1f, present %.1f, post %.1f, other %.1f",
                total, ms[C_3D], ms[C_2D], ms[C_MASK], ms[C_RESOLVE], ms[C_REFRESH], ms[C_COPY], ms[C_DEPTH], ms[C_IMAGE], ms[C_PRESENT], ms[C_POST], ms[C_OTHER]);
    }
}

void dx_list(void) {
    ID3D12DescriptorHeap *heaps[1] = {DXS.srv_heap};

    if (DXS.open) {
        return;
    }
    dx_wait_slot(DXS.slot);
    harvest(DXS.slot);
    ID3D12CommandAllocator_Reset(s_alloc[DXS.slot]);
    ID3D12GraphicsCommandList_Reset(DXS.list, s_alloc[DXS.slot], NULL);
    ID3D12GraphicsCommandList_SetDescriptorHeaps(DXS.list, 1, heaps);
    DXS.ring_off = (UINT)DXS.slot * (RING / FIF);
    DXS.open = 1;
}

/* a region starts: its category from here on; the one before it is returned, to be marked again at the region's end */
int dx_mark(int cat) {
    int prev = DXS.cat;

    if (DXS.qheap != NULL && DXS.qn < QMAX) {
        dx_list();
        ID3D12GraphicsCommandList_EndQuery(DXS.list, DXS.qheap, D3D12_QUERY_TYPE_TIMESTAMP, (UINT)DXS.qn);
        s_qc[DXS.slot][DXS.qn++] = (unsigned char)cat;
    }
    DXS.cat = cat;
    return prev;
}

/* the open list run and waited for (the whole queue, the last frames' too): what is recorded is done, the uploads are free
   again, and a resource can be released */
void dx_submit(void) {
    ID3D12CommandList *l[1] = {(ID3D12CommandList *)DXS.list};
    Uint64 t0 = SDL_GetTicksNS();

    if (DXS.open) {
        ID3D12GraphicsCommandList_Close(DXS.list);
        ID3D12CommandQueue_ExecuteCommandLists(s_queue, 1, l);
        DXS.open = 0;
    }
    wait_idle(); /* (with no list open, a frame of dx_submit_frame's may still be drawing) */
    DXS.wait_ns += SDL_GetTicksNS() - t0, DXS.nsub++;
}

/* the frame's list run, not waited for: the next frame is recorded in the other slot meanwhile */
void dx_submit_frame(void) {
    ID3D12CommandList *l[1] = {(ID3D12CommandList *)DXS.list};

    if (!DXS.open) {
        return;
    }
    ID3D12GraphicsCommandList_Close(DXS.list);
    ID3D12CommandQueue_ExecuteCommandLists(s_queue, 1, l);
    ID3D12CommandQueue_Signal(s_queue, s_fence, ++s_fence_value);
    s_slot_fence[DXS.slot] = s_fence_value;
    DXS.qn_slot[DXS.slot] = DXS.qn, DXS.qn = 0;
    DXS.slot = (DXS.slot + 1) % FIF;
    DXS.open = 0;
}

void dx_trans(Tex *t, D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER b = {0};

    if (t->st == to) {
        return;
    }
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t->r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t->st;
    b.Transition.StateAfter = to;
    ID3D12GraphicsCommandList_ResourceBarrier(DXS.list, 1, &b);
    t->st = to;
}

void dx_barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
    Tex t = {r, from};

    dx_trans(&t, to);
}

/* size bytes of the frame's upload ring, 512-aligned; its offset in *off */
UINT8 *dx_ring_alloc(UINT size, UINT *off) {
    DXS.ring_off = (DXS.ring_off + 511u) & ~511u;
    if (DXS.ring_off + size > (UINT)(DXS.slot + 1) * (RING / FIF)) {
        dx_submit(); /* full: what is recorded runs, the ring starts over */
        dx_list();
    }
    *off = DXS.ring_off;
    DXS.ring_off += size;
    return DXS.ring + *off;
}

void dx_srv(const Tex *t, int slot, DXGI_FORMAT fmt) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DXS.srv_heap);
    D3D12_SHADER_RESOURCE_VIEW_DESC d = {0};

    d.Format = fmt, d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, d.Texture2D.MipLevels = 1;
    h.ptr += (SIZE_T)slot * DXS.srv_inc;
    ID3D12Device_CreateShaderResourceView(DXS.dev, t->r, &d, h);
}

D3D12_CPU_DESCRIPTOR_HANDLE dx_rtv_at(int i) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DXS.rtv_heap);

    h.ptr += (SIZE_T)i * DXS.rtv_inc;
    return h;
}

/* w x h pixels from the upload buffer (rows padded to 256) into the texture */
void dx_upload_tex(Tex *tex, ID3D12Resource *up, int w, int h) {
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};

    dst.pResource = tex->r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = (UINT)w, src.PlacedFootprint.Footprint.Height = (UINT)h, src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = (UINT)((w * 4 + 255) & ~255);
    dx_trans(tex, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, NULL);
    dx_trans(tex, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

/* 1 when the back buffers are there */
static int backbuffers(void) {
    int i;

    for (i = 0; i < FRAMES; i++) {
        if (!OK(IDXGISwapChain3_GetBuffer(DXS.swap, (UINT)i, &IID_ID3D12Resource, (void **)&DXS.back[i]))) {
            DXS.back[i] = NULL;
            return 0;
        }
        ID3D12Device_CreateRenderTargetView(DXS.dev, DXS.back[i], NULL, dx_rtv_at(i));
    }
    if (DXS.read != NULL) {
        ID3D12Resource_Release(DXS.read);
    }
    DXS.read_pitch = (UINT)((DXS.w * 4 + 255) & ~255);
    DXS.read = dx_buffer(D3D12_HEAP_TYPE_READBACK, (UINT64)DXS.read_pitch * (UINT)DXS.h, D3D12_RESOURCE_STATE_COPY_DEST, &DXS.read_ptr);
    return 1;
}

static void rel(void *pp) {
    IUnknown **p = pp;

    if (*p != NULL) {
        IUnknown_Release(*p);
        *p = NULL;
    }
}

/* All the device's own let go, from a start that stopped anywhere (dx_init's failures; again is harmless): the GPU waited for, Streamline
   shut down before its proxies (the device, the swap chain) are released, each object once, the device last */
static void dx_release(void) {
    Tex *t[] = {&DXS.pic, &DXS.menu, &DXS.vram, &DXS.rt, &DXS.tmp, &DXS.depth, &DXS.mask, &DXS.blur, &DXS.p[0], &DXS.p[1], &DXS.p[2], &DXS.fsr,
                &DXS.area, &DXS.search, &DXS.sd, &DXS.fd, &DXS.dout, &DXS.mv, &DXS.mout, &DXS.f3};
    void *o[] = {&DXS.pso_present, &DXS.pso_copy, &DXS.pso_copyn, &DXS.pso_over, &DXS.pso_blur, &DXS.pso_dark, &DXS.pso_img, &DXS.pso_add,
                 &DXS.pso_mask[0], &DXS.pso_mask[1], &DXS.pso_dkeep, &DXS.pso_dout, &DXS.pso_dview, &DXS.pso_post, &DXS.pso_easu,
                 &DXS.pso_smaa[0], &DXS.pso_smaa[1], &DXS.pso_smaa[2], &DXS.pso_mview, &DXS.pso_mcopy, &DXS.main_root, &DXS.blit_root,
                 &DXS.smaa_root, &DXS.main_vs, &DXS.main_ps, &DXS.main_psm, &DXS.up_pic, &DXS.up_menu, &DXS.read, &DXS.ring_res, &DXS.qread,
                 &DXS.qheap, &DXS.back[0], &DXS.back[1], &DXS.list, &s_alloc[0], &s_alloc[1], &DXS.rtv_heap, &DXS.srv_heap, &DXS.dsv_heap,
                 &s_fence, &DXS.swap, &s_queue, &DXS.dev};
    size_t i;

    wait_idle();
    sl_stop();
    for (i = 0; i < SDL_arraysize(DXS.pso_main); i++) {
        rel(&DXS.pso_main[i]);
    }
    for (i = 0; i < SDL_arraysize(t); i++) {
        rel(&t[i]->r);
    }
    for (i = 0; i < SDL_arraysize(o); i++) {
        rel(o[i]);
    }
    if (s_event != NULL) {
        CloseHandle(s_event);
    }
    s_event = NULL, s_fence_value = 0, s_qptr = NULL;
    SDL_memset(s_slot_fence, 0, sizeof s_slot_fence);
    SDL_free(DXS.batch);
    DXS = (struct DxState)DXS_INIT;
}

/* The swap chain has the window from its creation on, shown empty until a present (in full screen, the desktop behind
   it): the boot image video.c drew there is presented again at once, and stays until the game's first frame */
static void boot_picture(void) {
    SDL_Surface *s = video_boot_picture();
    const UINT pitch = (UINT)((DXS.w * 4 + 255) & ~255);
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    ID3D12Resource *up, *back;
    UINT8 *m;
    int y;

    if (s == NULL || s->w != DXS.w || s->h != DXS.h ||
        (up = dx_buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)pitch * (UINT)DXS.h, D3D12_RESOURCE_STATE_GENERIC_READ, &m)) == NULL) {
        return;
    }
    for (y = 0; y < DXS.h; y++) {
        SDL_memcpy(m + (size_t)y * pitch, (const UINT8 *)s->pixels + (size_t)y * s->pitch, (size_t)DXS.w * 4);
    }
    back = DXS.back[IDXGISwapChain3_GetCurrentBackBufferIndex(DXS.swap)];
    dst.pResource = back, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    src.PlacedFootprint.Footprint.Width = (UINT)DXS.w, src.PlacedFootprint.Footprint.Height = (UINT)DXS.h;
    src.PlacedFootprint.Footprint.Depth = 1, src.PlacedFootprint.Footprint.RowPitch = pitch;
    dx_list();
    dx_barrier(back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, NULL);
    dx_barrier(back, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
    dx_submit();
    IDXGISwapChain3_Present(DXS.swap, 0, 0);
    ID3D12Resource_Release(up);
}

uint64_t dx_window_flags(void) {
    return 0; /* no GL: the window is plain */
}

int dx_init(SDL_Window *window) {
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    D3D12_COMMAND_QUEUE_DESC qd = {D3D12_COMMAND_LIST_TYPE_DIRECT, 0, 0, 0};
    D3D12_DESCRIPTOR_HEAP_DESC rd = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, R_COUNT, 0, 0};
    D3D12_DESCRIPTOR_HEAP_DESC sd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, S_COUNT + MAX_IMG,D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    D3D12_DESCRIPTOR_HEAP_DESC dd = {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, 0, 0};
    IDXGIFactory4 *fac = NULL;
    IDXGISwapChain1 *sc = NULL;
    DXGI_SWAP_CHAIN_DESC1 scd = {0};
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {0};
    D3D12_STATIC_SAMPLER_DESC ss[2] = {0};
    Uint64 t_init = SDL_GetTicksNS();
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    ID3DBlob *bvs = NULL, *bps = NULL, *bpsn = NULL, *bpsb = NULL;
    static const struct { const char *name; UINT idx; DXGI_FORMAT fmt; UINT off; } L[9] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0}, {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UINT, 16},
        {"TEXCOORD", 0, DXGI_FORMAT_R16G16B16A16_UINT, 20}, {"TEXCOORD", 1, DXGI_FORMAT_R16G16B16A16_UINT, 28},
        {"TEXCOORD", 2, DXGI_FORMAT_R16G16B16A16_UINT, 36}, {"TEXCOORD", 3, DXGI_FORMAT_R32_UINT, 44},
        {"TEXCOORD", 4, DXGI_FORMAT_R32_FLOAT, 48}, {"TEXCOORD", 5, DXGI_FORMAT_R32_FLOAT, 52}, {"TEXCOORD", 6, DXGI_FORMAT_R32G32_FLOAT, 56}};
    int i;

    DXS.window = window;
    SDL_GetWindowSizeInPixels(window, &DXS.w, &DXS.h);
    if ((DXS.batch = SDL_malloc(BATCH * sizeof(GVtx))) == NULL) {
        goto fail;
    }
    DXS.dlss = g_fsr == 3 && sl_start(); /* (before the device and the swap chain) */
    SDL_PumpEvents(); /* Streamline's start and the device with it take seconds each: Windows is answered in between,
                         so that the window is never "not responding" (5 s without its messages) */
    if (hwnd == NULL || !OK(p_D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&DXS.dev))) {
        SDL_Log("d3d12: no device");
        goto fail;
    }
    DXS.dlss = DXS.dlss && sl_device(&DXS.dev);
    SDL_PumpEvents();
    if (!OK(ID3D12Device_CreateCommandQueue(DXS.dev, &qd, &IID_ID3D12CommandQueue, (void **)&s_queue)) ||
        !OK(p_CreateDXGIFactory1(&IID_IDXGIFactory4, (void **)&fac))) {
        SDL_Log("d3d12: no device");
        goto fail;
    }
    DXS.dlss = DXS.dlss && sl_factory(&fac);
    scd.Width = (UINT)DXS.w, scd.Height = (UINT)DXS.h, scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM, scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, scd.BufferCount = FRAMES, scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (!OK(IDXGIFactory4_CreateSwapChainForHwnd(fac, (IUnknown *)s_queue, hwnd, &scd, NULL, NULL, &sc)) ||
        !OK(IDXGISwapChain1_QueryInterface(sc, &IID_IDXGISwapChain3, (void **)&DXS.swap))) {
        SDL_Log("d3d12: no swap chain");
        goto fail;
    }
    IDXGIFactory4_MakeWindowAssociation(fac, hwnd, DXGI_MWA_NO_ALT_ENTER);
    if (!OK(ID3D12Device_CreateDescriptorHeap(DXS.dev, &rd, &IID_ID3D12DescriptorHeap, (void **)&DXS.rtv_heap)) ||
        !OK(ID3D12Device_CreateDescriptorHeap(DXS.dev, &sd, &IID_ID3D12DescriptorHeap, (void **)&DXS.srv_heap)) ||
        !OK(ID3D12Device_CreateDescriptorHeap(DXS.dev, &dd, &IID_ID3D12DescriptorHeap, (void **)&DXS.dsv_heap))) {
        SDL_Log("d3d12: no descriptor heaps");
        goto fail;
    }
    DXS.rtv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(DXS.dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    DXS.srv_inc = ID3D12Device_GetDescriptorHandleIncrementSize(DXS.dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (i = 0; i < FIF; i++) {
        if (!OK(ID3D12Device_CreateCommandAllocator(DXS.dev, D3D12_COMMAND_LIST_TYPE_DIRECT, &IID_ID3D12CommandAllocator, (void **)&s_alloc[i]))) {
            SDL_Log("d3d12: no command allocator");
            goto fail;
        }
    }
    if (!OK(ID3D12Device_CreateCommandList(DXS.dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_alloc[0], NULL, &IID_ID3D12GraphicsCommandList, (void **)&DXS.list)) ||
        !OK(ID3D12Device_CreateFence(DXS.dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&s_fence)) ||
        (s_event = CreateEventA(NULL, FALSE, FALSE, NULL)) == NULL || !backbuffers()) {
        SDL_Log("d3d12: no command list, fence or back buffers");
        goto fail;
    }
    ID3D12GraphicsCommandList_Close(DXS.list);
    boot_picture();
    {
        D3D12_QUERY_HEAP_DESC qd2 = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, QMAX, 0};

        ID3D12CommandQueue_GetTimestampFrequency(s_queue, &s_qfreq);
        DXS.qread = dx_buffer(D3D12_HEAP_TYPE_READBACK, (UINT64)FIF * QMAX * 8, D3D12_RESOURCE_STATE_COPY_DEST, &s_qptr);
        if (DXS.qread != NULL && !OK(ID3D12Device_CreateQueryHeap(DXS.dev, &qd2, &IID_ID3D12QueryHeap, (void **)&DXS.qheap))) {
            DXS.qheap = NULL;
        }
    }
    /* the blit's root: a table of one SRV, 12 constants (the texture rectangle, then a pass's own), two samplers */
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range, rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, rp[1].Constants.Num32BitValues = 12, rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (i = 0; i < 2; i++) {
        ss[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss[i].AddressU = ss[i].AddressV = ss[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss[i].ShaderRegister = (UINT)i, ss[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL, ss[i].MaxLOD = D3D12_FLOAT32_MAX;
    }
    rsd.NumParameters = 2, rsd.pParameters = rp, rsd.NumStaticSamplers = 2, rsd.pStaticSamplers = ss;
    DXS.blit_root = dx_root(&rsd);
    /* the main shader's root: 16 constants, a table of one SRV (the VRAM) */
    rp[0].DescriptorTable.pDescriptorRanges = &range;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, rp[1].Constants.Num32BitValues = 16, rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rsd.NumStaticSamplers = 0, rsd.pStaticSamplers = NULL;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    DXS.main_root = dx_root(&rsd);
    DXS.fsr3 = (g_fsr == 2 && fx_load()) || DXS.dlss;
    {
        const char *e = SDL_getenv("DW3_UPSCALE_DATA");

        DXS.data_mshow = e != NULL && SDL_strstr(e, "mshow") != NULL;
        DXS.data_motion = DXS.fsr3 || DXS.data_mshow || (e != NULL && SDL_strstr(e, "motion") != NULL); /* (FSR 3.1 is given the data) */
    }
    if (DXS.blit_root == NULL || DXS.main_root == NULL || !dx_compile(BLIT_HLSL, "vs", "vs_5_0", &bvs) || !dx_compile(BLIT_HLSL, "ps", "ps_5_0", &bps) ||
        !dx_compile(BLIT_HLSL, "psn", "ps_5_0", &bpsn) || !dx_compile(BLIT_HLSL, "psb", "ps_5_0", &bpsb) ||!dx_compile(MAIN_HLSL, "vs", "vs_5_0", &DXS.main_vs) || !dx_compile(MAIN_HLSL, "ps", "ps_5_0", &DXS.main_ps)) {
        goto fail;
    }
    if (DXS.data_motion && !dx_compile(MAIN_HLSL, "psm", "ps_5_0", &DXS.main_psm)) {
        DXS.data_motion = 0;
    }
    for (i = 0; i < 9; i++) {
        DXS.layout[i].SemanticName = L[i].name, DXS.layout[i].SemanticIndex = L[i].idx, DXS.layout[i].Format = L[i].fmt;
        DXS.layout[i].AlignedByteOffset = L[i].off, DXS.layout[i].InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
    }
    DXS.pso_present = blit_pso(bvs, bps, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    DXS.pso_copy = blit_pso(bvs, bps, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    DXS.pso_over = blit_pso(bvs, bps, 2, DXGI_FORMAT_R8G8B8A8_UNORM);
    DXS.pso_copyn = blit_pso(bvs, bpsn, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    DXS.pso_img = blit_pso(bvs, bps, 4, DXGI_FORMAT_R8G8B8A8_UNORM);
    DXS.pso_add = blit_pso(bvs, bps, 5, DXGI_FORMAT_R8G8B8A8_UNORM);
    for (i = 0; i < 6 * 3; i++) { /* all the main shader's states now: made in the middle of a fight they would be a hitch */
        if (dx_main_pso(i / 3 - 1, i % 3) == NULL) {
            goto fail;
        }
    }
    DXS.pso_blur = blit_pso(bvs, bpsb, 1, DXGI_FORMAT_R8_UNORM);
    DXS.pso_dark = blit_pso(bvs, bpsb, 3, DXGI_FORMAT_R8G8B8A8_UNORM);
    dx_post_init();
    {
        const char *e = SDL_getenv("DW3_UPSCALE_DATA");

        DXS.data_jitter = DXS.fsr3 || (e != NULL && SDL_strstr(e, "jitter") != NULL);
    }
    DXS.pso_mask[0] = dx_mask_pso(0);
    DXS.pso_mask[1] = dx_mask_pso(1);
    DXS.pic = dx_texture(VRAM_W, VRAM_H, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
    DXS.menu = dx_texture(MENU_W, MENU_H, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
    DXS.vram = dx_texture(VRAM_W, VRAM_H, DXGI_FORMAT_R16_UINT, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
    DXS.up_pic = dx_buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)VRAM_W * 4 * VRAM_H, D3D12_RESOURCE_STATE_GENERIC_READ, &DXS.up_pic_ptr);
    DXS.up_menu = dx_buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)((MENU_W * 4 + 255) & ~255) * MENU_H, D3D12_RESOURCE_STATE_GENERIC_READ, &DXS.up_menu_ptr);
    DXS.ring_res = dx_buffer(D3D12_HEAP_TYPE_UPLOAD, RING, D3D12_RESOURCE_STATE_GENERIC_READ, &DXS.ring);
    if (DXS.pso_present == NULL || DXS.pso_copy == NULL || DXS.pso_copyn == NULL || DXS.pso_over == NULL || DXS.pso_blur == NULL || DXS.pso_dark == NULL || DXS.pso_img == NULL || DXS.pso_add == NULL ||DXS.pso_mask[0] == NULL || DXS.pso_mask[1] == NULL || DXS.pic.r == NULL || DXS.menu.r == NULL || DXS.vram.r == NULL ||
        DXS.up_pic == NULL || DXS.up_menu == NULL || DXS.ring_res == NULL) {
        SDL_Log("d3d12: resources");
        goto fail;
    }
    dx_srv(&DXS.pic, S_PIC, DXGI_FORMAT_R8G8B8A8_UNORM);
    dx_srv(&DXS.menu, S_MENU, DXGI_FORMAT_R8G8B8A8_UNORM);
    dx_srv(&DXS.vram, S_VRAM, DXGI_FORMAT_R16_UINT);
    if (DXS.fsr3) {
        gte_motion_enable(); /* (here, once nothing can fail: OpenGL taking over doesn't make the motion) */
    }
    rel(&bvs), rel(&bps), rel(&bpsn), rel(&bpsb), rel(&sc), rel(&fac); /* (the PSOs and DXS.swap hold what they need) */
    DXS.ready = 1;
    SDL_Log("d3d12: ready, %dx%d, in %.0f ms (shaders compiled in %.0f)", DXS.w, DXS.h, (SDL_GetTicksNS() - t_init) / 1e6, DXS.compile_ns / 1e6);
    return 1;
fail: /* what was made let go, Streamline first (sc and fac may be its proxies): OpenGL takes over (video.c) */
    SDL_Log("d3d12: the start failed, its resources released");
    wait_idle();
    sl_stop();
    rel(&bvs), rel(&bps), rel(&bpsn), rel(&bpsb), rel(&sc), rel(&fac);
    dx_release();
    return 0;
}

void dx_resize(int w, int h) {
    int i;

    dx_submit();
    wait_idle();
    for (i = 0; i < FRAMES; i++) {
        ID3D12Resource_Release(DXS.back[i]);
    }
    DXS.w = w, DXS.h = h;
    IDXGISwapChain3_ResizeBuffers(DXS.swap, FRAMES, (UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    backbuffers();
}

/* DLSS: -1 not here (no DLLs, or a GPU that isn't an RTX), 0 after a restart, 1 on, 2 the Resolution is too high for it, 3 running (as dx_fsr3_status) */
int dx_dlss_status(void) {
    if (sl_state() < 0 || (sl_state() == 0 && !sl_files_here())) {
        return -1;
    }
    return !DXS.dlss ? 0 : DXS.f3_state != 0 ? DXS.f3_state : 1;
}

int dx_fsr3_status(void) {
    if (!fx_load()) {
        return -1;
    }
    return !DXS.fsr3 ? 0 : DXS.f3_state != 0 ? DXS.f3_state : 1;
}

int dx_scale(void) {
    return SDL_max(DXS.scale, 1);
}

#define X(dll, f) __typeof__(f) *p_##f;
DX_FUNCS(X)
#undef X

/* The system's DLLs and their calls there, and a device that can be made */
int dx_available(void) {
#define X(dll, f)                                                                         \
    if ((p_##f = (__typeof__(f) *)SDL_LoadFunction(SDL_LoadObject(dll), #f)) == NULL) { \
        SDL_Log("d3d12: no %s in %s", #f, dll);                                           \
        return 0;                                                                         \
    }
    DX_FUNCS(X)
#undef X
    return SUCCEEDED(p_D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, NULL));
}

const struct Gfx GFX_D3D12 = GFX_TABLE(dx, "D3D12");

struct DxState DXS = DXS_INIT;
