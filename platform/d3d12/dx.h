/* The Direct3D 12 renderer's own: what its files share. The same drawing as the OpenGL one (platform/gl/), on
   Direct3D 12: the 1x VRAM goes up as an R16_UINT texture, the game's primitives are drawn into a copy of VRAM at
   render_scale times its size, and the display area of that copy is the picture. 64-bit Windows only. */
#ifndef DX_BACKEND_H
#define DX_BACKEND_H

#define RECT PSX_RECT /* windows.h has its own */
#include "../platform.h"
#include "../gfx.h"
#undef RECT
#define COBJMACROS
#define WIDL_C_INLINE_WRAPPERS /* mingw's d3d12.h: the getters that return a struct */
#include <SDL3/SDL.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include "fsr3.h"
#include "dlss.h"

/* The system's calls, loaded by dx_available: the exe starts on a Windows without them, with OpenGL */
#define DX_FUNCS(X) \
    X("d3d12.dll", D3D12CreateDevice) X("d3d12.dll", D3D12SerializeRootSignature) X("dxgi.dll", CreateDXGIFactory1) X("d3dcompiler_47.dll", D3DCompile)
#define X(dll, f) extern __typeof__(f) *p_##f;
DX_FUNCS(X)
#undef X

GFX_DECLARE(dx) /* renderer.h's, dx_* (gfx.c calls them through GFX_D3D12) */

#define FRAMES 2
#define MENU_W 1280
#define MENU_H 960
#define BATCH 6144
#define FIF 2 /* frames in flight */
#define RING (96u << 20) /* the frame's uploads: VRAM rects and vertices */
#define MAX_SCALE 8
/* --- the device --- */

typedef struct {
    ID3D12Resource *r;
    D3D12_RESOURCE_STATES st;
} Tex;
/* the SRV heap's slots; the picture's passes (post): its copies P0-P2 and FSR's output, SMAA's tables, and for SMAA's three
   passes a table of three views each (t0, t1, t2) */
enum { S_PIC, S_MENU, S_VRAM, S_RT, S_TMP, S_MASK, S_BLUR, S_P0, S_P1, S_P2, S_FSR, S_AREA, S_SEARCH, S_DSRC, S_SD, S_FD, S_DOUT, S_MV, S_MOUT, S_F3, S_TD, S_T0 = S_TD + 3, S_T1 = S_T0 + 3, S_T2 = S_T1 + 3, S_COUNT = S_T2 + 3 };
#define MAX_IMG 1024 /* the port's own images (icons, postcards, effects): their slots follow S_COUNT */
enum { R_RT = FRAMES, R_MASK, R_BLUR, R_P0, R_P1, R_P2, R_FSR, R_SD, R_FD, R_DOUT, R_MV, R_MOUT, R_COUNT }; /* the RTV heap's, after the back buffers */
#define OK(x) ((x) >= 0)
/* GPU time by what the frame spends it on: a timestamp at each change of region, the span after it counted to its region */
enum { C_OTHER, C_3D, C_2D, C_MASK, C_RESOLVE, C_REFRESH, C_COPY, C_DEPTH, C_IMAGE, C_PRESENT, C_POST, C_N };
#define QMAX 2048
#define blit_pso(vs, ps, wa, fmt) dx_blit_pso_on(DXS.blit_root, vs, ps, wa, fmt)
/* --- the batch --- */

typedef struct {
    float size[2], jit[2], fog[4], fogz[2], scale;
    int persp, pass, truecolor, smooth3d, pad;
} Consts;

struct DxState {
    int dlss; /* DLSS chosen at the start, its DLLs there and the GPU one it runs on: the device is Streamline's proxy */
    int fsr3; /* FSR 3.1 chosen at the start, and its DLLs there: its data is made */
    SDL_Window *window;
    ID3D12Device *dev;
    IDXGISwapChain3 *swap;
    ID3D12DescriptorHeap *rtv_heap, *srv_heap, *dsv_heap;
    ID3D12Resource *back[FRAMES], *up_pic, *up_menu, *read, *ring_res;
    Tex pic, menu, vram, rt, tmp, depth, mask, blur; /* the mask and its blur: battle shadows (R8) */
    Tex p[3], fsr, area, search; /* the picture's passes: the display area copied, SMAA's edges, its weights; FSR's output */
    ID3D12RootSignature *smaa_root; /* three views: SMAA's passes, the depth join */
    Tex sd, fd, dout; /* the upscalers' depth: the stage's and the fighters' kept (R32F, as the buffer), and the two joined at the picture's size */
    ID3D12PipelineState *pso_dkeep, *pso_dout, *pso_dview;
    int data_depth, data_view, sd_valid, fd_valid, sd_org[2], fd_org[2], dout_w, dout_h, do_depth;
    ID3D12PipelineState *pso_post, *pso_easu, *pso_smaa[3];
    int data_jitter;
    int data_motion, data_mshow, mv_clear, mv_org[2], mv_org_set, mv_area; /* DXS.mv_org: where the frame's 3D was drawn (the clip's corner), not always where it is shown */
    Tex mv;
    ID3D12PipelineState *pso_mview, *pso_mcopy;
    Tex mout; /* the motion at the picture's size (R16G16F, pixels of the picture, previous place minus this one): where the upscalers will read it */
    int mout_w, mout_h, do_mout;
    float jit_px[2], jit_used[2]; /* DXS.jit_used: the jitter the frame being shown was drawn with */
    unsigned jit_n;
    Tex f3; /* FSR 3.1's output at the size of the window's picture (a UAV) */
    int f3_w, f3_h, do_f3, f3_prev, f3_state;
    int p_w, p_h, fsr_w, fsr_h, smaa_tried, frame_3d; /* DXS.frame_3d: this frame has 3D in it */
    ID3D12GraphicsCommandList *list;
    ID3D12RootSignature *main_root, *blit_root;
    ID3D12PipelineState *pso_present, *pso_copy, *pso_copyn, *pso_over, *pso_blur, *pso_dark, *pso_img, *pso_add, *pso_mask[2]; /* the mask's: without and with the depth test */
    ID3D12PipelineState *pso_main[6 * 3]; /* made on first use: (blend + 1) * 3 + depth mode */
    ID3DBlob *main_vs, *main_ps, *main_psm;
    D3D12_INPUT_ELEMENT_DESC layout[9];
    UINT rtv_inc, srv_inc;
    int w, h, ready, open, scale;
    UINT8 *up_pic_ptr, *up_menu_ptr, *read_ptr, *ring;
    UINT read_pitch, ring_off;
    GVtx *batch;
    int count, semi, deep, clip[4];
    int mask_box[4]; /* what the mask holds, in VRAM pixels */
    float bg[3], fog[3], fog_amount, fog_from, zfar, zframe;
    int fog_on, fog_shown, depth_used; /* DXS.depth_used: some 3D has written depth since the last clear */
    Uint64 wait_ns, img_ns, compile_ns; /* compile_ns: the shaders compiled at start (D3DCompile), for dx_init's log */
    int nsub, nimg_made, nup;
    ID3D12QueryHeap *qheap;
    ID3D12Resource *qread; /* FIF frames' timestamps */
    int cat, qn, qn_slot[FIF];
    int slot;
};
extern struct DxState DXS;

extern const char *MAIN_HLSL;
extern const char *BLIT_HLSL;
ID3D12Resource *dx_buffer(D3D12_HEAP_TYPE type, UINT64 size, D3D12_RESOURCE_STATES state, UINT8 **map);
Tex dx_texture(int w, int h, DXGI_FORMAT fmt, D3D12_RESOURCE_FLAGS flags, D3D12_RESOURCE_STATES state, const D3D12_CLEAR_VALUE *clear);
void dx_wait_slot(int slot);
void dx_list(void);
int dx_mark(int cat);
void dx_submit(void);
void dx_submit_frame(void);
void dx_trans(Tex *t, D3D12_RESOURCE_STATES to);
void dx_barrier(ID3D12Resource *r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
UINT8 *dx_ring_alloc(UINT size, UINT *off);
void dx_srv(const Tex *t, int slot, DXGI_FORMAT fmt);
D3D12_CPU_DESCRIPTOR_HANDLE dx_rtv_at(int i);
void dx_upload_tex(Tex *tex, ID3D12Resource *up, int w, int h);
ID3D12PipelineState *dx_main_pso(int semi, int depth);
ID3D12PipelineState *dx_mask_pso(int depth);
ID3D12PipelineState *dx_blit_pso_on(ID3D12RootSignature *rs, ID3DBlob *vs, ID3DBlob *ps, int write_alpha, DXGI_FORMAT fmt);
ID3D12RootSignature *dx_root(const D3D12_ROOT_SIGNATURE_DESC *rsd);
int dx_compile(const char *src, const char *entry, const char *target, ID3DBlob **out);
void dx_unblob(ID3DBlob **b); /* (released and NULL; NULL is fine) */
void dx_resize(int w, int h);
void dx_upload(int x, int y, int w, int h);
void dx_upload_blocks(const uint16_t need[8]);
void dx_need_rect(uint16_t need[8], int x, int y, int w, int h);
void dx_flush(void);
void dx_quad(int slot, const int vp[4], const float uv[4], ID3D12PipelineState *pso);
void dx_post_init(void);
void dx_f3_size(int pw, int ph, const int vp[4], int *w, int *h);
void dx_post_prepare(const PSX_RECT *d, const int vp[4]);
void dx_rt_pass(Tex *t, int rtv, int clear);
void dx_draw_k(ID3D12RootSignature *rs, ID3D12PipelineState *pso, int slot, const int vp[4], const float k[12]);
void dx_post_depth(void);
void dx_post_motion(void);
int dx_post_f3(const PSX_RECT *d, const int vp[4]);
int dx_post_chain(const PSX_RECT *d, const int vp[4], float uv[4]);

#endif
