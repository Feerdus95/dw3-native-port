/* The Direct3D 12 renderer's shaders for the game's drawing (HLSL), its pipeline states and root signatures */
#include "dx.h"

/* --- shaders (the port of the OpenGL renderer's MAIN_VS / MAIN_FS; see there for what each mode bit means) --- */

const char *MAIN_HLSL =
    "Texture2D<uint> vram : register(t0);\n"
    "cbuffer K : register(b0) {\n"
    "    float2 size; float2 jit; float4 fog; float2 fogz; float scale; int persp; int pas; int truecolor; int smooth3d; int pad;\n"
    "};\n"
    "struct VI { float4 pos_uv : POSITION; uint4 colr : COLOR; uint4 tex : TEXCOORD0; uint4 limits : TEXCOORD1;\n"
    "            uint4 window : TEXCOORD2; uint mode : TEXCOORD3; float z : TEXCOORD4; float w : TEXCOORD5; float2 mv : TEXCOORD6; };\n"
    "struct VO { float4 p : SV_Position; float2 uv : TEXCOORD0; float3 col : TEXCOORD1; float vw : TEXCOORD2;\n"
    "            nointerpolation uint4 t : TEXCOORD3; nointerpolation uint4 lim : TEXCOORD4; nointerpolation uint4 tw : TEXCOORD5;\n"
    "            nointerpolation uint md : TEXCOORD6; nointerpolation uint deep : TEXCOORD7; float2 mv : TEXCOORD8; };\n"
    "VO vs(VI i) {\n"
    "    VO o;\n"
    "    float cz = i.z > 0.0 ? clamp(1.0 - 64.0 / i.z, 0.0, 1.0) : 0.0;\n"
    "    o.p = float4(i.pos_uv.x / size.x * 2.0 - 1.0, 1.0 - i.pos_uv.y / size.y * 2.0, cz, 1.0);\n"
    "    o.p *= (persp != 0 && i.w > 0.0) ? i.w : 1.0;\n"
    "    if (i.w > 0.0) o.p.xy += jit * o.p.w;\n"
    "    o.uv = i.pos_uv.zw; o.col = float3(i.colr.xyz); o.t = i.tex; o.lim = i.limits; o.tw = i.window; o.md = i.mode;\n"
    "    o.deep = i.w > 0.0 ? 1u : 0u; o.vw = i.w;\n"
    "    o.mv = i.w > 0.0 ? i.mv * scale : float2(0.0, 0.0);\n"
    "    return o;\n"
    "}\n"
    "static const float DITHER[16] = {-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0};\n"
    "static uint4 t, lim, tw;\n"
    "static uint md;\n"
    "uint fetch(int x, int y) { return vram.Load(int3(x & 1023, y & 511, 0)); }\n"
    "float3 rgb5(uint p) { return float3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u); }\n"
    "float nz(uint v) { return v != 0u ? 1.0 : 0.0; }\n"
    "uint texel(int u, int v) {\n"
    "    u = clamp(u, (int)lim.x, (int)lim.z) & 255;\n"
    "    v = clamp(v, (int)lim.y, (int)lim.w) & 255;\n"
    "    u = (u & ~(int)tw.x) | ((int)tw.z & (int)tw.x);\n"
    "    v = (v & ~(int)tw.y) | ((int)tw.w & (int)tw.y);\n"
    "    uint depth = (md >> 4) & 3u;\n"
    "    if (depth == 0u) {\n"
    "        uint w = fetch((int)t.z + u / 4, (int)t.w + v);\n"
    "        return fetch((int)t.x + (int)((w >> (uint)((u & 3) * 4)) & 15u), (int)t.y);\n"
    "    } else if (depth == 1u) {\n"
    "        uint w = fetch((int)t.z + u / 2, (int)t.w + v);\n"
    "        return fetch((int)t.x + (int)((w >> (uint)((u & 1) * 8)) & 255u), (int)t.y);\n"
    "    }\n"
    "    return fetch((int)t.z + u, (int)t.w + v);\n"
    "}\n"
    "float4 ps(VO i, float4 fc : SV_Position) : SV_Target {\n"
    "    t = i.t; lim = i.lim; tw = i.tw; md = i.md;\n"
    "    float2 uv = i.uv; float3 col = i.col; uint deep = i.deep; float vw = i.vw;\n"
    "    uint pix = 0u;\n"
    "    float3 tex = float3(0.0, 0.0, 0.0);\n"
    "    bool semi = (md & 8u) != 0u;\n"
    "    float3 c8 = col, c5;\n"
    "    float cover = 1.0;\n"
    "    if ((md & 1024u) != 0u) {\n"
    "        if (pas == 1) discard;\n"
    "        return float4(1.0, 1.0, 1.0, 1.0);\n"
    "    }\n"
    "    if ((md & 128u) != 0u) {\n"
    "        pix = fetch((int)(fc.x / scale), (int)(fc.y / scale));\n"
    "        return float4(rgb5(pix) / 31.0, (float)(pix >> 15));\n"
    "    }\n"
    "    if ((md & 2u) != 0u) {\n"
    "        float2 at = uv - 0.5 * ddx(uv) - 0.5 * ddy(uv);\n"
    "        if ((md & 512u) != 0u) {\n"
    "            float2 sz = float2(lim.zw - lim.xy) + 1.0, mid = float2(lim.xy) + sz * float2(0.484, 0.469);\n"
    "            cover = 1.0 - smoothstep(0.6, 1.0, length((uv - mid) / (sz * float2(0.42, 0.4))));\n"
    "            if (cover <= 0.0) discard;\n"
    "            at = mid;\n"
    "        }\n"
    "        pix = texel((int)floor(at.x + 0.002), (int)floor(at.y + 0.002));\n"
    "        tex = rgb5(pix);\n"
    "        if (smooth3d != 0 && deep != 0u && (md & 512u) == 0u) {\n"
    "            float2 p = uv - 0.5;\n"
    "            int2 ii = (int2)floor(p);\n"
    "            float2 f = p - float2(ii);\n"
    "            uint a = texel(ii.x, ii.y), b = texel(ii.x + 1, ii.y), c = texel(ii.x, ii.y + 1), e = texel(ii.x + 1, ii.y + 1);\n"
    "            float4 k = float4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y) *\n"
    "                       float4(nz(a), nz(b), nz(c), nz(e));\n"
    "            float sum = k.x + k.y + k.z + k.w;\n"
    "            if (sum < 0.5) discard;\n"
    "            tex = (rgb5(a) * k.x + rgb5(b) * k.y + rgb5(c) * k.z + rgb5(e) * k.w) / sum;\n"
    "            if (pix == 0u) pix = a != 0u ? a : b != 0u ? b : c != 0u ? c : e;\n"
    "        }\n"
    "        if (pix == 0u) discard;\n"
    "        semi = semi && (pix & 0x8000u) != 0u;\n"
    "        c8 = floor(tex * col / 16.0);\n"
    "    }\n"
    "    if ((pas == 1 && semi) || (pas == 2 && !semi)) discard;\n"
    "    if ((md & 6u) == 6u) {\n"
    "        c5 = tex;\n"
    "    } else {\n"
    "        float d = (md & 64u) != 0u ? DITHER[((int)fc.y & 3) * 4 + ((int)fc.x & 3)] : 0.0;\n"
    "        c5 = truecolor != 0 ? clamp(c8 / 8.0, 0.0, 31.0) : clamp(floor((c8 + d) / 8.0), 0.0, 31.0);\n"
    "    }\n"
    "    if ((md & 512u) != 0u) c5 = float3(18.6 * cover, 18.6 * cover, 18.6 * cover);\n"
    "    else if (fog.a > 0.0 && deep != 0u && !semi && (md & 2048u) == 0u) c5 = lerp(c5, fog.rgb, fog.a * smoothstep(fogz.x, fogz.y, vw));\n"
    "    return float4(c5 / 31.0, ((md & 256u) != 0u || (pix & 0x8000u) != 0u) ? 1.0 : 0.0);\n"
    "}\n"
    /* with the motion target (DW3_UPSCALE_DATA=motion): the picture, and in the second target the 3D's motion in pixels of the copy (0 for 2D), and 1 where the upscaler should trust its history less: the 2D, and a small 3D window's (the portrait) */
    "struct PM { float4 c : SV_Target0; float4 m : SV_Target1; };\n"
    "PM psm(VO i, float4 fc : SV_Position) {\n"
    "    PM o;\n"
    "    o.c = ps(i, fc);\n"
    "    o.m = float4(i.deep != 0u ? i.mv : float2(0.0, 0.0), 0.0, 0.0);\n"
    "    return o;\n"
    "}\n";

/* A texture on a rectangle: where it goes is the viewport, which part of the texture the constants */
const char *BLIT_HLSL =
    "cbuffer C : register(b0) { float4 uv; };\n"
    "Texture2D tx : register(t0);\n"
    "SamplerState lin : register(s0);\n"
    "SamplerState pt : register(s1);\n"
    "struct VO { float4 p : SV_Position; float2 t : TEXCOORD0; nointerpolation float4 k : TEXCOORD1; };\n"
    "VO vs(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VO o;\n"
    "    o.p = float4(c.x * 2 - 1, 1 - c.y * 2, 0, 1);\n"
    "    o.t = uv.xy + c * uv.zw;\n"
    "    o.k = uv;\n"
    "    return o;\n"
    "}\n"
    "float4 ps(VO i) : SV_Target { return tx.Sample(lin, i.t); }\n"
    "float4 psn(VO i) : SV_Target { return tx.Sample(pt, i.t); }\n"
    /* the battle shadows' blur (the OpenGL renderer's SOFT_FS): k.xy a tap's step in pixels, k.z 0 for the blurred mask itself, else how much of the ground it takes */
    /* a gaussian of 9 taps as 5 linear ones (the taps in pairs, between their two texels) */
    "float4 psb(VO i) : SV_Target {\n"
    "    float2 sz; tx.GetDimensions(sz.x, sz.y);\n"
    "    float2 c = i.p.xy / sz, d = i.k.xy / sz;\n"
    "    float m = tx.Sample(lin, c).r * 0.227027;\n"
    "    m += (tx.Sample(lin, c + d * 1.3846154).r + tx.Sample(lin, c - d * 1.3846154).r) * 0.3162162;\n"
    "    m += (tx.Sample(lin, c + d * 3.2307692).r + tx.Sample(lin, c - d * 3.2307692).r) * 0.07027;\n"
    "    return i.k.z > 0.0 ? float4(m * i.k.z, m * i.k.z, m * i.k.z, 0.0) : float4(m, m, m, m);\n"
    "}\n";

/* --- pipelines --- */

static void blend_of(D3D12_RENDER_TARGET_BLEND_DESC *b, int semi) {
    b->BlendEnable = semi >= 0;
    b->BlendOp = b->BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b->SrcBlendAlpha = D3D12_BLEND_ONE, b->DestBlendAlpha = D3D12_BLEND_ZERO;
    b->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    switch (semi) {
    case 0: /* (back + front) / 2: the blend factor is 0.5 */
        b->SrcBlend = b->DestBlend = D3D12_BLEND_BLEND_FACTOR;
        break;
    case 1: /* back + front */
        b->SrcBlend = b->DestBlend = D3D12_BLEND_ONE;
        break;
    case 2: /* back - front */
        b->SrcBlend = b->DestBlend = D3D12_BLEND_ONE;
        b->BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
        b->SrcBlendAlpha = D3D12_BLEND_ZERO, b->DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    case 4: /* back * (1 - front): a soft shadow */
        b->SrcBlend = D3D12_BLEND_ZERO, b->DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        b->SrcBlendAlpha = D3D12_BLEND_ZERO, b->DestBlendAlpha = D3D12_BLEND_ONE;
        break;
    default: /* back + front / 4: the blend factor is 0.25 */
        b->SrcBlend = D3D12_BLEND_BLEND_FACTOR, b->DestBlend = D3D12_BLEND_ONE;
        break;
    }
}

/* semi: -1 none; depth: 0 off, 1 test and write, 2 test only */
ID3D12PipelineState *dx_main_pso(int semi, int depth) {
    int k = (semi + 1) * 3 + depth;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {0};

    if (DXS.pso_main[k] != NULL) {
        return DXS.pso_main[k];
    }
    d.pRootSignature = DXS.main_root;
    d.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(DXS.main_vs), d.VS.BytecodeLength = ID3D10Blob_GetBufferSize(DXS.main_vs);
    d.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(DXS.data_motion ? DXS.main_psm : DXS.main_ps), d.PS.BytecodeLength = ID3D10Blob_GetBufferSize(DXS.data_motion ? DXS.main_psm : DXS.main_ps);
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID, d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE, d.RasterizerState.DepthClipEnable = TRUE;
    blend_of(&d.BlendState.RenderTarget[0], semi);
    if (DXS.data_motion) { /* the motion is written by the opaque pass alone, over what is there */
        d.BlendState.IndependentBlendEnable = TRUE;
        d.BlendState.RenderTarget[1].RenderTargetWriteMask = semi < 0 ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
    }
    d.DepthStencilState.DepthEnable = depth != 0;
    d.DepthStencilState.DepthWriteMask = depth == 1 ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.InputLayout.pInputElementDescs = DXS.layout, d.InputLayout.NumElements = 9;
    d.SampleMask = 0xFFFFFFFFu, d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = DXS.data_motion ? 2 : 1, d.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM; /* past NumRenderTargets: UNKNOWN, or no state */
    d.RTVFormats[1] = DXS.data_motion ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_UNKNOWN;
    d.DSVFormat = DXGI_FORMAT_D32_FLOAT, d.SampleDesc.Count = 1;
    if (!OK(ID3D12Device_CreateGraphicsPipelineState(DXS.dev, &d, &IID_ID3D12PipelineState, (void **)&DXS.pso_main[k]))) {
        SDL_Log("d3d12: pipeline %d", k);
    }
    return DXS.pso_main[k];
}

/* The battle shadows' batches: the main shader into the R8 mask, the largest of what overlaps (a pixel once), no depth */
ID3D12PipelineState *dx_mask_pso(int depth) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {0};
    D3D12_RENDER_TARGET_BLEND_DESC *b = &d.BlendState.RenderTarget[0];
    ID3D12PipelineState *p = NULL;

    d.pRootSignature = DXS.main_root;
    d.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(DXS.main_vs), d.VS.BytecodeLength = ID3D10Blob_GetBufferSize(DXS.main_vs);
    d.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(DXS.main_ps), d.PS.BytecodeLength = ID3D10Blob_GetBufferSize(DXS.main_ps);
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID, d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE, d.RasterizerState.DepthClipEnable = TRUE;
    b->BlendEnable = TRUE, b->SrcBlend = b->DestBlend = b->SrcBlendAlpha = b->DestBlendAlpha = D3D12_BLEND_ONE;
    b->BlendOp = b->BlendOpAlpha = D3D12_BLEND_OP_MAX, b->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    /* the models' depth hides what is behind them (not written), the shadow lying on the ground it falls on (the OpenGL renderer's polygon offset) */
    d.DepthStencilState.DepthEnable = depth != 0, d.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    d.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    d.RasterizerState.DepthBias = -4, d.RasterizerState.SlopeScaledDepthBias = -1.0f;
    d.InputLayout.pInputElementDescs = DXS.layout, d.InputLayout.NumElements = 9;
    d.SampleMask = 0xFFFFFFFFu, d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1, d.RTVFormats[0] = DXGI_FORMAT_R8_UNORM, d.DSVFormat = DXGI_FORMAT_D32_FLOAT, d.SampleDesc.Count = 1;
    ID3D12Device_CreateGraphicsPipelineState(DXS.dev, &d, &IID_ID3D12PipelineState, (void **)&p);
    return p;
}

/* write_alpha: 0 the window's (opaque), 1 the copy's, 2 over (the menu's alpha), 3 back * (1 - front) onto the copy (the shadows); fmt the target's */
ID3D12PipelineState *dx_blit_pso_on(ID3D12RootSignature *rs, ID3DBlob *vs, ID3DBlob *ps, int write_alpha, DXGI_FORMAT fmt) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {0};
    ID3D12PipelineState *p = NULL;

    d.pRootSignature = rs;
    d.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs), d.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    d.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps), d.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID, d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE, d.RasterizerState.DepthClipEnable = TRUE;
    d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE, d.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    d.BlendState.RenderTarget[0].BlendOp = d.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    d.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE, d.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    /* on screen the window stays opaque: the copy's alpha is the mask bit */
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = write_alpha ? D3D12_COLOR_WRITE_ENABLE_ALL : 7;
    if (write_alpha == 2) { /* the menu's overlay: its alpha is 0 where it is clear */
        d.BlendState.RenderTarget[0].BlendEnable = TRUE;
        d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA, d.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = 7;
    }
    if (write_alpha == 4 || write_alpha == 5) { /* a premultiplied image over the copy, or added to it; the copy's alpha (its mask bit) is kept */
        d.BlendState.RenderTarget[0].BlendEnable = TRUE;
        d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
        d.BlendState.RenderTarget[0].DestBlend = write_alpha == 5 ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
        d.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
        d.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
        d.BlendState.RenderTarget[0].RenderTargetWriteMask = 7;
    }
    if (write_alpha == 3) {
        d.BlendState.RenderTarget[0].BlendEnable = TRUE;
        d.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO, d.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_COLOR;
        d.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    }
    d.SampleMask = 0xFFFFFFFFu, d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1, d.RTVFormats[0] = fmt, d.SampleDesc.Count = 1;
    ID3D12Device_CreateGraphicsPipelineState(DXS.dev, &d, &IID_ID3D12PipelineState, (void **)&p);
    return p;
}

ID3D12RootSignature *dx_root(const D3D12_ROOT_SIGNATURE_DESC *rsd) {
    ID3DBlob *rs = NULL, *err = NULL;
    ID3D12RootSignature *r = NULL;

    if (OK(p_D3D12SerializeRootSignature(rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &err))) {
        ID3D12Device_CreateRootSignature(DXS.dev, 0, ID3D10Blob_GetBufferPointer(rs), ID3D10Blob_GetBufferSize(rs), &IID_ID3D12RootSignature, (void **)&r);
    }
    dx_unblob(&rs), dx_unblob(&err);
    return r;
}

int dx_compile(const char *src, const char *entry, const char *target, ID3DBlob **out) {
    ID3DBlob *err = NULL;
    Uint64 t0 = SDL_GetTicksNS();
    HRESULT hr = p_D3DCompile(src, SDL_strlen(src), NULL, NULL, NULL, entry, target, 0, 0, out, &err);

    DXS.compile_ns += SDL_GetTicksNS() - t0;
    if (!OK(hr)) {
        SDL_Log("d3d12: shader %s: %s", entry, err != NULL ? (const char *)ID3D10Blob_GetBufferPointer(err) : "?");
        dx_unblob(&err);
        return 0;
    }
    dx_unblob(&err);
    return 1;
}

void dx_unblob(ID3DBlob **b) {
    if (*b != NULL) {
        ID3D10Blob_Release(*b);
        *b = NULL;
    }
}
