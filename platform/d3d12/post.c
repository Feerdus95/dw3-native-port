/* The Direct3D 12 renderer's passes over the picture: sharpening and the colour options, SMAA, FSR 1 (EASU) */
#include "dx.h"

/* The picture's passes on Direct3D 12, in HLSL: what the OpenGL renderer's POST_FS, EASU_FS and SMAA do, written again for this backend
   (included once, by post.c). All three read the 12 root constants as uv (the rectangle of the texture, for the vertex
   shader), a and b (each pass's own). SMAA is the authors' shader in smaa/ (MIT, CREDITS.md); the sharpening is after AMD's
   FidelityFX CAS and the upscaling after its FSR 1 (EASU), both MIT (CREDITS.md). */

/* sharpen (contrast-adaptive), saturation, scanlines, brightness, gamma: a = (sharpen 0-1, brightness added, gamma's exponent,
   saturation's factor), b = (a screen pixel in uv, the scanlines' strength 0-1, the game's rows shown) */
static const char *POST_HLSL =
    "cbuffer C : register(b0) { float4 uv; float4 a; float4 b; };\n"
    "Texture2D tx : register(t0);\n"
    "SamplerState lin : register(s0);\n"
    "struct VO { float4 p : SV_Position; float2 t : TEXCOORD0; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    o.t = uv.xy + c * uv.zw;\n"
    "    return o;\n"
    "}\n"
    "float4 pp(VO i) : SV_Target {\n"
    "    float3 c = tx.Sample(lin, i.t).rgb;\n"
    "    if (a.x > 0.0) {\n"
    "        float3 n = tx.Sample(lin, i.t - float2(0.0, b.y)).rgb, s = tx.Sample(lin, i.t + float2(0.0, b.y)).rgb;\n"
    "        float3 w = tx.Sample(lin, i.t - float2(b.x, 0.0)).rgb, e = tx.Sample(lin, i.t + float2(b.x, 0.0)).rgb;\n"
    "        float3 lo = min(c, min(min(n, s), min(w, e))), hi = max(c, max(max(n, s), max(w, e)));\n"
    "        float3 k = sqrt(clamp(min(lo, 1.0 - hi) / max(hi, 1.0 / 255.0), 0.0, 1.0)) * lerp(-0.125, -0.2, a.x);\n"
    "        c = clamp((c + (n + s + w + e) * k) / (1.0 + 4.0 * k), 0.0, 1.0);\n"
    "    }\n"
    "    c = lerp(dot(c, float3(0.299, 0.587, 0.114)).xxx, c, a.w);\n"
    /* each of the game's rows brightest at its middle, dark between it and the next */
    "    c *= 1.0 - b.z * 0.6 * (0.5 + 0.5 * cos(6.2831853 * ((i.t.y - uv.y) / uv.w) * b.w));\n"
    "    return float4(pow(clamp(c + a.y, 0.0, 1.0), a.z), 1.0);\n"
    "}\n";

/* FSR 1's upscaling pass (EASU): the twelve texels around the output pixel, in a kernel turned along the edge found there and
   narrowed by how sharp it is, the result kept within the four nearest's range. a.xy: the picture's pixels to one of the
   output's; b: the picture's corner and size in the texture, in texels */
static const char *EASU_HLSL =
    "cbuffer C : register(b0) { float4 uv; float4 a; float4 b; };\n"
    "Texture2D tx : register(t0);\n"
    "struct VO { float4 p : SV_Position; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    return o;\n"
    "}\n"
    "static int2 at, org, top;\n"
    "float3 T(int x, int y) { return tx.Load(int3(clamp(at + int2(x, y), org, top), 0)).rgb; }\n"
    "float L(float3 c) { return c.b * 0.5 + (c.r * 0.5 + c.g); }\n"
    /* one of the four nearest (c) and those above, left, right and below it: which way the edge runs, how sharp */
    "void edge(inout float2 dir, inout float len, float w, float a, float b, float c, float d, float e) {\n"
    "    float dx = d - b, dy = e - a;\n"
    "    float lx = clamp(abs(dx) / max(max(abs(d - c), abs(c - b)), 1e-5), 0.0, 1.0);\n"
    "    float ly = clamp(abs(dy) / max(max(abs(e - c), abs(c - a)), 1e-5), 0.0, 1.0);\n"
    "    dir += float2(dx, dy) * w;\n"
    "    len += (lx * lx + ly * ly) * w;\n"
    "}\n"
    "void tap(inout float3 sum, inout float ws, float2 off, float2 dir, float2 len, float lob, float clp, float3 c) {\n"
    "    float2 v = float2(off.x * dir.x + off.y * dir.y, off.y * dir.x - off.x * dir.y) * len;\n"
    "    float d2 = min(dot(v, v), clp), wb = 0.4 * d2 - 1.0, wa = lob * d2 - 1.0;\n"
    "    wa = wa * wa * (1.5625 * wb * wb - 0.5625);\n"
    "    sum += c * wa;\n"
    "    ws += wa;\n"
    "}\n"
    "float4 ps(VO i) : SV_Target {\n"
    "    float2 p = i.p.xy * a.xy - 0.5, pp = frac(p);\n"
    "    org = int2(b.xy), top = org + int2(b.zw) - 1;\n"
    "    at = org + int2(floor(p));\n"
    /*   b c
       e f g h
       i j k l
         n o   */
    "    float3 b = T(0, -1), c = T(1, -1), e = T(-1, 0), f = T(0, 0), g = T(1, 0), h = T(2, 0);\n"
    "    float3 i1 = T(-1, 1), j = T(0, 1), k = T(1, 1), l = T(2, 1), n = T(0, 2), o = T(1, 2);\n"
    "    float lb = L(b), lc = L(c), le = L(e), lf = L(f), lg = L(g), lh = L(h);\n"
    "    float li = L(i1), lj = L(j), lk = L(k), ll = L(l), ln = L(n), lo = L(o);\n"
    "    float2 dir = float2(0.0, 0.0);\n"
    "    float len = 0.0;\n"
    "    edge(dir, len, (1.0 - pp.x) * (1.0 - pp.y), lb, le, lf, lg, lj);\n"
    "    edge(dir, len, pp.x * (1.0 - pp.y), lc, lf, lg, lh, lk);\n"
    "    edge(dir, len, (1.0 - pp.x) * pp.y, lf, li, lj, lk, ln);\n"
    "    edge(dir, len, pp.x * pp.y, lg, lj, lk, ll, lo);\n"
    "    float r = dot(dir, dir);\n"
    "    dir = r < 1.0 / 32768.0 ? float2(1.0, 0.0) : dir * rsqrt(r);\n"
    "    len = len * 0.5;\n"
    "    len *= len;\n"
    "    float stretch = dot(dir, dir) / max(abs(dir.x), abs(dir.y));\n"
    "    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
    "    float lob = 0.5 - 0.29 * len, clp = 1.0 / lob, ws = 0.0;\n"
    "    float3 sum = float3(0.0, 0.0, 0.0);\n"
    "    tap(sum, ws, float2(0.0, -1.0) - pp, dir, len2, lob, clp, b);\n"
    "    tap(sum, ws, float2(1.0, -1.0) - pp, dir, len2, lob, clp, c);\n"
    "    tap(sum, ws, float2(-1.0, 0.0) - pp, dir, len2, lob, clp, e);\n"
    "    tap(sum, ws, float2(0.0, 0.0) - pp, dir, len2, lob, clp, f);\n"
    "    tap(sum, ws, float2(1.0, 0.0) - pp, dir, len2, lob, clp, g);\n"
    "    tap(sum, ws, float2(2.0, 0.0) - pp, dir, len2, lob, clp, h);\n"
    "    tap(sum, ws, float2(-1.0, 1.0) - pp, dir, len2, lob, clp, i1);\n"
    "    tap(sum, ws, float2(0.0, 1.0) - pp, dir, len2, lob, clp, j);\n"
    "    tap(sum, ws, float2(1.0, 1.0) - pp, dir, len2, lob, clp, k);\n"
    "    tap(sum, ws, float2(2.0, 1.0) - pp, dir, len2, lob, clp, l);\n"
    "    tap(sum, ws, float2(0.0, 2.0) - pp, dir, len2, lob, clp, n);\n"
    "    tap(sum, ws, float2(1.0, 2.0) - pp, dir, len2, lob, clp, o);\n"
    "    return float4(min(max(max(f, g), max(j, k)), max(min(min(f, g), min(j, k)), sum / ws)), 1.0);\n"
    "}\n";

/* SMAA 1x: a head that sets their shader up, the shader as they publish it (SMAA_SRC, embedded by post.c), and the three
   passes' entry points (0: the edges, 1: how much each pixel blends, 2: the picture blended). rt: the picture's pixel metrics */
static const char *SMAA_HEAD =
    "#define SMAA_HLSL_4\n#define SMAA_PRESET_HIGH\n#define SMAA_RT_METRICS rt\n"
    "cbuffer C : register(b0) { float4 uv; float4 rt; float4 b; };\n"
    "Texture2D t0 : register(t0);\n"
    "Texture2D t1 : register(t1);\n"
    "Texture2D t2 : register(t2);\n";

static const char *SMAA_GLUE =
    "\nstruct SO { float4 p : SV_Position; float2 t : TEXCOORD0; float2 px : TEXCOORD1; float4 o0 : TEXCOORD2; float4 o1 : TEXCOORD3; float4 o2 : TEXCOORD4; };\n"
    "SO sbase(uint id) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    SO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    o.t = c;\n"
    "    o.px = float2(0, 0);\n"
    "    o.o0 = o.o1 = o.o2 = float4(0, 0, 0, 0);\n"
    "    return o;\n"
    "}\n"
    "SO svs0(uint id : SV_VertexID) {\n"
    "    SO o = sbase(id);\n"
    "    float4 off[3];\n"
    "    SMAAEdgeDetectionVS(o.t, off);\n"
    "    o.o0 = off[0], o.o1 = off[1], o.o2 = off[2];\n"
    "    return o;\n"
    "}\n"
    "SO svs1(uint id : SV_VertexID) {\n"
    "    SO o = sbase(id);\n"
    "    float4 off[3];\n"
    "    SMAABlendingWeightCalculationVS(o.t, o.px, off);\n"
    "    o.o0 = off[0], o.o1 = off[1], o.o2 = off[2];\n"
    "    return o;\n"
    "}\n"
    "SO svs2(uint id : SV_VertexID) {\n"
    "    SO o = sbase(id);\n"
    "    float4 off;\n"
    "    SMAANeighborhoodBlendingVS(o.t, off);\n"
    "    o.o0 = off;\n"
    "    return o;\n"
    "}\n"
    "float4 sps0(SO i) : SV_Target {\n"
    "    float4 off[3] = {i.o0, i.o1, i.o2};\n"
    "    return float4(SMAAColorEdgeDetectionPS(i.t, off, t0), 0.0, 0.0);\n"
    "}\n"
    "float4 sps1(SO i) : SV_Target {\n"
    "    float4 off[3] = {i.o0, i.o1, i.o2};\n"
    "    return SMAABlendingWeightCalculationPS(i.t, i.px, off, t0, t1, t2, float4(0.0, 0.0, 0.0, 0.0));\n"
    "}\n"
    "float4 sps2(SO i) : SV_Target { return SMAANeighborhoodBlendingPS(i.t, i.o0, t0, t1); }\n";

/* The 3D's depth for the upscalers (the game's: depth = 1 - 64 / z, a standard projection with near 64 and no far plane; 1 where
   nothing 3D was drawn). dkeep: the copy of the depth buffer, for the rectangle the viewport is (a table of the stage, or of the
   fighters, drawn); dout: the two joined, the fighters' over the stage's (a: the corner of each in its texture, b.xy: which there
   is); dview: the depth in grey, near bright, to look at (a.xy: the picture's texels to one of the window's, b.xy: its corner) */
static const char *DEPTH_HLSL =
    "cbuffer C : register(b0) { float4 uv; float4 a; float4 b; };\n"
    "Texture2D<float> t0 : register(t0);\n"
    "Texture2D<float> t1 : register(t1);\n"
    "struct VO { float4 p : SV_Position; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    return o;\n"
    "}\n"
    "float4 dkeep(VO i) : SV_Target { return float4(t0.Load(int3(int2(i.p.xy), 0)), 0, 0, 0); }\n"
    "float4 dout(VO i) : SV_Target {\n"
    "    int2 p = int2(i.p.xy);\n"
    "    float ds = b.x > 0.0 ? t0.Load(int3(p + int2(a.xy), 0)) : 1.0, df = b.y > 0.0 ? t1.Load(int3(p + int2(a.zw), 0)) : 1.0;\n"
    "    return float4(df < 1.0 ? df : ds, 0, 0, 0);\n"
    "}\n"
    "float4 dview(VO i) : SV_Target {\n"
    "    float d = t0.Load(int3(int2((i.p.xy - b.xy) * a.xy), 0));\n"
    "    float z = d < 1.0 ? 64.0 / (1.0 - d) : 0.0;\n"
    "    float g = z > 0.0 ? sqrt(min(1.0, 3000.0 / z)) : 0.0;\n"
    "    return float4(g, g, g, 1.0);\n"
    "}\n";

/* The motion data (DW3_UPSCALE_DATA=mshow), to look at: grey where nothing moves, red/green by the x/y motion (a.xy: the picture's
   texels to one of the window's, a.zw: where the picture starts in the copy, b.xy: the window's corner) */
static const char *MOTION_HLSL =
    "cbuffer C : register(b0) { float4 uv; float4 a; float4 b; };\n"
    "Texture2D<float4> t0 : register(t0);\n"
    "struct VO { float4 p : SV_Position; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    return o;\n"
    "}\n"
    "float4 mcopy(VO i) : SV_Target {\n"
    "    return float4(t0.Load(int3(int2((i.p.xy - b.xy) * a.xy) + int2(a.zw), 0)).xy, 0, 0);\n"
    "}\n"
    "float4 mview(VO i) : SV_Target {\n"
    "    float2 m = t0.Load(int3(int2((i.p.xy - b.xy) * a.xy) + int2(a.zw), 0)).xy;\n"
    "    float2 c = clamp(m / 0.5, -0.5, 0.5) + 0.5;\n"
    "    return float4(c.x, c.y, 0.5, 1.0);\n"
    "}\n";

/* SMAA's shader as its authors publish it (smaa/, MIT), in a section of its own after the game's memory: that has to end
   below 16 MB (main.c); and its two tables */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
__attribute__((section(".zsmaa"))) static char SMAA_SRC[] = {
#embed "../smaa/SMAA.hlsl"
    , 0};
#pragma clang diagnostic pop
#define areaTexBytes __attribute__((section(".zsmaar"))) areaTexBytes
#define searchTexBytes __attribute__((section(".zsmaar"))) searchTexBytes
#include "../smaa/AreaTex.h"
#include "../smaa/SearchTex.h"
#undef areaTexBytes
#undef searchTexBytes

/* --- the picture's passes: sharpening and the colour options, SMAA, FSR 1 (post.c) --- */

static int s_do_smaa, s_do_fsr; /* this frame's, decided before its list is opened (post_prepare) */

/* the root of three views, 12 constants and the two samplers: SMAA's passes, the depth join */
static void make_root3(void) {
    int i;
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, 0};
    D3D12_ROOT_PARAMETER rp[2] = {0};
    D3D12_STATIC_SAMPLER_DESC ss[2] = {0};
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    if (DXS.smaa_root != NULL) {
        return;
    }
    rp[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, rp[0].DescriptorTable.NumDescriptorRanges = 1;
    rp[0].DescriptorTable.pDescriptorRanges = &range, rp[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    rp[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS, rp[1].Constants.Num32BitValues = 12, rp[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (i = 0; i < 2; i++) {
        ss[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        ss[i].AddressU = ss[i].AddressV = ss[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        ss[i].ShaderRegister = (UINT)i, ss[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL, ss[i].MaxLOD = D3D12_FLOAT32_MAX;
    }
    rsd.NumParameters = 2, rsd.pParameters = rp, rsd.NumStaticSamplers = 2, rsd.pStaticSamplers = ss;
    DXS.smaa_root = dx_root(&rsd);
}

void dx_post_init(void) {
    ID3DBlob *vs = NULL, *ps = NULL;
    const char *e = SDL_getenv("DW3_UPSCALE_DATA");

    DXS.data_depth = DXS.fsr3 || (e != NULL && (SDL_strstr(e, "depth") != NULL || SDL_strstr(e, "view") != NULL));
    DXS.data_view = e != NULL && SDL_strstr(e, "view") != NULL;
    make_root3();
    if (DXS.data_depth && dx_compile(DEPTH_HLSL, "vs", "vs_5_0", &vs)) {
        ID3DBlob *pk = NULL, *po = NULL, *pv = NULL;

        if (dx_compile(DEPTH_HLSL, "dkeep", "ps_5_0", &pk) && dx_compile(DEPTH_HLSL, "dout", "ps_5_0", &po) && dx_compile(DEPTH_HLSL, "dview", "ps_5_0", &pv)) {
            DXS.pso_dkeep = blit_pso(vs, pk, 1, DXGI_FORMAT_R32_FLOAT);
            DXS.pso_dout = dx_blit_pso_on(DXS.smaa_root, vs, po, 1, DXGI_FORMAT_R32_FLOAT);
            DXS.pso_dview = blit_pso(vs, pv, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        dx_unblob(&pk), dx_unblob(&po), dx_unblob(&pv);
    }
    dx_unblob(&vs);

    if (DXS.data_motion && dx_compile(MOTION_HLSL, "vs", "vs_5_0", &vs)) {
        ID3DBlob *pc = NULL;

        if (dx_compile(MOTION_HLSL, "mcopy", "ps_5_0", &pc)) {
            DXS.pso_mcopy = blit_pso(vs, pc, 1, DXGI_FORMAT_R16G16_FLOAT);
        }
        if (DXS.data_mshow && dx_compile(MOTION_HLSL, "mview", "ps_5_0", &ps)) {
            DXS.pso_mview = blit_pso(vs, ps, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        dx_unblob(&pc);
    }
    dx_unblob(&vs), dx_unblob(&ps);
    if (dx_compile(POST_HLSL, "vs", "vs_5_0", &vs) && dx_compile(POST_HLSL, "pp", "ps_5_0", &ps)) {
        DXS.pso_post = blit_pso(vs, ps, 0, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    dx_unblob(&vs), dx_unblob(&ps);
    if (dx_compile(EASU_HLSL, "vs", "vs_5_0", &vs) && dx_compile(EASU_HLSL, "ps", "ps_5_0", &ps)) {
        DXS.pso_easu = blit_pso(vs, ps, 1, DXGI_FORMAT_R8G8B8A8_UNORM);
    }
    dx_unblob(&vs), dx_unblob(&ps);
}

/* one of SMAA's tables, uploaded and waited for (once) */
static Tex smaa_table(const unsigned char *data, int w, int h, int bpp, DXGI_FORMAT fmt) {
    Tex t = dx_texture(w, h, fmt, 0, D3D12_RESOURCE_STATE_COPY_DEST, NULL);
    UINT pitch = (UINT)((w * bpp + 255) & ~255);
    UINT8 *m = NULL;
    ID3D12Resource *up = dx_buffer(D3D12_HEAP_TYPE_UPLOAD, (UINT64)pitch * (UINT)h, D3D12_RESOURCE_STATE_GENERIC_READ, &m);
    D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
    int y;

    if (t.r == NULL || up == NULL) {
        if (t.r != NULL) ID3D12Resource_Release(t.r);
        if (up != NULL) ID3D12Resource_Release(up);
        t.r = NULL;
        return t;
    }
    for (y = 0; y < h; y++) {
        SDL_memcpy(m + (size_t)y * pitch, data + (size_t)y * w * bpp, (size_t)w * bpp);
    }
    dx_list();
    dst.pResource = t.r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.pResource = up, src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint.Format = fmt, src.PlacedFootprint.Footprint.Width = (UINT)w, src.PlacedFootprint.Footprint.Height = (UINT)h;
    src.PlacedFootprint.Footprint.Depth = 1, src.PlacedFootprint.Footprint.RowPitch = pitch;
    ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, NULL);
    dx_trans(&t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    dx_submit();
    ID3D12Resource_Release(up);
    return t;
}

/* SMAA's passes, when first asked for (its shader is the big one to compile): where it does not build, its option does nothing */
static void smaa_init(void) {
    static const char *VS[3] = {"svs0", "svs1", "svs2"}, *PS[3] = {"sps0", "sps1", "sps2"};
    ID3DBlob *vs[3] = {0}, *ps[3] = {0};
    size_t n = SDL_strlen(SMAA_HEAD) + SDL_strlen(SMAA_SRC) + SDL_strlen(SMAA_GLUE) + 1;
    char *src = SDL_malloc(n), *c;
    int i, ok = src != NULL;

    if (ok) {
        SDL_snprintf(src, n, "%s%s%s", SMAA_HEAD, SMAA_SRC, SMAA_GLUE);
        for (c = src; *c != 0; c++) { /* its comments have a letter or two that aren't ASCII */
            if ((unsigned char)*c > 126) {
                *c = ' ';
            }
        }
    }
    for (i = 0; ok && i < 3; i++) {
        ok = dx_compile(src, VS[i], "vs_5_0", &vs[i]) && dx_compile(src, PS[i], "ps_5_0", &ps[i]);
    }
    SDL_free(src);
    DXS.area = smaa_table(areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, 2, DXGI_FORMAT_R8G8_UNORM);
    DXS.search = smaa_table(searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1, DXGI_FORMAT_R8_UNORM);
    ok = ok && DXS.smaa_root != NULL && DXS.area.r != NULL && DXS.search.r != NULL;
    if (!ok) {
        SDL_Log("d3d12: SMAA does not build here");
    }
    for (i = 0; i < 3; i++) {
        if (ok) {
            DXS.pso_smaa[i] = dx_blit_pso_on(DXS.smaa_root, vs[i], ps[i], 1, DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        dx_unblob(&vs[i]), dx_unblob(&ps[i]);
    }
    if (!ok) {
        return;
    }
    dx_srv(&DXS.area, S_AREA, DXGI_FORMAT_R8G8_UNORM);
    dx_srv(&DXS.search, S_SEARCH, DXGI_FORMAT_R8_UNORM);
    dx_srv(&DXS.area, S_T1 + 1, DXGI_FORMAT_R8G8_UNORM);
    dx_srv(&DXS.search, S_T1 + 2, DXGI_FORMAT_R8_UNORM);
}

static Tex pass_target(int w, int h, int rtv, int slot, DXGI_FORMAT fmt) {
    D3D12_CLEAR_VALUE cc = {fmt, {{0, 0, 0, 0}}};
    Tex t = dx_texture(w, h, fmt, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &cc);

    if (t.r != NULL) {
        ID3D12Device_CreateRenderTargetView(DXS.dev, t.r, NULL, dx_rtv_at(rtv));
        dx_srv(&t, slot, fmt);
    }
    return t;
}

/* Before the frame's list is opened: which of the passes run, and their targets the size of the picture (pw x ph) and of where it is shown */
void dx_post_prepare(const PSX_RECT *d, const int vp[4]) {
    int pw = d->w * DXS.scale, ph = d->h * DXS.scale, i, f3, fw, fh;

    if (g_smaa && !DXS.smaa_tried) {
        DXS.smaa_tried = 1;
        smaa_init();
    }
    s_do_smaa = g_smaa && DXS.pso_smaa[2] != NULL;
    dx_f3_size(pw, ph, vp, &fw, &fh);
    f3 = ((g_fsr == 2 && DXS.fsr3 && fx_loaded()) || (g_fsr == 3 && DXS.dlss && sl_state() > 0)) && DXS.frame_3d && fw >= pw && fh >= ph;
    s_do_fsr = g_fsr && DXS.pso_easu != NULL && vp[3] > ph && DXS.frame_3d;
    if ((s_do_smaa || f3) && (pw != DXS.p_w || ph != DXS.p_h)) {
        dx_submit(); /* the last frames' passes are done before their targets go */
        for (i = 0; i < 3; i++) {
            if (DXS.p[i].r != NULL) ID3D12Resource_Release(DXS.p[i].r);
            DXS.p[i] = pass_target(pw, ph, R_P0 + i, S_P0 + i, DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        for (i = 0; i < 3; i++) {
            dx_srv(&DXS.p[0], S_T0 + i, DXGI_FORMAT_R8G8B8A8_UNORM); /* (the first pass reads its t0 only) */
        }
        dx_srv(&DXS.p[1], S_T1, DXGI_FORMAT_R8G8B8A8_UNORM);
        dx_srv(&DXS.p[0], S_T2, DXGI_FORMAT_R8G8B8A8_UNORM);
        dx_srv(&DXS.p[2], S_T2 + 1, DXGI_FORMAT_R8G8B8A8_UNORM);
        dx_srv(&DXS.p[2], S_T2 + 2, DXGI_FORMAT_R8G8B8A8_UNORM);
        DXS.p_w = pw, DXS.p_h = ph;
    }
    s_do_smaa = s_do_smaa && DXS.p[0].r != NULL && DXS.p[1].r != NULL && DXS.p[2].r != NULL;
    if (s_do_fsr && (vp[2] != DXS.fsr_w || vp[3] != DXS.fsr_h)) {
        dx_submit();
        if (DXS.fsr.r != NULL) ID3D12Resource_Release(DXS.fsr.r);
        DXS.fsr = pass_target(vp[2], vp[3], R_FSR, S_FSR, DXGI_FORMAT_R8G8B8A8_UNORM);
        DXS.fsr_w = vp[2], DXS.fsr_h = vp[3];
    }
    s_do_fsr = s_do_fsr && DXS.fsr.r != NULL;
    DXS.do_depth = DXS.data_depth && DXS.frame_3d && DXS.pso_dout != NULL && (DXS.sd_valid || DXS.fd_valid);
    if (DXS.do_depth && (pw != DXS.dout_w || ph != DXS.dout_h)) {
        dx_submit();
        if (DXS.dout.r != NULL) ID3D12Resource_Release(DXS.dout.r);
        DXS.dout = pass_target(pw, ph, R_DOUT, S_DOUT, DXGI_FORMAT_R32_FLOAT);
        DXS.dout_w = pw, DXS.dout_h = ph;
    }
    DXS.do_depth = DXS.do_depth && DXS.dout.r != NULL;
    DXS.do_mout = DXS.data_motion && DXS.frame_3d && DXS.pso_mcopy != NULL && DXS.mv.r != NULL;
    if (DXS.do_mout && (pw != DXS.mout_w || ph != DXS.mout_h)) {
        dx_submit();
        if (DXS.mout.r != NULL) ID3D12Resource_Release(DXS.mout.r);
        DXS.mout = pass_target(pw, ph, R_MOUT, S_MOUT, DXGI_FORMAT_R16G16_FLOAT);
        DXS.mout_w = pw, DXS.mout_h = ph;
    }
    DXS.do_mout = DXS.do_mout && DXS.mout.r != NULL;
    DXS.do_f3 = f3 && DXS.do_depth && DXS.do_mout && DXS.p[0].r != NULL;
    if (DXS.do_f3 && (fw != DXS.f3_w || fh != DXS.f3_h)) {
        dx_submit();
        if (DXS.f3.r != NULL) {
            ID3D12Resource_Release(DXS.f3.r);
        }
        DXS.f3 = dx_texture(fw, fh, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, NULL);
        if (DXS.f3.r != NULL) {
            dx_srv(&DXS.f3, S_F3, DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        DXS.f3_w = fw, DXS.f3_h = fh;
    }
    DXS.do_f3 = DXS.do_f3 && DXS.f3.r != NULL;
    if (DXS.fsr3 && DXS.frame_3d) { /* what the menu says of it, and the log once */
        int why = !(fw >= pw && fh >= ph), now = DXS.do_f3 ? 3 : why ? 2 : 1;

        if (now != DXS.f3_state) {
            SDL_Log(now == 3 ? "fsr3: running" : now == 2 ? "fsr3: idle, the render (%dx%d) is bigger than the window's picture (%dx%d): lower the Resolution" : "fsr3: idle, the data is not ready (%dx%d)", pw, ph, fw, fh);
            DXS.f3_state = now;
        }
    }
    if (DXS.do_f3) { /* it does the smoothing and the upscaling */
        s_do_smaa = s_do_fsr = 0;
    }
}

/* target t (index rtv in the RTV heap) set to be drawn into, cleared when asked */
void dx_rt_pass(Tex *t, int rtv, int clear) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = dx_rtv_at(rtv);
    const float zero[4] = {0, 0, 0, 0};

    dx_trans(t, D3D12_RESOURCE_STATE_RENDER_TARGET);
    ID3D12GraphicsCommandList_OMSetRenderTargets(DXS.list, 1, &h, FALSE, NULL);
    if (clear) {
        ID3D12GraphicsCommandList_ClearRenderTargetView(DXS.list, h, zero, 0, NULL);
    }
}

/* the strip over the viewport vp, reading the views from SLOT on, with the 12 constants k */
void dx_draw_k(ID3D12RootSignature *rs, ID3D12PipelineState *pso, int slot, const int vp[4], const float k[12]) {
    D3D12_VIEWPORT v = {(float)vp[0], (float)vp[1], (float)vp[2], (float)vp[3], 0, 1};
    D3D12_RECT sc = {vp[0], vp[1], vp[0] + vp[2], vp[1] + vp[3]};
    D3D12_GPU_DESCRIPTOR_HANDLE g = ID3D12DescriptorHeap_GetGPUDescriptorHandleForHeapStart(DXS.srv_heap);

    g.ptr += (UINT64)slot * DXS.srv_inc;
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(DXS.list, rs);
    ID3D12GraphicsCommandList_SetPipelineState(DXS.list, pso);
    ID3D12GraphicsCommandList_RSSetViewports(DXS.list, 1, &v);
    ID3D12GraphicsCommandList_RSSetScissorRects(DXS.list, 1, &sc);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(DXS.list, 0, g);
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(DXS.list, 1, 12, k, 0);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(DXS.list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ID3D12GraphicsCommandList_DrawInstanced(DXS.list, 4, 1, 0, 0);
}

/* The display area d of the copy through SMAA and FSR 1, those that are on: the SRV slot of what to show, and in it the rectangle uv */
int dx_post_chain(const PSX_RECT *d, const int vp[4], float uv[4]) {
    int s = DXS.scale, pw = d->w * s, ph = d->h * s, ox = d->x * s, oy = d->y * s, slot = S_RT, full[4] = {0, 0, pw, ph};
    float k[12] = {0, 0, 1, 1};

    uv[0] = d->x / (float)VRAM_W, uv[1] = d->y / (float)VRAM_H, uv[2] = d->w / (float)VRAM_W, uv[3] = d->h / (float)VRAM_H;
    if (!s_do_smaa && !s_do_fsr) {
        return slot;
    }
    dx_mark(C_POST);
    if (s_do_smaa) {
        D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
        D3D12_BOX box = {(UINT)ox, (UINT)oy, 0, (UINT)(ox + pw), (UINT)(oy + ph), 1};

        dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dx_trans(&DXS.p[0], D3D12_RESOURCE_STATE_COPY_DEST);
        dst.pResource = DXS.p[0].r, dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = DXS.rt.r, src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        ID3D12GraphicsCommandList_CopyTextureRegion(DXS.list, &dst, 0, 0, 0, &src, &box);
        dx_trans(&DXS.p[0], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        dx_trans(&DXS.rt, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        k[4] = 1.0f / pw, k[5] = 1.0f / ph, k[6] = (float)pw, k[7] = (float)ph;
        dx_rt_pass(&DXS.p[1], R_P1, 1); /* the edges */
        dx_draw_k(DXS.smaa_root, DXS.pso_smaa[0], S_T0, full, k);
        dx_trans(&DXS.p[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        dx_rt_pass(&DXS.p[2], R_P2, 1); /* how much each pixel blends */
        dx_draw_k(DXS.smaa_root, DXS.pso_smaa[1], S_T1, full, k);
        dx_trans(&DXS.p[2], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        dx_rt_pass(&DXS.p[1], R_P1, 0); /* the picture blended */
        dx_draw_k(DXS.smaa_root, DXS.pso_smaa[2], S_T2, full, k);
        dx_trans(&DXS.p[1], D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        slot = S_P1, ox = oy = 0;
        uv[0] = uv[1] = 0, uv[2] = uv[3] = 1;
    }
    if (s_do_fsr) {
        int out[4] = {0, 0, vp[2], vp[3]};
        float e[12] = {0, 0, 1, 1, pw / (float)vp[2], ph / (float)vp[3], 0, 0, (float)ox, (float)oy, (float)pw, (float)ph};

        dx_rt_pass(&DXS.fsr, R_FSR, 0);
        dx_draw_k(DXS.blit_root, DXS.pso_easu, slot, out, e);
        dx_trans(&DXS.fsr, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        slot = S_FSR;
        uv[0] = uv[1] = 0, uv[2] = uv[3] = 1;
    }
    dx_mark(C_PRESENT);
    return slot;
}
