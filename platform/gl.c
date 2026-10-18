/* The GPU's drawing on the graphics card (OpenGL 3.3), at render_scale
   times the PSX's resolution. gpu_draw.c still draws every primitive into
   VRAM in software, at 1x: that VRAM is what the game reads back and what
   the textures come from (uploaded here as an integer texture, CLUTs looked
   up in the shader). The card draws the same primitives, finer, into its
   own copy of VRAM, which is what the screen shows. */
#define RECT PSX_RECT /* windows.h, through SDL_opengl.h, has its own */
#include "platform.h"
#undef RECT
#define GL_GLEXT_PROTOTYPES
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

/* --- the functions, through SDL (opengl32 only exports 1.1) --- */
#define GL_FUNCS(X)                                                                                                  \
    X(glGetError) X(glGetIntegerv) X(glColorMask) X(glEnable) X(glDisable) X(glViewport) X(glScissor) X(glClear) X(glClearColor)   \
    X(glGenTextures) X(glDeleteTextures) X(glBindTexture) X(glTexImage2D) X(glTexSubImage2D) X(glTexParameteri)     \
    X(glPixelStorei) X(glReadPixels) X(glDrawArrays) X(glActiveTexture) X(glBlendFuncSeparate) X(glBlendEquation)   \
    X(glBlendColor) X(glCreateShader) X(glShaderSource) X(glCompileShader) X(glGetShaderiv) X(glGetShaderInfoLog)  \
    X(glCreateProgram) X(glAttachShader) X(glLinkProgram) X(glGetProgramiv) X(glGetProgramInfoLog) X(glUseProgram) \
    X(glGetUniformLocation) X(glUniform1i) X(glUniform1f) X(glUniform2f) X(glUniform4f) X(glUniform2i) X(glGenVertexArrays) X(glBindVertexArray)  \
    X(glGenBuffers) X(glBindBuffer) X(glBufferData) X(glEnableVertexAttribArray) X(glVertexAttribPointer)           \
    X(glVertexAttribIPointer) X(glGenFramebuffers) X(glDeleteFramebuffers) X(glBindFramebuffer)                     \
    X(glFramebufferTexture2D) X(glCheckFramebufferStatus) X(glBlitFramebuffer)                                  \
    X(glGenRenderbuffers) X(glDeleteRenderbuffers) X(glBindRenderbuffer) X(glRenderbufferStorage)                \
    X(glFramebufferRenderbuffer) X(glDepthFunc) X(glDepthMask) X(glClearDepth) X(glClearStencil)                  \
    X(glStencilFunc) X(glStencilOp) X(glPolygonOffset) X(glGenerateMipmap) X(glDrawArraysInstanced)              \
    X(glVertexAttribDivisor) X(glDeleteBuffers) X(glGenQueries) X(glBeginQuery) X(glEndQuery)      \
    X(glGetQueryObjectiv) X(glGetQueryObjectui64v)

#define X(f) static __typeof__(f) *p_##f;
GL_FUNCS(X)
#undef X
#define GL(f) p_##f

/* --- shaders --- */

static const char *MAIN_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec4 pos_uv;\n" /* VRAM pixels, texels */
    "layout(location = 1) in vec3 color;\n"
    "layout(location = 2) in uvec4 tex;\n" /* CLUT x, y, page x, y */
    "layout(location = 3) in uvec4 limits;\n" /* u, v min, max */
    "layout(location = 4) in uvec4 window;\n" /* texture window mask x, y, offset x, y */
    "layout(location = 5) in uint mode;\n"
    "layout(location = 6) in float z;\n" /* view depth, 0: none */
    "layout(location = 7) in float w;\n" /* the same for the perspective, 0: none */
    "uniform vec2 size;\n" /* VRAM's, 1024x512 */
    "uniform int persp;\n"
    "out vec2 uv;\n" /* in perspective by w, or straight across the screen (w 1) */
    "out vec3 col;\n"
    "out float vw;\n" /* the view depth across the polygon, for the fog */
    "flat out uvec4 t, lim, tw;\n"
    "flat out uint md, deep;\n" /* deep: a 3D polygon, the GTE gave its points their depth */
    "void main() {\n"
    /* linear in 1/z, as depth goes across the screen; the depth test is off without one */
    "    gl_Position = vec4(pos_uv.xy / size * 2.0 - 1.0, z > 0.0 ? clamp(1.0 - 64.0 / z, 0.0, 1.0) * 2.0 - 1.0 : 0.0, 1.0);\n"
    "    gl_Position *= persp != 0 && w > 0.0 ? w : 1.0;\n"
    "    uv = pos_uv.zw; col = color; t = tex; lim = limits; tw = window; md = mode; deep = w > 0.0 ? 1u : 0u; vw = w;\n"
    "}\n";

/* mode: 2 textured, 4 raw texture, 8 semi-transparent, 16-48 texel depth,
   64 dithered, 128 the copy of VRAM itself, 256 sets the mask bit, 512 a soft
   shadow (native.h's NATIVE_SOFT_SHADOW), 1024 a battle shadow's triangle,
   2048 a stage's painted horizon (no fog) */
static const char *MAIN_FS =
    "#version 330 core\n"
    "uniform usampler2D vram;\n"
    "uniform int pass;\n" /* 0 everything, 1 all but the semi-transparent, 2 only those */
    "uniform float scale;\n"
    "uniform int truecolor;\n" /* the colours at 8 bits, undithered */
    "uniform int smooth3d;\n" /* the 3D's textures smoothed */
    "uniform vec4 fog;\n" /* a battle's fog (stagefx.c): its colour at 5 bits, a how much at the far side */
    "uniform vec2 fogz;\n" /* the depths it starts and is thickest at */
    "in vec2 uv;\n"
    "in vec3 col;\n"
    "in float vw;\n"
    "flat in uvec4 t, lim, tw;\n"
    "flat in uint md, deep;\n"
    "out vec4 frag;\n"
    "const float DITHER[16] = float[16](-4.0, 0.0, -3.0, 1.0, 2.0, -2.0, 3.0, -1.0, -3.0, 1.0, -4.0, 0.0, 3.0, -1.0, 2.0, -2.0);\n"
    "uint fetch(int x, int y) { return texelFetch(vram, ivec2(x & 1023, y & 511), 0).r; }\n"
    "vec3 rgb5(uint p) { return vec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u); }\n"
    /* the texel at (u, v), kept inside the polygon's and through the texture window and the CLUT */
    "uint texel(int u, int v) {\n"
    "    u = clamp(u, int(lim.x), int(lim.z)) & 255;\n"
    "    v = clamp(v, int(lim.y), int(lim.w)) & 255;\n"
    "    u = (u & ~int(tw.x)) | (int(tw.z) & int(tw.x));\n"
    "    v = (v & ~int(tw.y)) | (int(tw.w) & int(tw.y));\n"
    "    uint depth = (md >> 4) & 3u;\n"
    "    if (depth == 0u) {\n"
    "        uint w = fetch(int(t.z) + u / 4, int(t.w) + v);\n"
    "        return fetch(int(t.x) + int((w >> uint((u & 3) * 4)) & 15u), int(t.y));\n"
    "    } else if (depth == 1u) {\n"
    "        uint w = fetch(int(t.z) + u / 2, int(t.w) + v);\n"
    "        return fetch(int(t.x) + int((w >> uint((u & 1) * 8)) & 255u), int(t.y));\n"
    "    }\n"
    "    return fetch(int(t.z) + u, int(t.w) + v);\n"
    "}\n"
    "void main() {\n"
    "    uint pix = 0u;\n"
    "    vec3 tex = vec3(0.0);\n"
    "    bool semi = (md & 8u) != 0u;\n"
    "    vec3 c8 = col, c5;\n"
    "    float cover = 1.0;\n"
    "    if ((md & 1024u) != 0u) {\n" /* a battle shadow: into the mask (gl_shadows_resolve) */
    "        if (pass == 1) discard;\n"
    "        frag = vec4(1.0);\n"
    "        return;\n"
    "    }\n"
    "    if ((md & 128u) != 0u) {\n"
    "        pix = fetch(int(gl_FragCoord.x / scale), int(gl_FragCoord.y / scale));\n"
    "        frag = vec4(rgb5(pix) / 31.0, float(pix >> 15));\n"
    "        return;\n"
    "    }\n"
    "    if ((md & 2u) != 0u) {\n"
    /* at the pixel's corner, where the PSX samples: mirrored textures keep their texels */
    "        vec2 at = uv - 0.5 * dFdx(uv) - 0.5 * dFdy(uv);\n"
    /* a soft shadow: an ellipse as the sprite's, fading out from its middle, in the colour of the texel there */
    "        if ((md & 512u) != 0u) {\n"
    "            vec2 size = vec2(lim.zw - lim.xy) + 1.0, mid = vec2(lim.xy) + size * vec2(0.484, 0.469);\n"
    "            cover = 1.0 - smoothstep(0.6, 1.0, length((uv - mid) / (size * vec2(0.42, 0.4))));\n"
    "            if (cover <= 0.0) discard;\n"
    "            at = mid;\n"
    "        }\n"
    "        pix = texel(int(floor(at.x + 0.002)), int(floor(at.y + 0.002)));\n"
    "        tex = rgb5(pix);\n"
    /* smoothed (a 3D polygon's, by its depth): the four texels around, the
       see-through ones (0) left out of the mix; the nearest's flags kept */
    "        if (smooth3d != 0 && deep != 0u && (md & 512u) == 0u) {\n"
    "            vec2 p = uv - 0.5;\n"
    "            ivec2 i = ivec2(floor(p));\n"
    "            vec2 f = p - vec2(i);\n"
    "            uint a = texel(i.x, i.y), b = texel(i.x + 1, i.y), c = texel(i.x, i.y + 1), e = texel(i.x + 1, i.y + 1);\n"
    "            vec4 k = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y) *\n"
    "                     vec4(a != 0u, b != 0u, c != 0u, e != 0u);\n"
    "            float sum = k.x + k.y + k.z + k.w;\n"
    "            if (sum < 0.5) discard;\n"
    "            tex = (rgb5(a) * k.x + rgb5(b) * k.y + rgb5(c) * k.z + rgb5(e) * k.w) / sum;\n"
    "            if (pix == 0u) pix = a != 0u ? a : b != 0u ? b : c != 0u ? c : e;\n"
    "        }\n"
    "        if (pix == 0u) discard;\n"
    "        semi = semi && (pix & 0x8000u) != 0u;\n"
    "        c8 = floor(tex * col / 16.0);\n"
    "    }\n"
    "    if ((pass == 1 && semi) || (pass == 2 && !semi)) discard;\n"
    "    if ((md & 6u) == 6u) {\n"
    "        c5 = tex;\n"
    "    } else {\n"
    "        float d = (md & 64u) != 0u ? DITHER[(int(gl_FragCoord.y) & 3) * 4 + (int(gl_FragCoord.x) & 3)] : 0.0;\n"
    "        c5 = truecolor != 0 ? clamp(c8 / 8.0, 0.0, 31.0) : clamp(floor((c8 + d) / 8.0), 0.0, 31.0);\n"
    "    }\n"
    "    if ((md & 512u) != 0u) c5 = vec3(18.6 * cover);\n" /* how much of the ground it takes, 0.6 inside */
    /* the opaque 3D fades into the sky with depth; the blended keeps its own */
    "    else if (fog.a > 0.0 && deep != 0u && !semi && (md & 2048u) == 0u) c5 = mix(c5, fog.rgb, fog.a * smoothstep(fogz.x, fogz.y, vw));\n"
    "    frag = vec4(c5 / 31.0, (md & 256u) != 0u || (pix & 0x8000u) != 0u ? 1.0 : 0.0);\n"
    "}\n";

/* The screen: a texture on a quad */
static const char *BLIT_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec4 pos_uv;\n"
    "out vec2 uv;\n"
    "void main() { gl_Position = vec4(pos_uv.xy, 0.0, 1.0); uv = pos_uv.zw; }\n";

static const char *BLIT_FS =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "void main() { frag = texture(tex, uv); }\n";

/* The world's props (stagefx.c): a textured billboard or mesh with its own RGBA
   image, drawn over the models. Cut out by alpha, and hidden by the stage where
   the stage is nearer (the stage's depth, saved before the models' table clears
   the depth buffer: gl_depth_save) and by the fighters (the depth buffer) */
static const char *PROP_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec3 p;\n" /* VRAM pixels, view depth */
    "layout(location = 1) in vec2 uvin;\n"
    "layout(location = 2) in vec4 colin;\n"
    "uniform vec2 size;\n"
    "out vec2 uv;\n"
    "out vec4 col;\n"
    "out float vz;\n"
    "void main() {\n"
    "    float zn = clamp(1.0 - 64.0 / p.z, 0.0, 1.0) * 2.0 - 1.0;\n" /* as MAIN_VS, so the depths compare */
    "    gl_Position = vec4(p.xy / size * 2.0 - 1.0, zn, 1.0) * p.z;\n"
    "    uv = uvin; col = colin; vz = p.z;\n"
    "}\n";

static const char *PROP_FS =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "uniform sampler2D sdepth;\n"
    "uniform int usedepth;\n"
    "uniform vec4 fog;\n" /* the battle's fog, as MAIN_FS has it */
    "uniform vec2 fogz;\n"
    "in vec2 uv;\n"
    "in vec4 col;\n"
    "in float vz;\n"
    "out vec4 frag;\n"
    "void main() {\n"
    "    vec4 t = texture(tex, uv);\n" /* premultiplied */
    "    if (t.a < 0.5) discard;\n"
    "    if (usedepth != 0 && gl_FragCoord.z > texelFetch(sdepth, ivec2(gl_FragCoord.xy), 0).r + 0.0001) discard;\n"
    "    vec3 c = t.rgb / t.a * col.rgb;\n"
    "    if (fog.a > 0.0) c = mix(c, fog.rgb / 31.0, fog.a * col.a * smoothstep(fogz.x, fogz.y, vz));\n"
    "    frag = vec4(c, 1.0);\n"
    "}\n";


/* The same props from tables, the card doing what stagefx.c used to do for each one every frame: the camera
   (gte.c's gte_camera_project), the wind's sway, and for the particles where each one is at this frame, found
   from its own number alone (no state kept: a hash of its number and of the round it is on gives its randoms).
   mode 0: an instance of the billboards' table; 1: a particle of an emitter; 2: a leaf, a diamond in greens */
static const char *PINST_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec3 ipos;\n"
    "layout(location = 1) in vec4 idim;\n"  /* width, height, sway, turn in degrees */
    "layout(location = 2) in vec4 icol;\n"  /* tint, fog */
    "layout(location = 3) in float ifixed;\n"
    "uniform vec2 size;\n"
    "uniform vec4 cam0, cam1, cam2;\n"      /* the world to the view: a row, its offset */
    "uniform vec4 camp;\n"                  /* focal length, screen offset x, y, the wide screen's 0.75 or 1 */
    "uniform vec4 clk;\n"                   /* the frame clock, brightness, the drawing offset */
    "uniform vec2 wnd;\n"                   /* the billboards' wind */
    "uniform int mode;\n"
    "uniform vec4 pc;\n"                    /* centre, radius */
    "uniform vec4 pa;\n"                    /* top, fall, wind, how far above the top they start */
    "uniform vec4 pb;\n"                    /* sizes, where they land, count */
    "uniform vec4 ptint;\n"                 /* tint 0-1, fog */
    "out vec2 uv;\n"
    "out vec4 col;\n"
    "out float vz;\n"
    "bool project(vec3 p, out vec3 s) {\n"
    "    vec3 v = vec3(dot(cam0.xyz, p), dot(cam1.xyz, p), dot(cam2.xyz, p)) + vec3(cam0.w, cam1.w, cam2.w);\n"
    "    s = vec3(0.0);\n"
    "    if (v.z <= camp.x * 0.5) return false;\n"
    "    s = vec3(camp.x * v.x / v.z * camp.w + camp.y, camp.x * v.y / v.z + camp.z, v.z);\n"
    "    return true;\n"
    "}\n"
    "void kill() { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); uv = vec2(0.0); col = vec4(0.0); vz = 1.0; }\n"
    "void put(vec3 s, vec2 tc, vec4 c) {\n"
    "    s.xy += clk.zw;\n"
    "    float zn = clamp(1.0 - 64.0 / s.z, 0.0, 1.0) * 2.0 - 1.0;\n" /* as PROP_VS */
    "    gl_Position = vec4(s.xy / size * 2.0 - 1.0, zn, 1.0) * s.z;\n"
    "    uv = tc; col = c; vz = s.z;\n"
    "}\n"
    "uint hsh(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }\n"
    "float rnd(uint x) { return float(hsh(x) >> 8) / 16777216.0; }\n"
    "vec2 windat(float t, float w) {\n" /* stagefx.c's wind_at */
    "    float gust = 0.6 + 0.4 * sin(t * 0.011) * sin(t * 0.0037 + 1.0);\n"
    "    float dir = 0.6 + 0.5 * sin(t * 0.0013);\n"
    "    return vec2(cos(dir), sin(dir)) * 18.0 * w * gust;\n"
    "}\n"
    "const int TRI[6] = int[6](0, 1, 2, 1, 3, 2);\n"
    "const int TRD[6] = int[6](0, 1, 2, 0, 2, 3);\n"
    "const vec3 PAL[10] = vec3[10](vec3(58, 102, 34), vec3(92, 140, 52), vec3(74, 118, 38), vec3(120, 156, 62),\n"
    "    vec3(110, 120, 40), vec3(150, 160, 70), vec3(128, 86, 36), vec3(170, 124, 60), vec3(96, 70, 34), vec3(136, 104, 52));\n"
    "void main() {\n"
    "    int k = gl_VertexID % 6;\n"
    "    if (mode == 0) {\n"
    "        int c = TRI[k];\n"
    "        float w = idim.x, h = idim.y;\n"
    "        vec3 base = vec3(ipos.x, -ipos.y, ipos.z), top = vec3(base.x, base.y - h, base.z);\n"
    "        vec4 cc = vec4(clamp(icol.rgb * clk.y, 0.0, 1.0), icol.a);\n"
    "        if (idim.z > 0.0) {\n" /* the tip leans and swings in the wind */
    "            float move = 0.5 + sin(clk.x * 0.042 + (base.x + base.z) * 0.0004);\n"
    "            top.xz += wnd / 18.0 * idim.z * h * 0.3 * move;\n"
    "        }\n"
    "        vec2 tc = vec2(float(c & 1), float(c >> 1));\n"
    "        vec3 s0, s1;\n"
    "        if (ifixed < 0.5) {\n"
    "            if (!project(base, s0) || !project(top, s1)) { kill(); return; }\n"
    "            float hw = w * 0.5 * (camp.x / s0.z) * camp.w, full = h * (camp.x / s0.z);\n"
    "            if (s0.y - s1.y < full * 0.6) s1.y = s0.y - full * 0.6;\n" /* from above it keeps 60% of its height */
    "            float r = s0.z / s1.z, side = (c & 1) == 1 ? 1.0 : -1.0;\n"
    "            if (c < 2) put(vec3(s1.x + side * hw * r, s1.y, s1.z), tc, cc);\n"
    "            else put(vec3(s0.x + side * hw, s0.y, s0.z), tc, cc);\n"
    "        } else {\n" /* flat in the world, turned about the vertical */
    "            float a = idim.w * 0.017453293, rx = cos(a) * w * 0.5, rz = sin(a) * w * 0.5;\n"
    "            vec3 mine = vec3(0.0);\n"
    "            for (int j = 0; j < 4; j++) {\n"
    "                float side = (j & 1) == 1 ? 1.0 : -1.0;\n"
    "                vec3 pw = vec3(base.x + rx * side + (j < 2 ? top.x - base.x : 0.0), base.y - (j < 2 ? h : 0.0), base.z + rz * side + (j < 2 ? top.z - base.z : 0.0)), s;\n"
    "                if (!project(pw, s)) { kill(); return; }\n"
    "                if (j == c) mine = s;\n"
    "            }\n"
    "            put(mine, tc, cc);\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "    uint i = uint(gl_InstanceID);\n"
    "    if (float(i) >= pb.w) { kill(); return; }\n"
    "    float vfall = pa.y * (5.0 + rnd(i * 7u + 1u) * 6.0) * 0.89;\n" /* the average of its fall's slowing at the swing's ends */
    "    float L = max((pa.x + pa.w * 0.5 - pb.z) / vfall, 1.0);\n"
    "    float ph = clk.x + rnd(i * 7u + 2u) * L, cyc = floor(ph / L), age = ph - cyc * L;\n"
    "    uint hs = i * 7919u + uint(cyc) * 104729u + 13u;\n"
    "    float a0 = rnd(hs) * 6.2831853, r = sqrt(rnd(hs + 1u)) * pc.w;\n"
    "    float psize = pb.x + rnd(hs + 2u) * (pb.y - pb.x), phase = rnd(hs + 3u) * 6.2831853;\n"
    "    float spin = (rnd(hs + 4u) - 0.5) * 0.12, ang = rnd(hs + 5u) * 6.2831853 + spin * age;\n"
    "    float ts = clk.x - age;\n"
    "    vec2 w0 = windat(clk.x - age * 0.5, pa.z), ws = windat(ts, pa.z) * 120.0;\n" /* the wind it was carried by, and upwind of where it came in */
    "    float dx = w0.x * age + 9.0 / 0.045 * (cos(0.045 * ts + phase) - cos(0.045 * clk.x + phase));\n"
    "    float dz = w0.y * age + 6.0 / 0.037 * (sin(0.037 * clk.x + phase) - sin(0.037 * ts + phase));\n"
    "    vec3 wp = vec3(pc.x + cos(a0) * r - ws.x + dx, -(pc.y + pa.x + pa.w * 0.5 - vfall * age), pc.z + sin(a0) * r - ws.y + dz), s;\n"
    "    if (!project(wp, s)) { kill(); return; }\n"
    "    float half_ = psize * (camp.x / s.z) * 0.5;\n"
    "    if (half_ < 0.35) { kill(); return; }\n"
    "    float flip = 0.25 + 0.75 * abs(cos(clk.x * 0.06 + phase * 2.0)), cs = cos(ang), sn = sin(ang), lx, ly;\n"
    "    vec4 cc;\n"
    "    int c;\n"
    "    if (mode == 1) {\n"
    "        c = TRI[k];\n"
    "        lx = ((c & 1) == 1 ? 0.5 : -0.5) * flip * 2.0, ly = c < 2 ? -1.0 : 1.0;\n"
    "        cc = vec4(clamp(ptint.rgb * clk.y, 0.0, 1.0), ptint.a);\n"
    "    } else {\n" /* a diamond along its length, narrow across, one side lit */
    "        c = TRD[k];\n"
    "        lx = (c == 1 ? 0.45 : c == 3 ? -0.45 : 0.0) * flip, ly = c == 0 ? -1.0 : c == 2 ? 1.0 : 0.0;\n"
    "        int sh = int(rnd(hs + 6u) * 5.0) % 5;\n"
    "        cc = vec4(clamp(PAL[sh * 2 + (c == 1 ? 1 : 0)] / 255.0 * clk.y, 0.0, 1.0), 1.0);\n"
    "    }\n"
    "    s.x += (lx * cs - ly * sn) * half_ * camp.w;\n"
    "    s.y += (lx * sn + ly * cs) * half_;\n"
    "    put(s, mode == 1 ? vec2(float(c & 1), float(c >> 1)) : vec2(0.5), cc);\n"
    "}\n";

/* Battle shadows: the mask blurred one way into another, then the other way
   as it darkens the copy (gl_shadows_resolve) */
static const char *SOFT_FS =
    "#version 330 core\n"
    "uniform sampler2D src;\n"
    "uniform ivec2 dir;\n" /* a tap's step, in the copy's pixels */
    "uniform float strength;\n" /* 0: the blurred mask itself, else how much of the ground it takes */
    "out vec4 frag;\n"
    "const float W[5] = float[5](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);\n"
    "void main() {\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy);\n"
    "    float m = texelFetch(src, at, 0).r * W[0];\n"
    "    for (int i = 1; i < 5; i++) {\n"
    "        m += (texelFetch(src, at + dir * i, 0).r + texelFetch(src, at - dir * i, 0).r) * W[i];\n"
    "    }\n"
    "    frag = strength > 0.0 ? vec4(vec3(m * strength), 0.0) : vec4(m);\n"
    "}\n";

/* --- state --- */

static SDL_Window *s_window;
static SDL_GLContext s_ctx;
static GLuint s_main, s_blit, s_vao, s_vbo, s_quad_vao, s_quad_vbo;
static GLint s_pass_loc, s_scale_loc, s_size_loc, s_true_loc, s_persp_loc, s_smooth_loc, s_fog_loc, s_fogz_loc;
static GLuint s_vram_tex;            /* the 1x VRAM, R16UI */
static GLuint s_fbo, s_fbo_tex;      /* the copy, render_scale times bigger */
static GLuint s_fbo_depth;           /* its depth buffer, for the 3D models (g_depth) */
int g_depth = 1;
int g_true_color = 1, g_perspective = 1, g_smooth_3d = 1; /* true_color, perspective, smooth_3d, the menu */
int g_soft_shadows;
int g_vsync = 1; /* vsync, the menu */
int g_battle_shadows = 1; /* battle_shadows, the menu (native.h) */ /* soft_shadows = 1, the menu (native.h) */
static int s_deep;                   /* the batch is by the depth buffer */
int g_battle_sky;                    /* battle_sky, the menu (native.h) */
static float s_bg[3];                /* the battle's background colour this frame, 5 bits; s_fog_on: drawn */
static float s_fog[3], s_fog_amount, s_fog_from; /* the fog shown: colour (5 bits), at the far side, where it starts */
static int s_fog_on, s_fog_shown;
static float s_zfar, s_zframe;       /* the farthest opaque 3D: smoothed, and this frame's */
static GLuint s_tmp_fbo, s_tmp_tex;  /* for a VRAM to VRAM copy */
static GLuint s_mask_fbo, s_mask_tex, s_blur_fbo, s_blur_tex; /* battle shadows: where they fall, blurred */
static GLuint s_soft;
static GLuint s_prop, s_prop_vao, s_prop_vbo, s_sd_tex, s_sd_fbo; /* props; the stage's depth saved */
static GLuint s_pinst, s_inst_vao, s_part_vao; /* the props from tables (PINST_VS) */
static GLint s_prop_depth_loc, s_prop_fog_loc, s_prop_fogz_loc;
static int s_sd_valid;

/* The card's own times (the battle's performance meter, perf.c): a timer query around each batch and each props draw,
   read back a few frames later so nothing waits for them */
#define QSLOTS 4
#define QMAX 600
static GLuint s_q[QSLOTS][QMAX];
static unsigned char s_qkind[QSLOTS][QMAX];
static int s_qn[QSLOTS], s_qslot, s_qopen = -1, s_q_made;

static void gq_begin(int kind) {
    int n;

    if (!perf_on() || s_qopen >= 0) {
        return;
    }
    if (!s_q_made) {
        int i;

        for (i = 0; i < QSLOTS; i++) {
            GL(glGenQueries)(QMAX, s_q[i]);
        }
        s_q_made = 1;
    }
    n = s_qn[s_qslot];
    if (n >= QMAX) {
        return;
    }
    GL(glBeginQuery)(GL_TIME_ELAPSED, s_q[s_qslot][n]);
    s_qkind[s_qslot][n] = (unsigned char)kind;
    s_qopen = n;
}

static void gq_end(void) {
    if (s_qopen >= 0) {
        GL(glEndQuery)(GL_TIME_ELAPSED);
        s_qn[s_qslot]++;
        s_qopen = -1;
    }
}

/* Once a frame, after the swap: the oldest slot's results go to the meter and it is the one used next */
static void gq_frame(void) {
    int old = (s_qslot + 1) % QSLOTS, i;

    if (!s_q_made) {
        return;
    }
    for (i = 0; i < s_qn[old]; i++) {
        GLint ok = 0;
        GLuint64 ns = 0;

        GL(glGetQueryObjectiv)(s_q[old][i], GL_QUERY_RESULT_AVAILABLE, &ok);
        if (ok) {
            GL(glGetQueryObjectui64v)(s_q[old][i], GL_QUERY_RESULT, &ns);
            perf_gpu(s_qkind[old][i], (double)ns / 1e6);
        }
    }
    s_qn[old] = 0;
    s_qslot = old;
}
static GLint s_soft_dir_loc, s_soft_str_loc;
static int s_mask_box[4] = {VRAM_W, VRAM_H, -1, -1}; /* what the mask holds, in VRAM pixels */
static GLuint s_movie_tex, s_menu_tex;
int g_res_scale = 1; /* native.ini, the menu */
static int s_scale, s_max_scale = 8;
uint16_t g_vram_dirty[8]; /* 64x64 blocks of VRAM written since they were uploaded */

#define BATCH 6144
static GVtx *s_batch; /* malloc'd: the image has to stay under 16 MB (main.c) */
static int s_count;
static int s_semi = -1;        /* the batch's semi-transparency mode, -1: none in it yet */
static int s_clip[4] = {0, 0, 1023, 511}; /* the drawing area the batch was made with */

static GLuint shader(GLenum type, const char *src) {
    GLuint s = GL(glCreateShader)(type);
    GLint ok;
    char log[1024];

    GL(glShaderSource)(s, 1, &src, NULL);
    GL(glCompileShader)(s);
    GL(glGetShaderiv)(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GL(glGetShaderInfoLog)(s, sizeof log, NULL, log);
        LOG("gl: shader: %s", log);
    }
    return s;
}

static GLuint program(const char *vs, const char *fs) {
    GLuint p = GL(glCreateProgram)();
    GLint ok;
    char log[1024];

    GL(glAttachShader)(p, shader(GL_VERTEX_SHADER, vs));
    GL(glAttachShader)(p, shader(GL_FRAGMENT_SHADER, fs));
    GL(glLinkProgram)(p);
    GL(glGetProgramiv)(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GL(glGetProgramInfoLog)(p, sizeof log, NULL, log);
        LOG("gl: program: %s", log);
        return 0;
    }
    return p;
}

static GLuint texture(GLint internal, int w, int h, GLenum format, GLenum type, GLint filter) {
    GLuint t;

    GL(glGenTextures)(1, &t);
    GL(glBindTexture)(GL_TEXTURE_2D, t);
    GL(glTexImage2D)(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, NULL);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static GLuint framebuffer(GLuint tex) {
    GLuint f;

    GL(glGenFramebuffers)(1, &f);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, f);
    GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (GL(glCheckFramebufferStatus)(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG("gl: framebuffer incomplete");
    }
    return f;
}

/* A rect of the 1x VRAM onto the card */
static void upload(int x, int y, int w, int h) {
    GL(glBindTexture)(GL_TEXTURE_2D, s_vram_tex);
    GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, VRAM_W);
    GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 2);
    GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, x, y, w, h, GL_RED_INTEGER, GL_UNSIGNED_SHORT, &g_vram[y * VRAM_W + x]);
    GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
}

/* The dirty blocks among `need` (a mask per block row), as runs of a row */
static void upload_blocks(const uint16_t need[8]) {
    int row, col;

    for (row = 0; row < 8; row++) {
        uint16_t bits = need[row] & g_vram_dirty[row];

        for (col = 0; col < 16; col++) {
            if (bits >> col & 1) {
                int start = col;

                while (col < 16 && (bits >> col & 1)) {
                    col++;
                }
                upload(start * 64, row * 64, (col - start) * 64, 64);
            }
        }
        g_vram_dirty[row] &= ~need[row];
    }
}

static void need_rect(uint16_t need[8], int x, int y, int w, int h) {
    int r, c;

    for (r = (y & 511) >> 6; r <= ((y + h - 1) & 511) >> 6 && r < 8; r++) {
        for (c = (x & 1023) >> 6; c <= ((x + w - 1) & 1023) >> 6 && c < 16; c++) {
            need[r] |= (uint16_t)(1u << c);
        }
    }
}

void gl_vram_dirty(int x, int y, int w, int h) {
    need_rect(g_vram_dirty, x, y, w, h);
}

/* The batch, in its two passes when some of it is semi-transparent: the
   opaque pixels first, then the blended ones over them */
static void flush_batch(void) {
    int s = s_scale;

    if (s_count == 0) {
        return;
    }
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_semi == 5 ? s_mask_fbo : s_fbo);
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(s_clip[0] * s, s_clip[1] * s, (s_clip[2] - s_clip[0] + 1) * s, (s_clip[3] - s_clip[1] + 1) * s);
    GL(glUseProgram)(s_main);
    GL(glBindVertexArray)(s_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)(s_count * sizeof(GVtx)), s_batch, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, s_vram_tex);
    GL(glUniform1f)(s_scale_loc, (float)s);
    GL(glUniform1i)(s_true_loc, g_true_color);
    GL(glUniform1i)(s_persp_loc, g_perspective);
    GL(glUniform1i)(s_smooth_loc, g_smooth_3d);
    GL(glUniform4f)(s_fog_loc, s_fog[0], s_fog[1], s_fog[2], s_fog_shown && s_zfar > 0 ? s_fog_amount : 0.0f);
    GL(glUniform2f)(s_fogz_loc, s_zfar * s_fog_from, s_zfar);
    GL(glDisable)(GL_BLEND);
    if (s_deep) {
        GL(glEnable)(GL_DEPTH_TEST);
        GL(glDepthFunc)(GL_LEQUAL);
        GL(glDepthMask)(GL_TRUE);
    }
    GL(glUniform1i)(s_pass_loc, s_semi < 0 ? 0 : 1);
    GL(glDrawArrays)(GL_TRIANGLES, 0, s_count);
    GL(glDepthMask)(GL_FALSE); /* the blended pixels test, don't hide what's behind */
    if (s_semi >= 0) {
        GL(glEnable)(GL_BLEND);
        GL(glBlendColor)(0.25f, 0.25f, 0.25f, 0.5f);
        switch (s_semi) {
        case 0: /* (back + front) / 2 */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA, GL_ONE, GL_ZERO);
            break;
        case 1: /* back + front */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
            break;
        case 2: /* back - front */
            GL(glBlendEquation)(GL_FUNC_REVERSE_SUBTRACT);
            GL(glBlendFuncSeparate)(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            break;
        case 4: /* back * (1 - front): a soft shadow (gpu_draw.c) */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
            break;
        case 5: /* a battle shadow (gpu_draw.c): into the mask, once a pixel */
            GL(glBlendEquation)(GL_MAX);
            GL(glBlendFuncSeparate)(GL_ONE, GL_ONE, GL_ONE, GL_ONE);
            GL(glEnable)(GL_POLYGON_OFFSET_FILL); /* on the ground it lies on */
            GL(glPolygonOffset)(-1.0f, -4.0f);
            break;
        default: /* back + front / 4 */
            GL(glBlendEquation)(GL_FUNC_ADD);
            GL(glBlendFuncSeparate)(GL_CONSTANT_COLOR, GL_ONE, GL_ONE, GL_ZERO);
            break;
        }
        GL(glUniform1i)(s_pass_loc, 2);
        GL(glDrawArrays)(GL_TRIANGLES, 0, s_count);
        GL(glDisable)(GL_BLEND);
        GL(glDisable)(GL_POLYGON_OFFSET_FILL);
        GL(glBlendEquation)(GL_FUNC_ADD);
    }
    GL(glDisable)(GL_DEPTH_TEST);
    GL(glDisable)(GL_SCISSOR_TEST);
    s_count = 0;
    s_semi = -1;
}

static void flush(void) {
    if (s_count == 0) {
        return;
    }
    gq_begin(PF_GPU);
    flush_batch();
    gq_end();
}

/* Triangles (n a multiple of 3) for the copy, with gpu_draw.c's state: the
   drawing area, the semi-transparency mode (4: a soft shadow's) */
void gl_triangles(const GVtx *v, int n, int x1, int y1, int x2, int y2, int semi) {
    int semi_prim = (v[0].mode & 8) != 0, deep = v[0].z > 0;

    if (s_scale == 0) {
        return;
    }
    if (s_count + n > BATCH || deep != s_deep || x1 != s_clip[0] || y1 != s_clip[1] || x2 != s_clip[2] || y2 != s_clip[3] ||
        (semi_prim && s_semi >= 0 && semi != s_semi) || (!semi_prim && s_semi >= 0) ||
        (semi == 5 && s_semi != 5 && s_count > 0)) { /* battle shadows go to the mask: a batch of their own */
        flush();
        s_clip[0] = x1, s_clip[1] = y1, s_clip[2] = x2, s_clip[3] = y2;
        s_deep = deep;
    }
    if (v[0].mode & 2) {
        /* the page and the CLUT as the card has them: what the CPU drew there
           since (a texture drawn to) goes up first */
        uint16_t need[8] = {0};
        int depth = v[0].mode >> 4 & 3;

        need_rect(need, v[0].tx, v[0].ty, 64 << depth, 256);
        need_rect(need, v[0].clut_x, v[0].clut_y, depth == 0 ? 16 : 256, 1);
        if ((need[0] & g_vram_dirty[0]) | (need[1] & g_vram_dirty[1]) | (need[2] & g_vram_dirty[2]) | (need[3] & g_vram_dirty[3]) |
            (need[4] & g_vram_dirty[4]) | (need[5] & g_vram_dirty[5]) | (need[6] & g_vram_dirty[6]) | (need[7] & g_vram_dirty[7])) {
            flush();
            upload_blocks(need);
        }
    }
    if (semi_prim) {
        s_semi = semi;
    } else if (v[0].w > 0 && !(v[0].mode & 2048)) {
        int i;

        for (i = 0; i < n; i++) {
            s_zframe = SDL_max(s_zframe, v[i].w);
        }
    }
    if (semi == 5) { /* where the mask has shadows */
        int i;

        for (i = 0; i < n; i++) {
            s_mask_box[0] = SDL_min(s_mask_box[0], (int)v[i].x);
            s_mask_box[1] = SDL_min(s_mask_box[1], (int)v[i].y);
            s_mask_box[2] = SDL_max(s_mask_box[2], (int)v[i].x + 1);
            s_mask_box[3] = SDL_max(s_mask_box[3], (int)v[i].y + 1);
        }
    }
    SDL_memcpy(&s_batch[s_count], v, n * sizeof(GVtx));
    s_count += n;
}

/* An ordering table drawn: the battle shadows in the mask, blurred, darken
   the copy, and the mask is emptied */
void gl_shadows_resolve(void) {
    static const float QUAD[16] = {-1, -1, 0, 0, 1, -1, 0, 0, -1, 1, 0, 0, 1, 1, 0, 0};
    int s = s_scale, step = SDL_max(1, s * 3 / 4), pad = 5 * step, r[4];

    if (s == 0 || s_mask_box[2] < 0) {
        return;
    }
    flush();
    r[0] = SDL_max(0, s_mask_box[0] * s - pad);
    r[1] = SDL_max(0, s_mask_box[1] * s - pad);
    r[2] = SDL_min(VRAM_W * s, s_mask_box[2] * s + pad) - r[0];
    r[3] = SDL_min(VRAM_H * s, s_mask_box[3] * s + pad) - r[1];
    s_mask_box[0] = VRAM_W, s_mask_box[1] = VRAM_H, s_mask_box[2] = s_mask_box[3] = -1;
    if (r[2] <= 0 || r[3] <= 0) {
        return;
    }
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(r[0], r[1], r[2], r[3]);
    GL(glUseProgram)(s_soft);
    GL(glBindVertexArray)(s_quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof QUAD, QUAD, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    /* across */
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_blur_fbo);
    GL(glBindTexture)(GL_TEXTURE_2D, s_mask_tex);
    GL(glUniform2i)(s_soft_dir_loc, step, 0);
    GL(glUniform1f)(s_soft_str_loc, 0.0f);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
    /* down, darkening the copy: back * (1 - front) */
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_fbo);
    GL(glBindTexture)(GL_TEXTURE_2D, s_blur_tex);
    GL(glUniform2i)(s_soft_dir_loc, 0, step);
    GL(glUniform1f)(s_soft_str_loc, 0.5f);
    GL(glEnable)(GL_BLEND);
    GL(glBlendEquation)(GL_FUNC_ADD);
    GL(glBlendFuncSeparate)(GL_ZERO, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
    GL(glDisable)(GL_BLEND);
    /* empty for the next */
    GL(glClearColor)(0, 0, 0, 0);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_mask_fbo);
    GL(glClear)(GL_COLOR_BUFFER_BIT);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_blur_fbo);
    GL(glClear)(GL_COLOR_BUFFER_BIT);
    GL(glDisable)(GL_SCISSOR_TEST);
}

/* The depth buffer emptied: an ordering table's 3D models are drawn by their
   depth among themselves, over what earlier tables drew */
void gl_depth_clear(void) {
    if (s_scale == 0 || !s_fbo_depth) {
        return;
    }
    flush();
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_fbo);
    GL(glDepthMask)(GL_TRUE);
    GL(glClearDepth)(1.0);
    GL(glClearStencil)(0);
    GL(glClear)(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

/* A quad of the copy that shows the 1x VRAM under it: after the CPU wrote
   VRAM (an image, a fill) */
void gl_refresh(int x, int y, int w, int h) {
    GVtx q[6];
    int i;

    if (s_scale == 0 || w <= 0 || h <= 0) {
        return;
    }
    flush();
    if (x + w > VRAM_W) w = VRAM_W - x;
    if (y + h > VRAM_H) h = VRAM_H - y;
    upload(x, y, w, h);
    SDL_memset(q, 0, sizeof q);
    for (i = 0; i < 6; i++) {
        static const int CX[6] = {0, 1, 0, 1, 1, 0}, CY[6] = {0, 0, 1, 0, 1, 1};

        q[i].x = (float)(x + CX[i] * w);
        q[i].y = (float)(y + CY[i] * h);
        q[i].mode = 128;
    }
    SDL_memcpy(s_batch, q, sizeof q);
    s_count = 6;
    s_clip[0] = 0, s_clip[1] = 0, s_clip[2] = 1023, s_clip[3] = 511;
    flush();
}

/* A battle's sky drawn (gpu_draw.c), its colour at the horizon the fog's:
   the stage's background colour, which fades in and out with the stage
   (the profile's colour fades with its brightness). The painted horizon
   (mode 2048) stays out of the fog and of its range.
   ponytail: the fog spans the farthest opaque 3D of the last frames (the
   stage's edge), as the stages' sizes are not known; a per-stage range
   when one looks wrong */
void gl_battle_fog(int r, int gg, int b) {
    s_bg[0] = r / 8.0f, s_bg[1] = gg / 8.0f, s_bg[2] = b / 8.0f;
    s_fog_on = 1;
}

/* VRAM to VRAM, in the copy */
void gl_copy(int sx, int sy, int w, int h, int dx, int dy) {
    gl_copy_to(sx, sy, w, h, (float)dx, dy, (float)w);
}

/* The same, to dw pixels across from dx (a widescreen battle's menus) */
void gl_copy_to(int sx, int sy, int w, int h, float dx, int dy, float dw) {
    int s = s_scale;

    if (s == 0 || w <= 0 || h <= 0) {
        return;
    }
    flush();
    if (sx + w > VRAM_W) w = VRAM_W - sx;
    if (dx + dw > VRAM_W) dw = VRAM_W - dx;
    if (sy + h > VRAM_H) h = VRAM_H - sy;
    if (dy + h > VRAM_H) h = VRAM_H - dy;
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, s_fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, s_tmp_fbo);
    GL(glBlitFramebuffer)(sx * s, sy * s, (sx + w) * s, (sy + h) * s, 0, 0, w * s, h * s, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, s_tmp_fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, s_fbo);
    GL(glBlitFramebuffer)(0, 0, w * s, h * s, (int)(dx * s + 0.5f), dy * s, (int)((dx + dw) * s + 0.5f), (dy + h) * s, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
}

/* An image's texture, made on first use: premultiplied, so that filtering
   doesn't bleed the transparent texels' colour (black, mostly) into the edges,
   and mipmapped, so that a big image shrunk to the picture keeps its detail */
static int image_tex(const SDL_Surface *img, unsigned *tex) {
    void *pm;

    if (*tex != 0) {
        return 1;
    }
    pm = SDL_malloc((size_t)img->w * img->h * 4);
    if (pm == NULL || !SDL_PremultiplyAlpha(img->w, img->h, img->format, img->pixels, img->pitch, SDL_PIXELFORMAT_RGBA32, pm,
                                            img->w * 4, false)) {
        SDL_free(pm);
        return 0;
    }
    *tex = texture(GL_RGBA8, img->w, img->h, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, img->w, img->h, GL_RGBA, GL_UNSIGNED_BYTE, pm);
    GL(glGenerateMipmap)(GL_TEXTURE_2D);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    SDL_free(pm);
    return 1;
}

/* An image of the port's own over the copy (platform.h) */
void gl_image(const SDL_Surface *img, unsigned *tex, float x0, float y0, float x1, float y1, int add, int cx1, int cy1, int cx2, int cy2) {
    int s = s_scale;
    float q[16] = {x0, y0, 0, 0, x1, y0, 1, 0, x0, y1, 0, 1, x1, y1, 1, 1};
    int i;

    if (s == 0) {
        return;
    }
    flush();
    if (!image_tex(img, tex)) {
        return;
    }
    for (i = 0; i < 16; i += 4) { /* VRAM pixels to the copy's -1..1, as MAIN_VS */
        q[i] = q[i] / VRAM_W * 2 - 1;
        q[i + 1] = q[i + 1] / VRAM_H * 2 - 1;
    }
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_fbo);
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(cx1 * s, cy1 * s, (cx2 - cx1 + 1) * s, (cy2 - cy1 + 1) * s);
    GL(glEnable)(GL_BLEND);
    GL(glBlendEquation)(GL_FUNC_ADD);
    GL(glBlendFuncSeparate)(GL_ONE, add ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); /* premultiplied; the copy's alpha is the mask bit: kept */
    GL(glUseProgram)(s_blit);
    GL(glBindVertexArray)(s_quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, *tex);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
    GL(glDisable)(GL_BLEND);
    GL(glDisable)(GL_SCISSOR_TEST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
}

/* The depth buffer as the stage's table left it, before the models' table
   empties it: what the props are hidden by (stagefx.c) */
void gl_depth_save(void) {
    int s = s_scale;

    s_sd_valid = 0;
    if (s == 0 || !s_sd_fbo || !g_depth) {
        return;
    }
    flush();
    GL(glBindFramebuffer)(GL_READ_FRAMEBUFFER, s_fbo);
    GL(glBindFramebuffer)(GL_DRAW_FRAMEBUFFER, s_sd_fbo);
    GL(glBlitFramebuffer)(0, 0, VRAM_W * s, VRAM_H * s, 0, 0, VRAM_W * s, VRAM_H * s, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    s_sd_valid = 1;
}

/* Triangles (n a multiple of 3) textured with an image of the port's own
   (platform.h's PropVtx), cut out by its alpha, by the depth where the depth
   buffer is on. FOGO: NULL for the battle's fog, else {r, g, b, amount, start} */
static int props_begin(GLuint prog, const SDL_Surface *img, unsigned *tex, int x1, int y1, int x2, int y2, const float *fogo) {
    int s = s_scale;
    float zf = s_zfar > 0 ? s_zfar : 24000.0f;

    if (s == 0) {
        return 0;
    }
    flush();
    if (!image_tex(img, tex)) {
        return 0;
    }
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_fbo);
    GL(glViewport)(0, 0, VRAM_W * s, VRAM_H * s);
    GL(glEnable)(GL_SCISSOR_TEST);
    GL(glScissor)(x1 * s, y1 * s, (x2 - x1 + 1) * s, (y2 - y1 + 1) * s);
    GL(glUseProgram)(prog);
    GL(glActiveTexture)(GL_TEXTURE1);
    GL(glBindTexture)(GL_TEXTURE_2D, s_sd_tex);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, *tex);
    GL(glUniform1i)(GL(glGetUniformLocation)(prog, "usedepth"), g_depth && s_sd_valid);
    if (fogo != NULL) { /* the list's own fog: colour 0-255, how much, where it starts (0-1 of the far side) */
        GL(glUniform4f)(GL(glGetUniformLocation)(prog, "fog"), fogo[0] / 255.0f * 31.0f, fogo[1] / 255.0f * 31.0f, fogo[2] / 255.0f * 31.0f, fogo[3]);
        GL(glUniform2f)(GL(glGetUniformLocation)(prog, "fogz"), zf * fogo[4], zf);
    } else {
        GL(glUniform4f)(GL(glGetUniformLocation)(prog, "fog"), s_fog[0], s_fog[1], s_fog[2], s_fog_shown && s_zfar > 0 ? s_fog_amount : 0.0f); /* as flush() */
        GL(glUniform2f)(GL(glGetUniformLocation)(prog, "fogz"), s_zfar * s_fog_from, s_zfar);
    }
    if (g_depth) {
        GL(glEnable)(GL_DEPTH_TEST);
        GL(glDepthFunc)(GL_LEQUAL);
        GL(glDepthMask)(GL_TRUE);
    }
    GL(glDisable)(GL_BLEND);
    gq_begin(PF_GPU_PROPS);
    return 1;
}

static void props_end(void) {
    gq_end();
    GL(glDisable)(GL_DEPTH_TEST);
    GL(glDisable)(GL_SCISSOR_TEST);
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
}

void gl_props(const SDL_Surface *img, unsigned *tex, const PropVtx *v, int n, int x1, int y1, int x2, int y2, const float *fogo) {
    if (n <= 0 || !props_begin(s_prop, img, tex, x1, y1, x2, y2, fogo)) {
        return;
    }
    GL(glBindVertexArray)(s_prop_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_prop_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)(n * sizeof *v), v, GL_STREAM_DRAW);
    GL(glDrawArrays)(GL_TRIANGLES, 0, n);
    props_end();
}

/* A table of billboards, uploaded once (the card keeps it); 0 when it can't */
unsigned gl_inst_make(const PInst *v, int n) {
    GLuint b = 0;

    if (s_scale == 0 || n <= 0) {
        return 0;
    }
    GL(glGenBuffers)(1, &b);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, b);
    GL(glBufferData)(GL_ARRAY_BUFFER, (GLsizeiptr)n * sizeof *v, v, GL_STATIC_DRAW);
    return b;
}

void gl_inst_free(unsigned b) {
    if (b != 0) {
        GL(glDeleteBuffers)(1, &b);
    }
}

static void props_cam(const PCam *c) {
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "cam0"), c->row[0][0], c->row[0][1], c->row[0][2], c->row[0][3]);
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "cam1"), c->row[1][0], c->row[1][1], c->row[1][2], c->row[1][3]);
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "cam2"), c->row[2][0], c->row[2][1], c->row[2][2], c->row[2][3]);
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "camp"), c->h, c->ofx, c->ofy, c->wide);
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "clk"), c->t, c->bright, c->ox, c->oy);
    GL(glUniform2f)(GL(glGetUniformLocation)(s_pinst, "wnd"), c->wind[0], c->wind[1]);
}

/* N billboards of the table B from FIRST, with image IMG, by the camera */
void gl_props_inst(const SDL_Surface *img, unsigned *tex, unsigned b, int first, int n, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    const size_t st = sizeof(PInst);

    if (n <= 0 || b == 0 || !props_begin(s_pinst, img, tex, x1, y1, x2, y2, fogo)) {
        return;
    }
    props_cam(cam);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_pinst, "mode"), 0);
    GL(glBindVertexArray)(s_inst_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, b);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PInst, pos)));
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(1, 4, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PInst, w)));
    GL(glEnableVertexAttribArray)(2);
    GL(glVertexAttribPointer)(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, st, (void *)(first * st + offsetof(PInst, c)));
    GL(glEnableVertexAttribArray)(3);
    GL(glVertexAttribPointer)(3, 1, GL_FLOAT, GL_FALSE, st, (void *)(first * st + offsetof(PInst, fixed)));
    for (int a = 0; a < 4; a++) {
        GL(glVertexAttribDivisor)(a, 1);
    }
    GL(glDrawArraysInstanced)(GL_TRIANGLES, 0, 6, n);
    props_end();
}

/* An emitter's particles, or the stage's leaves (P->leaf): no data at all, each one's place is worked out from its number */
void gl_props_part(const SDL_Surface *img, unsigned *tex, const PPart *p, const PCam *cam, int x1, int y1, int x2, int y2, const float *fogo) {
    GLint loc;

    if (p->count <= 0 || !props_begin(s_pinst, img, tex, x1, y1, x2, y2, fogo)) {
        return;
    }
    props_cam(cam);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_pinst, "mode"), p->leaf ? 2 : 1);
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "pc"), p->center[0], p->center[1], p->center[2], p->radius);
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "pa"), p->top, p->fall, p->wind, p->above);
    GL(glUniform4f)(GL(glGetUniformLocation)(s_pinst, "pb"), p->size[0], p->size[1], p->floor, (float)p->count);
    loc = GL(glGetUniformLocation)(s_pinst, "ptint");
    GL(glUniform4f)(loc, p->tint[0] / 255.0f, p->tint[1] / 255.0f, p->tint[2] / 255.0f, SDL_clamp(p->fog, 0.0f, 1.0f));
    GL(glBindVertexArray)(s_part_vao);
    GL(glDrawArraysInstanced)(GL_TRIANGLES, 0, 6, p->count);
    props_end();
}

/* Follows render_scale: a new copy, made from the 1x VRAM; force: VRAM
   changed under it (a loaded state) */
void gl_sync(int force) {
    int want = SDL_clamp(g_res_scale, 1, s_max_scale);

    if (s_main == 0 || g_vram == NULL) { /* VRAM comes with ResetGraph */
        return;
    }
    if (want != s_scale) {
        flush();
        if (s_fbo) {
            GL(glDeleteFramebuffers)(1, &s_fbo);
            GL(glDeleteTextures)(1, &s_fbo_tex);
            GL(glDeleteRenderbuffers)(1, &s_fbo_depth);
            GL(glDeleteFramebuffers)(1, &s_tmp_fbo);
            GL(glDeleteTextures)(1, &s_tmp_tex);
            GL(glDeleteFramebuffers)(1, &s_mask_fbo);
            GL(glDeleteTextures)(1, &s_mask_tex);
            GL(glDeleteFramebuffers)(1, &s_blur_fbo);
            GL(glDeleteTextures)(1, &s_blur_tex);
            GL(glDeleteFramebuffers)(1, &s_sd_fbo);
            GL(glDeleteTextures)(1, &s_sd_tex);
        }
        s_sd_valid = 0;
        s_scale = want;
        s_fbo_tex = texture(GL_RGBA8, VRAM_W * want, VRAM_H * want, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
        s_fbo = framebuffer(s_fbo_tex);
        GL(glGenRenderbuffers)(1, &s_fbo_depth);
        GL(glBindRenderbuffer)(GL_RENDERBUFFER, s_fbo_depth);
        GL(glRenderbufferStorage)(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, VRAM_W * want, VRAM_H * want); /* the stencil: battle shadows */
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_fbo);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_fbo_depth);
        s_tmp_tex = texture(GL_RGBA8, VRAM_W * want, VRAM_H * want, GL_RGBA, GL_UNSIGNED_BYTE, GL_NEAREST);
        s_tmp_fbo = framebuffer(s_tmp_tex);
        s_mask_tex = texture(GL_R8, VRAM_W * want, VRAM_H * want, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
        s_mask_fbo = framebuffer(s_mask_tex);
        GL(glFramebufferRenderbuffer)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s_fbo_depth);
        s_blur_tex = texture(GL_R8, VRAM_W * want, VRAM_H * want, GL_RED, GL_UNSIGNED_BYTE, GL_NEAREST);
        s_blur_fbo = framebuffer(s_blur_tex);
        s_sd_tex = texture(GL_DEPTH24_STENCIL8, VRAM_W * want, VRAM_H * want, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, GL_NEAREST);
        GL(glGenFramebuffers)(1, &s_sd_fbo);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_sd_fbo);
        GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, s_sd_tex, 0);
        GL(glClearColor)(0, 0, 0, 0);
        GL(glClear)(GL_COLOR_BUFFER_BIT);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, s_mask_fbo);
        GL(glClear)(GL_COLOR_BUFFER_BIT);
        GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    } else if (!force) {
        return;
    }
    gl_refresh(0, 0, VRAM_W, VRAM_H);
}

int gl_init(SDL_Window *window) {
    GLint max = 0;

    s_window = window;
    if ((s_batch = SDL_malloc(BATCH * sizeof(GVtx))) == NULL) {
        return 0;
    }
    if ((s_ctx = SDL_GL_CreateContext(window)) == NULL) {
        LOG("gl: %s", SDL_GetError());
        return 0;
    }
#define X(f)                                                         \
    if ((p_##f = (__typeof__(f) *)SDL_GL_GetProcAddress(#f)) == NULL) { \
        LOG("gl: no %s", #f);                                        \
        return 0;                                                    \
    }
    GL_FUNCS(X)
#undef X
    if ((s_main = program(MAIN_VS, MAIN_FS)) == 0 || (s_blit = program(BLIT_VS, BLIT_FS)) == 0 ||
        (s_soft = program(BLIT_VS, SOFT_FS)) == 0 || (s_prop = program(PROP_VS, PROP_FS)) == 0 || (s_pinst = program(PINST_VS, PROP_FS)) == 0) {
        return 0;
    }
    s_soft_dir_loc = GL(glGetUniformLocation)(s_soft, "dir");
    s_soft_str_loc = GL(glGetUniformLocation)(s_soft, "strength");
    GL(glUseProgram)(s_soft);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_soft, "src"), 0);
    s_pass_loc = GL(glGetUniformLocation)(s_main, "pass");
    s_scale_loc = GL(glGetUniformLocation)(s_main, "scale");
    s_size_loc = GL(glGetUniformLocation)(s_main, "size");
    s_true_loc = GL(glGetUniformLocation)(s_main, "truecolor");
    s_persp_loc = GL(glGetUniformLocation)(s_main, "persp");
    s_smooth_loc = GL(glGetUniformLocation)(s_main, "smooth3d");
    s_fog_loc = GL(glGetUniformLocation)(s_main, "fog");
    s_fogz_loc = GL(glGetUniformLocation)(s_main, "fogz");
    GL(glUseProgram)(s_main);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_main, "vram"), 0);
    GL(glUniform2f)(s_size_loc, VRAM_W, VRAM_H);
    GL(glUseProgram)(s_blit);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_blit, "tex"), 0);
    GL(glUseProgram)(s_prop);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_prop, "tex"), 0);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_prop, "sdepth"), 1);
    GL(glUniform2f)(GL(glGetUniformLocation)(s_prop, "size"), VRAM_W, VRAM_H);
    s_prop_depth_loc = GL(glGetUniformLocation)(s_prop, "usedepth");
    s_prop_fog_loc = GL(glGetUniformLocation)(s_prop, "fog");
    s_prop_fogz_loc = GL(glGetUniformLocation)(s_prop, "fogz");
    GL(glUseProgram)(s_pinst);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_pinst, "tex"), 0);
    GL(glUniform1i)(GL(glGetUniformLocation)(s_pinst, "sdepth"), 1);
    GL(glUniform2f)(GL(glGetUniformLocation)(s_pinst, "size"), VRAM_W, VRAM_H);

    GL(glGenVertexArrays)(1, &s_vao);
    GL(glBindVertexArray)(s_vao);
    GL(glGenBuffers)(1, &s_vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_vbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 4, GL_FLOAT, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, x));
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(1, 3, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, r));
    GL(glEnableVertexAttribArray)(2);
    GL(glVertexAttribIPointer)(2, 4, GL_UNSIGNED_SHORT, sizeof(GVtx), (void *)offsetof(GVtx, clut_x));
    GL(glEnableVertexAttribArray)(3);
    GL(glVertexAttribIPointer)(3, 4, GL_UNSIGNED_SHORT, sizeof(GVtx), (void *)offsetof(GVtx, umin));
    GL(glEnableVertexAttribArray)(4);
    GL(glVertexAttribIPointer)(4, 4, GL_UNSIGNED_SHORT, sizeof(GVtx), (void *)offsetof(GVtx, twmx));
    GL(glEnableVertexAttribArray)(5);
    GL(glVertexAttribIPointer)(5, 1, GL_UNSIGNED_INT, sizeof(GVtx), (void *)offsetof(GVtx, mode));
    GL(glEnableVertexAttribArray)(6);
    GL(glVertexAttribPointer)(6, 1, GL_FLOAT, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, z));
    GL(glEnableVertexAttribArray)(7);
    GL(glVertexAttribPointer)(7, 1, GL_FLOAT, GL_FALSE, sizeof(GVtx), (void *)offsetof(GVtx, w));

    GL(glGenVertexArrays)(1, &s_prop_vao);
    GL(glBindVertexArray)(s_prop_vao);
    GL(glGenBuffers)(1, &s_prop_vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_prop_vbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 3, GL_FLOAT, GL_FALSE, sizeof(PropVtx), (void *)offsetof(PropVtx, x));
    GL(glEnableVertexAttribArray)(1);
    GL(glVertexAttribPointer)(1, 2, GL_FLOAT, GL_FALSE, sizeof(PropVtx), (void *)offsetof(PropVtx, u));
    GL(glEnableVertexAttribArray)(2);
    GL(glVertexAttribPointer)(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(PropVtx), (void *)offsetof(PropVtx, r));

    GL(glGenVertexArrays)(1, &s_inst_vao);
    GL(glGenVertexArrays)(1, &s_part_vao); /* no attributes: a particle is its number */

    GL(glGenVertexArrays)(1, &s_quad_vao);
    GL(glBindVertexArray)(s_quad_vao);
    GL(glGenBuffers)(1, &s_quad_vbo);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_quad_vbo);
    GL(glEnableVertexAttribArray)(0);
    GL(glVertexAttribPointer)(0, 4, GL_FLOAT, GL_FALSE, 16, NULL);

    s_vram_tex = texture(GL_R16UI, VRAM_W, VRAM_H, GL_RED_INTEGER, GL_UNSIGNED_SHORT, GL_NEAREST);
    s_movie_tex = texture(GL_RGBA8, VRAM_W, VRAM_H, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    s_menu_tex = texture(GL_RGBA8, 1280, 960, GL_RGBA, GL_UNSIGNED_BYTE, GL_LINEAR);
    GL(glGetIntegerv)(GL_MAX_TEXTURE_SIZE, &max);
    s_max_scale = SDL_clamp(max / VRAM_W, 1, 8);
    if (GL(glGetError)() != GL_NO_ERROR) {
        LOG("gl: setting up failed");
        return 0;
    }
    return 1;
}

/* A texture onto the window's 4:3 area: uv from (u0, v0) top left to (u1, v1) */
static void quad(GLuint tex, const int vp[4], float u0, float v0, float u1, float v1, int linear) {
    float q[16] = {-1, 1, u0, v0, 1, 1, u1, v0, -1, -1, u0, v1, 1, -1, u1, v1};

    GL(glViewport)(vp[0], vp[1], vp[2], vp[3]);
    GL(glUseProgram)(s_blit);
    GL(glBindVertexArray)(s_quad_vao);
    GL(glBindBuffer)(GL_ARRAY_BUFFER, s_quad_vbo);
    GL(glBufferData)(GL_ARRAY_BUFFER, sizeof q, q, GL_STREAM_DRAW);
    GL(glActiveTexture)(GL_TEXTURE0);
    GL(glBindTexture)(GL_TEXTURE_2D, tex);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
}

/* The frame: the display area of the copy (or the 1x VRAM's 24-bit movie)
   letterboxed to 4:3, then the menu (a 1280x960 RGBA surface, or NULL) */
void gl_present(const SDL_Surface *menu, SDL_Surface **shot) {
    PSX_RECT d = g_disp.disp;
    int w, h, vp[4], menu_vp[4];
    int aw = gte_widescreen() ? 16 : 4, ah = gte_widescreen() ? 9 : 3; /* the picture's aspect */

    static int vsync = -1;

    flush();
    /* the fog for the next frame: on while a sky is drawn, its depths eased
       toward this frame's farthest (a camera's cut) */
    {
        float bright;
        const StageFx *fx = stagefx_current(&bright);
        int i;

        s_fog_shown = s_fog_on && fx != NULL && fx->fog > 0;
        if (s_fog_shown) {
            for (i = 0; i < 3; i++) {
                s_fog[i] = fx->fog_color[i] >= 0 ? fx->fog_color[i] * bright / 8.0f : s_bg[i];
            }
            s_fog_amount = fx->fog, s_fog_from = fx->fog_start;
        }
        stagefx_frame();
    }
    s_zfar = !s_fog_on || s_zframe <= 0 ? 0 : s_zfar > 0 ? s_zfar + (s_zframe - s_zfar) * 0.2f : s_zframe;
    s_fog_on = 0, s_zframe = 0;
    if (vsync != g_vsync) {
        /* the game keeps its own time (etc.c); vsync only holds the swap for
           the screen's refresh, so that a frame isn't shown torn */
        vsync = g_vsync;
        SDL_GL_SetSwapInterval(vsync);
    }
    SDL_GetWindowSizeInPixels(s_window, &w, &h);
    vp[2] = w * ah > h * aw ? h * aw / ah : w;
    vp[3] = w * ah > h * aw ? h : w * ah / aw;
    vp[0] = (w - vp[2]) / 2;
    vp[1] = (h - vp[3]) / 2;
    menu_vp[2] = w * 3 > h * 4 ? h * 4 / 3 : w;
    menu_vp[3] = w * 3 > h * 4 ? h : w * 3 / 4;
    menu_vp[0] = (w - menu_vp[2]) / 2;
    menu_vp[1] = (h - menu_vp[3]) / 2;
    GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
    GL(glViewport)(0, 0, w, h);
    GL(glClearColor)(0, 0, 0, 1);
    GL(glClear)(GL_COLOR_BUFFER_BIT);
    GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE); /* the copy's alpha is the mask bit: the window stays opaque */
    if (d.x + d.w > VRAM_W) d.w = VRAM_W - d.x;
    if (d.y + d.h > VRAM_H) d.h = VRAM_H - d.y;
    if (g_disp_on && d.w > 0 && d.h > 0 && g_disp.isrgb24) {
        /* 24-bit (the movies): d.w pixels of 3 bytes from d.x on, as RGBA */
        static uint32_t *rgba;
        int x, y;

        if (rgba == NULL && (rgba = SDL_malloc(VRAM_W * VRAM_H * 4)) == NULL) {
            return;
        }
        for (y = 0; y < d.h; y++) {
            const uint8_t *b = (const uint8_t *)&g_vram[(d.y + y) * VRAM_W + d.x];

            for (x = 0; x < d.w && (d.x * 2 + x * 3 + 3) <= VRAM_W * 2; x++) {
                rgba[y * d.w + x] = (uint32_t)(b[x * 3] | b[x * 3 + 1] << 8 | b[x * 3 + 2] << 16 | 0xFF000000u);
            }
        }
        GL(glBindTexture)(GL_TEXTURE_2D, s_movie_tex);
        GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
        GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, d.w, d.h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        quad(s_movie_tex, vp, 0, 0, d.w / (float)VRAM_W, d.h / (float)VRAM_H, g_smooth_movies);
    } else if (g_disp_on && d.w > 0 && d.h > 0 && s_scale > 0) {
        /* the copy's rows go up with VRAM's y: its top row on top */
        quad(s_fbo_tex, vp, d.x / (float)VRAM_W, d.y / (float)VRAM_H, (d.x + d.w) / (float)VRAM_W, (d.y + d.h) / (float)VRAM_H, 1);
    }
    if (menu != NULL) {
        GL(glBindTexture)(GL_TEXTURE_2D, s_menu_tex);
        GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);
        GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, menu->pitch / 4);
        GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, 1280, 960, GL_RGBA, GL_UNSIGNED_BYTE, menu->pixels);
        GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
        GL(glEnable)(GL_BLEND);
        GL(glBlendFuncSeparate)(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
        quad(s_menu_tex, menu_vp, 0, 0, 1, 1, 0);
        GL(glDisable)(GL_BLEND);
    }
    GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    if (shot != NULL && (*shot = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ABGR8888)) != NULL) {
        GL(glPixelStorei)(GL_PACK_ALIGNMENT, 4);
        GL(glReadPixels)(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (*shot)->pixels);
        SDL_FlipSurface(*shot, SDL_FLIP_VERTICAL);
    }
    gq_end();
    SDL_GL_SwapWindow(s_window);
    gq_frame();
}
