"""Renders transpiled texture shaders with Skia's CPU backend and compares with GL's rules computed in numpy. Run: python tests/check_textures.py --transpiler build/glsl-to-sksl.exe"""
import subprocess, sys
import numpy as np
import skia

import argparse
parser = argparse.ArgumentParser(description="Check texture lowering against GL results.")
parser.add_argument("--transpiler", required=True, help="path to the glsl-to-sksl executable")
args = parser.parse_args()
EXE = args.transpiler
rng = np.random.default_rng(7)
failures = 0

def transpile(src):
    r = subprocess.run([EXE, "-"], input=src, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    return r.stdout

def image(rgba):
    """rgba: uint8 array (h, w, 4)."""
    return skia.Image.fromarray(np.ascontiguousarray(rgba), colorType=skia.kRGBA_8888_ColorType, alphaType=skia.kUnpremul_AlphaType)

def child(rgba, tile=skia.TileMode.kRepeat, linear=False):
    h, w = rgba.shape[:2]
    sampling = skia.SamplingOptions(skia.FilterMode.kLinear if linear else skia.FilterMode.kNearest)
    return image(rgba).makeRawShader(tile, tile, sampling, skia.Matrix.Scale(1.0 / w, 1.0 / h))

def render(sksl, w, h, uniforms={}, children={}, background=None, blend=skia.BlendMode.kSrc):
    effect = skia.RuntimeEffect.MakeForShader(sksl)
    b = skia.RuntimeShaderBuilder(effect)
    for k, v in uniforms.items():
        if isinstance(v, (tuple, list)):
            v = [skia.V2, skia.V3, skia.V4][len(v) - 2](*v)
        b.setUniform(k, v)
    for k, v in children.items():
        b.setChild(k, v)
    info = skia.ImageInfo.Make(w, h, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
    surface = skia.Surface.MakeRaster(info)
    if background is not None:
        surface.getCanvas().clear(skia.Color4f(*background))
    paint = skia.Paint(); paint.setShader(b.makeShader()); paint.setBlendMode(blend)
    surface.getCanvas().drawPaint(paint)
    px = np.zeros((h, w, 4), dtype=np.float32)
    surface.readPixels(info, px)
    return px

def check(name, got, want, tol=1e-6, mask=None):
    global failures
    d = np.abs(got - want)
    if mask is not None:
        d = d[mask]
    err = float(d.max()) if d.size else 0.0
    ok = err <= tol
    failures += not ok
    print(f"{'ok  ' if ok else 'FAIL'} {name}: max error {err:.3g}")

H = "#version 320 es\nprecision highp float;\nout vec4 color;\n"

# ---------------------------------------------------------------- 2D
W2, H2 = 5, 3
img = rng.integers(0, 256, (H2, W2, 4), dtype=np.uint8); img[..., 3] = 255
T = img.astype(np.float32) / 255.0  # T[j, i]

src = H + "uniform sampler2D tex;\nvoid main(){ color = texelFetch(tex, ivec2(gl_FragCoord.xy), 0); }"
for linear in (False, True):
    check(f"texelFetch ({'linear' if linear else 'nearest'} child)", render(transpile(src), W2, H2, {"texSize": (W2, H2)}, {"tex": child(img, linear=linear)}), T)

src = H + "uniform sampler2D tex;\nvoid main(){ color = textureOffset(tex, gl_FragCoord.xy / vec2(5.0, 3.0), ivec2(1, -1)); }"
want = np.roll(np.roll(T, -1, axis=1), 1, axis=0)
check("textureOffset nearest repeat", render(transpile(src), W2, H2, {"texSize": (W2, H2)}, {"tex": child(img)}), want)

src = H + "uniform sampler2D tex;\nvoid main(){ color = vec4(vec2(textureSize(tex, 0)), vec2(textureSize(tex, 1))) / 10.0; }"
check("textureSize levels 0 and 1", render(transpile(src), 1, 1, {"texSize": (W2, H2)}, {"tex": child(img)})[0, 0], np.array([.5, .3, .2, .1], np.float32))

src = H + "uniform sampler2D tex;\nvoid main(){ color = vec4(textureLod(tex, gl_FragCoord.xy / vec2(5.0, 3.0), 3.0).rgb, 1.0) + textureGrad(tex, vec2(0.5), vec2(1.0), vec2(1.0)) * 0.0; }"
check("textureLod/Grad sample base level", render(transpile(src), W2, H2, {}, {"tex": child(img)}), T)

GW, GH = 13, 7
for comp in range(4):
    src = H + f"uniform sampler2D tex;\nvoid main(){{ vec2 uv = (gl_FragCoord.xy * vec2(0.73, 0.61) - 0.2) / vec2(5.0, 3.0); color = textureGatherOffset(tex, uv, ivec2(-1, 2), {comp}); }}"
    got = render(transpile(src), GW, GH, {"texSize": (W2, H2)}, {"tex": child(img)})
    want = np.zeros((GH, GW, 4), np.float32)
    for y in range(GH):
        for x in range(GW):
            u = ((x + .5) * np.float32(.73) - .2) / 5 * W2 - .5
            v = ((y + .5) * np.float32(.61) - .2) / 3 * H2 - .5
            i0, j0 = int(np.floor(u)) - 1, int(np.floor(v)) + 2
            texel = lambda i, j: T[j % H2, i % W2, comp]
            want[y, x] = [texel(i0, j0 + 1), texel(i0 + 1, j0 + 1), texel(i0 + 1, j0), texel(i0, j0)]
    check(f"textureGatherOffset component {comp}", got, want)

img2 = rng.integers(0, 256, (H2, W2, 4), dtype=np.uint8); img2[..., 3] = 255
src = H + """uniform sampler2D ts[2];
uniform int which;
vec4 pick(sampler2D s, vec2 uv) { return texture(s, uv); }
void main(){ vec2 uv = gl_FragCoord.xy / vec2(5.0, 3.0); color = texture(ts[which], uv) * 0.5 + pick(ts[1 - which], uv) * 0.0 + pick(ts[0], uv) * 0.5; }"""
sk = transpile(src)
for which, other in ((0, T), (1, img2.astype(np.float32) / 255)):
    check(f"sampler array index {which} and sampler parameter", render(sk, W2, H2, {"which": which}, {"ts_0": child(img), "ts_1": child(img2)}), other * .5 + T * .5, 1e-6)

# ---------------------------------------------------------------- cube
F = 4
faces = [rng.integers(0, 256, (F, F, 4), dtype=np.uint8) for _ in range(6)]
for f in faces: f[..., 3] = 255
def cube_ref(d):
    x, y, z = d; ax, ay, az = abs(x), abs(y), abs(z)
    if ax >= ay and ax >= az:
        face, sc, tc, ma = (0, -z, -y, ax) if x >= 0 else (1, z, -y, ax)
    elif ay >= az:
        face, sc, tc, ma = (2, x, z, ay) if y >= 0 else (3, x, -z, ay)
    else:
        face, sc, tc, ma = (4, x, -y, az) if z >= 0 else (5, -x, -y, az)
    s, t = .5 * (sc / ma + 1), .5 * (tc / ma + 1)
    return face, s, t
CW, CH = 64, 32
src = H + """uniform samplerCube sky;
void main(){ float th = gl_FragCoord.x / 64.0 * 6.2831853, ph = gl_FragCoord.y / 32.0 * 3.1415926;
  color = texture(sky, vec3(sin(ph) * cos(th), cos(ph), sin(ph) * sin(th))); }"""
got = render(transpile(src), CW, CH, {}, {f"sky_{n}": child(faces[i], skia.TileMode.kClamp) for i, n in enumerate(["px", "nx", "py", "ny", "pz", "nz"])})
want = np.zeros_like(got); mask = np.zeros(got.shape[:2], bool)
for yy in range(CH):
    for xx in range(CW):
        th = np.float32((xx + .5) / 64 * 6.2831853); ph = np.float32((yy + .5) / 32 * 3.1415926)
        d = (np.sin(ph) * np.cos(th), np.cos(ph), np.sin(ph) * np.sin(th))
        face, s, t = cube_ref(d)
        a, b = s * F, t * F
        mask[yy, xx] = min(a - np.floor(a), np.ceil(a) - a, b - np.floor(b), np.ceil(b) - b) > 1e-3 and sorted(map(abs, d))[-1] - sorted(map(abs, d))[-2] > 1e-4
        want[yy, xx] = faces[face][min(int(b), F - 1), min(int(a), F - 1)] / 255.0
check("samplerCube face selection (nearest)", got, want, 1e-6, mask)

# ---------------------------------------------------------------- 3D and arrays
VW, VH, VD = 4, 3, 5
vol = rng.integers(0, 256, (VD, VH, VW, 4), dtype=np.uint8); vol[..., 3] = 255
atlas = vol.reshape(VD * VH, VW, 4)
V = vol.astype(np.float64) / 255
def wrap(i, n, mode):
    if mode == "repeat": return i % n
    if mode == "clamp": return min(max(i, 0), n - 1)
    m = i % (2 * n) - n
    return n - 1 - (m if m >= 0 else -1 - m)
def tex3d(p, mode, nearest):
    size = (VW, VH, VD)
    if nearest:
        i = [wrap(int(np.floor(p[k] * size[k])), size[k], mode) for k in range(3)]
        return V[i[2], i[1], i[0]]
    q = [p[k] * size[k] - .5 for k in range(3)]
    b = [int(np.floor(c)) for c in q]; f = [c - np.floor(c) for c in q]
    out = 0
    for dz in (0, 1):
        for dy in (0, 1):
            for dx in (0, 1):
                w = (f[0] if dx else 1 - f[0]) * (f[1] if dy else 1 - f[1]) * (f[2] if dz else 1 - f[2])
                out = out + w * V[wrap(b[2] + dz, VD, mode), wrap(b[1] + dy, VH, mode), wrap(b[0] + dx, VW, mode)]
    return out
RW, RH = 23, 17
for mode in ("repeat", "clamp", "mirror"):
    for nearest in (False, True):
        ann = f"// @wrap{{{mode}}}" + (" @filter{nearest}" if nearest else "")
        src = H + f"""uniform sampler3D vol; {ann}
uniform float z;
void main(){{ color = texture(vol, vec3(gl_FragCoord.xy * vec2(0.11, 0.13) - vec2(0.7, 0.5), z)); }}"""
        sk = transpile(src)
        for z in (-0.37, 0.52, 1.61):
            got = render(sk, RW, RH, {"z": z, "volSize": (VW, VH, VD)}, {"vol": child(atlas, skia.TileMode.kClamp, linear=True)})
            want = np.zeros_like(got); mask = np.ones(got.shape[:2], bool)
            for yy in range(RH):
                for xx in range(RW):
                    p = (np.float32((xx + .5) * np.float32(.11) - np.float32(.7)), np.float32((yy + .5) * np.float32(.13) - np.float32(.5)), np.float32(z))
                    want[yy, xx] = tex3d(p, mode, nearest)
                    if nearest:  # skip texel boundaries
                        mask[yy, xx] = all(abs(p[k] * s - round(p[k] * s)) > 1e-4 for k, s in enumerate((VW, VH, VD)))
            check(f"sampler3D {mode} {'nearest' if nearest else 'linear'} z={z}", got, want, 2e-5, mask)

LW, LH, LN = 3, 4, 3
lay = rng.integers(0, 256, (LN, LH, LW, 4), dtype=np.uint8); lay[..., 3] = 255
L = lay.astype(np.float64) / 255
src = H + """uniform sampler2DArray layers; // @wrap{clamp}
uniform float layer;
void main(){ color = texture(layers, vec3(gl_FragCoord.xy * vec2(0.09, 0.07) - 0.2, layer)); }"""
sk = transpile(src)
for layer in (-0.7, 0.49, 0.51, 1.7, 5.0):
    got = render(sk, RW, RH, {"layer": layer, "layersSize": (LW, LH, LN)}, {"layers": child(lay.reshape(LN * LH, LW, 4), skia.TileMode.kClamp)})
    want = np.zeros_like(got)
    k = min(max(int(np.floor(layer + .5)), 0), LN - 1)
    for yy in range(RH):
        for xx in range(RW):
            u = np.float32((xx + .5) * np.float32(.09) - np.float32(.2)) * LW - .5
            v = np.float32((yy + .5) * np.float32(.07) - np.float32(.2)) * LH - .5
            i, j = int(np.floor(u)), int(np.floor(v)); fu, fv = u - i, v - j
            c = lambda a, b: L[k, min(max(b, 0), LH - 1), min(max(a, 0), LW - 1)]
            want[yy, xx] = (c(i, j) * (1 - fu) + c(i + 1, j) * fu) * (1 - fv) + (c(i, j + 1) * (1 - fu) + c(i + 1, j + 1) * fu) * fv
    check(f"sampler2DArray linear clamp layer={layer}", got, want, 2e-5)

# ---------------------------------------------------------------- discard
src = H + """bool hole(vec2 p) { if (length(p - 8.0) < 4.0) discard; return p.x > 12.0; }
void main(){ if (gl_FragCoord.y < 3.0) discard; color = vec4(0.0, 0.5, 1.0, 1.0); if (hole(gl_FragCoord.xy)) discard; }"""
got = render(transpile(src), 16, 16, background=(1, 0, 0, 1), blend=skia.BlendMode.kSrcOver)
want = np.zeros_like(got); want[...] = (0, .5, 1, 1)
yy, xx = np.mgrid[0:16, 0:16] + .5
gone = (yy < 3) | (np.hypot(xx - 8, yy - 8) < 4) | (xx > 12)
want[gone] = (1, 0, 0, 1)
check("discard keeps the destination (source-over)", got, want)
src = "#version 300 es\nprecision highp float;\nout vec4 color;\nvoid main(){ float d = dFdx(gl_FragCoord.x * gl_FragCoord.x); if (mod(gl_FragCoord.x, 2.0) < 1.0) discard; color = vec4(d / 32.0, 0.0, 0.0, 1.0); }"
got = render(transpile(src), 8, 2, background=(0, 0, 1, 1), blend=skia.BlendMode.kSrcOver)
want = np.zeros_like(got); want[...] = (0, 0, 1, 1)
for x in range(1, 8, 2):
    want[:, x] = ((2 * (x - 1) + 2) / 32.0, 0, 0, 1)  # fine dFdx of x^2 over the quad (x-1, x)
check("discard after a derivative", got, want, 1e-6)

print("failures:", failures)
sys.exit(1 if failures else 0)
