"""Regenerate named fragment fixtures; manifest is consumed by run_corpus.py."""
import json
from pathlib import Path
root = Path(__file__).parent / 'corpus'
root.mkdir(exist_ok=True)
cases = []
def case(name, body, ok=True, declaration='out vec4 color;', diagnostic='Unsupported'):
    source = '#version 320 es\nprecision highp float;\n' + declaration + '\n' + body + '\n'
    (root / (name+'.frag')).write_text(source, encoding='utf-8')
    cases.append(dict(name=name, file=name+'.frag', expect='accept' if ok else 'reject', diagnostic=diagnostic if not ok else ''))
expressions = {
 'constructors':'vec4(vec2(.25), .5, 1.)', 'swizzles':'vec4(vec3(.2).zyx,1.)',
 'trigonometry':'vec4(sin(.2),cos(.3),tan(.1),1.)', 'exponential':'vec4(exp(.2),log(2.),sqrt(.5),1.)',
 'clamp_mix':'mix(vec4(0.),vec4(1.),clamp(.3,0.,1.))', 'smoothstep':'vec4(smoothstep(0.,1.,.4))',
 'geometry':'vec4(normalize(vec3(1.,2.,3.)),1.)', 'dot_cross':'vec4(cross(vec3(1.,0.,0.),vec3(0.,1.,0.)),dot(vec2(1.),vec2(.5)))',
 'relational':'vec4(any(lessThan(vec2(.2),vec2(.5))) ? 1. : 0.)', 'matrix':'vec4(mat3(1.) * vec3(.5),1.)',
 'ternary':'true ? vec4(1.) : vec4(0.)', 'mod_fract':'vec4(fract(.7),mod(3.,2.),floor(.9),ceil(.1))',
 'reflect':'vec4(reflect(vec3(0.,-1.,0.),vec3(0.,1.,0.)),1.)', 'pow':'vec4(pow(.5,2.))'}
for name, expr in expressions.items(): case(name, 'void main(){color='+expr+';}')
for name, statements in {
 'component_write':'color=vec4(0.);color.xy=vec2(.5);color.a=1.;',
 'for_loop':'float a=0.;for(int i=0;i<4;++i){a+=.1;}color=vec4(a);',
 'nested_loops':'float a=0.;for(int i=0;i<2;++i){for(int j=0;j<2;++j){a+=.1;}}color=vec4(a);',
 'branches':'if(true){color=vec4(1.);}else{color=vec4(0.);}',
 'early_return':'color=vec4(.5);if(true)return;color=vec4(1.);',
 'local_array':'float a[2];a[0]=.2;a[1]=.4;color=vec4(a[0]+a[1]);',
 'integer_math':'int a=2;a+=3;a*=2;color=vec4(float(a)/10.);',
 'comments':'/* vec4 main gl_FragColor */ color=vec4(.5); // done\n',
 'numeric_exponent':'color=vec4(1e-2,.5,1.,1.0);',
}.items(): case(name,'void main(){'+statements+'}')
case('layout_zero','void main(){color=vec4(1.);}',declaration='layout(location=0) out vec4 color;')
case('helper','float scale(float x){return x*.5;} void main(){color=vec4(scale(1.));}')
case('uniform','void main(){color=tint;}',declaration='uniform vec4 tint; out vec4 color;')
case('checker','void main(){float v=mod(floor(position.x)+floor(position.y),2.);color=vec4(vec3(v),1.);}',declaration='uniform vec2 position;out vec4 color;')
case('sampler','void main(){color=texture(image,vec2(.5));}',declaration='uniform sampler2D image;out vec4 color;')
case('varying','void main(){color=vec4(uv,0.,1.);}',declaration='in vec2 uv;out vec4 color;')
case('uv_coords_input','void main(){out_color=vec4(uv_coords.xy,0.,1.);}',declaration='layout(location = 0) in highp vec4 uv_coords;uniform vec2 iResolution;layout(location = 0) out vec4 out_color;')
case('second_input','void main(){color=vec4(uv+tint.xy,0.,1.);}',False,declaration='layout(location = 0) in vec2 uv;layout(location = 1) in vec4 tint;out vec4 color;',diagnostic='unsupported.input')
for name, decl, body in [
 ('depth','out vec4 color;','gl_FragDepth=.5;color=vec4(1.);'),
 ('location_one','layout(location=1) out vec4 color;','color=vec4(1.);'),
 ('mrt','out vec4 color;out vec4 other;','color=other=vec4(1.);'),
 ('fragcoord','out vec4 color;','color=gl_FragCoord;'),
 ('atomic','uniform atomic_uint counter;out vec4 color;','color=vec4(float(atomicCounter(counter)));'),
 ('sample_mask','out vec4 color;','gl_SampleMask[0]=1;color=vec4(1.);'),
 ('sample_id','out vec4 color;','color=vec4(float(gl_SampleID));'),
 ('image_store','layout(rgba32f,binding=0) uniform highp image2D target;out vec4 color;','imageStore(target,ivec2(0),vec4(1.));color=vec4(1.);'),
 ('storage','layout(std430,binding=0) buffer Values {float value;};out vec4 color;','color=vec4(value);'),
]: case(name,'void main(){'+body+'}',False,decl, 'location' if name=='mrt' else 'Unsupported')
case('discard','void main(){discard;}')
case('front_facing','void main(){color=vec4(gl_FrontFacing ? 1. : 0.);}')
case('missing_main','void helper(){}',False,diagnostic='main')
case('struct','void main(){Value v;v.x=.5;color=vec4(v.x);}',declaration='struct Value {float x;};out vec4 color;')
case('duplicate_main','void main(){color=vec4(1.);}void main(){color=vec4(0.);}',False,diagnostic='main')
case('unclosed_brace','void main(){color=vec4(1.);',False,diagnostic='Unclosed delimiter')
case('unterminated_comment','void main(){color=vec4(1.);} /*',False,diagnostic='Unterminated comment')
case('define','#define VALUE 1.\nvoid main(){color=vec4(VALUE);}')
case('output_shadow','void main(){color=vec4(1.);{float color=.5;color+=.1;}}')
case('nonsquare_matrix','void main(){mat2x3 m=mat2x3(1.);color=vec4(m*vec2(.5),1.);}',False,diagnostic='Unsupported')
case('overload_prototype','float f(float x);float f(float x){return x*.5;}vec2 f(vec2 x){return x*.25;}void main(){color=vec4(f(vec2(1.)),f(1.),1.);}')
case('parameter_qualifiers','void f(in float x,out float y,inout float z){y=x;z+=x;}void main(){float y;float z=0.;f(1.,y,z);color=vec4(y+z);}')
case('break_continue','void main(){float a=0.;for(int i=0;i<5;++i){if(i==1)continue;if(i==4)break;a+=.1;}color=vec4(a);}')
case('generated_collision','float transpiler_color;float transpiler_entry;void main(){transpiler_color=.2;transpiler_entry=.3;color=vec4(transpiler_color+transpiler_entry);}')
case('matrix_identifier','void main(){float matrix=.2;float matrixValue=.3;float matExtra=.1;color=vec4(matrix+matrixValue+matExtra);}')
case('gradient','void main(){vec2 uv=point/size;color=vec4(uv,.5,1.);}',declaration='uniform vec2 point;uniform vec2 size;out vec4 color;')
case('lighting','void main(){vec3 n=normalize(normal);float d=max(dot(n,normalize(light)),0.);color=vec4(vec3(.1)+vec3(.8)*d,1.);}',declaration='uniform vec3 normal;uniform vec3 light;out vec4 color;')
case('sdf_circle','void main(){float d=length(point-center)-radius;float a=1.-smoothstep(-1.,1.,d);color=vec4(vec3(a),a);}',declaration='uniform vec2 point;uniform vec2 center;uniform float radius;out vec4 color;')
case('color_transform','void main(){vec3 rgb=transform*tint.rgb;color=vec4(clamp(rgb,0.,1.)*tint.a,tint.a);}',declaration='uniform mat3 transform;uniform vec4 tint;out vec4 color;')
case('crlf','void main(){color=vec4(.5);}')
crlf=root/'crlf.frag';crlf.write_bytes(crlf.read_bytes().replace(b'\n',b'\r\n'))
probes = {
 'dynamic_for_uniform':'float a=0.;for(int i=0;i<count;++i){a+=.1;}color=vec4(a);',
 'dynamic_for_start':'float a=0.;for(int i=start;i<8;++i){a+=.1;}color=vec4(a);',
 'dynamic_for_step':'float a=0.;for(int i=0;i<8;i+=step){a+=.1;}color=vec4(a);',
 'dependent_nested_loop':'float a=0.;for(int i=0;i<4;++i){for(int j=0;j<i;++j){a+=.1;}}color=vec4(a);',
 'dynamic_while':'float a=0.;int i=0;while(i<count){a+=.1;++i;}color=vec4(a);',
 'dynamic_do_while':'float a=0.;int i=0;do{a+=.1;++i;}while(i<count);color=vec4(a);',
 'dynamic_array_index':'float a[3];a[0]=.1;a[1]=.2;a[2]=.3;color=vec4(a[clamp(count,0,2)]);',
 'dynamic_vector_index':'vec4 a=vec4(.1,.2,.3,.4);color=vec4(a[clamp(count,0,3)]);',
 'switch_fallthrough':'float a=0.;switch(count){case 0:a+=.1;case 1:a+=.2;break;default:a=.5;}color=vec4(a);',
}
for name, body in probes.items():
    case(name,'void main(){'+body+'}',declaration='uniform int count;uniform int start;uniform int step;out vec4 color;')
    cases[-1]['target_expect']='reject'
    if name not in ('switch_fallthrough','dependent_nested_loop'):
        cases[-1]['expect']='reject'; cases[-1]['diagnostic']='Unsupported'
case('dynamic_for_helper','float sum(int n){float a=0.;for(int i=0;i<n;++i){a+=.1;}return a;}void main(){color=vec4(sum(count));}',declaration='uniform int count;out vec4 color;')
cases[-1]['target_expect']='reject'
cases[-1]['expect']='reject'; cases[-1]['diagnostic']='Unsupported'
case('const_bound_control','void main(){const int n=4;float a=0.;for(int i=0;i<n;++i){a+=.1;}color=vec4(a);}')
case('bounded_uniform_break','void main(){float a=0.;for(int i=0;i<8;++i){if(i>=count)break;a+=.1;}color=vec4(a);}',declaration='uniform int count;out vec4 color;')
case('annotated_uniform_bound','void main(){float a=0.;for(int i=-radius;i<=radius;++i){a+=.1;}color=vec4(a);}',declaration='uniform int radius; // @minmax{0, 8} @init{2}\nout vec4 color;')
case('annotated_previous_line','void main(){float a=0.;for(float x=0.;x<r*2.;x+=.5){a+=.01;}color=vec4(a);}',declaration='// @minmax{1.0, 4.0}\nuniform float r;\nout vec4 color;')
case('clamped_bound','void main(){float a=0.;for(int i=0;i<min(count,6);++i){a+=.1;}color=vec4(a);}',declaration='uniform int count;out vec4 color;')
case('annotated_bound_too_wide','void main(){float a=0.;for(int i=0;i<count;++i){a+=.1;}color=vec4(a);}',False,declaration='uniform int count; // @minmax{0, 1000000}\nout vec4 color;',diagnostic='unsupported.loop')
cases[-1]['target_expect']='reject'
case('integer_bitwise','void main(){int a=(count & 7) ^ 2;a=(a<<1)%7;color=vec4(float(a));}',declaration='uniform int count;out vec4 color;')
import edge_cases
edge_cases.register(case, cases, root)
for item in cases:
    item.setdefault('source_valid',item['name'] not in {'missing_main','duplicate_main','unclosed_brace','unterminated_comment'})
(root/'manifest.json').write_text(json.dumps(cases,indent=2)+'\n')
print(f'Generated {len(cases)} fragment cases')
