/* The Direct3D 12 renderer's frame: the picture, a movie or the menu onto the window, and the next frame's jitter */
#include "dx.h"

/* --- the frame --- */

/* The 24-bit movies' display area as RGBA in the upload buffer (rows padded to 256 bytes) */
static void movie_rgba(const PSX_RECT *d) {
    int x, y, pitch = (d->w * 4 + 255) & ~255;

    soft_read(d->x, d->y, d->w, d->h);
    for (y = 0; y < d->h; y++) {
        uint32_t *o = (uint32_t *)(DXS.up_pic_ptr + (size_t)y * pitch);
        const uint8_t *b = (const uint8_t *)&g_vram[(d->y + y) * VRAM_W + d->x];

        for (x = 0; x < d->w && (d->x * 2 + x * 3 + 3) <= VRAM_W * 2; x++) {
            o[x] = (uint32_t)(b[x * 3] | b[x * 3 + 1] << 8 | b[x * 3 + 2] << 16 | 0xFF000000u);
        }
    }
}

/* The frame: the display area of the copy (or the 1x VRAM's 24-bit movie) letterboxed to 4:3, then the menu (a 1280x960 RGBA surface, or NULL) */
void dx_present(const SDL_Surface *menu, SDL_Surface **shot) {
    PSX_RECT d = g_disp.disp;
    int w, h, vp[4], menu_vp[4], game, movie, i, slot = S_RT;
    float puv[4] = {0, 0, 1, 1};
    int aw = gte_widescreen() ? 16 : 4, ah = gte_widescreen() ? 9 : 3;
    UINT bi;
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    const float black[4] = {0, 0, 0, 1};

    if (!DXS.ready) {
        return;
    }
    dx_flush();
    /* the fog for the next frame: on while a sky is drawn, its depths eased toward this frame's farthest (a camera's cut) */
    {
        float bright;
        const StageFx *fx = stagefx_current(&bright);
        int k;

        DXS.fog_shown = DXS.fog_on && fx != NULL && fx->fog > 0;
        if (DXS.fog_shown) {
            for (k = 0; k < 3; k++) {
                DXS.fog[k] = fx->fog_color[k] >= 0 ? fx->fog_color[k] * bright / 8.0f : DXS.bg[k];
            }
            DXS.fog_amount = fx->fog, DXS.fog_from = fx->fog_start;
        }
        stagefx_frame();
    }
    DXS.frame_3d = DXS.zframe > 0;
    DXS.mv_clear = 1; /* (the motion of the next frame starts from nothing) */
    if (DXS.data_mshow) {
        static int shown_org[4] = {-1, -1, -1, -1};

        if (!DXS.mv_org_set) {
            DXS.mv_org[0] = d.x, DXS.mv_org[1] = d.y;
        } else if (!(DXS.mv_org[0] >= d.x && DXS.mv_org[0] < d.x + d.w && DXS.mv_org[1] >= d.y && DXS.mv_org[1] < d.y + d.h)) {
            /* the 3D is a window inside a buffer (not the one shown): the buffer's corner is the shown one's, 256 lines apart */
            DXS.mv_org[0] = d.x, DXS.mv_org[1] = d.y >= 256 ? d.y - 256 : d.y + 256;
        } else {
            DXS.mv_org[0] = d.x, DXS.mv_org[1] = d.y;
        }
        if (DXS.frame_3d && (shown_org[0] != DXS.mv_org[0] || shown_org[1] != DXS.mv_org[1] || shown_org[2] != d.x || shown_org[3] != d.y)) {
            shown_org[0] = DXS.mv_org[0], shown_org[1] = DXS.mv_org[1], shown_org[2] = d.x, shown_org[3] = d.y;
            SDL_Log("d3d12: motion: 3D drawn at (%d,%d), shown from (%d,%d)", DXS.mv_org[0], DXS.mv_org[1], d.x, d.y);
        }
        DXS.mv_org_set = 0, DXS.mv_area = 0;
    }
    DXS.zfar = !DXS.fog_on || DXS.zframe <= 0 ? 0 : DXS.zfar > 0 ? DXS.zfar + (DXS.zframe - DXS.zfar) * 0.2f : DXS.zframe;
    DXS.fog_on = 0, DXS.zframe = 0;
    DXS.jit_used[0] = DXS.jit_px[0], DXS.jit_used[1] = DXS.jit_px[1];
    if (DXS.data_jitter && DXS.fsr3 && !DXS.dlss && fx_loaded()) { /* FSR 3.1's own sequence */
        fx_jitter_now(++DXS.jit_n, &DXS.jit_px[0], &DXS.jit_px[1]);
    } else if (DXS.data_jitter) { /* the next frame's: Halton (2, 3), 8 of them, centred on the pixel */
        unsigned n = ++DXS.jit_n % (DXS.dlss ? sl_phases() : 8) + 1;
        float f, b;
        int k;

        for (k = 0; k < 2; k++) {
            unsigned j = n, base = k == 0 ? 2 : 3;

            for (f = 1, b = 0; j > 0; j /= base) {
                f /= base, b += f * (j % base);
            }
            DXS.jit_px[k] = b - 0.5f;
        }
    }
    SDL_GetWindowSizeInPixels(DXS.window, &w, &h);
    if (w < 1 || h < 1) {
        return;
    }
    if (w != DXS.w || h != DXS.h) {
        dx_resize(w, h);
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
    game = g_disp_on && d.w > 0 && d.h > 0 && !g_disp.isrgb24 && DXS.scale > 0;

    if (game) {
        dx_post_prepare(&d, vp);
    }
    dx_list();
    if (movie || menu != NULL) { /* their upload buffers are written now: the last frame's copy from them must be done */
        dx_wait_slot((DXS.slot + FIF - 1) % FIF);
    }
    dx_mark(C_PRESENT);
    if (movie) {
        movie_rgba(&d);
        dx_upload_tex(&DXS.pic, DXS.up_pic, d.w, d.h);
    }
    if (menu != NULL) {
        for (i = 0; i < MENU_H; i++) {
            SDL_memcpy(DXS.up_menu_ptr + (size_t)i * ((MENU_W * 4 + 255) & ~255), (const uint8_t *)menu->pixels + (size_t)i * menu->pitch, MENU_W * 4);
        }
        dx_upload_tex(&DXS.menu, DXS.up_menu, MENU_W, MENU_H);
    }
    if (game) {
        dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        slot = dx_post_chain(&d, vp, puv);
        if (DXS.do_depth) {
            dx_post_depth();
        }
        if (DXS.do_mout) {
            dx_post_motion();
        }
        if (DXS.do_f3 && dx_post_f3(&d, vp)) {
            slot = S_F3;
            puv[0] = puv[1] = 0, puv[2] = puv[3] = 1;
        }
        DXS.f3_prev = DXS.do_f3;
    }
    bi = IDXGISwapChain3_GetCurrentBackBufferIndex(DXS.swap);
    dx_barrier(DXS.back[bi], D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    rtv = dx_rtv_at((int)bi);
    ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &rtv, FALSE, NULL);
    ID3D12GraphicsCommandList_ClearRenderTargetView(DXS.list, rtv, black, 0, NULL);
    if (movie) {
        float uv[4] = {0, 0, d.w / (float)VRAM_W, d.h / (float)VRAM_H};

        dx_quad(S_PIC, vp, uv, DXS.pso_present);
    } else if (game && DXS.frame_3d && DXS.pso_mview != NULL && DXS.mv.r != NULL) { /* the 3D's motion itself, to look at (only 3D frames: menus stay as they are) */
        float k[12] = {0, 0, 1, 1, d.w * DXS.scale / (float)vp[2], d.h * DXS.scale / (float)vp[3], 0, 0, (float)vp[0], (float)vp[1]};

        if (DXS.do_mout) { /* what the upscalers are given */
            dx_draw_k(DXS.blit_root, DXS.pso_mview, S_MOUT, vp, k);
        } else {
            k[6] = (float)(DXS.mv_org[0] * DXS.scale), k[7] = (float)(DXS.mv_org[1] * DXS.scale);
            dx_trans(&DXS.mv, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            dx_draw_k(DXS.blit_root, DXS.pso_mview, S_MV, vp, k);
        }
    } else if (game && DXS.data_view && DXS.do_depth) { /* the depth itself, to look at */
        float k[12] = {0, 0, 1, 1, DXS.dout_w / (float)vp[2], DXS.dout_h / (float)vp[3], 0, 0, (float)vp[0], (float)vp[1]};

        dx_draw_k(DXS.blit_root, DXS.pso_dview, S_DOUT, vp, k);
    } else if (game) {
        if (DXS.pso_post != NULL && (g_sharpen | g_brightness | g_gamma | g_saturation | g_scanlines) != 0) {
            /* gamma 10 halves the exponent (brighter middles), -10 doubles it */
            float k[12] = {puv[0], puv[1], puv[2], puv[3], g_sharpen / 10.0f, g_brightness * 0.02f, SDL_powf(2.0f, -g_gamma / 10.0f), 1.0f + g_saturation / 10.0f,
                           puv[2] / vp[2], puv[3] / vp[3], g_scanlines / 10.0f, (float)d.h};

            dx_draw_k(DXS.blit_root, DXS.pso_post, slot, vp, k);
        } else {
            dx_quad(slot, vp, puv, DXS.pso_present);
        }
    }
    if (menu != NULL) {
        float uv[4] = {0, 0, 1, 1};

        dx_quad(S_MENU, menu_vp, uv, DXS.pso_over);
    }
    if (shot != NULL) {
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};

        dx_barrier(DXS.back[bi], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dst.pResource = DXS.read, dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = (UINT)w, dst.PlacedFootprint.Footprint.Height = (UINT)h, dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = DXS.read_pitch;
        src.pResource = DXS.back[bi], src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, NULL);
        dx_barrier(DXS.back[bi], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    } else {
        dx_barrier(DXS.back[bi], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    }
    dx_mark(C_OTHER);
    if (DXS.qheap != NULL && DXS.qn > 1) {
        ID3D12GraphicsCommandList_ResolveQueryData(DXS.list, DXS.qheap, D3D12_QUERY_TYPE_TIMESTAMP, 0, (UINT)DXS.qn, DXS.qread, (UINT64)DXS.slot * QMAX * 8);
    }
    if (shot != NULL) {
        dx_submit(); /* the screenshot reads the frame back: waited for */
        DXS.qn_slot[DXS.slot] = DXS.qn, DXS.qn = 0; /* (a slot's timestamps are read when it is reused) */
        DXS.slot = (DXS.slot + 1) % FIF;
    } else {
        dx_submit_frame();
    }
    if (shot != NULL && (*shot = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888)) != NULL) {
        for (i = 0; i < h; i++) {
            SDL_memcpy((uint8_t *)(*shot)->pixels + (size_t)i * (*shot)->pitch, DXS.read_ptr + (size_t)i * DXS.read_pitch, (size_t)w * 4);
        }
    }
    {
        Uint64 t0 = SDL_GetTicksNS(), pr;

        IDXGISwapChain3_Present(DXS.swap, g_vsync ? 1 : 0, 0);
        pr = SDL_GetTicksNS() - t0;
        /* (the effects' warm-up makes one image a frame, ~4.5 ms for a 1280x960 one: not a long frame) */
        if (DXS.wait_ns > 14000000 || DXS.img_ns > 8000000 || pr > 25000000) {
            SDL_Log("d3d12: long frame: gpu wait %.1f ms in %d submits, %d images made in %.1f ms, %d vram uploads, present %.1f ms",
                    DXS.wait_ns / 1e6, DXS.nsub, DXS.nimg_made, DXS.img_ns / 1e6, DXS.nup, pr / 1e6);
        }
        DXS.wait_ns = DXS.img_ns = 0, DXS.nsub = DXS.nimg_made = DXS.nup = 0;
    }
}
