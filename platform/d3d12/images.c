/* The Direct3D 12 renderer's images of the port's own (icons, effects); props are not drawn here yet */
#include "dx.h"

/* what the card does not draw yet: props, images */

static Tex s_imgs[MAX_IMG];

static int s_nimg;

/* An image's texture, made on first use (*tex is its number + 1): premultiplied, so that filtering doesn't bleed the transparent
   texels' colour into the edges, and mipmapped (a box filter), so that a big image shrunk to the picture keeps its detail */
static int image_tex(const SDL_Surface *img, unsigned *tex) {
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT, D3D12_CPU_PAGE_PROPERTY_UNKNOWN, D3D12_MEMORY_POOL_UNKNOWN, 1, 1};
    D3D12_RESOURCE_DESC rd = {D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 0, 0, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, 0};
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {0};
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    Tex t = {NULL, D3D12_RESOURCE_STATE_COPY_DEST};
    uint8_t *cur;
    int w = img->w, h2 = img->h, levels = 1, lv, i;

    Uint64 t0 = SDL_GetTicksNS();

    if (*tex != 0) {
        return 1;
    }
    if (s_nimg >= MAX_IMG) {
        return 0;
    }
    for (i = SDL_max(w, h2); i > 1; i >>= 1) {
        levels++;
    }
    cur = SDL_malloc((size_t)w * h2 * 4);
    if (cur == NULL || !SDL_PremultiplyAlpha(w, h2, img->format, img->pixels, img->pitch, SDL_PIXELFORMAT_RGBA32, cur, w * 4, false)) {
        SDL_free(cur);
        return 0;
    }
    rd.Width = (UINT64)w, rd.Height = (UINT)h2, rd.MipLevels = (UINT16)levels;
    if (!OK(ID3D12Device_CreateCommittedResource(DXS.dev, &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, NULL, &IID_ID3D12Resource, (void **)&t.r))) {
        SDL_free(cur);
        return 0;
    }
    dx_list();
    for (lv = 0; lv < levels; lv++) {
        UINT pitch = (UINT)((w * 4 + 255) & ~255), off;
        UINT8 *p = dx_ring_alloc(pitch * (UINT)h2, &off);
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        int nw = SDL_max(1, w / 2), nh = SDL_max(1, h2 / 2), x, y, c;
        uint8_t *nxt = lv + 1 < levels ? SDL_malloc((size_t)nw * nh * 4) : NULL;

        for (y = 0; y < h2; y++) {
            SDL_memcpy(p + (size_t)y * pitch, cur + (size_t)y * w * 4, (size_t)w * 4);
        }
        dst.pResource = t.r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX, dst.SubresourceIndex = (UINT)lv;
        src.pResource = DXS.ring_res, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, src.PlacedFootprint.Offset = off;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM, src.PlacedFootprint.Footprint.Width = (UINT)w;
        src.PlacedFootprint.Footprint.Height = (UINT)h2, src.PlacedFootprint.Footprint.Depth = 1, src.PlacedFootprint.Footprint.RowPitch = pitch;
        ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, NULL);
        if (nxt != NULL) {
            for (y = 0; y < nh; y++) {
                for (x = 0; x < nw; x++) {
                    int x1 = SDL_min(x * 2 + 1, w - 1), y1 = SDL_min(y * 2 + 1, h2 - 1);

                    for (c = 0; c < 4; c++) {
                        nxt[((size_t)y * nw + x) * 4 + c] = (uint8_t)((cur[((size_t)y * 2 * w + x * 2) * 4 + c] + cur[((size_t)y * 2 * w + x1) * 4 + c] +
                                                                    cur[((size_t)y1 * w + x * 2) * 4 + c] + cur[((size_t)y1 * w + x1) * 4 + c] + 2) / 4);
                    }
                }
            }
        }
        SDL_free(cur);
        cur = nxt, w = nw, h2 = nh;
    }
    dx_trans(&t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    s_imgs[s_nimg] = t;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM, sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING, sd.Texture2D.MipLevels = (UINT)levels;
    h = ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DXS.srv_heap);
    h.ptr += (SIZE_T)(S_COUNT + s_nimg) * DXS.srv_inc;
    ID3D12Device_CreateShaderResourceView(DXS.dev, t.r, &sd, h);
    *tex = (unsigned)++s_nimg;
    DXS.img_ns += SDL_GetTicksNS() - t0, DXS.nimg_made++;
    return 1;
}

/* An image's texture made ahead of its first use (effects.c's warm-up), not in the frame it first shows */
int dx_image_prepare(const SDL_Surface *img, unsigned *tex) {
    return DXS.scale != 0 && img != NULL && image_tex(img, tex); /* 0: the card isn't up yet */
}

/* An image of the port's own over the copy (platform.h): premultiplied over, or added; cut to the drawing area */
static void image_draw(const SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2);

void dx_image(const SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2) {
    int prev = dx_mark(C_IMAGE);

    image_draw(img, tex, x0, y0, x1, y1, add, cx1, cy1, cx2, cy2);
    dx_mark(prev);
}

static void image_draw(const SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2) {
    int s = DXS.scale;
    D3D12_VIEWPORT v;
    D3D12_RECT sc = {cx1 * s, cy1 * s, (cx2 + 1) * s, (cy2 + 1) * s};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx_rtv_at(R_RT);
    D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DXS.srv_heap);
    static const float uv[4] = {0, 0, 1, 1};

    if (s == 0 || x1 <= x0 || y1 <= y0) {
        return;
    }
    dx_flush();
    if (!image_tex(img, tex)) {
        return;
    }
    dx_list();
    dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_RENDER_TARGET);
    v.TopLeftX = x0 * s, v.TopLeftY = y0 * s, v.Width = (x1 - x0) * s, v.Height = (y1 - y0) * s, v.MinDepth = 0, v.MaxDepth = 1;
    g.ptr += (UINT64)(S_COUNT + (int)*tex - 1) * DXS.srv_inc;
    ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(DXS.list, DXS.blit_root);
    ID3D12GraphicsCommandList_SetPipelineState(DXS.list, add ? DXS.pso_add : DXS.pso_img);
    ID3D12GraphicsCommandList_RSSetViewports(DXS.list, 1, &v);
    ID3D12GraphicsCommandList_RSSetScissorRects(DXS.list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(DXS.list, 0, g);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(DXS.list, 1, 4, uv, 0);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(DXS.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawInstanced(DXS.list, 4, 1, 0, 0);
}

void dx_props(const SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)v, (void)n, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}

unsigned dx_inst_make(const PInst *v, int n) { (void)v, (void)n; return 0; }

void dx_inst_free(unsigned b) { (void)b; }

unsigned dx_buf_make(const void *p, unsigned long bytes) { (void)p, (void)bytes; return 0; }

void dx_mesh_make(GMesh *g, const float *v, int n) { (void)v, (void)n; g->n = 0; }

void dx_props_model(const SDL_Surface *img, unsigned *tex, const GMesh *g, float miny, float h, unsigned inst, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)g, (void)miny, (void)h, (void)inst, (void)first, (void)n, (void)cam, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}

void dx_props_inst(const SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)b, (void)first, (void)n, (void)cam, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}

void dx_props_part(const SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    (void)img, (void)tex, (void)p, (void)cam, (void)x1, (void)y1, (void)x2, (void)y2, (void)fogo;
}
