"""Renders transpiled shaders with Skia's CPU backend (skia-python, see requirements-validation.txt) and checks
that software derivatives equal GPU fine derivatives: differences over 2x2 quads of the values Skia renders
for the argument. Every comparison must be exact."""
import argparse, subprocess, sys
import numpy as np
import skia

parser = argparse.ArgumentParser(description="Check software derivatives against GPU fine-derivative semantics.")
parser.add_argument("--transpiler", required=True, help="path to the glsl-to-sksl executable")
EXE = parser.parse_args().transpiler
W, H = 16, 12
f32 = np.float32

def render(glsl, uniforms=None):
    t = subprocess.run([EXE, "-"], input=glsl, capture_output=True, text=True)
    if t.returncode != 0:
        raise RuntimeError("transpile failed:\n" + t.stdout + t.stderr)
    effect = skia.RuntimeEffect.MakeForShader(t.stdout)
    if effect is None:
        raise RuntimeError("Skia rejected:\n" + t.stdout)
    builder = skia.RuntimeShaderBuilder(effect)
    for name, value in (uniforms or {}).items():
        builder.setUniform(name, value)
    info = skia.ImageInfo.Make(W, H, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
    surface = skia.Surface.MakeRaster(info)
    paint = skia.Paint(); paint.setShader(builder.makeShader()); paint.setBlendMode(skia.BlendMode.kSrc)
    surface.getCanvas().drawPaint(paint)
    pixels = np.zeros((H, W, 4), dtype=np.float32)
    surface.readPixels(info, pixels)   # F32 surfaces are not clamped
    return pixels

def ddx(v):
    out = np.empty_like(v)
    for x in range(0, W, 2):
        out[:, x] = out[:, x + 1] = v[:, x + 1] - v[:, x]
    return out
def ddy(v):
    out = np.empty_like(v)
    for y in range(0, H, 2):
        out[y, :] = out[y + 1, :] = v[y + 1, :] - v[y, :]
    return out
def fwidth(v): return np.abs(ddx(v)) + np.abs(ddy(v))

failures = 0
def compare(name, got, want):
    global failures
    err = float(np.max(np.abs(got - want)))
    if err != 0: failures += 1
    print(f"{'ok  ' if err == 0 else 'FAIL'} {name}: max error {err:.3g}, values up to {float(np.max(np.abs(want))):.3g}")

HEAD = "#version 300 es\nprecision highp float;\nout vec4 o;\n"
F = "float f(vec2 p) { return sin(p.x * 0.37) * p.y * 0.05 + p.x * p.x * 0.002 - cos(p.y * 0.21); }\n"
V = render(HEAD + F + "void main() { o = vec4(f(gl_FragCoord.xy), gl_FragCoord.xy, 1.0); }")
fx, fy = V[..., 1], V[..., 2]
V = V[..., 0]
print("gl_FragCoord.xy at (0,0) and (15,11):", fx[0, 0], fy[0, 0], fx[-1, -1], fy[-1, -1])

p = render(HEAD + F + "void main() { float v = f(gl_FragCoord.xy); o = vec4(dFdx(v), dFdy(v), fwidth(v), 1.0); }")
compare("dFdx", p[..., 0], ddx(V)); compare("dFdy", p[..., 1], ddy(V)); compare("fwidth", p[..., 2], fwidth(V))

p = render(HEAD + F + "void main() { float v = f(gl_FragCoord.xy); o = vec4(dFdx(dFdy(v)), dFdx(dFdx(v)), dFdy(dFdx(v)), fwidth(fwidth(v))); }")
compare("dFdx(dFdy(v))", p[..., 0], ddx(ddy(V))); compare("dFdx(dFdx(v))", p[..., 1], ddx(ddx(V)))
compare("dFdy(dFdx(v))", p[..., 2], ddy(ddx(V))); compare("fwidth(fwidth(v))", p[..., 3], fwidth(fwidth(V)))

p = render(HEAD + F + """
float cover(float d) { float w = fwidth(d); if (d > w * 3.0) return -1.0; return w; }
void main() { float v = f(gl_FragCoord.xy); vec2 g = dFdy(gl_FragCoord.xy * vec2(2.0, 3.0)); o = vec4(cover(v), g, 1.0); }""")
w = fwidth(V)
compare("helper with an early return after fwidth", p[..., 0], np.where(V > w * f32(3.0), f32(-1.0), w))
compare("dFdy(vec2).x", p[..., 1], ddy(fx * f32(2.0))); compare("dFdy(vec2).y", p[..., 2], ddy(fy * f32(3.0)))

p = render(HEAD + F + "void main() { float v = f(gl_FragCoord.xy); float r = 7.0; if (v > 0.0) { r = dFdx(v); } o = vec4(r, 0.0, 0.0, 1.0); }")
compare("dFdx inside an if that depends on the pixel", p[..., 0], np.where(V > 0, ddx(V), f32(7.0)))

p = render(HEAD + F + """uniform float k;
void main() { float v = f(gl_FragCoord.xy); float acc = 0.0;
  for (int i = 0; i < 3; i++) { float s = v * float(i + 1); acc += dFdx(s); if (k > 0.5) acc += 1.0; }
  o = vec4(acc, 0.0, 0.0, 1.0); }""", {"k": 1.0})
acc = np.zeros_like(V)
for i in range(3):
    acc = acc + ddx(V * f32(i + 1))
    acc = acc + f32(1.0)
compare("derivatives in a loop with a uniform condition", p[..., 0], acc)

p = render("#extension GL_OES_standard_derivatives : enable\nprecision highp float;\n" + F +
           "void main() { float v = f(gl_FragCoord.xy); gl_FragColor = vec4(dFdy(v), 0.0, 0.0, 1.0); }")
compare("GLSL ES 1.00 extension", p[..., 0], ddy(V))

p = render(HEAD + F + """float g;
void setG(vec2 p) { g = f(p); }
void twice(inout float x) { x *= 2.0; }
void main() { setG(gl_FragCoord.xy); float v = g; twice(v); o = vec4(dFdx(v), dFdy(g), 0.0, 1.0); }""")
compare("global written by a helper, inout parameter", p[..., 0], ddx(V * f32(2.0)))
compare("derivative of a global", p[..., 1], ddy(V))

p = render(HEAD + F + """
vec2 grad(float v) { return vec2(dFdx(v), dFdy(v)); }
float edge(float v) { vec2 g = grad(v); return v / max(length(g), 1e-4); }
void main() { float v = f(gl_FragCoord.xy); o = vec4(edge(v), grad(v), 1.0); }""")
gx, gy = ddx(V), ddy(V)
compare("quad function calling a quad function", p[..., 0], V / np.maximum(np.sqrt(gx * gx + gy * gy), f32(1e-4)))
compare("vec2 result of a quad function", p[..., 1], gx)

# Loop whose exit depends on the pixel, before the derivative: each lane runs its own copy.
LOOP = "float t = 0.0; for (int i = 0; i < 40; i++) { if (t > v * 3.0 + 2.0) break; t += 0.37; }"
T = render(HEAD + F + "void main() { float v = f(gl_FragCoord.xy); " + LOOP + " o = vec4(t, 0.0, 0.0, 1.0); }")[..., 0]
p = render(HEAD + F + "void main() { float v = f(gl_FragCoord.xy); " + LOOP + " o = vec4(fwidth(t), dFdy(t), 0.0, 1.0); }")
compare("loop with a pixel-dependent break before fwidth", p[..., 0], fwidth(T))
compare("same, dFdy", p[..., 1], ddy(T))

# Uniform early return before the derivative, both ways.
for kv in (0.0, 1.0):
    p = render(HEAD + F + "uniform float k;\nvoid main() { float v = f(gl_FragCoord.xy); if (k > 0.5) { o = vec4(5.0); return; } o = vec4(dFdx(v), 0.0, 0.0, 1.0); }", {"k": kv})
    compare(f"uniform early return, k={kv}", p[..., 0], np.full_like(V, 5.0) if kv > 0.5 else ddx(V))

# Function with an out parameter that computes a derivative, called as the statement's value.
p = render(HEAD + F + """float g(vec2 q, out float x) { x = f(q) * 2.0; return dFdy(x); }
void main() { float a; float b = g(gl_FragCoord.xy, a); o = vec4(a, b, dFdx(a), 1.0); }""")
compare("out parameter of a quad function", p[..., 0], V * f32(2.0))
compare("its derivative result", p[..., 1], ddy(V * f32(2.0)))
compare("derivative of the out value", p[..., 2], ddx(V * f32(2.0)))

print("failures:", failures)
sys.exit(1 if failures else 0)
