/* The Direct3D 12 renderer's drawing of the game's primitives: the batch, the battle shadows, refreshes and copies */
#include "dx.h"

static void quad_sc(int slot, const int vp[4], const float uv[4], ID3D12PipelineState *pso, const D3D12_RECT *scr);

static void draw_batch(int semi, int depth, int pass, const Consts *k0) {
    Consts k = *k0;
    static const float half[4] = {0.5f, 0.5f, 0.5f, 0.5f}, quarter[4] = {0.25f, 0.25f, 0.25f, 0.25f};

    k.pass = pass;
    k.pad = 0; /* (the shaders' flags, none in use) */
    ID3D12GraphicsCommandList_SetPipelineState(DXS.list, dx_main_pso(semi, depth));
    ID3D12GraphicsCommandList_OMSetBlendFactor(DXS.list, semi == 0 ? half : quarter);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(DXS.list, 1, 16, &k, 0);
    ID3D12GraphicsCommandList_DrawInstanced(DXS.list, (UINT)DXS.count, 1, 0, 0);
}

/* The batch, in its two passes when some of it is semi-transparent: the opaque pixels first, then the blended ones over them */
static void flush_batch(void) {
    int s = DXS.scale;
    UINT off, bytes = (UINT)(DXS.count * sizeof(GVtx));
    UINT8 *p;
    D3D12_VERTEX_BUFFER_VIEW vb;
    D3D12_VIEWPORT vp = {0, 0, (float)(VRAM_W * s), (float)(VRAM_H * s), 0, 1};
    D3D12_RECT sc = {DXS.clip[0] * s, DXS.clip[1] * s, (DXS.clip[2] + 1) * s, (DXS.clip[3] + 1) * s};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx_rtv_at(FRAMES), dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DXS.dsv_heap);
    D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DXS.srv_heap);
    Consts k = {{VRAM_W, VRAM_H}, {0, 0}, {0, 0, 0, 0}, {0, 0}, (float)s, g_perspective, 0, g_true_color, g_smooth_3d, 0};

    if (DXS.count == 0) {
        return;
    }
    if (DXS.semi == 5 && DXS.mask.r == NULL) {
        DXS.count = 0, DXS.semi = -1;
        return;
    }
    k.jit[0] = DXS.jit_px[0] * 2.0f / (VRAM_W * s), k.jit[1] = -DXS.jit_px[1] * 2.0f / (VRAM_H * s); /* (clip y runs up) */
    k.fog[0] = DXS.fog[0], k.fog[1] = DXS.fog[1], k.fog[2] = DXS.fog[2], k.fog[3] = DXS.fog_shown && DXS.zfar > 0 ? DXS.fog_amount : 0.0f;
    k.fogz[0] = DXS.zfar * DXS.fog_from, k.fogz[1] = DXS.zfar;
    dx_list();
    p = dx_ring_alloc(bytes, &off);
    SDL_memcpy(p, DXS.batch, bytes);
    dx_trans(&DXS.vram, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    vb.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(DXS.ring_res) + off, vb.SizeInBytes = bytes, vb.StrideInBytes = sizeof(GVtx);
    if (DXS.semi == 5) { /* a battle shadow: into the mask (dx_shadows_resolve) */
        D3D12_CPU_DESCRIPTOR_HANDLE mrtv = dx_rtv_at(R_MASK);

        dx_trans(&DXS.mask, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &mrtv, FALSE, &dsv);
        ID3D12GraphicsCommandList_RSSetViewports(DXS.list, 1, &vp);
        ID3D12GraphicsCommandList_RSSetScissorRects(DXS.list, 1, &sc);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(DXS.list, DXS.main_root);
        g.ptr += (UINT64)S_VRAM * DXS.srv_inc;
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(DXS.list, 0, g);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(DXS.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(DXS.list, 0, 1, &vb);
        k.pass = 2;
        ID3D12GraphicsCommandList_SetPipelineState(DXS.list, DXS.pso_mask[DXS.deep ? 1 : 0]);
        ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(DXS.list, 1, 16, &k, 0);
        ID3D12GraphicsCommandList_DrawInstanced(DXS.list, (UINT)DXS.count, 1, 0, 0);
        DXS.count = 0;
        DXS.semi = -1;
        return;
    }
    dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    if (DXS.data_motion && DXS.mv.r != NULL) {
        D3D12_CPU_DESCRIPTOR_HANDLE two[2];

        two[0] = rtv, two[1] = dx_rtv_at(R_MV);
        if (DXS.deep && (DXS.clip[2] - DXS.clip[0] + 1) * (DXS.clip[3] - DXS.clip[1] + 1) > DXS.mv_area) { /* the biggest 3D (not a small window's, like the portrait) */
            DXS.mv_org[0] = DXS.clip[0], DXS.mv_org[1] = DXS.clip[1], DXS.mv_org_set = 1;
            DXS.mv_area = (DXS.clip[2] - DXS.clip[0] + 1) * (DXS.clip[3] - DXS.clip[1] + 1);
        }
        dx_trans(&DXS.mv, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (DXS.mv_clear) { /* the first draw of a frame: no motion yet */
            const float zero[4] = {0, 0, 0, 0};

            ID3D12GraphicsCommandList_ClearRenderTargetView(DXS.list, two[1], zero, 0, NULL);
            DXS.mv_clear = 0;
        }
        ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 2, two, FALSE, &dsv);
    } else {
        ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &rtv, FALSE, &dsv);
    }
    ID3D12GraphicsCommandList_RSSetViewports(DXS.list, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(DXS.list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(DXS.list, DXS.main_root);
    g.ptr += (UINT64)S_VRAM * DXS.srv_inc;
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(DXS.list, 0, g);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(DXS.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D12GraphicsCommandList_IASetVertexBuffers(DXS.list, 0, 1, &vb);
    DXS.depth_used |= DXS.deep;
    draw_batch(-1, DXS.deep ? 1 : 0, DXS.semi < 0 ? 0 : 1, &k);
    if (DXS.semi >= 0) { /* the blended pixels test the depth, they don't hide what is behind */
        draw_batch(DXS.semi, DXS.deep ? 2 : 0, 2, &k);
    }
    DXS.count = 0;
    DXS.semi = -1;
}

void dx_flush(void) {
    int prev;

    if (DXS.count == 0) {
        return;
    }
    dx_list();
    prev = dx_mark(DXS.semi == 5 ? C_MASK : DXS.deep ? C_3D : C_2D);
    flush_batch();
    dx_mark(prev);
}

/* Triangles (n a multiple of 3) for the copy, with gpu_draw.c's state: the drawing area, the semi-transparency mode (4: a soft shadow's) */
void dx_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {
    int semi_prim = (v[0].mode & 8) != 0, deep = v[0].z > 0;

    if (DXS.scale == 0) {
        return;
    }
    if (DXS.count + n > BATCH || deep != DXS.deep || x1 != DXS.clip[0] || y1 != DXS.clip[1] || x2 != DXS.clip[2] || y2 != DXS.clip[3] ||
        (semi_prim && DXS.semi >= 0 && semi != DXS.semi) || (!semi_prim && DXS.semi >= 0) ||
        (semi == 5 && DXS.semi != 5 && DXS.count > 0)) { /* battle shadows go to the mask: a batch of their own */
        dx_flush();
        DXS.clip[0] = x1, DXS.clip[1] = y1, DXS.clip[2] = x2, DXS.clip[3] = y2;
        DXS.deep = deep;
    }
    if (v[0].mode & 2) {
        /* the page and the CLUT as the card has them: what the CPU drew there since (a texture drawn to) goes up first */
        uint16_t need[8] = {0};
        int depth = v[0].mode >> 4 & 3;

        dx_need_rect(need, v[0].tx, v[0].ty, 64 << depth, 256);
        dx_need_rect(need, v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        soft_read(v[0].tx, v[0].ty, 64 << depth, 256);
        soft_read(v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        if ((need[0] & g_vram_dirty[0]) | (need[1] & g_vram_dirty[1]) | (need[2] & g_vram_dirty[2]) | (need[3] & g_vram_dirty[3]) |
            (need[4] & g_vram_dirty[4]) | (need[5] & g_vram_dirty[5]) | (need[6] & g_vram_dirty[6]) | (need[7] & g_vram_dirty[7])) {
            dx_flush();
            dx_upload_blocks(need);
        }
    }
    if (semi_prim) {
        DXS.semi = semi;
    } else if (v[0].w > 0 && !(v[0].mode & 2048)) { /* the fog's range: the farthest opaque 3D */
        int i;

        for (i = 0; i < n; i++) {
            DXS.zframe = SDL_max(DXS.zframe, v[i].w);
        }
    }
    if (semi == 5) { /* where the mask has shadows */
        int i;

        for (i = 0; i < n; i++) {
            DXS.mask_box[0] = SDL_min(DXS.mask_box[0], (int)v[i].x);
            DXS.mask_box[1] = SDL_min(DXS.mask_box[1], (int)v[i].y);
            DXS.mask_box[2] = SDL_max(DXS.mask_box[2], (int)v[i].x + 1);
            DXS.mask_box[3] = SDL_max(DXS.mask_box[3], (int)v[i].y + 1);
        }
    }
    SDL_memcpy(&DXS.batch[DXS.count], v, n * sizeof(GVtx));
    DXS.count += n;
}

/* An ordering table drawn: the battle shadows in the mask, blurred, darken the copy, and the mask is emptied */
static void shadows_resolve(void);

void dx_shadows_resolve(void) {
    int prev = dx_mark(C_RESOLVE);

    shadows_resolve();
    dx_mark(prev);
}

static void shadows_resolve(void) {
    int s = DXS.scale, step = SDL_max(1, s * 3 / 4), pad = 5 * step, r[4];
    int full[4];
    float across[4], down[4];
    static const float zero[4] = {0, 0, 0, 0};
    D3D12_RECT box;

    if (s == 0 || DXS.mask_box[2] < 0 || DXS.mask.r == NULL) {
        return;
    }
    dx_flush();
    r[0] = SDL_max(0, DXS.mask_box[0] * s - pad);
    r[1] = SDL_max(0, DXS.mask_box[1] * s - pad);
    r[2] = SDL_min(VRAM_W * s, DXS.mask_box[2] * s + pad) - r[0];
    r[3] = SDL_min(VRAM_H * s, DXS.mask_box[3] * s + pad) - r[1];
    DXS.mask_box[0] = VRAM_W, DXS.mask_box[1] = VRAM_H, DXS.mask_box[2] = DXS.mask_box[3] = -1;
    if (r[2] <= 0 || r[3] <= 0) {
        return;
    }
    box.left = r[0], box.top = r[1], box.right = r[0] + r[2], box.bottom = r[1] + r[3];
    full[0] = full[1] = 0, full[2] = VRAM_W * s, full[3] = VRAM_H * s;
    across[0] = (float)step, across[1] = 0, across[2] = 0, across[3] = 0;
    down[0] = 0, down[1] = (float)step, down[2] = 0.5f, down[3] = 0;
    dx_list();
    /* across: the mask into the blur */
    dx_trans(&DXS.mask, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_trans(&DXS.blur, D3D12_RESOURCE_STATE_RENDER_TARGET);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE b = dx_rtv_at(R_BLUR);

        ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &b, FALSE, NULL);
    }
    quad_sc(S_MASK, full, across, DXS.pso_blur, &box);
    /* down, darkening the copy: back * (1 - front) */
    dx_trans(&DXS.blur, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    {
        D3D12_CPU_DESCRIPTOR_HANDLE c = dx_rtv_at(R_RT);

        ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &c, FALSE, NULL);
    }
    quad_sc(S_BLUR, full, down, DXS.pso_dark, &box);
    /* empty for the next */
    dx_trans(&DXS.mask, D3D12_RESOURCE_STATE_RENDER_TARGET);
    dx_trans(&DXS.blur, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_ClearRenderTargetView(DXS.list, dx_rtv_at(R_MASK), zero, 1, &box);
    ID3D12GraphicsCommandList_ClearRenderTargetView(DXS.list, dx_rtv_at(R_BLUR), zero, 1, &box);
}

static void depth_clear(void);

void dx_depth_clear(void) {
    int prev = dx_mark(C_DEPTH);

    depth_clear();
    dx_mark(prev);
}

static void depth_clear(void) {
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DXS.dsv_heap);

    if (DXS.scale == 0 || DXS.depth.r == NULL) {
        return;
    }
    dx_flush();
    if (!DXS.depth_used) { /* nothing has written depth since the last clear */
        return;
    }
    DXS.depth_used = 0;
    dx_list();
    ID3D12GraphicsCommandList_ClearDepthStencilView(DXS.list, dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
}

/* A quad of the copy that shows the 1x VRAM under it: after the CPU wrote VRAM (an image, a fill) */
static void refresh(int x, int y, int w, int h);

void dx_refresh(int x, int y, int w, int h) {
    int prev = dx_mark(C_REFRESH);

    refresh(x, y, w, h);
    dx_mark(prev);
}

static void refresh(int x, int y, int w, int h) {
    GVtx q[6];
    int i;

    if (DXS.scale == 0 || w <= 0 || h <= 0) {
        return;
    }
    dx_flush();
    if (x + w > VRAM_W) w = VRAM_W - x;
    if (y + h > VRAM_H) h = VRAM_H - y;
    dx_upload(x, y, w, h);
    SDL_memset(q, 0, sizeof q);
    for (i = 0; i < 6; i++) {
        static const int CX[6] = {0, 1, 0, 1, 1, 0}, CY[6] = {0, 0, 1, 0, 1, 1};

        q[i].x = (float)(x + CX[i] * w);
        q[i].y = (float)(y + CY[i] * h);
        q[i].mode = 128;
    }
    SDL_memcpy(DXS.batch, q, sizeof q);
    DXS.count = 6;
    DXS.clip[0] = 0, DXS.clip[1] = 0, DXS.clip[2] = 1023, DXS.clip[3] = 511;
    DXS.deep = 0;
    dx_flush();
}

/* A battle's sky drawn (gpu_draw.c), its colour at the horizon the fog's: the stage's background colour (the OpenGL renderer has the notes) */
void dx_battle_fog(int r, int g, int b) {
    DXS.bg[0] = r / 8.0f, DXS.bg[1] = g / 8.0f, DXS.bg[2] = b / 8.0f;
    DXS.fog_on = 1;
}

/* A rectangle of tex (its slot in the SRV heap) onto the viewport vp (pixels, from the top left) */
void dx_quad(int slot, const int vp[4], const float uv[4], ID3D12PipelineState *pso) {
    quad_sc(slot, vp, uv, pso, NULL);
}

/* The same cut to the rectangle sc (the viewport's when NULL) */
static void quad_sc(int slot, const int vp[4], const float uv[4], ID3D12PipelineState *pso, const D3D12_RECT *scr) {
    D3D12_VIEWPORT v = {(float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3], 0, 1};
    D3D12_RECT sc = {vp[0], vp[1], vp[0] + vp[2], vp[1] + vp[3]};
    D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DXS.srv_heap);

    if (scr != NULL) {
        sc = *scr;
    }
    g.ptr += (UINT64)slot * DXS.srv_inc;
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(DXS.list, DXS.blit_root);
    ID3D12GraphicsCommandList_SetPipelineState(DXS.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(DXS.list, 1, &v);
    ID3D12GraphicsCommandList_RSSetScissorRects(DXS.list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(DXS.list, 0, g);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(DXS.list, 1, 4, uv, 0);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(DXS.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawInstanced(DXS.list, 4, 1, 0, 0);
}

/* VRAM to VRAM, in the copy */
void dx_copy(int sx, int sy, int w, int h, int dx, int dy) {
    dx_copy_to(sx, sy, w, h, (float)dx, dy, (float)w);
}

/* The same, to dw pixels across from dx (a widescreen battle's menus) */
static void copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw);

void dx_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) {
    int prev = dx_mark(C_COPY);

    copy_to(sx, sy, w, h, dx, dy, dw);
    dx_mark(prev);
}

static void copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) {
    int s = DXS.scale, x0, x1;
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    D3D12_BOX box;

    if (s == 0 || w <= 0 || h <= 0) {
        return;
    }
    dx_flush();
    if (sx + w > VRAM_W) w = VRAM_W - sx;
    if (dx + dw > VRAM_W) dw = VRAM_W - dx;
    if (sy + h > VRAM_H) h = VRAM_H - sy;
    if (dy + h > VRAM_H) h = VRAM_H - dy;
    x0 = (int)(dx * s + 0.5f), x1 = (int)((dx + dw) * s + 0.5f);
    dx_list();
    dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dx_trans(&DXS.tmp, D3D12_RESOURCE_STATE_COPY_DEST);
    dst.pResource = DXS.tmp.r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = DXS.rt.r, src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    box.left = (UINT)(sx * s), box.top = (UINT)(sy * s), box.right = (UINT)((sx + w) * s), box.bottom = (UINT)((sy + h) * s), box.front = 0, box.back = 1;
    ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, &box);
    if (x1 - x0 == w * s) { /* the same width: a plain copy back */
        dx_trans(&DXS.tmp, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_COPY_DEST);
        dst.pResource = DXS.rt.r, src.pResource = DXS.tmp.r;
        box.left = 0, box.top = 0, box.right = (UINT)(w * s), box.bottom = (UINT)(h * s);
        ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, (UINT)x0, (UINT)(dy * s), 0, &src, &box);
    } else { /* stretched across, by the nearest texel */
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx_rtv_at(FRAMES);
        int vp[4] = {x0, dy * s, x1 - x0, h * s};
        float uv[4] = {0, 0, w * s / (float)(VRAM_W * s), h * s / (float)(VRAM_H * s)};

        dx_trans(&DXS.tmp, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
        ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &rtv, FALSE, NULL);
        dx_quad(S_TMP, vp, uv, DXS.pso_copyn);
    }
}
