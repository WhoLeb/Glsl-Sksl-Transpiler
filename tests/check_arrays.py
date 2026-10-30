"""Renders transpiled shaders that use arrays with Skia's CPU backend (skia-python, see requirements-validation.txt) and compares pixel (0,0) with the result GLSL defines, including evaluation order. Run: python tests/check_arrays.py --transpiler build/glsl-to-sksl.exe"""
import subprocess, sys
import numpy as np
import skia

import argparse
parser = argparse.ArgumentParser(description="Check array lowering against GLSL results.")
parser.add_argument("--transpiler", required=True, help="path to the glsl-to-sksl executable")
parser.add_argument("-v", action="store_true", help="print the SkSL of failing cases")
args = parser.parse_args()
EXE = args.transpiler
VERBOSE = args.v
H = '#version 320 es\nprecision highp float;\nout vec4 color;\n'
H1 = '#version 100\nprecision highp float;\n'

def render(src):
    t = subprocess.run([EXE, '-'], input=src, capture_output=True, text=True)
    if t.returncode != 0:
        return None, t.stderr.strip().split('\n')[0], ''
    try:
        effect = skia.RuntimeEffect.MakeForShader(t.stdout)
    except RuntimeError as e:
        return None, 'SKIA: ' + str(e).strip().split('\n')[0], t.stdout
    info = skia.ImageInfo.Make(2, 2, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
    surface = skia.Surface.MakeRaster(info)
    paint = skia.Paint(); paint.setShader(skia.RuntimeShaderBuilder(effect).makeShader()); paint.setBlendMode(skia.BlendMode.kSrc)
    surface.getCanvas().drawPaint(paint)
    px = np.zeros((2, 2, 4), dtype=np.float32)
    surface.readPixels(info, px)
    return px[0, 0], None, t.stdout

# name, source, expected rgba (None = must be refused with the given code)
P = 'float n=0.; float h(){n+=.25;return n;} '
cases = [
 ('swap_via_ctor', H+'void main(){float a[2]=float[2](1.,2.);a=float[2](a[1],a[0]);color=vec4(a[0],a[1],0.,2.)/2.;}', (1,.5,0,1)),
 ('ctor_arg_then_read', H+P+'float f(float a[2],float b){return a[0]+b;} void main(){color=vec4(f(float[2](h(),1.),n));}', (.5,.5,.5,.5)),
 ('read_before_ctor_side_effect', H+P+'void main(){float x=n+float[2](h(),0.)[0];color=vec4(x,n,0.,1.);}', (.25,.25,0,1)),
 ('array_return', H+'float[2] mk(float x){return float[2](x,x*2.);} void main(){color=vec4(mk(.25)[1],mk(.125)[0],0.,1.);}', (.5,.125,0,1)),
 ('array_return_decl', H+'float[2] mk(float x){return float[2](x,x*2.);} void main(){float a[2]=mk(.25);color=vec4(a[0],a[1],0.,1.);}', (.25,.5,0,1)),
 ('array_return_overlap', H+'float[2] rev(float a[2]){return float[2](a[1],a[0]);} void main(){float a[2]=float[2](.25,.75);a=rev(a);color=vec4(a[0],a[1],0.,1.);}', (.75,.25,0,1)),
 ('array_return_side_effects', H+P+'float[2] mk(){float v=h();return float[2](v,v+h());} void main(){float a[2]=mk();color=vec4(a[0],a[1],n,1.);}', (.25,.75,.5,1)),
 ('struct_array_return', H+'struct S{float a[2];}; S mk(){S s;s.a[0]=.25;s.a[1]=.75;return s;} void main(){color=vec4(mk().a[1],0.,0.,1.);}', (.75,0,0,1)),
 ('ternary_arrays', H+'void main(){float a[2]=float[2](.25,.5);float b[2]=float[2](.75,1.);float c[2]=gl_FragCoord.x>0.?a:b;float d[2];d=gl_FragCoord.x<0.?a:b;color=vec4(c[0],c[1],d[0],d[1]);}', (.25,.5,.75,1)),
 ('ternary_array_branch_effects', H+P+'void main(){float c[2]=gl_FragCoord.x>0.?float[2](h(),0.):float[2](h(),h());color=vec4(c[0],n,0.,1.);}', (.25,.25,0,1)),
 ('ternary_scalar_branch_effects', H+P+'void main(){float x=gl_FragCoord.x<0.?float[2](h(),0.)[0]:.5;color=vec4(x,n,0.,1.);}', (.5,0,0,1)),
 ('ternary_index', H+'void main(){float a[2]=float[2](.25,.5);float b[2]=float[2](.75,1.);color=vec4((gl_FragCoord.x>0.?a:b)[1]);}', (.5,.5,.5,.5)),
 ('compare_arrays', H+'void main(){float a[2]=float[2](1.,2.);float b[2]=float[2](1.,2.);float c[2]=float[2](1.,3.);color=vec4(a==b?1.:0.,a!=b?1.:0.,a==c?1.:0.,a!=c?1.:0.);}', (1,0,0,1)),
 ('compare_structs', H+'struct S{float a[2];vec2 v;}; void main(){S s=S(float[2](1.,2.),vec2(3.));S t=s;S u=S(float[2](1.,2.),vec2(4.));color=vec4(s==t?1.:0.,s==u?1.:0.,s!=u?1.:0.,1.);}', (1,0,1,1)),
 ('compare_with_ctor', H+'void main(){float a[2]=float[2](1.,2.);color=vec4(a==float[2](1.,2.)?1.:0.,float[2](0.,2.)!=a?1.:0.,0.,1.);}', (1,1,0,1)),
 ('compare_returned', H+'float[2] mk(){return float[2](1.,2.);} void main(){float a[2]=float[2](1.,2.);color=vec4(mk()==a?1.:0.);}', (1,1,1,1)),
 ('and_skips_effect', H+P+'void main(){bool r=gl_FragCoord.x<0. && float[2](h(),0.)[0]>0.;color=vec4(n,r?1.:0.,0.,1.);}', (0,0,0,1)),
 ('or_skips_effect', H+P+'void main(){bool r=gl_FragCoord.x>0. || float[2](h(),0.)[0]>0.;color=vec4(n,r?1.:0.,0.,1.);}', (0,1,0,1)),
 ('and_runs_effect', H+P+'void main(){bool r=gl_FragCoord.x>0. && float[2](h(),0.)[0]>0.;color=vec4(n,r?1.:0.,0.,1.);}', (.25,1,0,1)),
 ('nested_assign', H+'void main(){float a[2]=float[2](.25,.5);float b[2];float x=(b=a)[1];color=vec4(x,b[0],0.,1.);}', (.5,.25,0,1)),
 ('comma_array', H+'void main(){float a[2]=float[2](.25,.5);float m=0.;float x=(m=.75,a)[1];color=vec4(x,m,0.,1.);}', (.5,.75,0,1)),
 ('comma_statement', H+'void main(){float a[2];float b[2]=float[2](.25,.5);float c[2];a=b,c=b;color=vec4(a[0],c[1],0.,1.);}', (.25,.5,0,1)),
 ('global_const_array', H+'const float W[3]=float[3](.2,.3,.5); const float X=W[1]; void main(){float s=0.;for(int i=0;i<3;++i)s+=W[i];color=vec4(s,X,0.,1.);}', (1,.3,0,1)),
 ('struct_array_ctor', H+'struct S{float a[2];float b;}; void main(){S s=S(float[2](.25,.5),.75);S t;t=s;color=vec4(t.a[0],t.a[1],t.b,1.);}', (.25,.5,.75,1)),
 ('array_of_structs_ctor', H+'struct L{vec3 c;float i;}; void main(){L ls[2]=L[2](L(vec3(1.),.5),L(vec3(.5),1.));color=vec4(ls[1].c*ls[0].i,1.);}', (.25,.25,.25,1)),
 ('ctor_loop_index', H+'void main(){float s=0.;for(int i=0;i<3;++i)s+=float[3](.125,.25,.5)[i];color=vec4(s);}', (.875,.875,.875,.875)),
 ('index_remainder_of_loop_index', H+'void main(){float v[3]=float[3](.1,.2,.3);float s=0.;for(int i=0;i<3;i++)s+=v[(i+1)%3]*float(i+1);color=vec4(s/2.);}', (.55,.55,.55,.55)),
 ('struct_out_unwritten', H+'struct L{float a;float b[2];};bool f(L i,out L o){return false;}void main(){L x=L(1.,float[2](2.,3.));bool b=f(x,x);color=vec4(x.a,x.b[1],b?1.:0.,1.);}', (0,0,0,1)),
 ('deep_nesting_spill_read_after', H+P+'void main(){color=vec4(' + 'max('*30 + 'h()' + ',0.)'*30 + ',n,0.,1.);}', (.25,.25,0,1)),
 ('deep_nesting_spill_read_before', H+P+'void main(){color=vec4(n,' + 'max('*30 + 'h()' + ',0.)'*30 + ',0.,1.);}', (0,.25,0,1)),
 ('long_chain_spill', H+'void main(){float x=gl_FragCoord.x*0.+.01;color=vec4(' + '+'.join(['x']*60) + ');}', (.6,.6,.6,.6)),
 ('index_single_value', H+'void main(){float a[3]=float[3](.1,.2,.3);int k=2;color=vec4(a[k]);}', (.3,.3,.3,.3)),
 ('index_local_from_loop', H+'void main(){float a[6]=float[6](.1,.2,.3,.4,.5,.6);float s=0.;for(int i=0;i<3;i++){int j=i*2+1;s+=a[j];}color=vec4(s/2.);}', (.6,.6,.6,.6)),
 ('index_local_two_levels', H+'void main(){float a[4]=float[4](.1,.2,.3,.4);float s=0.;for(int i=0;i<2;i++)for(int k=0;k<2;k++){int r=i*2;int c=r+k;s+=a[c];}color=vec4(s/2.);}', (.5,.5,.5,.5)),
 ('index_param_loop_array', H+'float f(int n){float a[4]=float[4](.1,.2,.3,.4);float s=0.;for(int i=0;i<n;i++)s+=a[i];return s;} void main(){color=vec4(f(2),f(4),0.,1.);}', (.3,1.,0,1)),
 ('return_ctor_index', H+'float f(int k){for(int i=0;i<2;++i)if(i==k)return float[2](.25,.75)[i];return 0.;} void main(){color=vec4(f(1));}', (.75,.75,.75,.75)),
 ('derivative_of_ctor', H+'void main(){color=vec4(abs(dFdx(float[2](gl_FragCoord.x*.25,1.)[0])),0.,0.,1.);}', (.25,0,0,1)),
 ('quad_fn_array_arg', H+'float g(float a[2]){return abs(dFdx(a[0]));} void main(){color=vec4(g(float[2](gl_FragCoord.x*.5,1.)),0.,0.,1.);}', (.5,0,0,1)),
 ('names_kept', H+'uniform float k; float coord=.25; float tmp1=.5; float color_L1=.125; void main(){float float2=.75; color=vec4(coord,tmp1,float2,1.)+vec4(dFdx(gl_FragCoord.x)-1.+color_L1-.125+k*0.);}', (.25,.5,.75,1)),
 ('es1_fragcolor', H1+'void main(){float fragColor=.5; gl_FragColor=vec4(fragColor);}', (.5,.5,.5,.5)),
 # refusals
 ('quad_fn_returns_array', H+'float[2] g(float x){return float[2](dFdx(x),1.);} void main(){color=vec4(g(gl_FragCoord.x)[0]);}', 'unsupported.array-return'),
 ('inout_read_before_effect', H+'float n=1.; float g(){n=5.;return 1.;} void f(inout float x, float a[2]){x+=a[0];} void main(){f(n,float[2](g(),0.));color=vec4(n);}', 'unsupported.evaluation-order'),
]
bad = 0
for name, src, want in cases:
    px, err, sksl = render(src)
    if isinstance(want, str):
        ok = px is None and err and want in err
        detail = err
    else:
        ok = px is not None and np.allclose(px, want, atol=1e-6)
        detail = err if px is None else f'got {np.round(px, 4).tolist()} want {list(want)}'
    if not ok: bad += 1
    print(('   ' if ok else '!! ') + name + ('' if ok and px is not None else ' | ' + str(detail)))
    if VERBOSE and not ok: print(sksl)
print('failures:', bad)
sys.exit(1 if bad else 0)
