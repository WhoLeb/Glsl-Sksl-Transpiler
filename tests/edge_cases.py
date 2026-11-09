"""Unusual / adversarial fragments. Registered from generate_corpus.py.

Conventions (the transpiler is expected to adopt them):
  ok=True                 -> must translate; emitted SkSL must compile for RuntimeEffect.MakeForShader
  ok=False, 'Unsupported' -> valid GLSL ES 3.20 the transpiler deliberately refuses
  ok=False, 'Invalid'     -> not valid GLSL ES 3.20; transpiler must diagnose, not translate
  note=...                -> why the case is nasty / which decision it forces
  target='reject'         -> output is expected to be refused by the ES2-strict Skia profile
                             (dynamic loops, %, bit ops, switch, uint ...); fixes belong in lowering
"""

DECL = 'out vec4 color;'
UNI = 'uniform vec2 position;uniform vec2 size;uniform float time;uniform int count;out vec4 color;'


def register(case, cases, root):
    def C(name, body, ok=True, decl=DECL, diag=None, valid=True, note=None, target=None):
        if diag is None:
            diag = 'Unsupported' if valid else 'Invalid'
        case(name, body, ok, decl, diag)
        e = cases[-1]
        if not valid: e['source_valid'] = False
        if note: e['note'] = note
        if target: e['target_expect'] = target

    def R(name, text, ok=True, diag=None, valid=True, note=None, target=None):
        """Verbatim source (bytes or str), for headers / lexer / directive tests."""
        if diag is None:
            diag = 'Unsupported' if valid else 'Invalid'
        data = text.encode('utf-8') if isinstance(text, str) else text
        (root / (name + '.frag')).write_bytes(data)
        e = dict(name=name, file=name + '.frag', expect='accept' if ok else 'reject',
                 diagnostic='' if ok else diag, source_valid=valid)
        if note: e['note'] = note
        if target: e['target_expect'] = target
        cases.append(e)

    # ------------------------------------------------------------------ A. output-variable lowering
    C('out_read_after_write', 'void main(){color=vec4(.5);color.rgb*=.5;color.a=1.;}',
      note='synthesized return local must be readable/modifiable')
    C('out_written_in_helper', 'void shade(){color=vec4(.25);} void main(){shade();}',
      note='out global becomes a local of main; helpers must receive it somehow')
    C('out_passed_inout', 'void tint(inout vec4 c){c.rgb*=.5;} void main(){color=vec4(1.);tint(color);}')
    C('out_partial_swizzle_writes', 'void main(){color.xy=vec2(1.,0.);color.zw=vec2(0.,1.);}')
    C('out_name_is_coord', 'void main(){coord=vec4(1.);}', decl='out vec4 coord;',
      note='collides with the wrapper parameter name float2 coord')
    C('out_name_is_half4', 'void main(){half4=vec4(1.);}', decl='out vec4 half4;')
    C('out_name_sk_prefix', 'void main(){sk_Result=vec4(1.);}', decl='out vec4 sk_Result;',
      note='sk_ identifiers are reserved in SkSL')
    C('out_highp_qualifier', 'void main(){color=vec4(1.);}', decl='out highp vec4 color;')
    C('out_qualifier_order', 'void main(){color=vec4(1.);}', decl='highp out vec4 color;')
    C('out_lowp', 'void main(){color=vec4(1.);}', decl='layout(location=0) out lowp vec4 color;',
      note='lowp output vs half4 return')
    C('out_invariant', 'void main(){color=vec4(1.);}', decl='invariant out vec4 color;')
    C('out_array', 'void main(){c[0]=vec4(1.);}', ok=False, decl='out vec4 c[1];')
    C('out_ivec4', 'void main(){c=ivec4(1);}', ok=False, decl='out ivec4 c;')
    C('out_vec3', 'void main(){c=vec3(1.);}', ok=False, decl='out vec3 c;',
      note='decision: widen to half4(c,1) or refuse')
    C('out_vec2', 'void main(){c=vec2(1.);}', ok=False, decl='out vec2 c;')
    C('out_float', 'void main(){c=1.;}', ok=False, decl='out float c;')
    C('out_struct', 'void main(){c.a=vec4(1.);}', ok=False, decl='struct S{vec4 a;}; out S c;', valid=False,
      note='output variables cannot be structs')
    C('out_missing', 'void main(){}', ok=False, decl='', diag='Unsupported',
      note='no output variable declared at all: ambiguous result')
    C('out_empty_main', 'void main(){}', note='output never written: return value must still be defined')
    C('out_conditional_only', 'void main(){if(position.x>1.)color=vec4(1.);}', decl=UNI)
    C('out_written_in_loop', 'void main(){color=vec4(0.);for(int i=0;i<3;++i){color+=vec4(.1);}}')
    C('out_return_in_nested_loop',
      'void main(){color=vec4(0.);for(int i=0;i<4;++i){for(int j=0;j<4;++j){if(i+j==3){color=vec4(1.);return;}}}color=vec4(.5);}',
      note='every `return;` must become `return color;`')
    C('out_return_in_else_chain',
      'void main(){if(position.x<1.){color=vec4(1.);return;}else if(position.x<2.){color=vec4(.5);return;}else{color=vec4(.25);}}',
      decl=UNI)
    C('out_compound_ops', 'void main(){color=vec4(1.);color+=vec4(1.);color-=vec4(.5);color*=vec4(.5);color/=vec4(2.);}')
    C('out_chained_assign', 'void main(){vec4 t;color=t=vec4(.5);}')
    C('out_used_as_scratch_arg', 'void main(){color=vec4(1.);color=vec4(length(color.xyz));}')
    C('out_name_and_local_same_in_helper', 'float f(){float color=.5;return color;} void main(){color=vec4(f());}')

    # ------------------------------------------------------------------ B. names & keywords
    C('ident_sksl_type_names', 'void main(){float float2=.1,float3=.2,half2=.3,int2=.4,bool2=.5,float2x2=.6;color=vec4(float2+float3+half2+int2+bool2+float2x2);}')
    C('ident_sksl_half_family', 'void main(){float half3x3=.1,half4=.2,short2=.3,int3=.4;color=vec4(half3x3+half4+short2+int3);}')
    C('ident_sksl_object_names', 'void main(){float shader=.1,colorFilter=.2,blender=.3;color=vec4(shader+colorFilter+blender);}',
      note='SkSL reserved effect-type names')
    C('ident_sk_prefix', 'void main(){float sk_FragCoord=.1,sk_Color=.2;color=vec4(sk_FragCoord+sk_Color);}',
      note='sk_ is reserved in SkSL but legal in GLSL; must rename')
    C('ident_function_named_float4', 'vec4 float4(float x){return vec4(x);} void main(){color=float4(.5);}')
    C('ident_user_function_saturate', 'float saturate(float x){return clamp(x,0.,1.);} void main(){color=vec4(saturate(2.));}',
      note='saturate is an SkSL builtin, not a GLSL one')
    C('ident_user_function_unpremul', 'vec4 unpremul(vec4 c){return vec4(c.rgb/max(c.a,.01),c.a);} void main(){color=unpremul(vec4(.5));}')
    C('ident_substring_types', 'void main(){float vec2s=.1,ivec2x=.2,mat3y=.3,bvec4_=.4,uvec,_vec3=.5;uvec=.6;color=vec4(vec2s+ivec2x+mat3y+bvec4_+uvec+_vec3);}')
    C('ident_type_prefix_vars', 'void main(){float vector=1.,matrix2=2.,floatValue=3.,intensity=4.,boolean=5.,vec=6.;color=vec4(vector+matrix2+floatValue+intensity+boolean+vec);}')
    C('ident_swizzle_like_names', 'void main(){float x=.1,y=.2,xy=.3,rgba=.4,st=.5;color=vec4(x+y,xy,rgba,st);}')
    C('ident_case_variants', 'void main(){float Color=.1,COLOR=.2,color2=.3,Main=.4;color=vec4(Color+COLOR+color2+Main);}')
    C('ident_underscores', 'void main(){float _=.1,_a=.2,a_=.3,a_b=.4;color=vec4(_+_a+a_+a_b);}')
    C('ident_double_underscore', 'void main(){float a__b=.1;color=vec4(a__b);}',
      note='reserved but allowed in GLSL ES 3.x; translated with a warning')
    C('ident_gl_prefix', 'void main(){float gl_mine=.1;color=vec4(gl_mine);}', ok=False, valid=False)
    C('ident_reserved_half', 'void main(){float half=.1;color=vec4(half);}', ok=False, valid=False,
      note='half is reserved in GLSL ES but a type in SkSL')
    C('ident_reserved_inline', 'void main(){float inline=.1;color=vec4(inline);}', ok=False, valid=False)
    C('ident_reserved_long', 'void main(){float long=.1;color=vec4(long);}', ok=False, valid=False)
    C('ident_very_long', 'void main(){float ' + 'n' * 4000 + '=.5;color=vec4(' + 'n' * 4000 + ');}')
    C('ident_struct_named_like_sksl_type', 'struct float2{float x;}; void main(){float2 v;v.x=.5;color=vec4(v.x);}')
    C('ident_struct_member_named_like_type', 'struct S{float float4; float half;}; void main(){}', ok=False, valid=False,
      note='member `half` reserved')
    C('ident_struct_member_swizzle_names', 'struct S{float x;float y;vec2 xy;vec3 rgb;}; void main(){S s;s.x=.1;s.y=.2;s.xy=vec2(.3);s.rgb=vec3(.4);color=vec4(s.xy,s.rgb.xy);}')
    C('ident_variable_shadows_function', 'float f(){return 1.;} void main(){float f=2.;color=vec4(f);}')
    C('ident_variable_shadows_struct', 'struct S{float x;}; void main(){S s;s.x=1.;{float S=2.;color=vec4(S+s.x);}}')
    C('ident_variable_named_main', 'float main_=1.; void main(){float main2=.5;color=vec4(main_+main2);}')
    C('ident_param_named_coord', 'float f(float coord){return coord*.5;} void main(){color=vec4(f(1.));}')
    C('ident_uniform_named_coord', 'void main(){color=vec4(coord,0.,1.);}', decl='uniform vec2 coord;out vec4 color;')
    C('ident_local_named_coord', 'void main(){vec2 coord=vec2(.5);color=vec4(coord,0.,1.);}')
    C('ident_user_global_float2_collision', 'float float2;float float3;void main(){float2=.1;float3=.2;color=vec4(float2+float3);}')
    C('ident_utf8_in_comment', '// こんにちは éè \U0001F600\nvoid main(){color=vec4(1.);}')
    C('ident_non_ascii', 'void main(){float é=.5;color=vec4(é);}', ok=False, valid=False)

    # ------------------------------------------------------------------ C. literals & types
    C('lit_float_forms', 'void main(){float a=1.,b=.5,c=1.e2,d=1E+2,e=5e-1,f=0.5e-1,g=1e1,h=00.5;color=vec4(a+b+c+d,e+f,g,h);}')
    C('lit_float_f_suffix', 'void main(){float a=1.0f,b=.5F,c=1e2f;color=vec4(a,b,c,1.);}',
      note='ES 3.x allows f/F; SkSL does not')
    C('lit_float_lf_suffix', 'void main(){float a=1.0lf;color=vec4(a);}', ok=False, valid=False)
    C('lit_int_hex', 'void main(){int a=0xFF,b=0x0,c=0XaB;color=vec4(float(a+b+c)/1000.);}')
    C('lit_int_octal', 'void main(){int a=010,b=0,c=007;color=vec4(float(a+b+c)/100.);}',
      note='010 == 8 in GLSL; SkSL has no octal so naive copy changes meaning')
    C('lit_int_bad_octal', 'void main(){int a=08;color=vec4(float(a));}', ok=False, valid=False)
    C('lit_int_overflow', 'void main(){int a=2147483648;color=vec4(float(a));}', note='GLSL ES 3.00 keeps the 32-bit pattern: the value is -2147483648')
    C('lit_int_max', 'void main(){int a=2147483647;color=vec4(float(a)/2147483647.);}')
    C('lit_int_min_expr', 'void main(){int a=-2147483647-1;color=vec4(float(a));}')
    C('lit_uint', 'void main(){uint a=1u;color=vec4(float(a));}')
    C('lit_uint_hex', 'void main(){uint a=0xFFFFFFFFu;color=vec4(float(a));}')
    C('lit_uvec', 'void main(){uvec3 v=uvec3(1u,2u,3u);color=vec4(vec3(v),1.);}')
    C('lit_uint_hash', 'uint pcg(uint v){uint s=v*747796405u+2891336453u;uint w=((s>>((s>>28u)+4u))^s)*277803737u;return (w>>22u)^w;}void main(){color=vec4(float(pcg(1u))/4294967295.);}')
    C('lit_uint_overloads', 'float f(int x){return 1.;}float f(uint x){return 2.;}void main(){color=vec4(f(1)+f(1u));}')
    C('lit_uint_compare', 'void main(){uint a=0xFFFFFFFFu;color=vec4(a>1u?1.:0.,a/2u>1u?1.:0.,float(a>>31u),1.);}')
    C('lit_uint_const', 'const uint A=0x80000000u;const uint B=A>>31u;const float E=float(A);void main(){color=vec4(float(B),E/4294967296.,0.,1.);}')
    C('lit_uint_builtins', 'void main(){uvec2 a=uvec2(1u,0xFFFFFFFFu);bvec2 l=lessThan(a,uvec2(2u));uint m=min(a.y,3u);color=vec4(l.x?1.:0.,l.y?1.:0.,float(clamp(m,1u,2u)),1.);}')
    C('cf_for_uint_index', 'void main(){float s=0.;for(uint i=0u;i<4u;i++)s+=.25;color=vec4(s);}')
    C('cf_for_uint_across_sign', 'void main(){float s=0.;for(uint i=0x7FFFFFFEu;i<0x80000002u;i++)s+=.25;color=vec4(s);}')
    C('cf_while_uint', 'void main(){uint i=0u;float s=0.;while(i<4u){i++;s+=.25;}color=vec4(s);}')
    C('lit_double', 'void main(){double d=1.0;color=vec4(float(d));}', ok=False, valid=False)
    C('lit_bool', 'void main(){bool a=true,b=false;color=vec4(a&&!b?1.:0.);}')
    C('lit_int_to_float_implicit', 'void main(){float x=1+2.0;color=vec4(x);}', ok=False, valid=False,
      note='ES has no implicit int->float; must not be silently fixed')
    C('lit_int_to_float_arg', 'void main(){color=vec4(pow(2,3));}', ok=False, valid=False)
    C('lit_float_to_int_implicit', 'void main(){int a=1.5;color=vec4(float(a));}', ok=False, valid=False)
    C('lit_float_overflow_half', 'void main(){mediump float big=70000.;color=vec4(big/70000.);}',
      note='semantic: half saturates at 65504')
    C('conv_float_to_int_trunc', 'void main(){int a=int(1.9),b=int(-1.9);color=vec4(float(a+b)+.5);}')
    C('conv_bool', 'void main(){float a=float(true),b=float(false);int c=int(true);bool d=bool(.5),e=bool(0),f=bool(0.);color=vec4(a+b,float(c),d?1.:0.,e||f?1.:0.);}')
    C('conv_vec_ctor_mix', 'void main(){vec4 a=vec4(1,2,3,4);vec3 b=vec3(vec4(1.));vec2 c=vec2(mat2(1.));ivec2 d=ivec2(vec2(1.5,2.5));bvec2 e=bvec2(vec2(0.,1.));color=vec4(a.x,b.x,c.x,float(d.x)+(e.y?1.:0.));}')
    C('conv_vec_ctor_all_forms', 'void main(){vec4 a=vec4(1.);vec4 b=vec4(vec2(1.),vec2(2.));vec4 c=vec4(vec3(1.),2.);vec4 d=vec4(1.,vec3(2.));vec4 e=vec4(1.,vec2(2.),3.);color=a+b+c+d+e;}')
    C('conv_vec_ctor_extra_args', 'void main(){vec2 a=vec2(1.,2.);color=vec4(a,3.,4.);}')
    C('conv_vec_ctor_too_many', 'void main(){vec2 a=vec2(1.,2.,3.);color=vec4(a,0.,1.);}', ok=False, valid=False)
    C('conv_vec_ctor_too_few', 'void main(){vec3 a=vec3(1.,2.);color=vec4(a,1.);}', ok=False, valid=False)
    C('precision_locals', 'void main(){lowp float a=.1;mediump float b=.2;highp float c=.3;color=vec4(a+b+c);}')
    C('precision_params_return', 'mediump float f(highp float x,lowp float y){return x+y;} void main(){color=vec4(f(.1,.2));}')
    C('precision_struct_members', 'struct S{lowp float a;mediump vec2 b;highp vec3 c;}; void main(){S s;s.a=.1;s.b=vec2(.2);s.c=vec3(.3);color=vec4(s.a+s.b.x+s.c.x);}')
    C('precision_mixed_arith', 'void main(){mediump float a=.5;highp float b=2.;lowp float c=.25;color=vec4(a*b+c);}',
      note='half*float promotion rules')
    C('precision_mixed_vec_ctor', 'void main(){mediump vec2 a=vec2(.5);highp vec2 b=vec2(.25);color=vec4(a,b);}')
    C('precision_int', 'void main(){lowp int a=1;mediump int b=2;highp int c=3;color=vec4(float(a+b+c)/6.);}')
    C('precision_statement_int', 'precision highp int;\nvoid main(){int a=1;color=vec4(float(a));}')
    C('bool_vectors', 'void main(){bvec3 b=bvec3(true,false,true);color=vec4(any(b)?1.:0.,all(b)?1.:0.,b.x==b.y?1.:0.,not(b).y?1.:0.);}')
    C('bool_vec_compare', 'void main(){bool a=vec2(1.)==vec2(1.),b=vec3(1.)!=vec3(2.),c=ivec2(1)==ivec2(1);color=vec4(a?1.:0.,b?1.:0.,c?1.:0.,1.);}')
    C('bool_ops_nonshort', 'void main(){bool a=true,b=false;bool c=a^^b;bool d=a&&b||!a;color=vec4(c?1.:0.,d?1.:0.,0.,1.);}')

    # ------------------------------------------------------------------ D. operators & expressions
    C('op_comma', 'void main(){int a=1,b=2;int c=(a++,b++,a+b);color=vec4(float(c));}')
    C('op_comma_in_for', 'void main(){int s=0;for(int i=0;i<3;i++,s++){}color=vec4(float(s));}', ok=False)
    C('op_ternary_nested_right', 'void main(){float x=.5;float r=x>.7?1.:x>.3?.5:0.;color=vec4(r);}')
    C('op_ternary_nested_left', 'void main(){float x=.5;float r=x>.1?x>.4?1.:2.:3.;color=vec4(r);}')
    C('op_ternary_chain_assign', 'void main(){float a=1.,b=2.;float r=true?a=3.:b=4.;color=vec4(r,a,b,1.);}',
      note='assignment inside ternary branches; precedence of ?: vs =')
    C('op_ternary_side_effects', 'float side(inout float a){a+=1.;return a;} void main(){float a=0.;float r=a>0.?side(a):-side(a);color=vec4(r,a,0.,1.);}',
      note='only one branch may run')
    C('op_ternary_vec_cond', 'void main(){vec4 v=position.x>1.?vec4(1.):vec4(0.);color=v;}', decl=UNI)
    C('op_ternary_lvalue', 'void main(){float a,b;(true?a:b)=1.;color=vec4(a);}', ok=False, valid=False)
    C('op_ternary_struct', 'struct S{float x;}; void main(){S a,b;a.x=1.;b.x=2.;S c=true?a:b;color=vec4(c.x);}')
    C('op_short_circuit_side_effects', 'void main(){int i=0;bool b=(i++>5)&&(i++>0);bool c=(i++<5)||(i++>0);color=vec4(float(i),b?1.:0.,c?1.:0.,1.);}',
      note='right operand must not run when short-circuited')
    C('op_unary_adjacent', 'void main(){float a=1.,b=2.;float c=a - -b;float d=a + +b;float e=-(-a);float f=- -a;float g=-+-a;color=vec4(c,d,e,f+g);}',
      note='`- -x` must never collapse to `--x`')
    C('op_postfix_ambiguous', 'void main(){int a=1,b=2;int c=a+++b;int d=a---b;color=vec4(float(c+d));}',
      note='maximal munch: a++ + b')
    C('op_negate_literal_spacing', 'void main(){float a=1.;float b=a- -.5;float c=a-(-.5);float d=a--.5;color=vec4(b,c,d,1.);}', ok=False, valid=False,
      note='`a--.5` lexes as a-- .5')
    C('op_not_chain', 'void main(){bool b=true;bool c=!!b,d=!!!b;color=vec4(c?1.:0.,d?1.:0.,0.,1.);}')
    C('op_bitnot', 'void main(){int a=~~5,b=-~5,c=~-5;color=vec4(float(a+b+c));}',
      note='bitwise ops absent from ES2 profile')
    C('op_shift_precedence', 'void main(){int a=1<<2+1,b=8>>1-1,c=1<<1<<1;color=vec4(float(a+b+c)/100.);}')
    C('op_bitwise_precedence', 'void main(){int a=1|2&3^1;int b=1+2<<1;bool c=(5&3)==1;color=vec4(float(a+b)/10.,c?1.:0.,0.,1.);}')
    C('op_compound_int', 'void main(){int a=100;a+=1;a-=2;a*=3;a/=2;a%=7;a<<=2;a>>=1;a&=15;a|=2;a^=5;color=vec4(float(a)/100.);}')
    C('op_int_ops_vector', 'void main(){ivec3 v=ivec3(count,count*3,-count);ivec3 w=(v&ivec3(5))|(v<<2)^(v>>1);w%=ivec3(7);color=vec4(vec3(w)/50.,1.);}', decl=UNI)
    C('op_bitnot_runtime', 'void main(){int x=~count;color=vec4(float(x)/10.);}', decl=UNI)
    C('op_mod_int', 'void main(){int a=7%3,b=7/2,c=-7/2;color=vec4(float(a+b+c));}',
      note='% missing in ES2 profile: lower to a - b*(a/b) for ints')
    C('op_mod_int_vector', 'void main(){ivec3 a=ivec3(7,8,9)%ivec3(2,3,4);color=vec4(vec3(a)/4.,1.);}')
    C('op_mod_float_invalid', 'void main(){float a=7.%3.;color=vec4(a);}', ok=False, valid=False)
    C('op_assign_chain', 'void main(){float a,b,c;a=b=c=.5;color=vec4(a+b+c);}')
    C('op_assign_expression', 'void main(){float a;float b=(a=.5)+1.;color=vec4(a,b,0.,1.);}')
    C('op_assign_in_condition', 'void main(){float a=0.;if((a=.5)>.25)color=vec4(a);else color=vec4(0.);}')
    C('op_increment_vector', 'void main(){vec2 v=vec2(1.);v++;++v;v.x--;--v.y;mat2 m=mat2(1.);m[0][0]++;m[1]--;color=vec4(v,m[0][0],m[1][1]);}')
    C('op_increment_in_index', 'void main(){float a[3]=float[3](.1,.2,.3);int i=0;float s=a[i++]+a[i++];color=vec4(s,float(i),0.,1.);}', ok=False)
    C('op_increment_float', 'void main(){float f=.5;f++;--f;++f;color=vec4(f);}')
    C('op_vec_scalar_arith', 'void main(){vec3 v=vec3(1.)*2.+1.-vec3(.5)/2.;vec3 w=2.-v;vec3 x=1./v;color=vec4(w,x.x);}')
    C('op_vec_compound_scalar', 'void main(){vec3 v=vec3(1.);v*=2.;v+=1.;v-=.5;v/=2.;color=vec4(v,1.);}')
    C('op_int_vec_arith', 'void main(){ivec2 a=ivec2(1,2)*3+ivec2(1);ivec2 b=a/2-1;color=vec4(vec2(b),0.,1.);}')
    C('op_precedence_mix', 'void main(){float a=1.+2.*3.-4./2.;bool b=1.+1.>1.&&2.<3.||false;float c=(1.+2.)*3.;color=vec4(a,b?1.:0.,c,1.);}')
    C('op_precedence_parens_dropped', 'void main(){float a=(1.+2.)*(3.-1.);float b=1.-(2.-3.);float c=1./(2./4.);float d=-(1.+2.);color=vec4(a,b,c,d);}',
      note='emitter must not drop parentheses')
    C('op_precedence_neg_pow', 'void main(){float a=-2.*-3.;float b=-(2.*3.);float c=2.-(-3.);color=vec4(a,b,c,1.);}')
    C('op_comparison_chain_invalid', 'void main(){bool b=1.<2.<3.;color=vec4(b?1.:0.);}', ok=False, valid=False)
    C('op_vec_equality', 'void main(){bool a=vec2(1.)==vec2(1.);bool b=mat2(1.)==mat2(1.);color=vec4(a&&b?1.:0.);}')
    C('op_struct_equality', 'struct S{float a;vec2 b;}; void main(){S x=S(1.,vec2(2.)),y=S(1.,vec2(2.));color=vec4(x==y?1.:0.,x!=y?1.:0.,0.,1.);}',
      note='SkSL struct comparison support is limited')
    C('op_array_length', 'void main(){float a[4];int n=a.length();vec4 v;int m=v.length();mat3 k;int j=k.length();color=vec4(float(n+m+j)/20.);}')

    # ------------------------------------------------------------------ E. swizzles, matrices, arrays, structs
    C('swz_chain', 'void main(){vec4 v=vec4(1.,2.,3.,4.);color=v.xyzw.wzyx.xy.yx.xxyy/4.;}')
    C('swz_on_rvalue', 'vec4 f(){return vec4(1.,2.,3.,4.);} void main(){color=vec4(f().zw,vec4(5.).xy)/5.;}')
    C('swz_on_ctor', 'void main(){color=vec4(vec3(1.,2.,3.).zyx,vec2(4.).y)/4.;}')
    C('swz_on_ternary', 'void main(){color=vec4((true?vec2(1.,2.):vec2(3.,4.)).yx,0.,1.);}')
    C('swz_stpq', 'void main(){vec4 v=vec4(.1,.2,.3,.4);color=v.stpq;}')
    C('swz_mixed_sets_invalid', 'void main(){vec4 v=vec4(1.);color=vec4(v.xg,0.,1.);}', ok=False, valid=False)
    C('swz_out_of_range_invalid', 'void main(){vec2 v=vec2(1.);color=vec4(v.z);}', ok=False, valid=False)
    C('swz_repeat_assign_invalid', 'void main(){vec2 v;v.xx=vec2(1.);color=vec4(v,0.,1.);}', ok=False, valid=False)
    C('swz_swap_assign', 'void main(){vec4 v=vec4(1.,2.,3.,4.);v.xy=v.yx;v.zw=v.wz;color=v/4.;}',
      note='swap via swizzle lvalue: needs temp')
    C('swz_assign_self_overlap', 'void main(){vec4 v=vec4(1.,2.,3.,4.);v.xyz=v.yzw;color=v/4.;}')
    C('swz_compound', 'void main(){vec4 v=vec4(1.);v.xy+=vec2(1.);v.zw*=vec2(2.,3.);v.x-=.5;color=v/4.;}')
    C('swz_scalar_member', 'void main(){vec2 v=vec2(1.,2.);float f=v.x;f+=v.y;color=vec4(f);}')
    C('swz_int_vec', 'void main(){ivec4 v=ivec4(1,2,3,4);color=vec4(v.wzyx)/4.;}')
    C('swz_bool_vec', 'void main(){bvec4 v=bvec4(true,false,true,false);bvec2 w=v.zw;color=vec4(w.x?1.:0.,w.y?1.:0.,v.x?1.:0.,1.);}')
    C('swz_as_out_arg', 'void f(out vec2 a){a=vec2(1.,2.);} void main(){vec4 v=vec4(0.);f(v.zw);color=v/2.;}')
    C('swz_as_inout_arg_reversed', 'void f(inout vec2 a){a=a.yx;} void main(){vec4 v=vec4(1.,2.,3.,4.);f(v.wz);color=v/4.;}',
      note='swizzle lvalue passed as inout: copy-in/copy-out order matters')
    C('swz_as_out_arg_repeat_invalid', 'void f(out vec2 a){a=vec2(1.);} void main(){vec4 v;f(v.xx);color=v;}', ok=False, valid=False)
    C('mat_ctor_layout', 'void main(){mat2 m=mat2(1.,2.,3.,4.);color=vec4(m[0][1],m[1][0],m[0][0],m[1][1])/4.;}',
      note='column-major construction; asymmetric on purpose')
    C('mat_from_vectors', 'void main(){mat3 m=mat3(vec3(1.,2.,3.),vec3(4.,5.,6.),vec3(7.,8.,9.));color=vec4(m[0].x,m[1].y,m[2].z,m[0].z)/9.;}')
    C('mat_from_scalars_mixed', 'void main(){mat3 m=mat3(vec2(1.,2.),3.,vec3(4.,5.,6.),vec3(7.,8.,9.));color=vec4(m[0].z,m[1].x,m[2].z,1.)/9.;}',
      note='constructor args do not align with columns')
    C('mat_from_larger', 'void main(){mat4 b=mat4(1.,2.,3.,4.,5.,6.,7.,8.,9.,10.,11.,12.,13.,14.,15.,16.);mat2 m=mat2(b);color=vec4(m[0],m[1])/16.;}')
    C('mat_from_smaller', 'void main(){mat2 s=mat2(2.,3.,4.,5.);mat4 m=mat4(s);color=vec4(m[0].x,m[1].y,m[2].z,m[3].w)/5.;}',
      note='remaining entries come from identity')
    C('mat_diagonal', 'void main(){mat3 m=mat3(2.);color=vec4(m[0].x,m[0].y,m[1].y,m[2].z)/2.;}')
    C('mat_column_assign', 'void main(){mat3 m=mat3(0.);m[1]=vec3(1.,2.,3.);m[2][0]=4.;color=vec4(m[1],m[2].x)/4.;}')
    C('mat_dynamic_column_in_loop', 'void main(){mat3 m=mat3(1.);float s=0.;for(int i=0;i<3;++i){s+=m[i][i];m[i][2-i]+=1.;}color=vec4(s/3.);}')
    C('mat_compound', 'void main(){mat2 m=mat2(1.);m*=m;m+=m;m*=2.;m-=mat2(1.);m/=2.;color=vec4(m[0],m[1]);}')
    C('mat_vec_order', 'void main(){mat2 m=mat2(1.,2.,3.,4.);vec2 v=vec2(1.,1.);vec2 a=m*v;vec2 b=v*m;color=vec4(a,b)/7.;}',
      note='M*v and v*M differ')
    C('mat_mat_product', 'void main(){mat3 a=mat3(1.,2.,3.,4.,5.,6.,7.,8.,9.);mat3 b=mat3(9.,8.,7.,6.,5.,4.,3.,2.,1.);mat3 c=a*b;color=vec4(c[0].x,c[1].y,c[2].z,c[0].z)/300.;}')
    C('mat_negate_scalar', 'void main(){mat2 m=-mat2(1.)*2.+.5;color=vec4(m[0],m[1]);}')
    C('mat_compmult', 'void main(){mat2 m=matrixCompMult(mat2(1.,2.,3.,4.),mat2(2.));color=vec4(m[0],m[1])/8.;}')
    C('mat_transpose', 'void main(){mat2 m=transpose(mat2(1.,2.,3.,4.));color=vec4(m[0],m[1])/4.;}', note='ES3 builtin; lowering required')
    C('mat_determinant', 'void main(){color=vec4(determinant(mat2(1.,2.,3.,4.)),determinant(mat3(2.)),0.,1.);}', note='ES3 builtin; lowering required')
    C('mat_inverse', 'void main(){mat2 m=inverse(mat2(2.,0.,0.,4.));color=vec4(m[0],m[1]);}', note='ES3 builtin; lowering required')
    C('mat_outer_product', 'void main(){mat2 m=outerProduct(vec2(1.,2.),vec2(3.,4.));color=vec4(m[0],m[1])/8.;}', note='ES3 builtin; lowering required')
    C('mat_explicit_square_names', 'void main(){mat2x2 a=mat2x2(1.);mat3x3 b=mat3x3(1.);mat4x4 c=mat4x4(1.);color=vec4(a[0].x,b[1].y,c[3].w,1.);}')
    C('mat_nonsquare_2x3', 'void main(){mat2x3 m=mat2x3(1.);color=vec4(m[1],1.);}', ok=False)
    C('mat_nonsquare_4x2_product', 'void main(){mat4x2 a=mat4x2(1.);mat2x4 b=mat2x4(1.);mat2 c=a*b;color=vec4(c[0],c[1]);}', ok=False)
    C('mat_index_oob_const', 'void main(){mat2 m=mat2(1.);color=vec4(m[2]);}', ok=False, valid=False)
    C('mat_uniform_use', 'void main(){color=vec4(m*vec2(1.),0.,1.);}', decl='uniform mat2 m;out vec4 color;')
    C('mat_uniform_mat4_columns', 'void main(){color=m[3];}', decl='uniform mat4 m;out vec4 color;')
    C('arr_ctor_typed', 'void main(){float a[3]=float[3](1.,2.,3.);color=vec4(a[0],a[1],a[2],1.)/3.;}')
    C('arr_ctor_unsized_ctor', 'void main(){float a[3]=float[](1.,2.,3.);color=vec4(a[0],a[1],a[2],1.)/3.;}')
    C('arr_type_style_decl', 'void main(){float[3] a=float[3](1.,2.,3.);color=vec4(a[0],a[1],a[2],1.)/3.;}',
      note='declarator on the type, not the name')
    C('arr_type_style_unsized_init', 'void main(){float[] a=float[](1.,2.,3.);color=vec4(a[0],a[1],a[2],1.)/3.;}')
    C('arr_multi_decl_mixed', 'void main(){float a=1.,b[2],c=3.;b[0]=a;b[1]=c;color=vec4(b[0],b[1],c,1.)/3.;}')
    C('arr_multi_decl_type_style', 'void main(){float[2] a=float[2](1.,2.),b=float[2](3.,4.);color=vec4(a[0],a[1],b[0],b[1])/4.;}')
    C('arr_vec_elements', 'void main(){vec3 a[2];a[0]=vec3(1.);a[1]=vec3(2.);a[1].y=3.;color=vec4(a[0].x,a[1].y,a[1].z,1.)/3.;}')
    C('arr_of_struct', 'struct S{float x;vec2 y;}; void main(){S a[2];a[0].x=1.;a[1].y=vec2(2.,3.);a[1].y.x+=1.;color=vec4(a[0].x,a[1].y,1.)/3.;}')
    C('arr_struct_member_array', 'struct S{float a[2];vec3 v;}; void main(){S s;s.a[0]=.1;s.a[1]=.2;s.v=vec3(.3);color=vec4(s.a[0]+s.a[1],s.v.xy,1.);}')
    C('arr_assign_copy', 'void main(){float a[2]=float[2](1.,2.);float b[2];b=a;b[0]=5.;color=vec4(a[0],b[0],0.,1.)/5.;}',
      note='array assignment copies')
    C('arr_pass_by_value', 'float f(float a[3]){a[0]=9.;return a[0];} void main(){float x[3]=float[3](1.,2.,3.);float r=f(x);color=vec4(r,x[0],0.,1.)/9.;}',
      note='callee mutation must not leak')
    C('arr_pass_type_style', 'float f(float[3] a){return a[1];} void main(){color=vec4(f(float[3](1.,2.,3.))/3.);}')
    C('arr_inout', 'void f(inout float a[2]){a[1]=7.;} void main(){float x[2]=float[2](1.,2.);f(x);color=vec4(x[1]/7.);}')
    C('arr_return', 'float[2] f(){return float[2](1.,2.);} void main(){float a[2]=f();color=vec4(a[0],a[1],0.,1.)/2.;}',
      note='returned through an out parameter')
    C('arr_of_arrays', 'void main(){float a[2][3];a[1][2]=.5;color=vec4(a[1][2]);}', ok=False)
    C('arr_of_arrays_ctor', 'void main(){float a[2][2]=float[2][2](float[2](1.,2.),float[2](3.,4.));color=vec4(a[1][0]/4.);}', ok=False)
    C('arr_const_oob', 'void main(){float a[3];color=vec4(a[3]);}', ok=False, valid=False)
    C('arr_negative_index', 'void main(){float a[3];color=vec4(a[-1]);}', ok=False, valid=False)
    C('arr_zero_size', 'void main(){float a[0];color=vec4(1.);}', ok=False, valid=False)
    C('arr_size_const_expr', 'const int N=2*2;void main(){float a[N+1];a[4]=.5;color=vec4(a[4]);}')
    C('arr_size_from_float_invalid', 'void main(){float a[2.];color=vec4(1.);}', ok=False, valid=False)
    C('arr_loop_fill_sum', 'void main(){float a[8];for(int i=0;i<8;++i)a[i]=float(i);float s=0.;for(int i=0;i<8;++i)s+=a[7-i];color=vec4(s/28.);}')
    C('arr_global_mutated', 'float g[3];void main(){g[1]=.5;g[2]=g[1]*2.;color=vec4(g[1],g[2],0.,1.);}')
    C('arr_global_const', 'const float W[3]=float[3](.2,.3,.5);void main(){float s=0.;for(int i=0;i<3;++i)s+=W[i];color=vec4(s);}')
    C('arr_const_vec_array', 'const vec2 P[2]=vec2[2](vec2(1.,2.),vec2(3.,4.));void main(){color=vec4(P[0],P[1])/4.;}')
    C('arr_ctor_swap_self', 'void main(){float a[2]=float[2](1.,2.);a=float[2](a[1],a[0]);color=vec4(a[0],a[1],0.,1.)/2.;}',
      note='constructor reads the assigned array: elements go through a temporary')
    C('arr_ctor_loop_index', 'void main(){float s=0.;for(int i=0;i<3;++i)s+=float[3](.1,.2,.3)[i];color=vec4(s);}')
    C('arr_ctor_const_index_folded', 'void main(){color=vec4(float[3](.1,.2,.3)[1]);}')
    C('arr_ctor_nested_args', 'float f(float a[2]){return a[0]+a[1];} void main(){color=vec4(f(float[2](f(float[2](.1,.2)),.3)));}')
    C('arr_ctor_in_if_condition', 'void main(){color=vec4(0.);for(int i=0;i<2;++i)if(float[2](.1,.9)[i]>.5)color=vec4(1.);}')
    C('arr_ctor_in_return', 'float f(int k){for(int i=0;i<2;++i)if(i==k)return float[2](.1,.9)[i];return 0.;} void main(){color=vec4(f(1));}')
    C('arr_global_from_const_elem', 'const float W[3]=float[3](.2,.3,.5); const float X=W[1]; float Y=W[2]; void main(){color=vec4(X,Y,0.,1.);}',
      note='const arrays become variables; global initializers read the folded elements')
    C('arr_global_init_with_derivative', 'float G[2]=float[2](.25,.5); void main(){G[1]+=gl_FragCoord.x; color=vec4(G[0],dFdx(G[1]),0.,1.);}')
    C('arr_ctor_in_derivative_arg', 'void main(){color=vec4(dFdx(float[2](gl_FragCoord.x,1.)[0]));}')
    C('arr_of_struct_ctor', 'struct L{vec3 c;float i;}; void main(){L ls[2]=L[2](L(vec3(1.),.5),L(vec3(.2),1.));color=vec4(ls[1].c*ls[0].i,1.);}')
    C('arr_ctor_in_quad_function_arg', 'float g(float a[2]){return dFdx(a[0]);} void main(){color=vec4(g(float[2](gl_FragCoord.x,1.)));}')
    C('arr_ctor_side_effect_order', 'float n=0.; float h(){n+=1.;return n;} float f(float a[2],float b){return a[0]+b;} void main(){color=vec4(f(float[2](h(),1.),n));}',
      note='h() runs before n is read, as in GLSL')
    C('arr_assign_ternary', 'uniform float u; void main(){float a[2]=float[2](1.,2.);float b[2]=float[2](3.,4.);float c[2];c=u>0.?a:b;color=vec4(c[0]);}')
    C('arr_init_ternary', 'uniform float u; void main(){float a[2]=float[2](1.,2.);float b[2]=float[2](3.,4.);float c[2]=u>0.?a:b;color=vec4(c[0]);}')
    C('arr_compare', 'void main(){float a[2]=float[2](1.,2.);float b[2]=float[2](1.,2.);color=vec4(a==b?1.:0.);}')
    C('arr_assign_nested_in_expr', 'void main(){float a[2]=float[2](1.,2.);float b[2];float x=(b=a)[0];color=vec4(x);}')
    C('arr_index_by_expr', 'void main(){float a[4]=float[4](1.,2.,3.,4.);int i=1;color=vec4(a[i+1]/4.);}',
      note='i has the single value 1, so the index becomes the constant 2')
    C('arr_index_by_array_elem', 'void main(){int idx[2]=int[2](1,0);float a[2]=float[2](.25,.75);color=vec4(a[idx[0]]);}', ok=False,
      note='index is itself an array read')
    C('arr_index_by_vec_comp', 'void main(){ivec2 i=ivec2(1,0);float a[2]=float[2](.25,.75);color=vec4(a[i.x]);}',
      note='i is only initialized, from constants: its initializer replaces it in the index')
    C('struct_decl_and_var', 'struct S{float x;} s=S(.5);void main(){color=vec4(s.x);}')
    C('struct_local_decl', 'void main(){struct L{float v;};L l;l.v=.5;color=vec4(l.v);}')
    C('struct_ctor_nested', 'struct A{float x;};struct B{A a;vec2 v;}; void main(){B b=B(A(.5),vec2(.25));color=vec4(b.a.x,b.v,1.);}')
    C('struct_nested_assign', 'struct A{vec3 v;};struct B{A a;}; void main(){B b;b.a.v=vec3(0.);b.a.v.y=.5;b.a.v.zx=vec2(.25);color=vec4(b.a.v,1.);}')
    C('struct_param_return', 'struct S{float x;vec2 y;}; S make(float a){S s;s.x=a;s.y=vec2(a);return s;} float get(S s){return s.x+s.y.x;} void main(){color=vec4(get(make(.25)));}')
    C('struct_inout_param', 'struct S{float x;}; void bump(inout S s){s.x+=.25;} void main(){S s=S(.25);bump(s);bump(s);color=vec4(s.x);}')
    C('struct_copy_semantics', 'struct S{float x;}; void main(){S a=S(1.);S b=a;b.x=2.;color=vec4(a.x,b.x,0.,1.)/2.;}')
    C('struct_array_in_struct_ctor', 'struct S{float a[2];}; void main(){S s=S(float[2](.25,.5));color=vec4(s.a[0],s.a[1],0.,1.);}')
    C('struct_array_assign', 'struct S{float a[2];float b;}; void main(){S s=S(float[2](1.,2.),3.);S t;t=s;color=vec4(t.a[1],t.b,0.,1.)/3.;}')
    C('struct_array_self_ctor', 'struct S{float a[2];}; void main(){S s=S(float[2](1.,2.));s=S(float[2](s.a[1],s.a[0]));color=vec4(s.a[0]/2.);}')
    C('struct_array_const_global', 'struct S{float a[2];float b;}; const S C=S(float[2](.1,.2),.3); const float Z=C.b; void main(){color=vec4(C.a[0],C.b,Z,1.);}')
    C('struct_array_return', 'struct S{float a[2];}; S g(){S s=S(float[2](1.,2.));return s;} void main(){S t=g();color=vec4(t.a[0]);}',
      note='returned through an out parameter')
    C('struct_with_matrix', 'struct S{mat2 m;vec2 v;}; void main(){S s=S(mat2(2.),vec2(1.));color=vec4(s.m*s.v,0.,1.)/2.;}')
    C('struct_with_bool_int', 'struct S{bool b;int i;ivec2 iv;bvec2 bv;}; void main(){S s=S(true,2,ivec2(3),bvec2(false));color=vec4(s.b?1.:0.,float(s.i+s.iv.x)/5.,s.bv.x?1.:0.,1.);}')
    C('struct_empty', 'struct S{}; void main(){color=vec4(1.);}', ok=False, valid=False)
    C('struct_member_init_invalid', 'struct S{float x=1.;}; void main(){color=vec4(1.);}', ok=False, valid=False)
    C('struct_recursive_invalid', 'struct S{S s;}; void main(){color=vec4(1.);}', ok=False, valid=False)
    C('struct_redefined_invalid', 'struct S{float x;};struct S{float y;}; void main(){color=vec4(1.);}', ok=False, valid=False)
    C('struct_ctor_wrong_args_invalid', 'struct S{float x;vec2 y;}; void main(){S s=S(1.);color=vec4(s.x);}', ok=False, valid=False)
    C('struct_ctor_scalar_into_vec_invalid', 'struct S{vec2 y;}; void main(){S s=S(1.);color=vec4(s.y,0.,1.);}', ok=False, valid=False,
      note='struct ctor does not broadcast')
    C('struct_forward_use_invalid', 'void main(){S s;color=vec4(1.);} struct S{float x;};', ok=False, valid=False)
    C('struct_global_mutated_by_helper', 'struct S{float x;};S g;void bump(){g.x+=.25;}void main(){g.x=0.;bump();bump();color=vec4(g.x);}')
    C('struct_uniform', 'struct S{vec3 a;float b;}; uniform S u;void main(){color=vec4(u.a,u.b);}', ok=False, decl='out vec4 color;',
      note='struct uniforms: flatten or refuse (decision)')

    # ------------------------------------------------------------------ F. declarations, scope, constants, globals
    C('scope_shadow_nested', 'void main(){float x=1.;{float x=2.;{float x=3.;color=vec4(x/3.);}}}')
    C('scope_shadow_after_use', 'void main(){float x=.5;{float y=x;float x=y+.25;color=vec4(x);}}')
    C('scope_self_reference_init', 'void main(){float x=.5;{float x=x+.25;color=vec4(x);}}',
      note='inner x is declared after its initializer: initializer sees the OUTER x')
    C('scope_shadow_param', 'float f(float x){{float x=2.;return x;}} void main(){color=vec4(f(1.)/2.);}',
      note='declaring in a nested block shadows the parameter')
    C('scope_param_redeclared_invalid', 'float f(float x){float x=2.;return x;} void main(){color=vec4(f(1.));}', ok=False, valid=False)
    C('scope_redeclared_invalid', 'void main(){float x=1.;float x=2.;color=vec4(x);}', ok=False, valid=False)
    C('scope_for_var_leaks_invalid', 'void main(){for(int i=0;i<2;++i){}color=vec4(float(i));}', ok=False, valid=False)
    C('scope_for_var_shadow', 'void main(){int i=5;float s=0.;for(int i=0;i<3;++i)s+=.1;s+=float(i);color=vec4(s/5.);}')
    C('scope_same_name_sibling_loops', 'void main(){float s=0.;for(int i=0;i<2;++i)s+=.1;for(int i=0;i<3;++i)s+=.1;color=vec4(s);}')
    C('scope_decl_in_if_body', 'void main(){if(true)float a=1.;color=vec4(1.);}')
    C('scope_bare_blocks', 'void main(){{}{{}}{float a=.5;{color=vec4(a);}}}')
    C('scope_use_before_decl_invalid', 'void main(){color=vec4(a);float a=1.;}', ok=False, valid=False)
    C('scope_global_shadowed_by_local', 'float g=.5;void main(){float g=.25;color=vec4(g);}')
    C('scope_uniform_shadowed', 'void main(){float time=.5;color=vec4(time);}', decl='uniform float time;out vec4 color;')
    C('const_global_ctor_chain', 'const float K=sqrt(2.)*3.;const vec2 V=vec2(K,K*.5);void main(){color=vec4(V/5.,0.,1.);}')
    C('const_global_dependent', 'const float A=1.;const float B=A+1.;const float C=A*B;void main(){color=vec4(C/2.);}')
    C('const_global_int_float', 'const int N=3;const float F=float(N)/2.;void main(){color=vec4(F);}')
    C('const_local', 'void main(){const float c=.5;const vec2 v=vec2(c);color=vec4(v,0.,1.);}')
    C('const_from_uniform_invalid', 'const float c=u;void main(){color=vec4(c);}', ok=False, valid=False, decl='uniform float u;out vec4 color;')
    C('const_from_user_fn_invalid', 'float f(float x){return x;}const float c=f(1.);void main(){color=vec4(c);}', ok=False, valid=False)
    C('const_assign_invalid', 'void main(){const float c=.5;c=1.;color=vec4(c);}', ok=False, valid=False)
    C('const_uninit_invalid', 'void main(){const float c;color=vec4(1.);}', ok=False, valid=False)
    C('const_param', 'float f(const in float x){return x*.5;} void main(){color=vec4(f(1.));}')
    C('const_param_write_invalid', 'float f(const in float x){x=1.;return x;} void main(){color=vec4(f(1.));}', ok=False, valid=False)
    C('const_array_index_ctor', 'const int I[3]=int[3](2,1,0);void main(){color=vec4(float(I[0])/2.);}')
    C('global_init_builtin_call', 'float g=sqrt(2.);void main(){color=vec4(g);}')
    C('global_init_uniform_invalid', 'float g=u*2.;void main(){color=vec4(g);}', ok=False, valid=False, decl='uniform float u;out vec4 color;')
    C('global_mutated_by_helper', 'float g=0.;void bump(){g+=.1;}void main(){bump();bump();color=vec4(g);}',
      note='SkSL has no mutable globals: must be threaded or hoisted')
    C('global_mutated_order', 'float g=1.;float twice(){g*=2.;return g;}void main(){float a=twice();float b=twice();color=vec4(a,b,g,1.)/4.;}')
    C('global_vec_component_write', 'vec3 g=vec3(0.);void set(){g.y=.5;g.zx=vec2(.25);}void main(){set();color=vec4(g,1.);}')
    C('global_multi_decl', 'float a=1.,b=2.,c;void main(){c=a+b;color=vec4(c/3.);}')
    C('global_uninitialised_read', 'float g;void main(){g=.5;color=vec4(g);}')
    C('global_matrix', 'mat2 m=mat2(1.);void rot(){m=mat2(0.,1.,-1.,0.)*m;}void main(){rot();color=vec4(m[0],m[1]);}')
    C('multi_decl_dependent', 'void main(){float a=1.,b=a+1.,c=b*2.;color=vec4(c/4.);}')
    C('multi_decl_vec', 'void main(){vec2 a=vec2(1.),b,c=vec2(3.);b=a+c;color=vec4(b/4.,0.,1.);}')
    C('uniform_multi_decl', 'void main(){color=vec4(a,b,c,1.);}', decl='uniform float a,b,c;out vec4 color;')
    C('uniform_int_vec', 'void main(){color=vec4(vec2(iv),float(i),1.);}', decl='uniform int i;uniform ivec2 iv;out vec4 color;')
    C('uniform_precision_variants', 'void main(){color=vec4(a.xyz,b+c);}', decl='uniform lowp vec4 a;uniform mediump float b;uniform highp float c;out vec4 color;')
    C('uniform_array_const_index', 'void main(){color=u[2]+u[0];}', decl='uniform vec4 u[4];out vec4 color;')
    C('uniform_array_loop_index', 'void main(){vec4 s=vec4(0.);for(int i=0;i<4;++i)s+=u[i];color=s;}', decl='uniform vec4 u[4];out vec4 color;')
    C('uniform_array_dynamic_index', 'void main(){color=u[count];}', ok=False, decl='uniform vec4 u[4];uniform int count;out vec4 color;', target='reject')
    C('uniform_array_length', 'void main(){color=vec4(float(u.length()));}', decl='uniform float u[4];out vec4 color;')
    C('uniform_initialised_invalid', 'void main(){color=vec4(u);}', ok=False, valid=False, decl='uniform float u=1.;out vec4 color;')
    C('uniform_uint', 'void main(){color=vec4(float(u));}', decl='uniform uint u;out vec4 color;')
    C('uniform_bool', 'void main(){color=b?vec4(1.):vec4(0.);}', ok=False, decl='uniform bool b;out vec4 color;',
      note='SkSL runtime uniforms have no bool: lower to int/float or refuse (decision)')
    C('uniform_block_anonymous', 'void main(){color=vec4(a,b);}', decl='layout(std140) uniform U{vec3 a;float b;};out vec4 color;',
      note='block members become plain uniforms')
    C('uniform_block_named', 'void main(){color=vec4(u.a,u.b);}', decl='uniform U{vec3 a;float b;} u;out vec4 color;')
    C('uniform_block_binding', 'void main(){color=vec4(a);}', decl='layout(binding=0,std140) uniform U{vec4 a;};out vec4 color;')
    C('uniform_block_array_instance', 'void main(){color=vec4(u[0].a);}', ok=False, decl='uniform U{float a;} u[2];out vec4 color;')
    C('uniform_block_mat_rowmajor', 'void main(){color=vec4(m*vec2(1.),0.,1.);}', decl='layout(row_major) uniform U{mat2 m;};out vec4 color;', ok=False,
      note='row_major changes semantics; refuse')
    C('uniform_sampler_in_struct', 'void main(){color=vec4(1.);}', ok=False, decl='struct S{sampler2D t;}; uniform S s;out vec4 color;')
    C('uniform_unused_only', 'void main(){color=vec4(1.);}', decl='uniform float unused;uniform vec4 unused2;out vec4 color;')
    C('uniform_name_collision_with_helper', 'float scale(float x){return x*scale_;}void main(){color=vec4(scale(1.));}', decl='uniform float scale_;out vec4 color;')
    C('qual_flat_in', 'void main(){color=vec4(float(i));}', ok=False, decl='flat in int i;out vec4 color;')
    C('qual_smooth_in', 'void main(){color=vec4(uv,0.,1.);}', decl='smooth in vec2 uv;out vec4 color;')
    C('qual_centroid_in', 'void main(){color=vec4(uv,0.,1.);}', decl='centroid in vec2 uv;out vec4 color;')
    C('qual_noperspective_in', 'void main(){color=vec4(uv,0.,1.);}', ok=False, decl='noperspective in vec2 uv;out vec4 color;', valid=False,
      note='not in ES 3.2 core')
    C('qual_early_fragment_tests', 'void main(){color=vec4(1.);}', decl='layout(early_fragment_tests) in;out vec4 color;', note='no depth or stencil test in runtime shaders, so it has no observable effect')
    C('qual_layout_origin', 'void main(){color=vec4(1.);}', ok=False, decl='layout(origin_upper_left) in vec4 gl_FragCoord;out vec4 color;', valid=False)
    C('qual_precise', 'void main(){precise float x=.5;color=vec4(x);}', note='no equivalent in SkSL: drop or refuse')
    C('qual_coherent_volatile', 'void main(){color=vec4(v);}', ok=False, decl='coherent volatile uniform float v;out vec4 color;', valid=False)
    C('qual_shared', 'void main(){color=vec4(1.);}', ok=False, decl='shared float s;out vec4 color;', valid=False)
    C('qual_attribute_keyword', 'void main(){color=vec4(a);}', ok=False, decl='attribute float a;out vec4 color;', valid=False)
    C('qual_varying_keyword', 'void main(){color=vec4(a);}', ok=False, decl='varying float a;out vec4 color;', valid=False)
    C('qual_inout_global_invalid', 'void main(){color=vec4(1.);}', ok=False, decl='inout vec4 color;', valid=False)
    C('qual_buffer_unsized_array', 'void main(){color=vec4(d[0]);}', ok=False, decl='layout(std430,binding=1) buffer B{float d[];};out vec4 color;')
    C('qual_buffer_readonly', 'void main(){color=vec4(d);}', ok=False, decl='layout(std430,binding=1) readonly buffer B{float d;};out vec4 color;')
    C('qual_image_size', 'void main(){color=vec4(vec2(imageSize(img)),0.,1.);}', ok=False, decl='layout(rgba8,binding=0) readonly uniform highp image2D img;out vec4 color;')
    C('qual_image_atomic', 'void main(){imageAtomicAdd(img,ivec2(0),1u);color=vec4(1.);}', ok=False, decl='layout(r32ui,binding=0) uniform highp uimage2D img;out vec4 color;')
    C('qual_const_in_out_combo_invalid', 'void f(const out float x){x=1.;} void main(){float y;f(y);color=vec4(y);}', ok=False, valid=False)

    # ------------------------------------------------------------------ G. control flow
    C('cf_if_no_braces_chain', 'void main(){float a=0.;if(position.x>1.)a=1.;else if(position.x>0.)a=.5;else a=.25;color=vec4(a);}', decl=UNI)
    C('cf_dangling_else', 'void main(){float a=0.;if(true)if(false)a=1.;else a=2.;color=vec4(a/2.);}', note='else binds to inner if')
    C('cf_empty_statements', 'void main(){;;color=vec4(1.);;}')
    C('cf_empty_if_body', 'void main(){if(true);else;color=vec4(1.);}')
    C('cf_empty_block_bodies', 'void main(){if(true){}else{}for(int i=0;i<2;++i){}color=vec4(1.);}')
    C('cf_for_empty_body_semicolon', 'void main(){int i;for(i=0;i<3;++i);color=vec4(float(i)/3.);}')
    C('cf_for_init_outside', 'void main(){int i=0;for(;i<3;++i){}color=vec4(float(i)/3.);}', note='counter declared before the loop; accepted with a software.loop warning')
    C('cf_for_no_cond_break', 'void main(){float a=0.;for(;;){a+=.1;if(a>.3)break;}color=vec4(a);}', ok=False, target='reject')
    C('cf_for_no_increment', 'void main(){float a=0.;for(int i=0;i<4;){a+=.1;++i;}color=vec4(a);}')
    C('cf_for_multi_init', 'void main(){float a=0.;for(int i=0,j=4;i<j;++i)a+=.1;color=vec4(a);}',
      note='j is only ever 4, so the counter i proves the number of iterations')
    C('cf_for_float_counter', 'void main(){float a=0.;for(float f=0.;f<1.;f+=.25)a+=f;color=vec4(a);}')
    C('cf_for_float_dynamic_end', 'void main(){float a=0.;for(float f=0.;f<size.x;f+=1.)a+=.1;color=vec4(a);}', ok=False, decl=UNI, target='reject')
    C('cf_for_counts_down', 'void main(){float a=0.;for(int i=4;i>0;--i)a+=.1;color=vec4(a);}')
    C('cf_for_step_two', 'void main(){float a=0.;for(int i=0;i<10;i+=2)a+=.1;color=vec4(a);}')
    C('cf_for_cond_le_ge', 'void main(){float a=0.;for(int i=0;i<=3;++i)a+=.1;for(int j=3;j>=0;--j)a+=.1;color=vec4(a);}')
    C('cf_for_cond_ne', 'void main(){float a=0.;for(int i=0;i!=4;++i)a+=.1;color=vec4(a);}',
      note='!= conditions are not unrollable')
    C('cf_for_counter_modified_in_body', 'void main(){float a=0.;for(int i=0;i<8;++i){a+=.1;i++;}color=vec4(a);}', ok=False, target='reject')
    C('cf_for_counter_read_in_body', 'void main(){float a=0.;for(int i=0;i<4;++i)a+=float(i)*.1;color=vec4(a);}')
    C('cf_for_counter_as_array_index', 'void main(){float v[4]=float[4](.1,.2,.3,.4);float a=0.;for(int i=0;i<4;++i)a+=v[i];color=vec4(a);}')
    C('cf_for_zero_trip', 'void main(){float a=.5;for(int i=0;i<0;++i)a+=1.;color=vec4(a);}')
    C('cf_for_one_trip', 'void main(){float a=0.;for(int i=0;i<1;++i)a+=.5;color=vec4(a);}')
    C('cf_for_large_trip_count', 'void main(){float a=0.;for(int i=0;i<2000;++i)a+=.0001;color=vec4(a);}', note='SkSL unrolls: may hit complexity limits')
    C('cf_for_triple_nest', 'void main(){float a=0.;for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)a+=.01;color=vec4(a);}')
    C('cf_for_shadowed_counter_nested', 'void main(){float a=0.;for(int i=0;i<2;++i){for(int i=0;i<2;++i){a+=.1;}}color=vec4(a);}')
    C('cf_for_return_inside', 'float f(){for(int i=0;i<4;++i){if(i==2)return float(i);}return -1.;} void main(){color=vec4(f()/2.);}')
    C('cf_for_continue_in_nested', 'void main(){float a=0.;for(int i=0;i<3;++i){for(int j=0;j<3;++j){if(j==1)continue;a+=.1;}}color=vec4(a);}')
    C('cf_for_break_in_nested_only_inner', 'void main(){float a=0.;for(int i=0;i<3;++i){for(int j=0;j<3;++j){if(j==1)break;a+=.1;}}color=vec4(a);}')
    C('cf_for_with_discard_free_early_exit', 'void main(){color=vec4(0.);for(int i=0;i<4;++i){color+=vec4(.1);if(color.r>.25)return;}}')
    C('cf_while_const_break', 'void main(){float a=0.;int i=0;while(true){a+=.1;if(++i>3)break;}color=vec4(a);}', ok=False, target='reject')
    C('cf_while_false', 'void main(){while(false){}color=vec4(1.);}')
    C('cf_do_while_once', 'void main(){float a=0.;do{a+=.5;}while(false);color=vec4(a);}')
    C('cf_do_while_continue', 'void main(){float a=0.;int i=0;do{++i;if(i==2)continue;a+=.1;}while(i<4);color=vec4(a);}',
      note='continue jumps to the condition')
    C('cf_while_counter', 'void main(){int i=0;float a=0.;while(i<10){a+=.1;i++;}color=vec4(a);}')
    C('cf_while_increment_in_condition', 'void main(){int i=0;int k=0;while(i++<5){k++;}color=vec4(float(k)/10.,float(i)/10.,0.,1.);}')
    C('cf_while_maxiter_annotation', 'void main(){float d=1.;while(d>time){ // @maxiter{64}\nd*=.5;}color=vec4(d);}', decl=UNI)
    C('cf_for_maxiter_annotation', 'void main(){float a=0.;for(int i=0;i<count;i++){ // @maxiter{32}\na+=.1;}color=vec4(a);}', decl=UNI)
    C('cf_while_unbounded', 'void main(){float d=1.;while(d>time){d*=.5;}color=vec4(d);}', ok=False, decl=UNI, target='reject')
    C('cf_while_decl_in_cond', 'void main(){float a=0.;int i=0;while(bool b=i<3){a+=.1;++i;}color=vec4(a);}', ok=False, note='declaration in condition', target='reject')
    C('cf_if_decl_in_cond_invalid', 'void main(){if(bool b=true)color=vec4(1.);}', ok=False, valid=False)
    C('cf_condition_not_bool_invalid', 'void main(){if(1)color=vec4(1.);}', ok=False, valid=False)
    C('cf_condition_vec_bool_invalid', 'void main(){if(bvec2(true))color=vec4(1.);}', ok=False, valid=False)
    C('cf_break_outside_loop_invalid', 'void main(){break;color=vec4(1.);}', ok=False, valid=False)
    C('cf_continue_outside_loop_invalid', 'void main(){continue;color=vec4(1.);}', ok=False, valid=False)
    C('cf_code_after_return', 'void main(){color=vec4(1.);return;color=vec4(0.);}')
    C('cf_code_after_break', 'void main(){float a=0.;for(int i=0;i<3;++i){break;a+=1.;}color=vec4(a);}')
    C('cf_return_value_in_void_invalid', 'void main(){color=vec4(1.);return 1.;}', ok=False, valid=False)
    C('cf_discard_in_if', 'void main(){if(position.x>1.)discard;color=vec4(1.);}', decl=UNI)
    C('cf_discard_in_helper', 'void kill(){discard;} void main(){kill();color=vec4(1.);}')
    C('cf_discard_in_dead_branch', 'void main(){if(false){discard;}color=vec4(1.);}')
    C('cf_switch_basic', 'void main(){float a=0.;switch(count){case 0:a=.1;break;case 1:a=.2;break;default:a=.3;}color=vec4(a);}', decl=UNI)
    C('cf_switch_constant_selector', 'void main(){float a=0.;switch(2){case 1:a=.1;break;case 2:a=.2;break;}color=vec4(a);}')
    C('cf_switch_default_middle', 'void main(){float a=0.;switch(count){case 0:a+=.1;default:a+=.2;case 1:a+=.3;break;case 2:a+=.4;}color=vec4(a);}', decl=UNI,
      note='default in the middle participates in fallthrough: it moves to the end with a copy of what it runs')
    C('cf_switch_default_first_declares', 'void main(){float a=0.;switch(count){default:float t=.5;a=t;break;case 1:a=.1;}color=vec4(a);}', ok=False, decl=UNI,
      note='a first default whose statements declare a variable cannot move: the declaration would come before the first label')
    C('cf_switch_only_default', 'void main(){float a=0.;switch(count){default:a=.5;}color=vec4(a);}', decl=UNI)
    C('cf_switch_empty', 'void main(){switch(count){}color=vec4(1.);}', decl=UNI)
    C('cf_switch_no_default_nomatch', 'void main(){float a=.5;switch(count){case 100:a=1.;break;}color=vec4(a);}', decl=UNI)
    C('cf_switch_stacked_labels', 'void main(){float a=0.;switch(count){case 0:case 1:case 2:a=.5;break;default:a=.1;}color=vec4(a);}', decl=UNI)
    C('cf_switch_case_const_expr', 'const int K=2;void main(){float a=0.;switch(count){case K+1:a=.5;break;case 1<<1:a=.25;break;}color=vec4(a);}', decl=UNI)
    C('cf_switch_negative_case', 'void main(){float a=0.;switch(count){case -1:a=.5;break;case 0:a=.25;}color=vec4(a);}', decl=UNI)
    C('cf_switch_break_in_loop_in_switch', 'void main(){float a=0.;switch(count){case 0:for(int i=0;i<4;++i){if(i==2)break;a+=.1;}a+=.5;break;}color=vec4(a);}', decl=UNI,
      note='inner break leaves the loop, not the switch')
    C('cf_switch_continue_in_loop', 'void main(){float a=0.;for(int i=0;i<4;++i){switch(i){case 1:continue;default:a+=.1;}a+=.05;}color=vec4(a);}', ok=False, target='reject',
      note='continue inside switch targets the enclosing loop')
    C('cf_switch_break_in_switch_in_loop', 'void main(){float a=0.;for(int i=0;i<4;++i){switch(i){case 1:break;default:a+=.1;}a+=.05;}color=vec4(a);}',
      note='break exits only the switch')
    C('cf_switch_nested', 'void main(){float a=0.;switch(count){case 0:switch(count+1){case 1:a=.5;break;}break;default:a=.1;}color=vec4(a);}', decl=UNI)
    C('cf_switch_return_in_case', 'float f(int n){switch(n){case 0:return .5;case 1:return .25;}return 0.;} void main(){color=vec4(f(count));}', decl=UNI)
    C('cf_switch_scoped_decl_in_braces', 'void main(){float a=0.;switch(count){case 0:{float t=.5;a=t;}break;case 1:{float t=.25;a=t;}break;}color=vec4(a);}', decl=UNI)
    C('cf_switch_duplicate_case_invalid', 'void main(){switch(count){case 1:break;case 1:break;}color=vec4(1.);}', ok=False, valid=False, decl=UNI)
    C('cf_switch_duplicate_default_invalid', 'void main(){switch(count){default:break;default:break;}color=vec4(1.);}', ok=False, valid=False, decl=UNI)
    C('cf_switch_float_selector_invalid', 'void main(){switch(1.){case 1:break;}color=vec4(1.);}', ok=False, valid=False)
    C('cf_switch_nonconst_label_invalid', 'void main(){switch(count){case count:break;}color=vec4(1.);}', ok=False, valid=False, decl=UNI)
    C('cf_switch_uint_selector', 'void main(){switch(1u){case 1u:break;}color=vec4(1.);}')
    C('cf_case_outside_switch_invalid', 'void main(){case 1:color=vec4(1.);}', ok=False, valid=False)
    C('cf_goto_invalid', 'void main(){goto x;color=vec4(1.);}', ok=False, valid=False)

    # ------------------------------------------------------------------ H. functions
    C('fn_overload_by_type', 'float f(float x){return 1.;}float f(int x){return 2.;}float f(vec2 x){return 3.;}float f(vec3 x){return 4.;}float f(float x,float y){return 5.;} void main(){color=vec4((f(1.)+f(1)+f(vec2(1.))+f(vec3(1.))+f(1.,2.))/15.);}',
      note='SkSL overloads need mangling-free resolution; int vs float literal matters')
    C('fn_overload_mat_vec', 'float f(mat2 m){return 1.;}float f(vec4 v){return 2.;} void main(){color=vec4((f(mat2(1.))+f(vec4(1.)))/3.);}')
    C('fn_overload_bool_int', 'float f(bool b){return 1.;}float f(int i){return 2.;} void main(){color=vec4((f(true)+f(1))/3.);}')
    C('fn_overload_array_sizes', 'float f(float a[2]){return 1.;}float f(float a[3]){return 2.;} void main(){float x[2];float y[3];color=vec4((f(x)+f(y))/3.);}')
    C('fn_overload_by_return_invalid', 'float f(){return 1.;}int f(){return 1;} void main(){color=vec4(1.);}', ok=False, valid=False)
    C('fn_overload_ambiguous_invalid', 'float f(float a,int b){return 1.;}float f(int a,float b){return 2.;} void main(){color=vec4(f(1,1));}', ok=False, valid=False)
    C('fn_prototype_unused', 'float f(float x);void main(){color=vec4(1.);}')
    C('fn_prototype_called_undefined_invalid', 'float f(float x);void main(){color=vec4(f(1.));}', ok=False, valid=False,
      note='glslang link-level error')
    C('fn_prototype_before_and_def_after_main', 'float f(float x);void main(){color=vec4(f(.5));}float f(float x){return x;}')
    C('fn_prototype_repeated', 'float f(float x);float f(float x);float f(float x){return x;}void main(){color=vec4(f(.5));}')
    C('fn_prototype_qualifier_mismatch_invalid', 'void f(in float x);void f(inout float x){x=1.;}void main(){color=vec4(1.);}', ok=False, valid=False)
    C('fn_use_before_decl_invalid', 'void main(){color=vec4(f());}float f(){return 1.;}', ok=False, valid=False)
    C('fn_definition_order_dependent', 'float a(){return .25;}float b(){return a()*2.;}float c(){return b()*2.;}void main(){color=vec4(c());}')
    C('fn_unnamed_param', 'float f(float,float y){return y;}void main(){color=vec4(f(1.,.5));}')
    C('fn_void_param', 'float f(void){return .5;}void main(void){color=vec4(f());}')
    C('fn_void_return_early', 'void f(inout float x){if(x>.5)return;x=1.;}void main(){float a=.25,b=.75;f(a);f(b);color=vec4(a,b,0.,1.);}')
    C('fn_many_params', 'float f(float a,float b,float c,float d,float e,float g,float h,float i){return a+b+c+d+e+g+h+i;}void main(){color=vec4(f(.1,.1,.1,.1,.1,.1,.1,.1));}')
    C('fn_nested_calls', 'float f(float x){return x*.5;}void main(){color=vec4(f(f(f(f(1.)))));}')
    C('fn_param_modified_locally', 'float f(float x){x*=2.;return x;}void main(){float a=.25;float b=f(a);color=vec4(a,b,0.,1.);}', note='in params are copies')
    C('fn_out_param_uninitialised', 'void f(out float x){x=.5;}void main(){float a;f(a);color=vec4(a);}')
    C('fn_out_param_not_written', 'void f(out float x){}void main(){float a=.5;f(a);color=vec4(0.);}', note='out param left unwritten: value undefined, but must compile')
    C('fn_out_array_elem', 'void f(out float x){x=.5;}void main(){float a[2];f(a[1]);a[0]=0.;color=vec4(a[0],a[1],0.,1.);}')
    C('fn_out_array_dynamic_elem', 'void f(out float x){x=.5;}void main(){float a[2];for(int i=0;i<2;++i)f(a[i]);color=vec4(a[0],a[1],0.,1.);}')
    C('fn_out_struct_field', 'struct S{float x;};void f(out float x){x=.5;}void main(){S s;f(s.x);color=vec4(s.x);}')
    C('fn_out_matrix_column', 'void f(out vec2 x){x=vec2(.5);}void main(){mat2 m=mat2(0.);f(m[1]);color=vec4(m[0],m[1]);}')
    C('fn_out_global', 'float g;void f(out float x){x=.5;g=1.;}void main(){f(g);color=vec4(g);}', note='aliasing a global with an out param')
    C('fn_inout_aliased_args', 'void f(inout float a,inout float b){a=1.;b=2.;}void main(){float x=0.;f(x,x);color=vec4(x/2.);}', note='result is undefined by spec but must compile')
    C('fn_in_arg_expression_with_sideeffect', 'float f(float x){return x;}void main(){float a=.25;float b=f(a++);color=vec4(a,b,0.,1.);}')
    C('fn_const_arg_to_out_invalid', 'void f(out float x){x=1.;}void main(){f(1.);color=vec4(1.);}', ok=False, valid=False)
    C('fn_swizzle_dup_to_out_invalid', 'void f(out vec2 x){x=vec2(1.);}void main(){vec3 v;f(v.xx);color=vec4(v,1.);}', ok=False, valid=False)
    C('fn_returns_struct_member_chain', 'struct S{vec2 v;};S make(){return S(vec2(.25,.5));}void main(){color=vec4(make().v.yx,0.,1.);}')
    C('fn_returns_vec_swizzle_assign_invalid', 'vec2 f(){return vec2(1.);}void main(){f().x=1.;color=vec4(1.);}', ok=False, valid=False)
    C('fn_recursion_direct_invalid', 'float f(float x){return x>0.?f(x-1.):0.;}void main(){color=vec4(f(2.));}', ok=False, valid=False)
    C('fn_recursion_mutual_invalid', 'float b(float x);float a(float x){return b(x);}float b(float x){return a(x);}void main(){color=vec4(a(1.));}', ok=False, valid=False)
    C('fn_main_called_invalid', 'void f(){main();}void main(){color=vec4(1.);}', ok=False, valid=False)
    C('fn_main_with_params_invalid', 'void main(int x){color=vec4(1.);}', ok=False, valid=False)
    C('fn_main_returns_invalid', 'int main(){return 1;}', ok=False, valid=False, decl=DECL)
    C('fn_main_prototype_first', 'void main();void main(){color=vec4(1.);}')
    C('fn_main_prototype_only', 'void main();', ok=False, valid=False, diag='main')
    C('fn_main_not_last', 'void main(){color=vec4(f());}float f(){return 1.;}', ok=False, valid=False)
    C('fn_main_in_middle', 'float a(){return .25;}void main(){color=vec4(a());}float b(){return .5;}')
    C('fn_main_overloaded', 'void main(){color=vec4(1.);}void main(int x){}', ok=False, valid=False)
    C('fn_returns_array_element_chain', 'void main(){float a[2]=float[2](.25,.5);color=vec4(a[int(a[0]*4.)]);}', ok=False)
    C('fn_returns_bool_vec', 'bvec2 f(){return bvec2(true,false);}void main(){color=vec4(f().x?1.:0.,f().y?1.:0.,0.,1.);}')
    C('fn_returns_matrix', 'mat2 f(){return mat2(2.);}void main(){color=vec4(f()[1],f()[0]);}')
    C('fn_returns_int_used_as_float', 'int f(){return 2;}void main(){color=vec4(float(f())/2.);}')
    C('fn_name_same_as_type_in_sksl', 'float half2(float x){return x;}void main(){color=vec4(half2(.5));}')
    C('fn_long_chain_30', ''.join('float f%d(float x){return %s;}' % (i, 'x' if i == 0 else 'f%d(x)+.01' % (i - 1)) for i in range(30)) + 'void main(){color=vec4(f29(0.));}')

    # ------------------------------------------------------------------ I. builtins
    C('bi_scalar_overloads', 'void main(){color=vec4(length(-2.),dot(2.,3.)/6.,distance(1.,3.)/2.,normalize(-2.));}')
    C('bi_vec_scalar_mixed', 'void main(){vec3 v=vec3(1.,2.,3.);color=vec4(mod(v,2.),min(v,2.).x+max(v,2.).y+clamp(v,1.,2.).z);}')
    C('bi_step_smoothstep_scalar_edge', 'void main(){vec3 v=vec3(.2,.5,.8);color=vec4(step(.5,v),smoothstep(0.,1.,v).y);}')
    C('bi_mix_forms', 'void main(){color=vec4(mix(vec3(0.),vec3(1.),.5),mix(0.,1.,.25));}')
    C('bi_mix_bool_select', 'void main(){color=vec4(mix(vec2(0.),vec2(1.),bvec2(true,false)),0.,1.);}', note='ES 3.0 mix(genType,genType,genBType): lowering required')
    C('bi_mix_int_select', 'void main(){color=vec4(mix(ivec2(0),ivec2(1),bvec2(true,false)),0,1);}')
    C('bi_clamp_int', 'void main(){color=vec4(float(clamp(5,0,3))/3.);}')
    C('bi_min_max_int', 'void main(){color=vec4(float(min(2,3)+max(ivec2(1),ivec2(2)).x)/5.);}')
    C('bi_abs_sign_int', 'void main(){color=vec4(float(abs(-2)+sign(-3)+abs(ivec2(-1)).x)/4.);}')
    C('bi_abs_sign_float', 'void main(){color=vec4(abs(-.5),sign(-3.),sign(0.)+1.,1.);}')
    C('bi_rounding_family', 'void main(){color=vec4(floor(1.5),ceil(1.5),round(1.5),trunc(-1.5))/2.;}', note='round/trunc are ES3 builtins')
    C('bi_round_even', 'void main(){color=vec4(roundEven(2.5),roundEven(3.5),0.,1.)/4.;}', note='replaced by an exact SkSL helper')
    C('bi_fract_negative', 'void main(){color=vec4(fract(-.25),fract(-1.75),fract(1.25),1.);}')
    C('bi_mod_negative', 'void main(){color=vec4(mod(-1.,3.),mod(5.,-3.)+3.,mod(-5.5,2.),1.)/3.;}', note='mod = x - y*floor(x/y)')
    C('bi_modf', 'void main(){float i;float f=modf(2.75,i);color=vec4(f,i/4.,0.,1.);}', note='ES3 builtin with out param')
    C('bi_isnan_isinf', 'void main(){float z=0.;color=vec4(isnan(z/z)?1.:0.,isinf(1./z)?1.:0.,0.,1.);}', ok=False, note='ES3 builtins')
    C('bi_exp_log_family', 'void main(){color=vec4(exp2(2.)/4.,log2(8.)/3.,inversesqrt(4.),pow(2.,.5)/2.);}')
    C('bi_pow_vec', 'void main(){color=vec4(pow(vec3(2.),vec3(1.,2.,3.))/8.,1.);}')
    C('bi_trig_inverse', 'void main(){color=vec4(asin(.5),acos(.5),atan(1.),atan(1.,2.));}')
    C('bi_atan2_arg_order', 'void main(){color=vec4(atan(1.,-1.),atan(-1.,-1.),atan(vec2(1.),vec2(2.)).y,1.)/4.;}', note='atan(y,x) quadrant behaviour')
    C('bi_radians_degrees', 'void main(){color=vec4(radians(180.)/3.2,degrees(3.14159)/180.,0.,1.);}')
    C('bi_hyperbolic', 'void main(){color=vec4(sinh(.5),cosh(.5),tanh(.5),1.);}', note='ES3 builtins; SkSL support partial')
    C('bi_inverse_hyperbolic', 'void main(){color=vec4(asinh(.5),acosh(1.5),atanh(.5),1.);}')
    C('bi_faceforward_refract', 'void main(){color=vec4(faceforward(vec3(1.),vec3(0.,0.,1.),vec3(0.,0.,-1.)).x,refract(vec3(0.,0.,-1.),vec3(0.,0.,1.),.5).z,0.,1.);}')
    C('bi_relational_vec_families', 'void main(){vec2 a=vec2(1.,2.),b=vec2(2.,2.);color=vec4(lessThan(a,b).x?1.:0.,lessThanEqual(a,b).y?1.:0.,greaterThan(a,b).x?1.:0.,greaterThanEqual(a,b).y?1.:0.);}')
    C('bi_relational_equal_notequal', 'void main(){color=vec4(equal(ivec2(1),ivec2(1)).x?1.:0.,notEqual(bvec2(true),bvec2(false)).y?1.:0.,equal(vec2(1.),vec2(2.)).x?1.:0.,all(equal(bvec2(true),bvec2(true)))?1.:0.);}')
    C('bi_int_relational', 'void main(){color=vec4(lessThan(ivec3(1),ivec3(2)).z?1.:0.,any(greaterThan(ivec2(1),ivec2(2)))?1.:0.,0.,1.);}')
    C('bi_matrixcompmult_vs_star', 'void main(){mat2 a=mat2(1.,2.,3.,4.);color=vec4(matrixCompMult(a,a)[1],(a*a)[1])/16.;}')
    C('bi_cross_dot_distance', 'void main(){color=vec4(cross(vec3(1.,0.,0.),vec3(0.,0.,1.)),distance(vec2(0.),vec2(3.,4.))/5.);}')
    C('bi_normalize_length', 'void main(){color=vec4(normalize(vec2(3.,4.)),length(vec3(1.,2.,2.))/3.,1.);}')
    C('bi_reflect_refract', 'void main(){color=vec4(reflect(vec2(1.,-1.),vec2(0.,1.)),refract(vec2(0.,-1.),vec2(0.,1.),.5));}')
    C('bi_dfdx_family', 'void main(){float v=position.x;color=vec4(dFdx(v),dFdy(v),fwidth(v),1.);}', decl=UNI, note='derivatives are computed in software from the 2x2 quad')
    C('bi_dfdx_fragcoord', 'void main(){vec2 p=gl_FragCoord.xy*.01;color=vec4(dFdx(p),fwidth(p.x*p.y),1.);}', note='software derivatives of a function of gl_FragCoord')
    C('bi_dfdx_after_pixel_return', 'void main(){float v=gl_FragCoord.x;if(v>4.){color=vec4(1.);return;}color=vec4(dFdx(v));}', ok=False, diag='Unsupported', note='derivative after a pixel-dependent return is undefined in GLSL')
    C('bi_fragcoord_z', 'void main(){color=vec4(gl_FragCoord.z);}', ok=False, diag='Unsupported')
    C('bi_float_bits', 'void main(){color=vec4(float(floatBitsToInt(1.))/1e9);}')
    C('bi_float_bits_uint', 'void main(){color=vec4(uintBitsToFloat(1065353216u));}')
    C('bi_float_bits_vec', 'void main(){uvec3 u=floatBitsToUint(vec3(1.,2.,3.));ivec2 i=floatBitsToInt(gl_FragCoord.xy);color=vec4(uintBitsToFloat(u),float(i.x));}')
    C('bi_float_bits_infinity_const', 'const float INF=uintBitsToFloat(0x7F800000u);void main(){color=vec4(INF>1.?1.:0.);}')
    C('bi_float_bits_infinity_global', 'float inf=uintBitsToFloat(0x7F800000u);void main(){color=vec4(inf>1.?1.:0.);}')
    C('bi_float_bits_infinity_derived', 'const float INF=uintBitsToFloat(0x7F800000u);const float NEG=-INF;void main(){color=vec4(NEG<1.?1.:0.);}')
    C('bi_float_bits_hash', 'float h(uint i){return uintBitsToFloat(i*0x1b74a659u>>9u|0x3f800000u)-1.;}void main(){color=vec4(h(floatBitsToUint(gl_FragCoord.x)));}')
    C('bi_float_bits_const_fold', 'const uint A=floatBitsToUint(1.0);const float B=uintBitsToFloat(0x3FC00000u);void main(){color=vec4(float(A>>23u)/255.,B/2.,0.,1.);}')
    C('bi_pack_unpack', 'void main(){color=vec4(unpackHalf2x16(packHalf2x16(vec2(.5))),0.,1.);}', ok=False)
    C('bi_pack_unorm', 'void main(){color=unpackUnorm4x8(packUnorm4x8(vec4(.5)));}', ok=False)
    C('bi_bit_ops', 'void main(){color=vec4(float(bitCount(7u)+findLSB(8u)+findMSB(8u))/10.);}', ok=False)
    C('bi_bitfield', 'void main(){color=vec4(float(bitfieldExtract(255,0,4))/15.);}', ok=False)
    C('bi_fma_frexp', 'void main(){int e;color=vec4(fma(1.,2.,3.)/5.,frexp(8.,e)/2.,float(e)/4.,1.);}', ok=False, diag='Unsupported',
      note='frexp is valid GLSL ES 3.10+, but SkSL has no bit-level access')
    C('bi_fma', 'void main(){color=vec4(fma(1.,2.,3.)/5.);}', note='replaced by an exact SkSL helper')
    C('bi_texture_without_sampler_invalid', 'void main(){color=texture(t,vec2(.5));}', ok=False, valid=False)
    C('bi_texelfetch', 'void main(){color=texelFetch(t,ivec2(0),0);}', decl='uniform sampler2D t;out vec4 color;')
    C('bi_texture_size', 'void main(){color=vec4(vec2(textureSize(t,0)),0.,1.);}', decl='uniform sampler2D t;out vec4 color;')
    C('bi_texture2d_legacy_invalid', 'void main(){color=texture2D(t,vec2(.5));}', ok=False, valid=False, decl='uniform sampler2D t;out vec4 color;')
    C('bi_sampler_as_param', 'vec4 f(sampler2D s){return texture(s,vec2(.5));}void main(){color=f(t);}', decl='uniform sampler2D t;out vec4 color;')
    C('bi_sampler_array', 'void main(){color=texture(t[0],vec2(.5));}', decl='uniform sampler2D t[2];out vec4 color;')
    C('bi_sampler2dshadow', 'void main(){color=vec4(texture(t,vec3(.5)));}', ok=False, decl='uniform sampler2DShadow t;out vec4 color;')
    C('bi_sampler_2d_array_ms', 'void main(){color=vec4(1.);}', ok=False, decl='uniform sampler2DMS t;out vec4 color;')
    C('bi_gl_FragColor_in_320_invalid', 'void main(){gl_FragColor=vec4(1.);}', ok=False, valid=False, decl='')
    C('bi_gl_FragData_in_320_invalid', 'void main(){gl_FragData[0]=vec4(1.);}', ok=False, valid=False, decl='')
    C('bi_gl_FragCoord_xy', 'void main(){color=vec4(gl_FragCoord.xy/size,0.,1.);}', decl='uniform vec2 size;out vec4 color;',
      note='maps to coord; origin flip (bottom-left GL vs top-left Skia) is a decision')
    C('bi_gl_FragCoord_z', 'void main(){color=vec4(gl_FragCoord.z);}', ok=False)
    C('bi_gl_FragCoord_w', 'void main(){color=vec4(gl_FragCoord.w);}', ok=False)
    C('bi_gl_FragCoord_assign_invalid', 'void main(){gl_FragCoord.x=1.;color=vec4(1.);}', ok=False, valid=False)
    C('bi_gl_FragCoord_in_helper', 'float f(){return gl_FragCoord.x;}void main(){color=vec4(f()/size.x);}', decl='uniform vec2 size;out vec4 color;')
    C('bi_gl_FragCoord_passed_out_invalid', 'void f(out vec4 v){v=vec4(1.);}void main(){f(gl_FragCoord);color=vec4(1.);}', ok=False, valid=False)
    C('bi_gl_PointCoord', 'void main(){color=vec4(gl_PointCoord,0.,1.);}')
    C('bi_gl_PrimitiveID', 'void main(){color=vec4(float(gl_PrimitiveID));}', ok=False)
    C('bi_gl_Layer', 'void main(){color=vec4(float(gl_Layer));}', ok=False)
    C('bi_gl_HelperInvocation', 'void main(){color=vec4(gl_HelperInvocation?1.:0.);}', ok=False)
    C('bi_gl_SamplePosition', 'void main(){color=vec4(gl_SamplePosition,0.,1.);}', ok=False)
    C('bi_gl_max_constants', 'void main(){color=vec4(float(gl_MaxDrawBuffers)/8.);}')
    C('bi_gl_vertex_var_in_fragment_invalid', 'void main(){gl_Position=vec4(1.);color=vec4(1.);}', ok=False, valid=False)
    C('bi_front_facing_in_helper', 'float f(){return gl_FrontFacing?1.:0.;}void main(){color=vec4(f());}')

    # ------------------------------------------------------------------ J. headers, directives, lexing (verbatim)
    BODY = 'out vec4 color;\nvoid main(){color=vec4(1.);}\n'
    R('hdr_version_300_es', '#version 300 es\nprecision highp float;\n' + BODY)
    R('hdr_version_310_es', '#version 310 es\nprecision highp float;\n' + BODY)
    R('hdr_version_330_core', '#version 330 core\nout vec4 color;\nvoid main(){color=vec4(1.);}\n', ok=False)
    R('hdr_version_320_no_es_invalid', '#version 320\nprecision highp float;\n' + BODY, ok=False, valid=False)
    R('hdr_version_320_es_twice_invalid', '#version 320 es\n#version 320 es\nprecision highp float;\n' + BODY, ok=False, valid=False)
    R('hdr_version_after_statement_invalid', 'precision highp float;\n#version 320 es\n' + BODY, ok=False, valid=False)
    R('hdr_version_after_comment', '// leading comment\n/* and block */\n#version 320 es\nprecision highp float;\n' + BODY)
    R('hdr_version_after_blank_lines', '\n\n\n#version 320 es\nprecision highp float;\n' + BODY)
    R('hdr_version_indented', '   \t#version 320 es\nprecision highp float;\n' + BODY)
    R('hdr_version_trailing_comment', '#version 320 es // modern\nprecision highp float;\n' + BODY)
    R('hdr_version_block_comment_inside', '#version /* c */ 320 /* d */ es\nprecision highp float;\n' + BODY)
    R('hdr_version_missing_es_wrong_case_invalid', '#version 320 ES\nprecision highp float;\n' + BODY, ok=False, valid=False)
    R('hdr_version_garbage_number', '#version abc es\nprecision highp float;\n' + BODY, ok=False, valid=False)
    R('hdr_version_absent', 'precision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False,
      note='no #version means GLSL 1.10: out qualifier is invalid there')
    R('hdr_precision_missing_invalid', '#version 320 es\n' + BODY, ok=False, valid=False,
      note='fragment shaders need a default float precision')
    R('hdr_precision_mediump', '#version 320 es\nprecision mediump float;\n' + BODY)
    R('hdr_precision_lowp', '#version 320 es\nprecision lowp float;\n' + BODY)
    R('hdr_precision_redeclared', '#version 320 es\nprecision mediump float;\nprecision highp float;\n' + BODY)
    R('hdr_precision_after_use', '#version 320 es\nprecision mediump float;\nout vec4 color;\nfloat a=1.;\nprecision highp float;\nfloat b=2.;\nvoid main(){color=vec4(a+b);}\n',
      note='default precision changes mid-file: b is highp, a is mediump')
    R('hdr_precision_int_only_invalid', '#version 320 es\nprecision highp int;\n' + BODY, ok=False, valid=False)
    R('hdr_precision_sampler', '#version 320 es\nprecision highp float;\nprecision mediump sampler2D;\n' + BODY)
    R('hdr_precision_inside_function', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){precision mediump float;float a=.5;color=vec4(a);}\n')
    R('pp_define_empty', '#version 320 es\nprecision highp float;\n#define EMPTY\n' + BODY)
    R('pp_define_function_macro', '#version 320 es\nprecision highp float;\n#define SQ(x) ((x)*(x))\nout vec4 color;\nvoid main(){color=vec4(SQ(.5));}\n')
    R('pp_ifdef_gl_es', '#version 320 es\n#ifdef GL_ES\nprecision highp float;\n#endif\n' + BODY,
      note='the most common real-world preprocessor idiom')
    R('pp_if_0', '#version 320 es\nprecision highp float;\n#if 0\nthis is not glsl\n#endif\n' + BODY)
    R('pp_if_version', '#version 320 es\nprecision highp float;\n#if __VERSION__ >= 300\nout vec4 color;\n#endif\nvoid main(){color=vec4(1.);}\n')
    R('pp_line', '#version 320 es\nprecision highp float;\n#line 100\n' + BODY)
    R('pp_error', '#version 320 es\nprecision highp float;\n#error nope\n' + BODY, ok=False, valid=False, diag='#error')
    R('pp_pragma', '#version 320 es\nprecision highp float;\n#pragma optimize(off)\n#pragma STDGL invariant(all)\n' + BODY)
    R('pp_pragma_unknown', '#version 320 es\nprecision highp float;\n#pragma something_unknown(1,2)\n' + BODY)
    R('pp_extension_enable', '#version 320 es\n#extension GL_OES_standard_derivatives : enable\nprecision highp float;\n' + BODY)
    R('pp_extension_require_unknown', '#version 320 es\n#extension GL_FAKE_nothing : require\nprecision highp float;\n' + BODY, ok=False, valid=False, diag='Invalid #extension')
    R('pp_extension_all_warn', '#version 320 es\n#extension all : warn\nprecision highp float;\n' + BODY)
    R('pp_null_directive', '#version 320 es\nprecision highp float;\n#\n' + BODY)
    R('pp_hash_midline_invalid', '#version 320 es\nprecision highp float;\nout vec4 color; # pragma x\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False)
    R('pp_undef', '#version 320 es\nprecision highp float;\n#undef X\n' + BODY)
    R('pp_directive_inside_function', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){\n#define X 1\ncolor=vec4(X);}\n')
    R('pp_directive_with_continuation', '#version 320 es\nprecision highp float;\n#pragma a \\\n b\n' + BODY)
    R('lex_comment_between_plus_plus', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){float a=1.,b=2.;float c=a +/**/+ b;float d=a -/**/- b;color=vec4(c+d);}\n',
      note='comment must separate tokens: `+/**/+` is `+ +`, never `++`')
    R('lex_comment_splits_identifier_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){float ab=1.;color=vec4(a/**/b);}\n', ok=False, valid=False)
    R('lex_comment_in_swizzle', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){vec4 v=vec4(1.,2.,3.,4.);color=v/**/.wzyx/4.;}\n')
    R('lex_comment_between_decl_tokens', '#version 320 es\nprecision highp float;\nout/**/vec4/**/color;\nvoid/**/main/**/(/**/)/**/{/**/color/**/=/**/vec4/**/(/**/1./**/)/**/;/**/}\n')
    R('lex_comment_block_divides', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){float a=4.;float b=a//* not a block comment\n/2.;color=vec4(b);}\n',
      note='`//*` starts a line comment, not a block comment')
    R('lex_comment_divide_vs_comment', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){float a=4.,b=2.;color=vec4(a/ /*c*/b);}\n')
    R('lex_comment_star_slash_in_line', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){ // */ not a terminator\ncolor=vec4(1.);}\n')
    R('lex_comment_block_contains_line', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){/* // */ color=vec4(1.);}\n')
    R('lex_comment_block_stars', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){/***** x **/color=vec4(1.);/**/}\n')
    R('lex_comment_nested_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){/* a /* b */ c */ color=vec4(1.);}\n', ok=False, valid=False)
    R('lex_comment_line_continuation', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);// swallows next line \\\ncolor=vec4(0.);\n}\n',
      note='backslash-newline extends a // comment')
    R('lex_comment_eof_no_newline', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);}\n// trailing')
    R('lex_comment_multiline_block_line_count', '#version 320 es\nprecision highp float;\nout vec4 color;\n/*\n\n\n*/\nvoid main(){color=vec4(1.);}\n')
    R('lex_no_trailing_newline', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);}')
    R('lex_cr_only_newlines', '#version 320 es\rprecision highp float;\rout vec4 color;\rvoid main(){color=vec4(1.);}\r',
      note='lone CR is a newline (also ends // comments)')
    R('lex_cr_only_line_comment', '#version 320 es\rprecision highp float;\rout vec4 color;\rvoid main(){// c\rcolor=vec4(1.);}\r')
    R('lex_mixed_newlines', '#version 320 es\r\nprecision highp float;\nout vec4 color;\rvoid main(){color=vec4(1.);}\r\n')
    R('lex_lf_cr_inverted', '#version 320 es\n\rprecision highp float;\n\rout vec4 color;\n\rvoid main(){color=vec4(1.);}\n\r')
    R('lex_tabs_formfeed_vtab', '#version 320 es\nprecision\thighp\x0cfloat;\nout\x0bvec4\tcolor;\nvoid\x0cmain(){color=vec4(1.);}\n')
    R('lex_one_token_per_line', '#version 320 es\nprecision\nhighp\nfloat\n;\nout\nvec4\ncolor\n;\nvoid\nmain\n(\n)\n{\ncolor\n=\nvec4\n(\n1.\n)\n;\n}\n')
    R('lex_no_whitespace_at_all', '#version 320 es\nprecision highp float;out vec4 color;void main(){color=vec4(1.);}')
    R('lex_utf8_bom_invalid', b'\xef\xbb\xbf#version 320 es\nprecision highp float;\n' + BODY.encode(), ok=False, valid=False,
      note='BOM before #version: compilers differ; transpiler must not crash')
    R('lex_embedded_nul_invalid', b'#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);\x00}\n', ok=False, valid=False,
      note='NUL must not truncate the input silently')
    R('lex_stray_chars_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);@}\n', ok=False, valid=False)
    R('lex_backtick_dollar_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){float $a=1.;color=vec4(1.);}\n', ok=False, valid=False)
    R('lex_string_literal_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);"x";}\n', ok=False, valid=False)
    R('lex_backslash_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.\\);}\n', ok=False, valid=False)
    R('lex_line_continuation_mid_token', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.\\\n5);}\n',
      note='backslash-newline joins tokens: 1.5')
    R('lex_line_continuation_in_identifier', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){float ab\\\ncd=.5;color=vec4(abcd);}\n')
    R('lex_number_dot_member', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.0.xxxx);}\n', ok=False, valid=False)
    R('lex_number_then_ident_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1x);}\n', ok=False, valid=False)
    R('lex_exponent_without_digits_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1e);}\n', ok=False, valid=False)
    R('lex_exponent_signed_forms', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1e+0,1E-0,.1e1,1.e+1)/10.;}\n')
    R('lex_empty_input', '', ok=False, diag='main')
    R('lex_whitespace_only', ' \n\t\n  ', ok=False, diag='main')
    R('lex_only_version', '#version 320 es\n', ok=False, diag='main')
    R('lex_only_comment', '// nothing here\n', ok=False, diag='main')
    R('lex_extra_closing_brace', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.);}}\n', ok=False, valid=False, diag='delimiter')
    R('lex_mismatched_delims', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.];}\n', ok=False, valid=False, diag='delimiter')
    R('lex_unclosed_paren', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.;}\n', ok=False, valid=False, diag='delimiter')
    R('lex_missing_semicolon', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(1.)}\n', ok=False, valid=False)
    R('lex_stmt_at_global_scope', '#version 320 es\nprecision highp float;\nout vec4 color;\ncolor=vec4(1.);\nvoid main(){}\n', ok=False, valid=False)
    R('lex_global_expression_invalid', '#version 320 es\nprecision highp float;\nout vec4 color;\n1.+2.;\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False)
    R('lex_deep_parens_500', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(' + '(' * 500 + '.5' + ')' * 500 + ');}\n', ok=False, diag='Unsupported', note='exceeds the transpiler nesting limit; SkSL also rejects it (max parse depth)')
    R('lex_deep_if_nesting_200', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(0.);' + 'if(true){' * 200 + 'color=vec4(1.);' + '}' * 200 + '}\n', ok=False, diag='Unsupported', note='exceeds the transpiler nesting limit; SkSL also rejects it (max parse depth)')
    R('lex_deep_blocks_1000', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){' + '{' * 1000 + 'color=vec4(1.);' + '}' * 1000 + '}\n', ok=False, diag='Unsupported', note='exceeds the transpiler nesting limit; SkSL also rejects it (max parse depth)')
    R('lex_long_sum_5000_terms', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(' + '+'.join(['.0001'] * 5000) + ');}\n', ok=False, diag='Unsupported', note='exceeds the transpiler nesting limit; SkSL also rejects it (max parse depth)')
    R('lex_right_assoc_ternary_200', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){color=vec4(' + 'true?1.:' * 200 + '0.);}\n', ok=False, diag='Unsupported', note='exceeds the transpiler nesting limit; SkSL also rejects it (max parse depth)')
    R('lex_many_statements_20000', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){float a=0.;' + 'a+=.00001;' * 20000 + 'color=vec4(a);}\n', note='stress; also SkSL program-size limits')
    R('lex_many_functions_2000', '#version 320 es\nprecision highp float;\nout vec4 color;\n' + ''.join('float f%d(float x){return x+.0001;}' % i for i in range(2000)) + 'void main(){color=vec4(f1999(0.));}\n')
    R('lex_many_uniforms_500', '#version 320 es\nprecision highp float;\n' + ''.join('uniform float u%d;' % i for i in range(500)) + 'out vec4 color;\nvoid main(){color=vec4(u499+u0);}\n')
    R('lex_long_line_1mb', '#version 320 es\nprecision highp float;\nout vec4 color;\nvoid main(){' + 'color=vec4(1.);' * 70000 + '}\n', note='~1MB single line')

    # ------------------------------------------------------------------ K. real-shader shapes (combinations)
    C('mix_hash_noise',
      'float hash(vec2 p){p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}'
      'float vnoise(vec2 p){vec2 i=floor(p);vec2 f=fract(p);f=f*f*(3.-2.*f);'
      'return mix(mix(hash(i),hash(i+vec2(1.,0.)),f.x),mix(hash(i+vec2(0.,1.)),hash(i+vec2(1.)),f.x),f.y);}'
      'void main(){color=vec4(vec3(vnoise(position*.1)),1.);}', decl=UNI)
    C('mix_fbm_loop',
      'float hash(vec2 p){return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453);}'
      'float fbm(vec2 p){float v=0.,a=.5;for(int i=0;i<5;++i){v+=a*hash(floor(p));p*=2.;a*=.5;}return v;}'
      'void main(){color=vec4(vec3(fbm(position*.05)),1.);}', decl=UNI)
    C('mix_hsv_to_rgb',
      'vec3 hsv(vec3 c){vec4 K=vec4(1.,2./3.,1./3.,3.);vec3 p=abs(fract(c.xxx+K.xyz)*6.-K.www);return c.z*mix(K.xxx,clamp(p-K.xxx,0.,1.),c.y);}'
      'void main(){color=vec4(hsv(vec3(position.x/size.x,1.,1.)),1.);}', decl=UNI)
    C('mix_cosine_palette',
      'vec3 pal(float t){return .5+.5*cos(6.28318*(vec3(1.)*t+vec3(0.,.33,.67)));}'
      'void main(){color=vec4(pal(position.x/size.x),1.);}', decl=UNI,
      note='float + vec3 broadcast with a leading-dot literal')
    C('mix_rotation_matrix',
      'mat2 rot(float a){float c=cos(a),s=sin(a);return mat2(c,-s,s,c);}'
      'void main(){vec2 p=rot(time)*(position/size-.5);color=vec4(p+.5,0.,1.);}', decl=UNI)
    C('mix_raymarch_sphere',
      'float map(vec3 p){return length(p)-1.;}'
      'void main(){vec2 uv=(position-.5*size)/size.y;vec3 ro=vec3(0.,0.,-3.),rd=normalize(vec3(uv,1.));float t=0.;'
      'for(int i=0;i<32;++i){float d=map(ro+rd*t);if(d<.001)break;t+=d;}'
      'color=vec4(vec3(1.-t/6.),1.);}', decl=UNI)
    C('mix_voronoi_cells',
      'vec2 h2(vec2 p){return fract(sin(vec2(dot(p,vec2(127.1,311.7)),dot(p,vec2(269.5,183.3))))*43758.5453);}'
      'void main(){vec2 p=position*.05;vec2 i=floor(p),f=fract(p);float m=8.;'
      'for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){vec2 g=vec2(float(x),float(y));vec2 o=h2(i+g);m=min(m,length(g+o-f));}'
      'color=vec4(vec3(m),1.);}', decl=UNI)
    C('mix_gaussian_kernel',
      'const float W[5]=float[5](.06,.24,.4,.24,.06);'
      'void main(){float s=0.;for(int i=0;i<5;++i){float x=position.x+float(i-2);s+=W[i]*sin(x*.1);}color=vec4(vec3(s),1.);}', decl=UNI)
    C('mix_struct_light',
      'struct Light{vec3 dir;vec3 col;float k;};'
      'vec3 shade(Light l,vec3 n){return l.col*max(dot(n,normalize(l.dir)),0.)*l.k;}'
      'void main(){Light ls[2];ls[0]=Light(vec3(1.),vec3(1.,.5,.5),1.);ls[1]=Light(vec3(-1.,1.,0.),vec3(.2,.2,1.),.5);'
      'vec3 c=vec3(0.);for(int i=0;i<2;++i)c+=shade(ls[i],vec3(0.,1.,0.));color=vec4(c,1.);}')
    C('mix_srgb_roundtrip',
      'vec3 toLin(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(.04045,c));}'
      'vec3 toSrgb(vec3 c){return mix(c*12.92,1.055*pow(c,vec3(1./2.4))-.055,step(.0031308,c));}'
      'void main(){color=vec4(toSrgb(toLin(vec3(.5))),1.);}')
    C('mix_sdf_boolean_ops',
      'float circle(vec2 p,float r){return length(p)-r;}float box(vec2 p,vec2 b){vec2 d=abs(p)-b;return length(max(d,0.))+min(max(d.x,d.y),0.);}'
      'float smin(float a,float b,float k){float h=clamp(.5+.5*(b-a)/k,0.,1.);return mix(b,a,h)-k*h*(1.-h);}'
      'void main(){vec2 p=position-.5*size;float d=smin(circle(p,40.),box(p-vec2(30.,0.),vec2(30.,20.)),10.);color=vec4(vec3(1.-smoothstep(0.,2.,d)),1.);}', decl=UNI)
    C('mix_premultiplied_alpha', 'void main(){color=vec4(1.,.5,.25,.5);}', note='GL straight alpha vs Skia premultiplied: decision')
    C('mix_bubble_sort_const', 'void main(){float a[5]=float[5](.5,.2,.9,.1,.4);for(int i=0;i<5;++i)for(int j=0;j<4;++j)if(a[j]>a[j+1]){float t=a[j];a[j]=a[j+1];a[j+1]=t;}color=vec4(a[0],a[2],a[4],1.);}')
    C('mix_matrix_chain', 'void main(){mat4 m=mat4(1.);m[3]=vec4(.1,.2,.3,1.);vec4 p=m*m*vec4(0.,0.,0.,1.);color=vec4(p.xyz,1.);}')
    C('mix_int_float_conversion_heavy', 'void main(){int n=int(position.x)%4;float f=float(n)*.25+float(int(position.y)/2)*.01;color=vec4(f);}', decl=UNI)
    C('mix_everything_in_one_expression', 'void main(){color=vec4(mix(vec3(.1,.2,.3).bgr,vec3(min(max(abs(-.5),.1),.9)),clamp(dot(normalize(vec3(1.)),vec3(.5))*2.,0.,1.)),true?1.:0.);}')

    # ------------------------------------------------------------------ L. GLSL ES 1.00 shaders (verbatim)
    E1 = '#version 100\nprecision mediump float;\n'
    R('es100_fragcolor', E1 + 'void main(){gl_FragColor=vec4(.25,.5,.75,1.);}\n')
    R('es100_no_version_fragcolor', 'precision mediump float;\nvoid main(){gl_FragColor=vec4(1.);}\n',
      note='no #version means GLSL ES 1.00')
    R('es100_fragcolor_read_back', E1 + 'void main(){gl_FragColor=vec4(.5);gl_FragColor.rgb*=.5;gl_FragColor.a=1.;}\n')
    R('es100_fragcolor_in_helper', E1 + 'void shade(){gl_FragColor=vec4(.25);}\nvoid main(){shade();}\n')
    R('es100_fragcoord', E1 + 'uniform vec2 size;\nvoid main(){gl_FragColor=vec4(gl_FragCoord.xy/size,0.,1.);}\n')
    # Texture-coordinate input declared vec4 but used as vec2: invalid as written, retried with a vec2 input.
    UV_HEADER = '#version 310 es\nprecision highp float;\nlayout(location = 0) in highp vec4 uv_coords;\nuniform vec2 iResolution;\nout vec4 out_color;\n'
    R('uv_vec4_used_as_vec2', UV_HEADER + 'void main(){ vec2 d = uv_coords * 2.0 - vec2(1.0); out_color = vec4(d, 0.0, 1.0); }\n',
      valid=False, note='vec4 uv_coords used as vec2; accepted via the sema.input-vec2 retry')
    R('uv_vec4_used_as_vec2_and_vec4_invalid', UV_HEADER + 'void main(){ vec2 d = uv_coords - vec2(0.5); out_color = uv_coords; }\n',
      ok=False, valid=False, note='uses the vec4 input whole as well as a vec2; keeps the sema.operator error')
    R('es100_fragdata0', E1 + 'void main(){gl_FragData[0]=vec4(1.);}\n',
      note='gl_FragData[0] alone equals gl_FragColor: it becomes the returned color')
    R('es100_varying', E1 + 'varying vec2 uv;\nvoid main(){gl_FragColor=vec4(uv,0.,1.);}\n')
    R('es100_texture2d', E1 + 'uniform sampler2D t;\nvoid main(){gl_FragColor=texture2D(t,vec2(.5));}\n')
    R('es100_texture_cube_lod_ext', '#extension GL_EXT_shader_texture_lod : enable\nprecision mediump float;\nuniform samplerCube env;\nuniform sampler2D t;\nvoid main(){gl_FragColor=textureCube(env,vec3(1.))+textureCubeLodEXT(env,vec3(1.),1.)+texture2DLodEXT(t,vec2(.5),1.);}\n')
    R('es100_depth_range_front_facing', 'precision mediump float;\nvoid main(){gl_FragColor=vec4(gl_DepthRange.far,gl_FrontFacing?1.:0.,0.,1.);}\n')
    R('es100_dfdx_extension', '#version 100\n#extension GL_OES_standard_derivatives : enable\nprecision mediump float;\nvoid main(){gl_FragColor=vec4(dFdx(gl_FragCoord.x),fwidth(gl_FragCoord.y),0.,1.);}\n')
    R('es100_dfdx_without_extension_invalid', E1 + 'void main(){gl_FragColor=vec4(dFdx(gl_FragCoord.x));}\n', ok=False, valid=False,
      note='derivatives need GL_OES_standard_derivatives in GLSL ES 1.00')
    R('es100_user_fn_named_es3_builtin', E1 + 'float round(float x){return floor(x+.5);}\nfloat sinh(float x){return x;}\nmat2 transpose(mat2 m){return mat2(m[0][0],m[1][0],m[0][1],m[1][1]);}\nvoid main(){mat2 m=transpose(mat2(1.,2.,3.,4.));gl_FragColor=vec4(round(.6),sinh(.5),m[0])/4.;}\n',
      note='not built-ins in GLSL ES 1.00, but SkSL intrinsics: user functions must be renamed')
    R('es100_var_named_es3_builtin', E1 + 'void main(){float round=.5,trunc=.25;gl_FragColor=vec4(round,trunc,0.,1.);}\n')
    R('es100_ident_uint', E1 + 'float uint(float x){return x;}\nvoid main(){float uint2=.25;gl_FragColor=vec4(uint(.5),uint2,0.,1.);}\n',
      note='uint is an identifier in GLSL ES 1.00 but a type in SkSL')
    R('pp_fragment_precision_high', '#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\nvoid main(){gl_FragColor=vec4(1.);}\n',
      note='common real-world idiom')
    R('pp_version_macro_es100', E1 + '#if __VERSION__ == 100\n#define V .25\n#endif\nvoid main(){gl_FragColor=vec4(V);}\n')

    # ------------------------------------------------------------------ M. constructors SkSL spells differently
    C('conv_vec_ctor_partial_last_arg', 'void main(){vec3 a=vec3(vec2(1.,2.),vec2(3.,4.));vec2 b=vec2(1.,vec3(2.,3.,4.));color=vec4(a,b.y)/4.;}',
      note='the last argument is only partly used; SkSL wants exact component counts')
    C('conv_mat_ctor_partial_last_arg', 'void main(){mat2 m=mat2(vec3(1.,2.,3.),vec2(4.,5.));color=vec4(m[0],m[1])/4.;}')
    C('conv_ctor_truncating_args', 'void main(){vec3 a=vec3(mat2(1.,2.,3.,4.));float b=float(vec3(.25,.5,.75));ivec2 c=ivec2(vec4(1.5,2.5,3.5,4.5));bvec2 d=bvec2(vec3(0.,1.,1.));color=vec4(a.z/4.,b,float(c.y)/4.,d.y?1.:0.);}')
    C('mat_from_single_vec4', 'void main(){mat2 m=mat2(vec4(1.,2.,3.,4.));color=vec4(m[0],m[1])/4.;}')
    C('conv_vec_from_mat_call_side_effect', 'float n=0.; mat2 h(){n+=.25;return mat2(n);} void main(){vec3 a=vec3(h());color=vec4(a.x,n,0.,1.);}',
      note='matrix argument with side effects: evaluate once, then take its components')
    C('conv_ctor_mat_arg_side_effect_partial', 'float n=0.; mat3 h(){n+=.25;return mat3(n);} void main(){vec3 a=vec3(.25,h());color=vec4(a.y,float(h()),n,1.);}')
    C('conv_vec_ctor_unused_arg_invalid', 'void main(){vec2 a=vec2(vec2(1.),1.);color=vec4(a,0.,1.);}', ok=False, valid=False)
    C('lit_int_hex_full_bits', 'void main(){int a=0xFFFFFFFF,b=0x80000000;color=vec4(a==-1?1.:0.,b<0?1.:0.,0.,1.);}',
      note='hex literals keep the 32-bit pattern: -1 and INT_MIN')
    C('op_int_overflow_const_fold', 'void main(){int a=2147483647+1;int b=65536*65536;color=vec4(a<0?1.:0.,b==0?1.:0.,0.,1.);}',
      note='GLSL ES 3.00 integer arithmetic wraps')

    # ------------------------------------------------------------------ N. operators and swizzles
    C('swz_on_scalar_variable_invalid', 'void main(){float f=.5;color=f.xxxx;}', ok=False, valid=False,
      note='scalar swizzles are valid SkSL but not GLSL ES')
    C('swz_too_long_invalid', 'void main(){vec2 v=vec2(1.);color=vec4(v.xxxxx.x);}', ok=False, valid=False)
    C('op_vec_relational_invalid', 'void main(){bool b=vec2(1.)<vec2(2.);color=vec4(b?1.:0.);}', ok=False, valid=False)
    C('op_not_bvec_invalid', 'void main(){bvec2 b=!bvec2(true,false);color=vec4(b.x?1.:0.);}', ok=False, valid=False)
    C('op_logical_and_bvec_invalid', 'void main(){bvec2 b=bvec2(true)&&bvec2(false);color=vec4(b.x?1.:0.);}', ok=False, valid=False)
    C('mat_vec_compound_assign', 'void main(){vec2 v=vec2(1.,1.);v*=mat2(1.,2.,3.,4.);color=vec4(v/7.,0.,1.);}', note='v*=m means v=v*m')
    C('mat_div_componentwise', 'void main(){mat2 a=mat2(2.,4.,6.,8.);mat2 b=mat2(2.,2.,2.,2.);mat2 c=a/b;mat2 d=8./a;color=vec4(c[1],d[0])/4.;}')
    C('op_comma_void_left', 'void bump(inout float a){a+=.25;} void main(){float a=0.;float b=(bump(a),a);color=vec4(a,b,0.,1.);}')
    C('op_ternary_matrix', 'void main(){mat2 m=u>0.?mat2(1.):mat2(.5);color=vec4(m[0],m[1]);}', decl='uniform float u;out vec4 color;')
    C('op_swizzle_assign_value', 'void main(){vec2 v=vec2(0.);vec2 r=(v.yx=vec2(.25,.5));color=vec4(v,r);}')

    # ------------------------------------------------------------------ O. functions, names, arrays, structs, scope
    C('fn_redefine_builtin_invalid', 'float sin(float x){return x;} void main(){color=vec4(sin(.5));}', ok=False, valid=False)
    C('fn_overload_builtin_invalid', 'vec2 sin(vec2 a,vec2 b){return a+b;} void main(){color=vec4(sin(vec2(.1),vec2(.2)),0.,1.);}', ok=False, valid=False)
    C('fn_call_shadowed_by_variable_invalid', 'float f(){return 1.;} void main(){float f=2.;color=vec4(f());}', ok=False, valid=False)
    C('arr_out_param', 'void fill(out float a[2]){a[0]=.25;a[1]=.5;} void main(){float x[2];fill(x);color=vec4(x[0],x[1],0.,1.);}')
    C('generated_names_collision', 'float glsl_sinh(float x){return x;} void main(){float tmp1=.1,ret=.1,done=.1,result=.1,quad=.1,fragCoord=.1,coord=.1,v_L1=.1;float v=gl_FragCoord.x;color=vec4(sinh(.5)+glsl_sinh(.1),tmp1+ret+done+result,quad+fragCoord+coord+v_L1,dFdx(v));}',
      note='shader identifiers spelled like generated helper, quad and lane names')
    C('ident_local_named_like_builtin_fn', 'void main(){float sin=.5,dot=.25,mix=.125;color=vec4(sin+dot+mix);}')
    C('ident_param_named_like_builtin_fn', 'float f(float length){return length*.5;} void main(){color=vec4(f(1.));}')
    C('arr_name_style_unsized_init', 'void main(){float a[]=float[](1.,2.,3.);color=vec4(a[0],a[1],a[2],float(a.length()))/3.;}')
    C('arr_of_matrices', 'void main(){mat2 a[2];a[0]=mat2(1.);a[1]=mat2(2.);a[1][0][1]=.5;color=vec4(a[0][1],a[1][0])/2.;}')
    C('struct_anonymous_global', 'struct {float x;vec2 y;} s; void main(){s.x=.5;s.y=vec2(.25);color=vec4(s.x,s.y,1.);}',
      note='SkSL structs need a name')
    C('struct_anonymous_local', 'void main(){struct {float v;} a,b;a.v=.25;b.v=.5;color=vec4(a.v,b.v,0.,1.);}')
    C('struct_embedded_definition_invalid', 'struct B{struct A{float x;} a;}; void main(){color=vec4(1.);}', ok=False, valid=False)
    C('struct_local_shadows_global', 'struct S{float x;}; void main(){S g=S(.25);{struct S{vec2 y;};S l=S(vec2(.5));color=vec4(g.x,l.y,1.);}}')
    C('struct_array_of_struct_member', 'struct A{float v;}; struct B{A a[2];}; void main(){B b=B(A[2](A(.25),A(.5)));b.a[1].v+=.25;color=vec4(b.a[0].v,b.a[1].v,0.,1.);}')
    C('struct_member_of_ctor', 'struct S{float x;vec2 y;}; void main(){color=vec4(S(.25,vec2(.5,.75)).y,S(1.,vec2(0.)).x,1.);}')
    C('struct_compare_with_array_member', 'struct S{float a[2];}; void main(){S x=S(float[2](1.,2.));S y=S(float[2](1.,2.));color=vec4(x==y?1.:0.,x!=y?1.:0.,0.,1.);}')
    C('scope_for_body_redeclares_counter_invalid', 'void main(){float s=0.;for(int i=0;i<2;++i){int i=1;s+=float(i);}color=vec4(s);}', ok=False, valid=False,
      note='a for body does not open a new scope for the init variable')
    C('scope_struct_and_variable_same_name_invalid', 'struct S{float x;}; float S; void main(){color=vec4(1.);}', ok=False, valid=False)
    C('scope_function_and_global_same_name_invalid', 'float f(){return 1.;} float f; void main(){color=vec4(1.);}', ok=False, valid=False)
    C('global_init_from_nonconst_global_invalid', 'float a=1.; float b=a*2.; void main(){color=vec4(b);}', ok=False, valid=False)

    # ------------------------------------------------------------------ P. loops and switch
    C('cf_for_cond_reversed_operands', 'void main(){float a=0.;for(int i=0;4>i;++i)a+=.1;color=vec4(a);}',
      note='4>i is i<4; normalized and accepted')
    C('cf_for_increment_assign_form', 'void main(){float a=0.;for(int i=0;i<4;i=i+1)a+=.1;color=vec4(a);}', ok=False,
      note='i=i+1 is i+=1; refused today, could be normalized')
    C('cf_for_postfix_decrement', 'void main(){float a=0.;for(int i=3;i>=0;i--)a+=.1;color=vec4(a);}')
    C('cf_for_compound_condition', 'void main(){float a=0.;for(int i=0;i<8&&a<u;++i)a+=.1;color=vec4(a);}', decl='uniform float u;out vec4 color;')
    C('cf_for_and_condition_index_first', 'void main(){float t=1.;float r=0.;for(int i=0;i<16&&t<2.;i++){r+=.1;t+=.05;}color=vec4(r);}')
    C('cf_for_and_condition_index_second', 'void main(){float t=0.;for(int i=0;t<time&&i<16;i++){t+=.25;}color=vec4(t);}', decl=UNI)
    C('cf_for_and_condition_side_effect_first', 'float g=0.;bool f(){g+=1.;return g<5.;}void main(){float s=0.;for(int i=0;f()&&i<16;i++){s+=1.;}color=vec4(s/10.,g/10.,0.,1.);}')
    C('cf_while_and_condition', 'void main(){int i=0;float t=0.;while(t<time&&i<10){t+=.5;i++;}color=vec4(t/10.,float(i)/10.,0.,1.);}', decl=UNI)
    C('cf_for_cond_not_counter', 'void main(){float a=0.;for(int i=0;a<.35;++i)a+=.1;color=vec4(a);}', ok=False)
    C('cf_for_counter_passed_inout', 'void skip(inout int i){i+=1;} void main(){float a=0.;for(int i=0;i<8;++i){a+=.1;skip(i);}color=vec4(a);}', ok=False,
      note='index modified through an inout argument')
    C('cf_for_counter_passed_in', 'float w(int i){return float(i)*.1;} void main(){float a=0.;for(int i=0;i<4;++i)a+=w(i);color=vec4(a);}')
    C('cf_for_const_expr_bound', 'void main(){float a=0.;const int n=3;for(int i=0;i<n*2-1;++i)a+=.1;color=vec4(a);}')
    C('cf_switch_stmt_before_case_invalid', 'void main(){float a=0.;switch(count){a=1.;case 0:a=.5;break;}color=vec4(a);}', ok=False, valid=False, decl=UNI)
    C('cf_switch_trailing_label_invalid', 'void main(){float a=0.;switch(count){case 0:a=.5;break;case 1:}color=vec4(a);}', ok=False, valid=False, decl=UNI)
    C('cf_switch_selector_side_effect', 'void main(){float a=0.;int k=count;switch(k++){case 0:a=.5;break;default:a=.25;}color=vec4(a,float(k),0.,1.);}', decl=UNI,
      note='the selector is evaluated once')
    C('cf_switch_fallthrough_into_default_end', 'void main(){float a=0.;switch(count){case 0:a+=.25;default:a+=.5;}color=vec4(a);}', decl=UNI)
    C('cf_return_inside_switch_in_loop', 'float f(){for(int i=0;i<4;++i){switch(i){case 2:return float(i)*.25;default:break;}}return 0.;} void main(){color=vec4(f());}')

    # ------------------------------------------------------------------ Q. software derivatives
    C('bi_dfdx_nested', 'void main(){float v=gl_FragCoord.x*gl_FragCoord.x;color=vec4(dFdx(dFdx(v)),dFdy(dFdx(v)),0.,1.);}',
      note='fine derivatives: a derivative of a derivative is 0 within a quad')
    C('bi_dfdx_arg_with_side_effect', 'float n=0.; float bump(){n+=1.;return n*gl_FragCoord.x;} void main(){float d=dFdx(bump());color=vec4(d,n,0.,1.);}', ok=False,
      note='neighbour values would repeat the side effect')
    C('bi_dfdx_postincrement_arg', 'void main(){float v=gl_FragCoord.x;float d=dFdx(v++);color=vec4(d,v,0.,1.);}', ok=False)
    C('bi_dfdx_in_const_loop', 'void main(){float s=0.;for(int i=0;i<3;++i)s+=dFdx(gl_FragCoord.x*float(i));color=vec4(s);}')
    C('bi_dfdx_in_pixel_dependent_branch', 'void main(){float v=gl_FragCoord.x;color=vec4(0.);if(v>4.)color=vec4(dFdx(v*v));}',
      note='undefined on a GPU; every lane still has v, so the software value is well defined')
    C('bi_dfdx_in_uniform_branch', 'void main(){float v=gl_FragCoord.x;color=vec4(0.);if(u>0.)color=vec4(dFdx(v*v));}', decl='uniform float u;out vec4 color;')
    C('bi_dfdx_of_uniform', 'void main(){color=vec4(dFdx(u),dFdy(u),fwidth(u),1.);}', decl='uniform float u;out vec4 color;')
    C('bi_dfdx_vec4', 'void main(){vec4 p=gl_FragCoord.xyxy*vec4(.1,.2,.3,.4);color=fwidth(p)+dFdy(p);}')
    C('bi_dfdx_in_helper_called_twice', 'float d(float x){return dFdx(x);} void main(){float a=d(gl_FragCoord.x);float b=d(gl_FragCoord.y*2.);color=vec4(a,b,0.,1.);}')
    C('bi_dfdx_of_global_mutated', 'float g=0.; void main(){g=gl_FragCoord.x*.5;g+=gl_FragCoord.y;color=vec4(dFdx(g),dFdy(g),0.,1.);}')
    C('bi_dfdx_after_pixel_dependent_break', 'void main(){float s=0.;for(int i=0;i<4;++i){if(gl_FragCoord.x>float(i)*2.)break;s+=1.;}color=vec4(dFdx(s));}',
      note='control flow reconverges after the loop')
    C('bi_dfdx_after_uniform_return', 'void main(){if(u>1.){color=vec4(1.);return;}color=vec4(dFdx(gl_FragCoord.x));}', decl='uniform float u;out vec4 color;',
      note='contrast with bi_dfdx_after_pixel_return')

    # ------------------------------------------------------------------ R. preprocessor (verbatim)
    P3 = '#version 320 es\nprecision highp float;\nout vec4 color;\n'
    R('pp_ifndef_else', P3 + '#ifndef MISSING\n#define V .25\n#else\n#define V .75\n#endif\nvoid main(){color=vec4(V);}\n')
    R('pp_elif_chain', P3 + '#define MODE 2\n#if MODE == 1\n#define V .1\n#elif MODE == 2\n#define V .2\n#else\n#define V .3\n#endif\nvoid main(){color=vec4(V);}\n')
    R('pp_if_defined_operator', P3 + '#define A\n#if defined(A) && !defined B\n#define V .25\n#else\n#define V .75\n#endif\nvoid main(){color=vec4(V);}\n')
    R('pp_if_arithmetic_nested', P3 + '#if (1+2)*3 == 9\n#if 7/2 == 3 && -1 < 0 && (5 % 3) == 2 && (1 << 3) == 8\n#define V .25\n#endif\n#endif\nvoid main(){color=vec4(V);}\n')
    R('pp_if_undefined_macro_invalid', P3 + '#if UNDEFINED_THING\n#endif\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False,
      note='GLSL ES does not treat an undefined macro in #if as 0')
    R('pp_if_unterminated_invalid', P3 + '#if 1\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False)
    R('pp_else_without_if_invalid', P3 + '#else\n#endif\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False)
    R('pp_define_redefined_invalid', P3 + '#define V .25\n#define V .5\nvoid main(){color=vec4(V);}\n', ok=False, valid=False)
    R('pp_define_identical_redefinition', P3 + '#define V .25\n#define V .25\nvoid main(){color=vec4(V);}\n')
    R('pp_define_self_referential', P3 + '#define a a\nvoid main(){float a=.5;color=vec4(a);}\n', note='a macro is not expanded inside its own expansion')
    R('pp_define_mutually_recursive', P3 + '#define X Y\n#define Y X\nvoid main(){float X=.5;color=vec4(X);}\n')
    R('pp_function_macro_paren_args', P3 + '#define ADD(a,b) ((a)+(b))\nvoid main(){color=vec4(ADD(vec2(.1,.2).x,ADD(.1,max(.1,.2))));}\n',
      note='commas inside nested parentheses do not split macro arguments')
    R('pp_function_macro_name_without_parens', P3 + '#define F(x) (x*2.)\nvoid main(){float F=.25;color=vec4(F);}\n',
      note='a function-like macro name without ( is not expanded')
    R('pp_line_macro_value', P3 + '#line 40\nvoid main(){color=vec4(float(__LINE__)/100.);}\n', note='GLSL ES 3.00: the line after #line 40 is 40')
    R('pp_undef_then_use_invalid', P3 + '#define V .25\n#undef V\nvoid main(){color=vec4(V);}\n', ok=False, valid=False)
    R('pp_undef_builtin_macro_invalid', P3 + '#undef __LINE__\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False)
    R('pp_define_gl_prefix_invalid', P3 + '#define GL_MINE 1\nvoid main(){color=vec4(1.);}\n', ok=False, valid=False)
    R('pp_token_pasting', P3 + '#define CAT(a,b) a##b\nvoid main(){float xy=.5;color=vec4(CAT(x,y));}\n', ok=False)

    # ------------------------------------------------------------------ S. qualifiers and built-ins
    C('qual_invariant_redeclaration', 'invariant color; void main(){color=vec4(1.);}')
    C('qual_sample_in', 'void main(){color=vec4(uv,0.,1.);}', decl='sample in vec2 uv;out vec4 color;')
    C('bi_interpolate_at_centroid', 'void main(){color=vec4(interpolateAtCentroid(uv),0.,1.);}', ok=False, decl='in vec2 uv;out vec4 color;')
    C('bi_texture_lod_family', 'void main(){color=textureLod(t,vec2(.5),0.)+textureProj(t,vec3(.5,.5,1.))+textureGrad(t,vec2(.5),vec2(0.),vec2(0.));}',
      decl='uniform sampler2D t;out vec4 color;')
    C('bi_texture_cube', 'void main(){color=texture(env,vec3(.3,.5,-1.))+textureLod(env,vec3(1.,0.,0.),2.);}', decl='uniform samplerCube env;out vec4 color;')
    C('bi_texture_3d', 'void main(){color=texture(vol,vec3(.5))+texelFetch(vol,ivec3(1),0);}', decl='uniform highp sampler3D vol; // @wrap{clamp}\nout vec4 color;')
    C('bi_texture_2d_array', 'void main(){color=texture(layers,vec3(.5,.5,1.));}', decl='uniform highp sampler2DArray layers;out vec4 color;')
    C('bi_texture_gather', 'void main(){color=textureGather(t,vec2(.5),1)+textureOffset(t,vec2(.5),ivec2(1,0));}', decl='uniform sampler2D t;out vec4 color;')
    C('bi_sampler_array_dynamic', 'vec4 f(sampler2D s){return texture(s,vec2(.5));}void main(){color=texture(t[k],vec2(.5))+f(t[k]);}', decl='uniform sampler2D t[3];uniform int k;out vec4 color;')
    C('cf_discard_with_derivative', 'void main(){float d=dFdx(gl_FragCoord.x);if(d>0.)discard;color=vec4(d);}')
    C('bi_ldexp', 'void main(){color=vec4(ldexp(.5,2)/2.);}', ok=False)
    C('bi_bitfield_insert_reverse', 'void main(){color=vec4(float(bitfieldInsert(0,3,2,2)+bitfieldReverse(1))/16.);}', ok=False)
    # Identifiers SkSL reserves or gives to intrinsics are renamed and listed in Result::renamedIdentifiers.
    RH = '#version 300 es\r\nprecision highp float;\r\n'
    R('rename_intrinsic_uniform_collision', RH + 'uniform float step; uniform float step_; out vec4 c; void main(){c=vec4(step+step_+abs(-1.));}')
    R('rename_intrinsic_sampler', RH + 'uniform sampler2D step;out vec4 c;void main(){c=texture(step,vec2(.5));}')
    R('rename_block_field', RH + 'uniform B { float step; } block;out vec4 c;void main(){c=vec4(block.step);}')
    R('rename_reserved_uniform', RH + 'uniform float float2;out vec4 c;void main(){c=vec4(float2);}')
    C('bi_mat4_inverse_determinant', 'void main(){mat4 m=mat4(2.);m[3]=vec4(1.,2.,3.,1.);mat4 i=inverse(m);color=vec4(i[3].xyz/-2.,determinant(m)/16.);}')

    # ------------------------------------------------------------------ T. limits of Skia's compiler (found in Shadertoy shaders)
    U1 = 'uniform float u;out vec4 color;'
    C('skia_const_helper_tanh', 'void main(){const float f=2./tanh(1.);color=vec4(f);}',
      note='a generated helper call is not a constant expression in SkSL: the const initializer is folded')
    C('skia_const_helper_int_max', 'void main(){const int a=5;const int b=max(a-2,0);color=vec4(float(b));}',
      note='int max is a generated helper; folded in a const initializer')
    C('skia_const_transpose', 'void main(){const float a=.5;const mat2 r=mat2(cos(a),sin(a),-sin(a),cos(a));const vec2 p=vec2(1.)*transpose(r);color=vec4(p,0.,1.);}',
      note='transpose of a const matrix is folded')
    C('skia_index_remainder', 'void main(){float v[3]=float[3](.1,.2,.3);float s=0.;for(int i=0;i<3;i++)s+=v[(i+1)%3];color=vec4(s);}',
      note='a remainder in an index must stay an operator expression for Skia')
    C('skia_long_sum_chain', 'void main(){color=vec4(' + '+'.join(['u'] * 60) + ');}', decl=U1,
      note='Skia limits parse depth to 50; the left part of the chain moves into temporaries')
    C('skia_nested_calls', 'void main(){color=vec4(' + 'min(' * 30 + 'u' + ',u)' * 30 + ');}', decl=U1,
      note='each nested call costs two levels of Skia parse depth')
    C('skia_else_if_chain', 'void main(){float c=0.;if(u==0.)c=1.;' + ''.join(f'else if(u=={k}.)c={k}.;' for k in range(1, 40)) + 'color=vec4(c);}', decl=U1,
      note='else if stays flat instead of nesting a block per branch')
    C('skia_struct_out_unassigned', 'struct L{float a;float b[2];};bool f(L i,out L o){return false;}void main(){L x=L(1.,float[2](2.,3.));bool b=f(x,x);color=vec4(b?1.:x.a);}',
      note='Skia rejects an out parameter that is never assigned; structs with arrays are zeroed field by field')
    C('skia_const_zero_division', 'void main(){const float a=1.;color=vec4((a-a)/(a-a),1./(a-a),0.,1.);}',
      note='Skia rejects division by a constant zero; the divisor gets a run-time zero')
    C('skia_loop_over_limit', 'void main(){float s=0.;for(int i=0;i<100000;i++){s+=u;if(s>1.)break;}color=vec4(s);}', decl=U1,
      note='Skia allows fewer than 100000 iterations per loop: two nested loops')

    # ------------------------------------------------------------------ U. ranges of locals and parameters
    C('range_param_bound', 'float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.1;return s;}void main(){color=vec4(f(3),f(5),0.,1.);}',
      note='the bound is a parameter: its range is that of the arguments of every call')
    C('range_param_bound_from_uniform', 'uniform int u;float f(int n){float s=0.;for(int i=0;i<n;i++)s+=.1;return s;}void main(){color=vec4(f(u));}',
      ok=False, note='a call passes an unannotated uniform, so the bound has no known range')
    C('range_local_bound', 'void main(){int n=3;if(gl_FragCoord.x>100.)n=6;float s=0.;for(int i=0;i<n;i++)s+=.1;color=vec4(s);}',
      note='a local bound ranges over every value assigned to it')
    C('range_index_local_from_loop', 'void main(){float a[6]=float[6](.1,.2,.3,.4,.5,.6);float s=0.;for(int i=0;i<3;i++){int j=i*2+1;s+=a[j];}color=vec4(s);}',
      note='a local initialized from the loop index is replaced by its initializer in the index')
    C('range_index_reassigned', 'uniform int u;void main(){float a[3]=float[3](.1,.2,.3);int k=0;k+=u;color=vec4(a[k]);}',
      ok=False, note='k changes by +=, so it has neither a known value nor a substitutable initializer')
