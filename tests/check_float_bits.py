"""Checks the generated floatBitsToInt, floatBitsToUint, intBitsToFloat and uintBitsToFloat against IEEE 754 bits with Skia's CPU backend. Run: python tests/check_float_bits.py --transpiler build/glsl-to-sksl.exe"""
import argparse, math, random, struct, subprocess, sys
import numpy as np
import skia

parser = argparse.ArgumentParser(description="Check float bit casts against IEEE 754 bits.")
parser.add_argument("--transpiler", required=True, help="path to the glsl-to-sksl executable")
args = parser.parse_args()
EXE = args.transpiler
H = "#version 300 es\nprecision highp float;\nuniform float fa;\nuniform int ib;\nout vec4 color;\n"
FLAG = "color = vec4(ok ? 1. : 0., 1., 1., 1.);"


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v >= 1 << 31 else v


def to_float(bits):
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def effect(body, header=H):
    r = subprocess.run([EXE, "-"], input=header + body, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(body + "\n" + r.stderr + r.stdout)
    try:
        return skia.RuntimeEffect.MakeForShader(r.stdout)
    except RuntimeError as e:
        raise RuntimeError(f"{e}\n{r.stdout}")


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


rng = random.Random(9)
EDGE = [0x00000000, 0x80000000, 0x3F800000, 0xBF800000, 0x3F800001, 0x3FFFFFFF, 0x40000000, 0x00800000, 0x80800000, 0x00FFFFFF,
        0x7F7FFFFF, 0xFF7FFFFF, 0x4B800000, 0x4B7FFFFF, 0x2F800000, 0x5F800000, 0x1F800000, 0x7F000000, 0x01000000, 0x3DCCCCCD]
SUBNORMAL = [0x00000001, 0x80000001, 0x007FFFFF, 0x00400000, 0x00000400]
INFINITE = [0x7F800000, 0xFF800000]
finite = lambda n: [b for b in (rng.randint(0, 0xFFFFFFFF) for _ in range(n)) if (b >> 23) & 255 != 255]

failures = 0
def report(name, bad, n):
    global failures
    failures += bad
    print(f"{'ok  ' if not bad else 'FAIL'} {name}: {n - bad}/{n}")


# float to bits; vectors and the int version too
fwd = effect("void main(){ uint w = uint(ib); uvec3 v = floatBitsToUint(vec3(fa)); ivec2 i = floatBitsToInt(vec2(fa));"
             " bool ok = floatBitsToUint(fa) == w && floatBitsToInt(fa) == ib && v.z == w && i.y == ib; " + FLAG + " }")
values = EDGE + SUBNORMAL + INFINITE + finite(3000)
bad = 0
for b in values:
    if run(fwd, fa=to_float(b), ib=s32(b))[0] != 1:
        bad += 1
        if bad <= 5:
            print(f"  floatBitsToUint({to_float(b)}) != {b:#010x}")
report("floatBitsToUint, floatBitsToInt", bad, len(values))

# bits to float: the result must have exactly the expected bits, -0 and subnormals included
back = effect("void main(){ float f = uintBitsToFloat(uint(ib)); vec2 v = uintBitsToFloat(uvec2(uint(ib))); vec3 g = intBitsToFloat(ivec3(ib));"
              " uint w = uint(ib); bool ok = floatBitsToUint(f) == w && f == fa && floatBitsToUint(v.y) == w && floatBitsToUint(g.z) == w && floatBitsToUint(intBitsToFloat(ib)) == w; " + FLAG + " }")
values = EDGE + SUBNORMAL + INFINITE + finite(3000)
bad = 0
for b in values:
    if run(back, fa=to_float(b), ib=s32(b))[0] != 1:
        bad += 1
        if bad <= 5:
            print(f"  uintBitsToFloat({b:#010x}) wrong")
report("uintBitsToFloat, intBitsToFloat", bad, len(values))

# NaN: canonical in both directions
nan = effect("void main(){ float f = uintBitsToFloat(uint(ib)); bool number = false; if (f >= 0.) number = true; if (f < 0.) number = true; bool ok = !number && floatBitsToUint(f) == 0x7FC00000u; " + FLAG + " }")
values = [0x7FC00000, 0x7F800001, 0x7FFFFFFF]
bad = sum(run(nan, fa=0.0, ib=s32(b))[0] != 1 for b in values)
report("NaN", bad, len(values))

CASES = [
    ("const float to bits", "const uint A = floatBitsToUint(1.0); const int B = floatBitsToInt(-2.0); const uvec2 C = floatBitsToUint(vec2(0.5, -0.0));"
                            " void main(){ bool ok = A == 0x3F800000u && B == int(0xC0000000u) && C == uvec2(0x3F000000u, 0x80000000u); " + FLAG + " }"),
    ("const bits to float", "const float A = uintBitsToFloat(0x3FC00000u); const float B = intBitsToFloat(-1082130432); const float S = uintBitsToFloat(1u);"
                            " void main(){ bool ok = A == 1.5 && B == -1.0 && floatBitsToUint(S + float(ib)) == 1u; " + FLAG + " }"),
    ("const infinity", "const float INF = uintBitsToFloat(0x7F800000u); void main(){ bool ok = INF > 1e38 && -INF < -1e38; " + FLAG + " }"),
    ("global infinity", "float inf = uintBitsToFloat(0x7F800000u); void main(){ bool ok = inf > 1e38 && floatBitsToUint(inf) == 0x7F800000u; " + FLAG + " }"),
    ("local infinity", "void main(){ float inf = uintBitsToFloat(0x7F800000u); float n = uintBitsToFloat(0x7FC00000u); bool number = false; if (n >= 0.) number = true; bool ok = inf > 1e38 && !number; " + FLAG + " }"),
    ("local zero", "void main(){ float z = 0.0; float m = -0.0; bool ok = floatBitsToUint(z) == 0u && floatBitsToUint(m) == 0x80000000u; " + FLAG + " }"),
    ("hash of a float", "float h(uint i){ return uintBitsToFloat(i * 0x1b74a659u >> 9u | 0x3f800000u) - 1.; }"
                        " void main(){ float r = h(floatBitsToUint(1.5 + fa)); bool ok = r == to_expected; " + FLAG + " }"),
    ("nested", "void main(){ uint u = floatBitsToUint(uintBitsToFloat(633u)); bool ok = u == 633u; " + FLAG + " }"),
]
expected = to_float((((0x3FC00000 * 0x1b74a659) & 0xFFFFFFFF) >> 9) | 0x3F800000) - 1.0
bad = 0
for name, body in CASES:
    body = body.replace("to_expected", repr(float(np.float32(expected))))
    try:
        ok = run(effect(body), fa=0.0, ib=0)[0] == 1
    except RuntimeError as e:
        ok = False
        print(f"  {name}: {e}"[:1600])
    if not ok:
        bad += 1
        print(f"FAIL {name}")
report("constants and idioms", bad, len(CASES))

print("failures:", failures)
sys.exit(1 if failures else 0)
