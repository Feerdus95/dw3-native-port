/* The Direct3D 12 renderer's data for the temporal upscalers (depth, motion) and the FSR 3.1 or DLSS pass */
#include "dx.h"

/* The size of the upscaled picture (FSR 3.1, DLSS) in W x H. DLSS takes square pixels, so its picture keeps the game's 4:3 and the present
   stretches it over the viewport; FSR 3.1 is given the viewport */
void dx_f3_size(int pw, int ph, const int vp[4], int *w, int *h) {
    *w = vp[2], *h = vp[3];
    if (g_fsr == 3) {
        *w = vp[3] * pw / ph;
        if (*w > vp[2]) {
            *w = vp[2], *h = vp[2] * ph / pw;
        }
    }
}

/* The depth buffer as a table left it, kept (a viewport of its drawing area, so only that is read): the stage's after its table, before the
   models' table empties the buffer, and the fighters' after theirs */
static void depth_keep(Tex *to, int rtv, int *valid, int org[2]) {
    int s = DXS.scale, box[4] = {DXS.clip[0] * s, DXS.clip[1] * s, (DXS.clip[2] - DXS.clip[0] + 1) * s, (DXS.clip[3] - DXS.clip[1] + 1) * s}, prev;
    const float k[12] = {0, 0, 1, 1};

    *valid = 0;
    if (!DXS.data_depth || s == 0 || to->r == NULL || !g_depth || DXS.pso_dkeep == NULL || box[2] <= 0 || box[3] <= 0) {
        return;
    }
    dx_flush();
    dx_list();
    prev = dx_mark(C_DEPTH);
    dx_trans(&DXS.depth, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_rt_pass(to, rtv, 0);
    dx_draw_k(DXS.blit_root, DXS.pso_dkeep, S_DSRC, box, k);
    dx_trans(to, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_trans(&DXS.depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    dx_mark(prev);
    *valid = 1, org[0] = DXS.clip[0], org[1] = DXS.clip[1];
}

void dx_depth_save(void) {
    depth_keep(&DXS.sd, R_SD, &DXS.sd_valid, DXS.sd_org);
}

void dx_depth_models(void) {
    depth_keep(&DXS.fd, R_FD, &DXS.fd_valid, DXS.fd_org);
}

/* The two joined, for this frame's picture (DXS.do_depth): where the upscalers will read it */
void dx_post_depth(void) {
    int s = DXS.scale, full[4] = {0, 0, DXS.dout_w, DXS.dout_h};
    float k[12] = {0, 0, 1, 1, (float)(DXS.sd_org[0] * s), (float)(DXS.sd_org[1] * s), (float)(DXS.fd_org[0] * s), (float)(DXS.fd_org[1] * s), (float)(DXS.sd_valid != 0), (float)(DXS.fd_valid != 0)};
    static int shown = -1;
    int now = DXS.sd_valid << 1 | DXS.fd_valid;

    if (shown != now) { /* what the frame has, once per change */
        shown = now;
        SDL_Log("d3d12: upscale data: 3D frame, stage depth %d, fighters' depth %d (picture %dx%d)", DXS.sd_valid, DXS.fd_valid, DXS.dout_w, DXS.dout_h);
    }
    dx_mark(C_POST);
    dx_rt_pass(&DXS.dout, R_DOUT, 0);
    dx_draw_k(DXS.smaa_root, DXS.pso_dout, S_TD, full, k);
    dx_trans(&DXS.dout, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_mark(C_PRESENT);
}

/* The motion cropped to the picture, from the buffer the 3D was drawn in (DXS.do_mout) */
void dx_post_motion(void) {
    int s = DXS.scale, full[4] = {0, 0, DXS.mout_w, DXS.mout_h};
    float k[12] = {0, 0, 1, 1, 1, 1, (float)(DXS.mv_org[0] * s), (float)(DXS.mv_org[1] * s), 0, 0};

    dx_mark(C_POST);
    dx_trans(&DXS.mv, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_rt_pass(&DXS.mout, R_MOUT, 0);
    dx_draw_k(DXS.blit_root, DXS.pso_mcopy, S_MV, full, k);
    dx_trans(&DXS.mout, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_mark(C_PRESENT);
}

/* The picture through FSR 3.1 (DXS.do_f3), the depth and the motion of this frame made: into DXS.f3, which is shown. 0 when it did not run */
int dx_post_f3(const PSX_RECT *d, const int vp[4]) {
    int s = DXS.scale, pw = d->w * s, ph = d->h * s, ok;
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    D3D12_BOX box = {(UINT)(d->x * s), (UINT)(d->y * s), 0, (UINT)(d->x * s + pw), (UINT)(d->y * s + ph), 1};
    ID3D12DescriptorHeap *heaps[1] = {DXS.srv_heap};
    static Uint64 last;
    Uint64 now = SDL_GetTicksNS();
    float ms = last != 0 ? SDL_clamp((now - last) / 1e6f, 1.0f, 100.0f) : 16.6f, fov = gte_proj_h() > 0 ? 2.0f * SDL_atanf(d->h * 0.5f / gte_proj_h()) : 0.5f;

    last = now;
    dx_mark(C_POST);
    dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
    dx_trans(&DXS.p[0], D3D12_RESOURCE_STATE_COPY_DEST);
    dst.pResource = DXS.p[0].r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = DXS.rt.r, src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, &box);
    dx_trans(&DXS.p[0], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_trans(&DXS.f3, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (g_fsr == 3 && DXS.dlss) {
        ok = sl_run(DXS.list, DXS.p[0].r, DXS.dout.r, DXS.mout.r, DXS.f3.r, pw, ph, DXS.f3_w, DXS.f3_h, DXS.jit_used[0], DXS.jit_used[1], !DXS.f3_prev, fov);
    } else {
        ok = fx_run(DXS.dev, DXS.list, DXS.p[0].r, DXS.dout.r, DXS.mout.r, DXS.f3.r, pw, ph, DXS.f3_w, DXS.f3_h, DXS.jit_used[0], DXS.jit_used[1], !DXS.f3_prev, fov, ms);
    }
    ID3D12GraphicsCommandList_SetDescriptorHeaps(DXS.list, 1, heaps); /* (the upscaler set its own) */
    dx_trans(&DXS.f3, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_mark(C_PRESENT);
    return ok;
}
