"""Checks the generated integer remainder and bit operations against 32-bit two's complement results with Skia's CPU backend. Run: python tests/check_integer_ops.py --transpiler build/glsl-to-sksl.exe"""
import subprocess, sys, random
import numpy as np
import skia

import argparse
parser = argparse.ArgumentParser(description="Check integer remainder, bit and shift lowering against 32-bit results.")
parser.add_argument("--transpiler", required=True, help="path to the glsl-to-sksl executable")
args = parser.parse_args()
EXE = args.transpiler
H = "#version 300 es\nprecision highp float;\nuniform int a;\nuniform int b;\nuniform int w;\nout vec4 color;\n"

def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v >= 1 << 31 else v

def trunc_mod(x, y):
    q = abs(x) // abs(y)
    q = q if (x >= 0) == (y >= 0) else -q
    return x - y * q

OPS = {
    "&": lambda x, y: s32(x & y), "|": lambda x, y: s32(x | y), "^": lambda x, y: s32(x ^ y),
    "<<": lambda x, y: s32(x << y), ">>": lambda x, y: x >> y, "%": trunc_mod,
}

def effect(body):
    r = subprocess.run([EXE, "-"], input=H + body, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr)
    return skia.RuntimeEffect.MakeForShader(r.stdout)

def run(eff, **u):
    b = skia.RuntimeShaderBuilder(eff)
    for k, v in u.items():
        b.setUniform(k, int(v))
    info = skia.ImageInfo.Make(1, 1, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
    s = skia.Surface.MakeRaster(info)
    p = skia.Paint(); p.setShader(b.makeShader()); p.setBlendMode(skia.BlendMode.kSrc)
    s.getCanvas().drawPaint(p)
    px = np.zeros((1, 1, 4), np.float32); s.readPixels(info, px)
    return px[0, 0]

rng = random.Random(3)
edge = [0, 1, -1, 2, -2, 7, -7, 255, 2**30, -2**30, 2**31 - 1, -2**31, 12345678, -87654321]
def values(n):
    return edge + [rng.randint(-2**31, 2**31 - 1) for _ in range(n)]

failures = 0
for op, f in OPS.items():
    scalar = effect(f"void main(){{ ivec3 va = ivec3(a, 1, a); ivec3 vb = ivec3(b, 1, b); int s = a {op} b; int c = a; c {op}= b; ivec3 v = va {op} vb; ivec3 m = va {op} b;"
                    f" color = vec4(s == w ? 1. : 0., c == w ? 1. : 0., v.x == w && v.z == w ? 1. : 0., m.z == w ? 1. : 0.); }}")
    bad = 0; n = 0
    for x in values(60):
        for y in values(20):
            if op in ("<<", ">>"):
                y = y % 32
            if op == "%":
                if y == 0 or x < 0 or y < 0:   # GLSL leaves negative operands undefined
                    continue
            want = f(x, y)
            got = run(scalar, a=x, b=y, w=want)
            n += 1
            if not np.all(got == 1):
                bad += 1
                if bad <= 3:
                    print(f"  {x} {op} {y}: want {want}, flags {got}")
    failures += bad
    print(f"{'ok  ' if not bad else 'FAIL'} {op}: {n - bad}/{n}")

inv = effect("void main(){ ivec2 v = ~ivec2(a, b); color = vec4(~a == w ? 1. : 0., v.x == w ? 1. : 0., 1., 1.); }")
bad = 0
for x in values(100):
    if not np.all(run(inv, a=x, b=0, w=s32(~x)) == 1):
        bad += 1
failures += bad
print(f"{'ok  ' if not bad else 'FAIL'} ~: {len(values(0)) + 100 - bad}")
print("failures:", failures)
sys.exit(1 if failures else 0)
