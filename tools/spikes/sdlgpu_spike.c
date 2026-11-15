/* Phase 2 spike: can SDL3's GPU API (32-bit, as the port is) do what the PS1 renderer needs?
   Usage: spike.exe <driver>   (direct3d12 | vulkan) */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

typedef struct IBlobVtbl IBlobVtbl;
typedef struct { const IBlobVtbl *v; } IBlob;
struct IBlobVtbl {
    void *qi, *addref;
    ULONG(__stdcall *release)(IBlob *);
    void *(__stdcall *ptr)(IBlob *);
    SIZE_T(__stdcall *size)(IBlob *);
};
typedef HRESULT(__stdcall *D3DCompileFn)(const void *, SIZE_T, const char *, const void *, void *, const char *, const char *, UINT, UINT, IBlob **, IBlob **);

static const char *HLSL_VS =
    "float4 main(uint id : SV_VertexID) : SV_Position {\n"
    "    float2 p = float2((id << 1) & 2, id & 2);\n"
    "    return float4(p * 2.0 - 1.0, 0, 1);\n"
    "}\n";
/* the PS1 path in miniature: an integer R16 texel fetched by position, split into 5-bit channels, plus integer shifts */
static const char *HLSL_PS =
    "Texture2D<uint> vram : register(t0, space2);\n"
    "SamplerState smp : register(s0, space2);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target {\n"
    "    uint v = vram.Load(int3((int)pos.x, 0, 0));\n"
    "    uint r = v & 31u, g = (v >> 5) & 31u, b = (v >> 10) & 31u, m = (v >> 15) & 1u;\n"
    "    return float4(r / 31.0, g / 31.0, b / 31.0, m);\n"
    "}\n";

static int check(int ok, const char *what) {
    printf("  %-58s %s\n", what, ok ? "OK" : "FAIL");
    return ok;
}

static SDL_GPUShader *compile(SDL_GPUDevice *dev, D3DCompileFn d3dc, const char *src, const char *target, SDL_GPUShaderStage stage, int samplers) {
    IBlob *code = NULL, *err = NULL;
    SDL_GPUShaderCreateInfo ci;
    SDL_GPUShader *sh;
    if (d3dc(src, strlen(src), "spike", NULL, NULL, "main", target, 0, 0, &code, &err) < 0) {
        printf("  HLSL: %s\n", err ? (char *)err->v->ptr(err) : "?");
        return NULL;
    }
    SDL_zero(ci);
    ci.code = (const Uint8 *)code->v->ptr(code);
    ci.code_size = code->v->size(code);
    ci.entrypoint = "main";
    ci.format = SDL_GPU_SHADERFORMAT_DXBC;
    ci.stage = stage;
    ci.num_samplers = (Uint32)samplers;
    sh = SDL_CreateGPUShader(dev, &ci);
    code->v->release(code);
    return sh;
}

static SDL_GPUGraphicsPipeline *pipeline(SDL_GPUDevice *dev, SDL_GPUShader *vs, SDL_GPUShader *ps, SDL_GPUColorTargetBlendState blend, int depth) {
    SDL_GPUColorTargetDescription ct;
    SDL_GPUGraphicsPipelineCreateInfo pi;
    SDL_zero(ct);
    ct.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ct.blend_state = blend;
    SDL_zero(pi);
    pi.vertex_shader = vs;
    pi.fragment_shader = ps;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions = &ct;
    pi.target_info.num_color_targets = 1;
    if (depth) {
        pi.rasterizer_state.enable_depth_bias = true;
        pi.rasterizer_state.depth_bias_constant_factor = -4.0f;
        pi.rasterizer_state.depth_bias_slope_factor = -1.0f;
        pi.depth_stencil_state.enable_depth_test = true;
        pi.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        pi.target_info.has_depth_stencil_target = true;
        pi.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    return SDL_CreateGPUGraphicsPipeline(dev, &pi);
}

int main(int argc, char **argv) {
    const char *driver = argc > 1 ? argv[1] : "direct3d12";
    int d3d = SDL_strcmp(driver, "direct3d12") == 0, fails = 0;
    SDL_GPUDevice *dev;
    printf("driver requested: %s, pointer size %d\n", driver, (int)sizeof(void *));
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    dev = SDL_CreateGPUDevice(d3d ? SDL_GPU_SHADERFORMAT_DXBC : SDL_GPU_SHADERFORMAT_SPIRV, false, driver);
    if (!dev) {
        printf("SDL_CreateGPUDevice: %s\n", SDL_GetError());
        return 1;
    }
    printf("device driver: %s\n", SDL_GetGPUDeviceDriver(dev));
    printf("formats:\n");
    fails += !check(SDL_GPUTextureSupportsFormat(dev, SDL_GPU_TEXTUREFORMAT_R16_UINT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_SAMPLER), "R16_UINT sampled (the 1x VRAM)");
    fails += !check(SDL_GPUTextureSupportsFormat(dev, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER), "RGBA8 target + sampled (the scaled copy)");
    fails += !check(SDL_GPUTextureSupportsFormat(dev, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET), "D32_FLOAT_S8 depth-stencil target");
    fails += !check(SDL_GPUTextureSupportsFormat(dev, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER), "D32_FLOAT target + sampled (depth for the props)");
    fails += !check(SDL_GPUTextureSupportsFormat(dev, SDL_GPU_TEXTUREFORMAT_R8_UNORM, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER), "R8 target + sampled (shadow mask)");
    if (!d3d) {
        printf("(Vulkan: no SPIR-V compiler here, so no shader test)\n");
        SDL_DestroyGPUDevice(dev);
        return fails != 0;
    }
    {
        HMODULE lib = LoadLibraryA("d3dcompiler_47.dll");
        D3DCompileFn d3dc = lib ? (D3DCompileFn)GetProcAddress(lib, "D3DCompile") : NULL;
        SDL_GPUShader *vs, *ps;
        SDL_GPUGraphicsPipeline *pl;
        SDL_GPUColorTargetBlendState blend;
        SDL_GPUTextureCreateInfo ti;
        SDL_GPUTexture *vram, *target;
        SDL_GPUTransferBufferCreateInfo tbi;
        SDL_GPUTransferBuffer *up, *down;
        SDL_GPUSampler *smp;
        SDL_GPUSamplerCreateInfo si;
        SDL_GPUCommandBuffer *cmd;
        SDL_GPUCopyPass *cp;
        SDL_GPURenderPass *rp;
        SDL_GPUColorTargetInfo cti;
        SDL_GPUTextureSamplerBinding bind;
        SDL_GPUTextureTransferInfo tti;
        SDL_GPUTextureRegion reg;
        SDL_GPUFence *fence;
        Uint16 texels[4] = {0x0001, 0x7FFF, 0x1234, 0x8000};
        Uint8 *p;
        int i, ok = 1;
        static const Uint8 want[4][4] = {{8, 0, 0, 0}, {255, 255, 255, 0}, {0, 0, 0, 0}, {0, 0, 0, 255}};

        printf("shaders:\n");
        fails += !check(d3dc != NULL, "d3dcompiler_47.dll (32-bit)");
        if (!d3dc) return 1;
        vs = compile(dev, d3dc, HLSL_VS, "vs_5_1", SDL_GPU_SHADERSTAGE_VERTEX, 0);
        ps = compile(dev, d3dc, HLSL_PS, "ps_5_1", SDL_GPU_SHADERSTAGE_FRAGMENT, 1);
        fails += !check(vs && ps, "HLSL SM5.1 -> DXBC -> SDL_CreateGPUShader");
        if (!vs || !ps) { printf("%s\n", SDL_GetError()); return 1; }
        SDL_zero(blend);
        blend.color_write_mask = 0xF;
        pl = pipeline(dev, vs, ps, blend, 0);
        fails += !check(pl != NULL, "pipeline, RGBA8 target, no blend");
        printf("blend modes the PS1 uses:\n");
        {
            struct { const char *name; SDL_GPUBlendFactor sc, dc, sa, da; SDL_GPUBlendOp op, aop; int depth; } t[] = {
                {"(back+front)/2: CONSTANT_ALPHA", SDL_GPU_BLENDFACTOR_CONSTANT_COLOR, SDL_GPU_BLENDFACTOR_CONSTANT_COLOR, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_ADD, 0},
                {"back+front: ONE/ONE", SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_ADD, 0},
                {"back-front: REVERSE_SUBTRACT", SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDOP_REVERSE_SUBTRACT, SDL_GPU_BLENDOP_ADD, 0},
                {"soft shadow: ZERO/ONE_MINUS_SRC_COLOR", SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_GPU_BLENDFACTOR_ZERO, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDOP_ADD, SDL_GPU_BLENDOP_ADD, 0},
                {"battle shadow mask: MAX + depth bias + depth", SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDFACTOR_ONE, SDL_GPU_BLENDOP_MAX, SDL_GPU_BLENDOP_MAX, 1},
            };
            for (i = 0; i < 5; i++) {
                SDL_GPUGraphicsPipeline *q;
                SDL_zero(blend);
                blend.enable_blend = true;
                blend.src_color_blendfactor = t[i].sc, blend.dst_color_blendfactor = t[i].dc;
                blend.src_alpha_blendfactor = t[i].sa, blend.dst_alpha_blendfactor = t[i].da;
                blend.color_blend_op = t[i].op, blend.alpha_blend_op = t[i].aop;
                blend.color_write_mask = 0xF;
                q = pipeline(dev, vs, ps, blend, t[i].depth);
                fails += !check(q != NULL, t[i].name);
            }
        }
        printf("integer texture round trip (4x1 R16_UINT -> RGBA8, read back):\n");
        SDL_zero(ti);
        ti.type = SDL_GPU_TEXTURETYPE_2D, ti.format = SDL_GPU_TEXTUREFORMAT_R16_UINT, ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        ti.width = 4, ti.height = 1, ti.layer_count_or_depth = 1, ti.num_levels = 1;
        vram = SDL_CreateGPUTexture(dev, &ti);
        ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        target = SDL_CreateGPUTexture(dev, &ti);
        SDL_zero(tbi);
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, tbi.size = sizeof texels;
        up = SDL_CreateGPUTransferBuffer(dev, &tbi);
        tbi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, tbi.size = 16;
        down = SDL_CreateGPUTransferBuffer(dev, &tbi);
        SDL_zero(si);
        si.min_filter = si.mag_filter = SDL_GPU_FILTER_NEAREST;
        smp = SDL_CreateGPUSampler(dev, &si);
        fails += !check(vram && target && up && down && smp, "textures, transfer buffers, sampler created");
        p = SDL_MapGPUTransferBuffer(dev, up, false);
        SDL_memcpy(p, texels, sizeof texels);
        SDL_UnmapGPUTransferBuffer(dev, up);
        cmd = SDL_AcquireGPUCommandBuffer(dev);
        cp = SDL_BeginGPUCopyPass(cmd);
        SDL_zero(tti), SDL_zero(reg);
        tti.transfer_buffer = up, tti.pixels_per_row = 4, tti.rows_per_layer = 1;
        reg.texture = vram, reg.w = 4, reg.h = 1, reg.d = 1;
        SDL_UploadToGPUTexture(cp, &tti, &reg, false);
        SDL_EndGPUCopyPass(cp);
        SDL_zero(cti);
        cti.texture = target, cti.load_op = SDL_GPU_LOADOP_CLEAR, cti.store_op = SDL_GPU_STOREOP_STORE;
        rp = SDL_BeginGPURenderPass(cmd, &cti, 1, NULL);
        SDL_BindGPUGraphicsPipeline(rp, pl);
        bind.texture = vram, bind.sampler = smp;
        SDL_BindGPUFragmentSamplers(rp, 0, &bind, 1);
        SDL_DrawGPUPrimitives(rp, 3, 1, 0, 0);
        SDL_EndGPURenderPass(rp);
        cp = SDL_BeginGPUCopyPass(cmd);
        SDL_zero(tti), SDL_zero(reg);
        tti.transfer_buffer = down, tti.pixels_per_row = 4, tti.rows_per_layer = 1;
        reg.texture = target, reg.w = 4, reg.h = 1, reg.d = 1;
        SDL_DownloadFromGPUTexture(cp, &reg, &tti);
        SDL_EndGPUCopyPass(cp);
        fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        SDL_WaitForGPUFences(dev, true, &fence, 1);
        p = SDL_MapGPUTransferBuffer(dev, down, false);
        for (i = 0; i < 4; i++) {
            printf("    texel %04X -> RGBA %3d %3d %3d %3d\n", texels[i], p[i * 4], p[i * 4 + 1], p[i * 4 + 2], p[i * 4 + 3]);
        }
        /* 0x0001: r=1 -> 8/255; 0x7FFF: all 31 -> 255; 0x1234: r=20,g=17,b=4 -> mid values; 0x8000: mask bit only */
        ok = p[0] == 8 && p[1] == 0 && p[2] == 0 && p[3] == 0 && p[4] == 255 && p[5] == 255 && p[6] == 255 && p[7] == 0 &&
             p[8] == 165 && p[9] == 140 && p[10] == 33 && p[11] == 0 && p[12] == 0 && p[13] == 0 && p[14] == 0 && p[15] == 255;
        SDL_UnmapGPUTransferBuffer(dev, down);
        fails += !check(ok, "integer fetch, shifts and masks give the expected colours");
        (void)want;
    }
    SDL_DestroyGPUDevice(dev);
    printf("%s\n", fails ? "SOME CHECKS FAILED" : "ALL OK");
    return fails != 0;
}
