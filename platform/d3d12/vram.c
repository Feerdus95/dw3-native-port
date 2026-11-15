/* The Direct3D 12 renderer's VRAM on the card: uploads of what the CPU wrote, and the copy's size (render_scale) */
#include "dx.h"

/* --- VRAM on the card --- */

/* A rect of the 1x VRAM onto the card */
void dx_upload(int x, int y, int w, int h) {
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    UINT pitch = (UINT)((w * 2 + 255) & ~255), off;
    UINT8 *p;
    int r;

    DXS.nup++;
    dx_list();
    p = dx_ring_alloc(pitch * (UINT)h, &off);
    for (r = 0; r < h; r++) {
        SDL_memcpy(p + (size_t)r * pitch, &g_vram[(y + r) * VRAM_W + x], (size_t)w * 2);
    }
    dst.pResource = DXS.vram.r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = DXS.ring_res, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = off;
    src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16_UINT;
    src.PlacedFootprint.Footprint.Width = (UINT)w, src.PlacedFootprint.Footprint.Height = (UINT)h, src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = pitch;
    dx_trans(&DXS.vram, D3D12_RESOURCE_STATE_COPY_DEST);
    ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, (UINT)x, (UINT)y, 0, &src, NULL);
}

/* The dirty blocks among `need` (a mask per block row), as runs of a row */
void dx_upload_blocks(const uint16_t need[8]) {
    int row, col;

    for (row = 0; row < 8; row++) {
        uint16_t bits = need[row] & g_vram_dirty[row];

        for (col = 0; col < 16; col++) {
            if (bits >> col & 1) {
                int start = col;

                while (col < 16 && (bits >> col & 1)) {
                    col++;
                }
                dx_upload(start * 64, row * 64, (col - start) * 64, 64);
            }
        }
        g_vram_dirty[row] &= ~need[row];
    }
}

void dx_need_rect(uint16_t need[8], int x, int y, int w, int h) {
    int r, c;

    for (r = (y & 511) >> 6; r <= ((y + h - 1) & 511) >> 6 && r < 8; r++) {
        for (c = (x & 1023) >> 6; c <= ((x + w - 1) & 1023) >> 6 && c < 16; c++) {
            need[r] |= (uint16_t)(1u << c);
        }
    }
}

void dx_vram_dirty(int x, int y, int w, int h) {
    dx_need_rect(g_vram_dirty, x, y, w, h);
}

/* 1 when FSR 3.1 or DLSS is chosen (and was at the start): then the upscaler mode, not Resolution, sets the render */
int dx_scale_tied(void) {
    return (g_fsr == 2 && DXS.fsr3 && !DXS.dlss && fx_loaded()) || (g_fsr == 3 && DXS.dlss && sl_state() > 0);
}

/* The render scale: Resolution's, or with an upscaler its mode's share of the picture's height (100, 67, 58, 50 %) in whole 240 rows, the nearest,
   never above the picture's own (the render is a whole number of the game's rows: a share in between rounds) */
static int render_scale(void) {
    static const float SHARE[4] = {1.0f, 0.667f, 0.58f, 0.5f};
    int pic = DXS.w * 3 >= DXS.h * 4 ? DXS.h : DXS.w * 3 / 4, top = SDL_max(1, pic / 240); /* (the picture is 4:3 in the window, at most) */

    if (!dx_scale_tied() || pic <= 0) {
        return SDL_clamp(g_res_scale, 1, MAX_SCALE);
    }
    return SDL_clamp((int)(pic * SHARE[SDL_clamp(g_up_mode, 0, 3)] / 240.0f + 0.5f), 1, SDL_min(top, MAX_SCALE));
}

/* The render scale in use, made to follow render_scale: the copy, its depth, the temporary of the copies */
void dx_sync(int force) {
    static int s_cap = MAX_SCALE; /* below the scale the card couldn't hold */
    int want = SDL_min(render_scale(), s_cap);

    if (!DXS.ready || g_vram == NULL) { /* VRAM comes with ResetGraph */
        return;
    }
    if (want != DXS.scale) {
        D3D12_CLEAR_VALUE cc = {DXGI_FORMAT_R8G8B8A8_UNORM, {{0, 0, 0, 0}}}, cd = {DXGI_FORMAT_D32_FLOAT, {{0}}};
        const float zero[4] = {0, 0, 0, 0};

        dx_flush();
        dx_submit();
        if (DXS.rt.r != NULL) {
            ID3D12Resource_Release(DXS.rt.r);
            ID3D12Resource_Release(DXS.tmp.r);
            ID3D12Resource_Release(DXS.depth.r);
        }
        if (DXS.mv.r != NULL) {
            ID3D12Resource_Release(DXS.mv.r);
            DXS.mv.r = NULL;
        }
        if (DXS.mask.r != NULL) {
            ID3D12Resource_Release(DXS.mask.r);
            ID3D12Resource_Release(DXS.blur.r);
            DXS.mask.r = DXS.blur.r = NULL;
        }
        if (DXS.sd.r != NULL) {
            ID3D12Resource_Release(DXS.sd.r);
            ID3D12Resource_Release(DXS.fd.r);
            DXS.sd.r = DXS.fd.r = NULL;
        }
        DXS.mask_box[0] = VRAM_W, DXS.mask_box[1] = VRAM_H, DXS.mask_box[2] = DXS.mask_box[3] = -1;
        cd.DepthStencil.Depth = 1.0f;
        for (;; want--) { /* a scale the card can't hold: the next one down, not a black picture */
            DXS.rt = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc);
            DXS.tmp = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
            DXS.depth = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R32_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cd);
            if (DXS.rt.r != NULL && DXS.tmp.r != NULL && DXS.depth.r != NULL) {
                break;
            }
            SDL_Log("d3d12: the copy at scale %d", want);
            if (DXS.rt.r != NULL) ID3D12Resource_Release(DXS.rt.r);
            if (DXS.tmp.r != NULL) ID3D12Resource_Release(DXS.tmp.r);
            if (DXS.depth.r != NULL) ID3D12Resource_Release(DXS.depth.r);
            DXS.rt.r = DXS.tmp.r = DXS.depth.r = NULL;
            s_cap = SDL_max(want - 1, 1);
            if (want == 1) {
                DXS.scale = 0; /* tried again next frame */
                return;
            }
        }
        DXS.scale = want;
        cc.Format = DXGI_FORMAT_R8_UNORM;
        DXS.mask = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc);
        DXS.blur = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &cc);
        if (DXS.mask.r == NULL || DXS.blur.r == NULL) {
            SDL_Log("d3d12: the shadows' mask at scale %d", want); /* the shadows are left out: flush_batch drops them */
            if (DXS.mask.r != NULL) ID3D12Resource_Release(DXS.mask.r);
            if (DXS.blur.r != NULL) ID3D12Resource_Release(DXS.blur.r);
            DXS.mask.r = DXS.blur.r = NULL;
        } else {
            ID3D12Device_CreateRenderTargetView(DXS.dev, DXS.mask.r, NULL, dx_rtv_at(R_MASK));
            ID3D12Device_CreateRenderTargetView(DXS.dev, DXS.blur.r, NULL, dx_rtv_at(R_BLUR));
            dx_srv(&DXS.mask, S_MASK, DXGI_FORMAT_R8_UNORM);
            dx_srv(&DXS.blur, S_BLUR, DXGI_FORMAT_R8_UNORM);
        }
        ID3D12Device_CreateRenderTargetView(DXS.dev, DXS.rt.r, NULL, dx_rtv_at(FRAMES));
        if (DXS.data_motion) {
            D3D12_CLEAR_VALUE cm = {DXGI_FORMAT_R16G16B16A16_FLOAT, {{0, 0, 0, 0}}};

            DXS.mv = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, &cm);
            if (DXS.mv.r != NULL) {
                ID3D12Device_CreateRenderTargetView(DXS.dev, DXS.mv.r, NULL, dx_rtv_at(R_MV));
                dx_srv(&DXS.mv, S_MV, DXGI_FORMAT_R16G16B16A16_FLOAT);
            } else {
                SDL_Log("d3d12: the motion target at scale %d", want);
            }
            DXS.mv_clear = 1;
        }
        {
            D3D12_DEPTH_STENCIL_VIEW_DESC dv = {DXGI_FORMAT_D32_FLOAT, D3D12_DSV_DIMENSION_TEXTURE2D, D3D12_DSV_FLAG_NONE};

            ID3D12Device_CreateDepthStencilView(DXS.dev, DXS.depth.r, &dv, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DXS.dsv_heap));
        }
        if (DXS.data_depth) { /* what the upscalers are given: the buffer's depth read, kept per table */
            D3D12_CLEAR_VALUE cf = {DXGI_FORMAT_R32_FLOAT, {{1, 0, 0, 0}}};

            DXS.sd = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cf);
            DXS.fd = dx_texture(VRAM_W * want, VRAM_H * want, DXGI_FORMAT_R32_FLOAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cf);
            if (DXS.sd.r != NULL && DXS.fd.r != NULL) {
                ID3D12Device_CreateRenderTargetView(DXS.dev, DXS.sd.r, NULL, dx_rtv_at(R_SD));
                ID3D12Device_CreateRenderTargetView(DXS.dev, DXS.fd.r, NULL, dx_rtv_at(R_FD));
                dx_srv(&DXS.sd, S_SD, DXGI_FORMAT_R32_FLOAT);
                dx_srv(&DXS.fd, S_FD, DXGI_FORMAT_R32_FLOAT);
                dx_srv(&DXS.sd, S_TD, DXGI_FORMAT_R32_FLOAT);
                dx_srv(&DXS.fd, S_TD + 1, DXGI_FORMAT_R32_FLOAT);
                dx_srv(&DXS.fd, S_TD + 2, DXGI_FORMAT_R32_FLOAT);
                dx_srv(&DXS.depth, S_DSRC, DXGI_FORMAT_R32_FLOAT);
            }
        }
        DXS.sd_valid = DXS.fd_valid = 0;
        dx_srv(&DXS.rt, S_RT, DXGI_FORMAT_R8G8B8A8_UNORM);
        dx_srv(&DXS.tmp, S_TMP, DXGI_FORMAT_R8G8B8A8_UNORM);
        dx_list();
        ID3D12GraphicsCommandList_ClearRenderTargetView(DXS.list, dx_rtv_at(FRAMES), zero, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(DXS.list, ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(DXS.dsv_heap),
                                                        D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);
    } else if (!force) {
        return;
    }
    soft_all();
    dx_refresh(0, 0, VRAM_W, VRAM_H);
}
