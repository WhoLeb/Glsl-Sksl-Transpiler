"""Checks uint lowering against exact 32-bit unsigned results with Skia's CPU backend. Run: python tests/check_uint_ops.py --transpiler build/glsl-to-sksl.exe"""
import argparse, math, random, subprocess, sys
import numpy as np
import skia

parser = argparse.ArgumentParser(description="Check uint operators, conversions, built-ins and loops against 32-bit unsigned results.")
parser.add_argument("--transpiler", required=True, help="path to the glsl-to-sksl executable")
args = parser.parse_args()
EXE = args.transpiler
# Scalar int uniforms carry the bits: skia-python writes int-vector uniforms as floats.
H = "#version 300 es\nprecision highp float;\nuniform int ia;\nuniform int ib;\nuniform int iw;\nout vec4 color;\n"
M = 0xFFFFFFFF
FLAG = "color = vec4(ok ? 1. : 0., 1., 1., 1.);"


def s32(v):
    v &= M
    return v - (1 << 32) if v >= 1 << 31 else v


def transpile(source):
    r = subprocess.run([EXE, "-"], input=source, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(source + "\n" + r.stderr + r.stdout)
    return r.stdout


def effect(body, header=H):
    sksl = transpile(header + body)
    try:
        return skia.RuntimeEffect.MakeForShader(sksl)
    except RuntimeError as e:
        raise RuntimeError(f"{e}\n{sksl}")


def run(eff, **u):
    b = skia.RuntimeShaderBuilder(eff)
    for k, v in u.items():
        b.setUniform(k, v)
    info = skia.ImageInfo.Make(1, 1, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
    s = skia.Surface.MakeRaster(info)
    p = skia.Paint(); p.setShader(b.makeShader()); p.setBlendMode(skia.BlendMode.kSrc)
    s.getCanvas().drawPaint(p)
    px = np.zeros((1, 1, 4), np.float32); s.readPixels(info, px)
    return px[0, 0]


rng = random.Random(5)
EDGE = [0, 1, 2, 3, 7, 255, 65535, 65536, 2**30, 2**31 - 1, 2**31, 2**31 + 1, 3000000000, 2**32 - 2, 2**32 - 1, 0x9E3779B1, 12345678]
def values(n):
    return EDGE + [rng.randint(0, M) for _ in range(n)]


failures = 0
def report(name, bad, n):
    global failures
    failures += bad
    print(f"{'ok  ' if not bad else 'FAIL'} {name}: {n - bad}/{n}")


def binary(op, f, rhs=None, n=40):
    """a op b as an expression, compound assignment, vector op and vector-scalar op."""
    eff = effect("void main(){ uint a = uint(ia); uint b = uint(ib); uint w = uint(iw); uvec3 va = uvec3(a, 1u, a); uvec3 vb = uvec3(b, 1u, b);"
                 f" uint s = a {op} b; uint c = a; c {op}= b; uvec3 v = va {op} vb; uvec3 m = va {op} b;"
                 " color = vec4(s == w ? 1. : 0., c == w ? 1. : 0., v.x == w && v.z == w ? 1. : 0., m.z == w ? 1. : 0.); }")
    bad = count = 0
    for x in values(n):
        for y in values(n // 2):
            if rhs:
                y = rhs(y)
            if y is None:
                continue
            count += 1
            want = f(x, y) & M
            got = run(eff, ia=s32(x), ib=s32(y), iw=s32(want))
            if not np.all(got == 1):
                bad += 1
                if bad <= 3:
                    print(f"  {x} {op} {y}: want {want}, flags {got}")
    report(op, bad, count)


binary("+", lambda x, y: x + y)
binary("-", lambda x, y: x - y)
binary("*", lambda x, y: x * y)
binary("/", lambda x, y: x // y, rhs=lambda y: y or None)
binary("%", lambda x, y: x % y, rhs=lambda y: y or None)
binary("&", lambda x, y: x & y)
binary("|", lambda x, y: x | y)
binary("^", lambda x, y: x ^ y)
binary("<<", lambda x, y: x << y, rhs=lambda y: y % 32)
binary(">>", lambda x, y: x >> y, rhs=lambda y: y % 32)

# comparisons, including lessThan and friends, and min, max, clamp, mix
cmp = effect("void main(){ uint a = uint(ia); uint b = uint(ib); uvec2 x = uvec2(a); uvec2 y = uvec2(b); bool lt = iw == 1;"
             " bool ok = (a < b) == ((iw & 1) != 0) && (a <= b) == ((iw & 2) != 0) && (a > b) == ((iw & 4) != 0) && (a >= b) == ((iw & 8) != 0) && (a == b) == ((iw & 16) != 0);"
             " ok = ok && lessThan(x, y).y == (a < b) && lessThanEqual(x, y).y == (a <= b) && greaterThan(x, y).y == (a > b) && greaterThanEqual(x, y).y == (a >= b);"
             " ok = ok && min(a, b) == (a < b ? a : b) && max(a, b) == (a > b ? a : b) && min(x, b).y == (a < b ? a : b) && max(x, y).y == (a > b ? a : b);"
             " ok = ok && clamp(a, 5u, 3000000000u) == (a < 5u ? 5u : a > 3000000000u ? 3000000000u : a);"
             " " + FLAG + " }")
bad = count = 0
for x in values(60):
    for y in values(30):
        count += 1
        w = (x < y) | (x <= y) << 1 | (x > y) << 2 | (x >= y) << 3 | (x == y) << 4
        got = run(cmp, ia=s32(x), ib=s32(y), iw=w)
        if got[0] != 1:
            bad += 1
            if bad <= 3:
                print(f"  compare {x} {y}: {got}")
report("comparisons and min/max/clamp/mix", bad, count)

# conversions
conv = effect("void main(){ uint a = uint(ia); uvec3 v = uvec3(a); float f = float(a); vec3 g = vec3(v); vec2 h = vec2(a, 1.0); int i = int(a);"
              " uvec3 back = uvec3(g); bool ok = f == float(iw) * 65536.0 + float(ib) && g.z == f && h.x == f && h.y == 1.0 && i == ia && uint(i) == a; " + FLAG + " }")
bad = count = 0
for x in values(300):
    # float(a) in single precision through the two parts the shader recombines
    count += 1
    f = float(np.float32(x))
    got = run(conv, ia=s32(x), iw=int(f) >> 16, ib=int(f) & 0xFFFF)
    if got[0] != 1:
        # float(u) may differ from the correctly rounded value by one unit through double rounding
        bad += 1
        if bad <= 3:
            print(f"  float({x}): {got}")
report("float(uint), vec3(uvec3), int(uint)", bad, count)

f2u = effect("uniform float fa; void main(){ uint w = uint(iw); bool ok = uint(fa) == w && uvec2(fa).y == w && uvec2(vec2(fa)).x == w && uint(vec2(fa)) == w; " + FLAG + " }")
bad = count = 0
for x in [0.0, 0.5, 1.9, 255.5, 2.0**24, 2.0**31 - 128, 2.0**31, 2.0**31 + 256, 3e9, 4e9, 2.0**32 - 256]:
    count += 1
    if run(f2u, fa=x, iw=s32(int(x)))[0] != 1:
        bad += 1
        print(f"  uint({x}) wrong")
report("uint(float)", bad, count)

# compile-time values and declarations
one = lambda name, body, want=True, header=H: (name, body, want, header)
CASES = [
    one("shift count uses its low 5 bits", "void main(){ uint a = 0x80000000u + uint(ia); int c = 33 + ia; int d = -1 + ia; int s = -8 + ia; bool ok = (a >> c) == 0x40000000u && (a >> d) == 1u && (1u << c) == 2u && (1u << d) == 0x80000000u && (s >> c) == -4 && (1 << d) == (-2147483647 - 1); " + FLAG + " }"),
    one("uint(float) clamps", "void main(){ float f = -1.5 + float(ia); float g = 5e9 + float(ia); bool ok = uint(f) == 0u && uvec2(f).y == 0u && uint(g) == 0xFFFFFFFFu; " + FLAG + " }"),
    one("mix", "void main(){ uint a = 5u + uint(ia); uvec2 v = mix(uvec2(a), uvec2(7u), bvec2(false, true)); bool ok = mix(a, 7u, true) == 7u && mix(a, 7u, false) == 5u && v == uvec2(5u, 7u); " + FLAG + " }", True, H.replace("300 es", "310 es")),
    one("literal above 2^31", "void main(){ uint u = 0xFFFFFFFFu; bool ok = u == 4294967295u && u > 2147483648u && ia != 7 || ia == 7; " + FLAG + " }"),
    one("const folding", "const uint A = 0x80000000u; const uint B = A >> 31u; const uint C = A / 2u; const uint D = uint(3000000000.0); const float E = float(A);"
                         " void main(){ bool ok = B == 1u && C == 0x40000000u && D == 3000000000u && E == 2147483648.0; " + FLAG + " }"),
    one("const in array size", "const uint N = 3u; void main(){ float a[N]; a[0u] = 1.; bool ok = a[0] == 1.; " + FLAG + " }"),
    one("struct and array", "struct S { uint x; uvec2 y; }; void main(){ S s = S(4000000000u, uvec2(1u, 2u)); uint a[2] = uint[2](5u, 6u); uint t = s.x / 2u + a[1];"
                            " bool ok = t == 2000000006u && s.y.y == 2u; " + FLAG + " }"),
    one("overloads", "float f(int x){ return 1.; } float f(uint x){ return 2.; } float f(float x){ return 3.; }"
                     " void main(){ bool ok = f(1) == 1. && f(1u) == 2. && f(1.) == 3.; " + FLAG + " }"),
    one("overload order", "float f(uint x){ return 2.; } float f(int x){ return 1.; }"
                          " void main(){ bool ok = f(1u) == 2. && f(1) == 1.; " + FLAG + " }"),
    one("out parameter", "void g(out uint x, inout uint y){ x = 4000000000u; y >>= 1u; } void main(){ uint a; uint b = 0x80000000u; g(a, b);"
                         " bool ok = a / 2u == 2000000000u && b == 0x40000000u; " + FLAG + " }"),
    one("++ and --", "void main(){ uint a = 0xFFFFFFFFu; a++; uint b = 0u; b--; bool ok = a == 0u && b == 0xFFFFFFFFu && -1u == b && ~0u == b; " + FLAG + " }"),
    one("switch", "void main(){ uint a = uint(ia); bool ok = false; switch(a){ case 0xFFFFFFFFu: break; case 0u: ok = true; break; default: break; } " + FLAG + " }"),
    one("index", "void main(){ float a[3] = float[3](1., 2., 3.); uint k = 2u; float s = 0.; for(uint i = 0u; i < 3u; i++) s += a[i]; bool ok = a[k] == 3. && s == 6.; " + FLAG + " }"),
    one("index arithmetic", "void main(){ float a[4] = float[4](1., 2., 3., 4.); float s = 0.; for(uint i = 0u; i < 8u; i++) s += a[i % 4u] + a[i / 2u]; bool ok = s == 40.; " + FLAG + " }"),
    one("texture-free hash (PCG)", "uint pcg(uint v){ uint s = v * 747796405u + 2891336453u; uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u; return (w >> 22u) ^ w; }"
                                   " void main(){ bool ok = pcg(uint(ia)) == 129708002u; " + FLAG + " }"),
    one("for up", "void main(){ int n = 0; for(uint i = 0u; i < 5u; i++) n++; bool ok = n == 5; " + FLAG + " }"),
    one("for <=", "void main(){ int n = 0; for(uint i = 1u; i <= 5u; i += 2u) n++; bool ok = n == 3; " + FLAG + " }"),
    one("for down", "void main(){ int n = 0; for(uint i = 5u; i > 0u; i--) n++; bool ok = n == 5; " + FLAG + " }"),
    one("for above 2^31", "void main(){ int n = 0; for(uint i = 0xFFFFFFF0u; i < 0xFFFFFFF5u; i++) n++; bool ok = n == 5; " + FLAG + " }"),
    one("for across 2^31", "void main(){ int n = 0; for(uint i = 0x7FFFFFFEu; i < 0x80000002u; i++) n++; bool ok = n == 4; " + FLAG + " }"),
    one("for with uint local bound", "void main(){ uint m = uint(ia) & 3u; m = m + 2u; int n = 0; for(uint i = 0u; i < min(m, 6u); i++) n++; bool ok = n >= 2 && n <= 5; " + FLAG + " }"),
    one("while", "void main(){ uint i = 0u; int n = 0; while(i < 4u){ i++; n++; } bool ok = n == 4; " + FLAG + " }"),
    one("do while", "void main(){ uint i = 3u; int n = 0; do { i--; n++; } while(i > 0u); bool ok = n == 3; " + FLAG + " }"),
    one("uniform uint with @minmax", "uniform uint n; // @minmax{0, 8} @init{3}\nvoid main(){ int k = 0; for(uint i = 0u; i < n; i++) k++; bool ok = k == 3; " + FLAG + " }"),
    one("uvec equal", "void main(){ uvec2 a = uvec2(1u, 0xFFFFFFFFu); bool ok = a == uvec2(1u, 0xFFFFFFFFu) && all(equal(a, a)) && !any(notEqual(a, a)); " + FLAG + " }"),
    one("bool conversions", "void main(){ uint a = uint(true); bool ok = a == 1u && bool(0xFFFFFFFFu) && !bool(0u) && float(uint(true)) == 1.; " + FLAG + " }"),
    one("mat from uint", "void main(){ uint a = 3000000000u + uint(ia); mat2 m = mat2(a); bool ok = m[0][0] == 3000000000.0 && m[0][1] == 0.0; " + FLAG + " }"),
    one("uvec from mat", "void main(){ mat2 m = mat2(3000000000.0 + float(ia)); uvec2 v = uvec2(m); bool ok = v.x == 3000000000u && v.y == 0u; " + FLAG + " }"),
]
bad = 0
for name, body, want, header in CASES:
    try:
        eff = effect(body, header)
        b = skia.RuntimeShaderBuilder(eff)
        uniforms = {"ia": 0, "ib": 0, "iw": 0}
        names = [u.name for u in eff.uniforms()] if hasattr(eff, "uniforms") else []
        for k, v in uniforms.items():
            if not names or k in names:
                try:
                    b.setUniform(k, v)
                except Exception:
                    pass
        if "uniform uint n" in body:
            b.setUniform("n", 3)
        info = skia.ImageInfo.Make(1, 1, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
        s = skia.Surface.MakeRaster(info)
        p = skia.Paint(); p.setShader(b.makeShader()); p.setBlendMode(skia.BlendMode.kSrc)
        s.getCanvas().drawPaint(p)
        px = np.zeros((1, 1, 4), np.float32); s.readPixels(info, px)
        ok = px[0, 0, 0] == 1
    except RuntimeError as e:
        ok = False
        print(f"  {name}: {e}")
    if not ok:
        bad += 1
        print(f"FAIL {name}")
report("declarations, constants and loops", bad, len(CASES))

print("failures:", failures)
sys.exit(1 if failures else 0)
