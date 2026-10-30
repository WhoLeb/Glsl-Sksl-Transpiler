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
 ('and_index_first', '', 'void main(){float t=1.;float r=0.;for(int i=0;i<16&&t<2.;i++){r+=.1;t+=.25;}color=vec4(r,t/10.,0.,1.);}', {}, (0.4,0.2,0,1)),
 ('and_index_second', 'uniform float k;\n', 'void main(){float t=0.;for(int i=0;t<k&&i<16;i++){t+=.25;}color=vec4(t,0.,0.,1.);}', {'k': 1.0}, (1.0,0,0,1)),
 ('and_index_second_cap', 'uniform float k;\n', 'void main(){float t=0.;for(int i=0;t<k&&i<16;i++){t+=.25;}color=vec4(t,0.,0.,1.);}', {'k': 100.0}, (4.0,0,0,1)),
 ('and_side_effect_first', '', 'float g=0.;bool f(){g+=1.;return g<5.;}void main(){float s=0.;for(int i=0;f()&&i<16;i++){s+=1.;}color=vec4(s/10.,g/10.,0.,1.);}', {}, (0.4,0.5,0,1)),
 ('while_and', 'uniform float k;\n', 'void main(){int i=0;float t=0.;while(t<k&&i<10){t+=.5;i++;}color=vec4(t/10.,float(i)/10.,0.,1.);}', {'k': 2.0}, (0.2,0.4,0,1)),
 ('while_and_cap', 'uniform float k;\n', 'void main(){int i=0;float t=0.;while(t<k&&i<10){t+=.5;i++;}color=vec4(t/10.,float(i)/10.,0.,1.);}', {'k': 100.0}, (0.5,1.0,0,1)),
 ('for_over_skia_limit_break', 'uniform float k;\n', 'void main(){int n=0;for(int i=0;i<200000;i++){n++;if(float(i)>=k)break;}color=vec4(float(n)/100000.,0.,0.,1.);}', {'k': 150000.0}, (1.50001,0,0,1)),
 ('for_over_skia_limit_continue', '', 'void main(){float s=0.;for(int i=0;i<120000;i++){if(i-2*(i/2)==1)continue;s+=1.;}color=vec4(s/100000.,0.,0.,1.);}', {}, (0.6,0,0,1)),
 ('while_over_skia_limit', '', 'void main(){int i=0;float s=0.;while(i<150000){i++;s+=1.;}color=vec4(s/100000.,float(i)/100000.,0.,1.);}', {}, (1.5,1.5,0,1)),
 ('param_bound_two_calls', '', 'float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.1;return s;} void main(){color=vec4(f(3),f(5),0.,1.);}', {}, (0.3,0.5,0,1)),
 ('param_bound_through_caller', '', 'float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.1;return s;} float g(int m){return f(m+1);} void main(){color=vec4(g(2),g(4),0.,1.);}', {}, (0.3,0.5,0,1)),
 ('param_bound_from_loop_index', '', 'float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.01;return s;} void main(){float t=0.;for(int k=0;k<4;k++)t+=f(k);color=vec4(t,0.,0.,1.);}', {}, (0.06,0,0,1)),
 ('param_clamped_in_body', '', 'float f(int n){n=min(n,4);float s=0.;for(int i=0;i<n;i++)s+=.1;return s;} void main(){color=vec4(f(2),f(9),0.,1.);}', {}, (0.2,0.4,0,1)),
 ('local_bound_two_values', '', 'void main(){int n=3;if(gl_FragCoord.x>100.)n=6;float s=0.;for(int i=0;i<n;i++)s+=.1;color=vec4(s,0.,0.,1.);}', {}, (0.3,0,0,1)),
 ('float_param_bound', '', 'float f(float m){float s=0.;for(float x=0.;x<m;x+=1.)s+=.1;return s;} void main(){color=vec4(f(2.),f(4.5),0.,1.);}', {}, (0.2,0.5,0,1)),
 ('param_bound_annotated_uniform', 'uniform int u; // @minmax{0, 8}\n', 'float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.1;return s;} void main(){color=vec4(f(u),0.,0.,1.);}', {'u': 3}, (0.3,0,0,1)),
 ('switch_default_middle', 'float run(int k){float r=0.;switch(k){case 1: r=r*10.+1.; default: r=r*10.+2.; case 2: r=r*10.+3.; break; case 3: r=r*10.+4.;}return r;}\n', 'void main(){color=vec4(run(0)+run(1)/1e3,run(2)+run(3)/1e3,run(4),1.);}', {}, (23.123,3.004,23,1)),
 ('switch_default_first', 'float run(int k){float r=0.;switch(k){default: r=r*10.+2.; break; case 1: r=r*10.+1.; break; case 3: r=r*10.+4.;}return r;}\n', 'void main(){color=vec4(run(0)+run(1)/1e3,run(2)+run(3)/1e3,run(4),1.);}', {}, (2.001,2.004,2,1)),
 ('switch_default_shared_label', 'float run(int k){float r=0.;switch(k){case 1: r=r*10.+1.; break; case 4: default: r=r*10.+2.; case 2: r=r*10.+3.; break;}return r;}\n', 'void main(){color=vec4(run(0)+run(1)/1e3,run(2)+run(3)/1e3,run(4),1.);}', {}, (23.001,3.023,23,1)),
 ('switch_default_after_return', 'float run(int k){float r=0.;switch(k){case 1: return 7.; default: r=r*10.+2.; case 3: r=r*10.+4.;}return r;}\n', 'void main(){color=vec4(run(0)+run(1)/1e3,run(2)+run(3)/1e3,run(4),1.);}', {}, (24.007,24.004,24,1)),
 ('switch_default_declaration', 'float run(int k){float r=0.;switch(k){case 1: r=r*10.+1.; break; default: float t=5.; r=r*10.+t; case 2: r=r*10.+3.;}return r;}\n', 'void main(){color=vec4(run(0)+run(1)/1e3,run(2)+run(3)/1e3,run(4),1.);}', {}, (53.001,3.053,53,1)),
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
