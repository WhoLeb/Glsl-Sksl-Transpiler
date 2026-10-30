"""Renders transpiled bounded-loop shaders with Skia's CPU backend and compares pixel (0,0) with the result GLSL defines. Run: python tests/check_loops.py --transpiler build/glsl-to-sksl.exe"""
import subprocess, sys
import numpy as np
import skia

import argparse
parser = argparse.ArgumentParser(description="Check bounded while/do-while/for lowering against GLSL results.")
parser.add_argument("--transpiler", required=True, help="path to the glsl-to-sksl executable")
parser.add_argument("-v", action="store_true", help="print the SkSL of failing cases")
args = parser.parse_args()
EXE = args.transpiler
VERBOSE = args.v
H = '#version 300 es\nprecision highp float;\nout vec4 color;\n'

def render(src, uniforms):
    t = subprocess.run([EXE, '-'], input=src, capture_output=True, text=True)
    if t.returncode != 0:
        return None, t.stderr.strip().split('\n')[0], ''
    try:
        effect = skia.RuntimeEffect.MakeForShader(t.stdout)
    except RuntimeError as e:
        return None, 'SKIA: ' + str(e).strip().split('\n')[0], t.stdout
    builder = skia.RuntimeShaderBuilder(effect)
    for name, value in uniforms.items():
        builder.setUniform(name, value)
    info = skia.ImageInfo.Make(1, 1, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
    surface = skia.Surface.MakeRaster(info)
    paint = skia.Paint(); paint.setShader(builder.makeShader()); paint.setBlendMode(skia.BlendMode.kSrc)
    surface.getCanvas().drawPaint(paint)
    px = np.zeros((1, 1, 4), dtype=np.float32)
    surface.readPixels(info, px)
    return px[0, 0], None, t.stdout

# name, extra declarations (after the header), main body, uniforms, expected rgba
cases = [
 ('while_counter', '', 'void main(){int i=0;float s=0.;while(i<10){s+=.1;i++;}color=vec4(s,0.,0.,1.);}', {}, (1.0,0,0,1)),
 ('while_increment_in_condition', 'uniform int n; // @minmax{0, 20}\n', 'void main(){int i=0;float s=0.;while(i++<n){if(s>5.)continue;s+=.1;}color=vec4(s,float(i)/10.,0.,1.);}', {'n': 5}, (0.5,0.6,0,1)),
 ('while_maxiter', 'uniform float eps;\n', 'void main(){float d=1.;while(d>eps){ // @maxiter{64}\nd*=.5;}color=vec4(d,0.,0.,1.);}', {'eps': 0.01}, (0.0078125,0,0,1)),
 ('maxiter_stops', '', 'void main(){float s=0.;while(true){ // @maxiter{4}\ns+=1.;}color=vec4(s/10.,0.,0.,1.);}', {}, (0.4,0,0,1)),
 ('do_while_continue', '', 'void main(){int k=0;float s=0.;do{s+=1.;k+=2;if(k==4)continue;s+=.5;}while(k<9);color=vec4(s/10.,float(k)/10.,0.,1.);}', {}, (0.7,1.0,0,1)),
 ('do_while_false', '', 'void main(){int k=0;do{k++;}while(false);color=vec4(float(k),0.,0.,1.);}', {}, (1,0,0,1)),
 ('for_init_outside', '', 'void main(){int i=0;for(;i<3;++i){}color=vec4(float(i)/3.,0.,0.,1.);}', {}, (1,0,0,1)),
 ('for_no_increment', '', 'void main(){float a=0.;for(int i=0;i<4;){a+=.1;++i;}color=vec4(a,0.,0.,1.);}', {}, (0.4,0,0,1)),
 ('for_step_and_continue', '', 'void main(){float a=0.;int i=0;for(i=0;i<10;i+=3){if(i==3)continue;a+=float(i);}color=vec4(a/100.,float(i)/100.,0.,1.);}', {}, (0.15,0.12,0,1)),
 ('for_annotated_uniform', 'uniform int m;\n', 'void main(){float a=0.;for(int i=0;i<m;i++){ // @maxiter{32}\na+=.1;}color=vec4(a,0.,0.,1.);}', {'m': 7}, (0.7,0,0,1)),
]

bad = 0
for name, decl, body, uniforms, want in cases:
    px, err, sksl = render(H + decl + body, uniforms)
    if px is None:
        ok = False
        detail = err
    else:
        ok = np.allclose(px, want, atol=1e-5, rtol=0)
        detail = f'got {np.round(px, 6).tolist()} want {list(want)}'
    if not ok:
        bad += 1
    print(('ok   ' if ok else 'FAIL ') + name + ('' if ok else ' | ' + str(detail)))
    if VERBOSE and not ok and sksl:
        print(sksl)
print(f'failures: {bad}')
sys.exit(1 if bad else 0)
