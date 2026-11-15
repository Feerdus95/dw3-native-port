/* The OpenGL renderer's shaders for the game's drawing and the port's images, and how a program is built */
#include "gl.h"

/* --- shaders --- */

const char *MAIN_VS =
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
    "uniform vec2 jit;\n" /* the 3D shifted this much (clip units), for an upscaler: 0 without one */
    "out vec2 uv;\n" /* in perspective by w, or straight across the screen (w 1) */
    "out vec3 col;\n"
    "out float vw;\n" /* the view depth across the polygon, for the fog */
    "flat out uvec4 t, lim, tw;\n"
    "flat out uint md, deep;\n" /* deep: a 3D polygon, the GTE gave its points their depth */
    "void main() {\n"
    /* linear in 1/z, as depth goes across the screen; the depth test is off without one */
    "    gl_Position = vec4(pos_uv.xy / size * 2.0 - 1.0, z > 0.0 ? clamp(1.0 - 64.0 / z, 0.0, 1.0) * 2.0 - 1.0 : 0.0, 1.0);\n"
    "    gl_Position *= persp != 0 && w > 0.0 ? w : 1.0;\n"
    "    if (w > 0.0) gl_Position.xy += jit * gl_Position.w;\n"
    "    uv = pos_uv.zw; col = color; t = tex; lim = limits; tw = window; md = mode; deep = w > 0.0 ? 1u : 0u; vw = w;\n"
    "}\n";

/* mode: 2 textured, 4 raw texture, 8 semi-transparent, 16-48 texel depth,
   64 dithered, 128 the copy of VRAM itself, 256 sets the mask bit, 512 a soft
   shadow (native.h's NATIVE_SOFT_SHADOW), 1024 a battle shadow's triangle,
   2048 a stage's painted horizon (no fog) */
const char *MAIN_FS =
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
const char *BLIT_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec4 pos_uv;\n"
    "out vec2 uv;\n"
    "void main() { gl_Position = vec4(pos_uv.xy, 0.0, 1.0); uv = pos_uv.zw; }\n";

const char *BLIT_FS =
    "#version 330 core\n"
    "uniform sampler2D tex;\n"
    "in vec2 uv;\n"
    "out vec4 frag;\n"
    "void main() { frag = texture(tex, uv); }\n";

/* The world's props (stagefx.c): a textured billboard or mesh with its own RGBA
   image, drawn over the models. Cut out by alpha, and hidden by the stage where
   the stage is nearer (the stage's depth, saved before the models' table clears
   the depth buffer: gl_depth_save) and by the fighters (the depth buffer) */
const char *PROP_VS =
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

const char *PROP_FS =
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
const char *PINST_VS =
    "#version 330 core\n"
    "layout(location = 0) in vec3 ipos;\n"
    "layout(location = 1) in vec4 idim;\n"  /* width, height, sway, turn in degrees */
    "layout(location = 2) in vec4 icol;\n"  /* tint, fog */
    "layout(location = 3) in float ifixed;\n"
    "layout(location = 4) in vec3 vnrm;\n"  /* mode 3: the mesh vertex's normal; ipos is its place and idim.xy its texture coordinate */
    "layout(location = 5) in vec4 mi0;\n"   /* mode 3, per instance: foot, scale */
    "layout(location = 6) in vec4 mi1;\n"   /* turn in degrees, sway, fog */
    "layout(location = 7) in vec4 mi2;\n"   /* tint 0-1 */
    "uniform vec2 mmesh;\n"                 /* the mesh's lowest y and height */
    "uniform vec2 size;\n"
    "uniform sampler2D fdepth;\n" /* the fighters' depth: a blade under one is trodden down */
    "uniform int usefd;\n"
    "uniform float clearz;\n"
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
    "float trodden(vec3 s0, vec3 s1, float hw) {\n" /* 0-1: how much a fighter stands on, or is covered by, the blade from S0 (its foot) to S1 (its tip); hw: its half width, pixels */
    "    if (usefd == 0) return 0.0;\n"
    "    vec2 k = vec2(textureSize(fdepth, 0)) / size, a = s0.xy + clk.zw, b = s1.xy + clk.zw;\n"
    "    float r = max(hw, 3.0), sum = 0.0;\n"
    "    for (int j = 0; j < 7; j++) {\n" /* along it, and a point to each side of its middle: grass in front of a foot covers it as much as grass under it */
    "        vec2 p = j < 5 ? mix(a, b, float(j) * 0.25) : mix(a, b, 0.5) + vec2(j == 5 ? -r : r, 0.0);\n"
    "        float f = texelFetch(fdepth, ivec2(p * k), 0).r;\n"
    "        if (f < 1.0) sum += 1.0 - smoothstep(clearz * 0.4, clearz, abs(64.0 / (1.0 - f) - s0.z));\n"
    "    }\n"
    "    return min(sum / 3.0, 1.0);\n" /* a third of the points on a fighter treads it all */
    "}\n"
    "const int TRI[6] = int[6](0, 1, 2, 1, 3, 2);\n"
    "const int TRD[6] = int[6](0, 1, 2, 0, 2, 3);\n"
    "const vec3 PAL[10] = vec3[10](vec3(58, 102, 34), vec3(92, 140, 52), vec3(74, 118, 38), vec3(120, 156, 62),\n"
    "    vec3(110, 120, 40), vec3(150, 160, 70), vec3(128, 86, 36), vec3(170, 124, 60), vec3(96, 70, 34), vec3(136, 104, 52));\n"
    "void main() {\n"
    "    if (mode == 3) {\n" /* a mesh vertex of an instance: stagefx.c's models_draw as it was, but on the card */
    "        float sc = mi0.w, a = mi1.x * 0.017453293, ca = cos(a), sa = sin(a), height = mmesh.y * sc;\n"
    "        float lx = ipos.x * sc, ly = (ipos.y - mmesh.x) * sc, lz = ipos.z * sc, rel = clamp(ly / height, 0.0, 1.0);\n"
    "        float move = 0.5 + sin(clk.x * 0.042 + (mi0.x + mi0.z) * 0.0004), bend = mi1.y / 18.0 * height * 0.3 * move;\n"
    "        vec2 wd = wnd * (bend * rel * rel);\n"
    "        vec3 P = vec3(mi0.x + lx * ca + lz * sa + wd.x, -(mi0.y + ly), mi0.z - lx * sa + lz * ca + wd.y);\n"
    "        vec3 v = vec3(dot(cam0.xyz, P), dot(cam1.xyz, P), dot(cam2.xyz, P)) + vec3(cam0.w, cam1.w, cam2.w);\n"
    "        float nx = vnrm.x * ca + vnrm.z * sa, nz = -vnrm.x * sa + vnrm.z * ca;\n"
    "        float shade = 0.55 + 0.45 * max((nx * 0.35 + vnrm.y * 0.8 + nz * 0.45) / 0.98, 0.0);\n" /* from above and a side */
    "        col = vec4(clamp(mi2.rgb * clk.y * shade, 0.0, 1.0), mi1.z);\n"
    "        uv = idim.xy; vz = v.z;\n"
    "        vec2 off = vec2(camp.y, camp.z) + clk.zw;\n" /* put() and project() in clip space: linear in the view's x, y, z, so the card clips what is behind the camera */
    "        vec2 ndc = vec2(camp.x * v.x * camp.w, camp.x * v.y) * 2.0 / size + (off * 2.0 / size - 1.0) * v.z;\n"
    "        gl_Position = vec4(ndc, v.z - 128.0, v.z);\n" /* z: (1 - 128 / z) * z, as put()'s */
    "        return;\n"
    "    }\n"
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
    "            float hw = w * 0.5 * (camp.x / s0.z) * camp.w, press = trodden(s0, s1, hw);\n"
    "            if (press > 0.0) {\n" /* flattened to a sixth under or before a foot */
    "                h *= 1.0 - 0.85 * press, top = base + (top - base) * (1.0 - 0.85 * press);\n"
    "                if (!project(top, s1)) { kill(); return; }\n"
    "            }\n"
    "            float full = h * (camp.x / s0.z);\n"
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
const char *SOFT_FS =
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

static GLuint shader(GLenum type, const char *const *src, int n) {
    GLuint s = GL(glCreateShader)(type);
    GLint ok;
    char log[1024];

    GL(glShaderSource)(s, n, src, NULL);
    GL(glCompileShader)(s);
    GL(glGetShaderiv)(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GL(glGetShaderInfoLog)(s, sizeof log, NULL, log);
        LOG("gl: shader: %s", log);
    }
    return s;
}

/* A program of a vertex and a fragment shader, each in N parts */
GLuint gl_program_of(const char *const *vs, const char *const *fs, int n) {
    GLuint p = GL(glCreateProgram)();
    GLint ok;
    char log[1024];

    GL(glAttachShader)(p, shader(GL_VERTEX_SHADER, vs, n));
    GL(glAttachShader)(p, shader(GL_FRAGMENT_SHADER, fs, n));
    GL(glLinkProgram)(p);
    GL(glGetProgramiv)(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GL(glGetProgramInfoLog)(p, sizeof log, NULL, log);
        LOG("gl: program: %s", log);
        return 0;
    }
    return p;
}

GLuint gl_program(const char *vs, const char *fs) {
    return gl_program_of(&vs, &fs, 1);
}
