/* The picture's passes on Direct3D 12, in HLSL: what gl.c's POST_FS, EASU_FS and SMAA do, written again for this backend
   (included once, by d3d12.c). All three read the 12 root constants as uv (the rectangle of the texture, for the vertex
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

/* SMAA 1x: a head that sets their shader up, the shader as they publish it (SMAA_SRC, embedded by d3d12.c), and the three
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
