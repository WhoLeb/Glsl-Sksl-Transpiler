#include "language.hpp"
#include <cfloat>
#include <charconv>
#include <cmath>
#include <set>
#include <sstream>
namespace glsl_sksl {
namespace {
using Words=std::set<std::string,std::less<>>;
Words split(const char* text) {
    Words words; std::istringstream in(text); std::string w;
    while(in>>w)words.insert(w);
    return words;
}
Words join(Words a, const Words& b) { a.insert(b.begin(),b.end()); return a; }
Words without(Words a, const Words& b) { for(const auto& w:b)a.erase(w); return a; }
struct Tables { Words keywords, reserved; };
// Keyword and reserved-word lists from section 3.7 (3.8 in 1.00) of each GLSL ES specification.
const Tables& tables(int version) {
    static const Tables es100{split(
        "attribute const uniform varying break continue do for while if else in out inout float int void bool true false "
        "lowp mediump highp precision invariant discard return mat2 mat3 mat4 vec2 vec3 vec4 ivec2 ivec3 ivec4 "
        "bvec2 bvec3 bvec4 sampler2D samplerCube struct"),split(
        "asm class union enum typedef template this packed goto switch default inline noinline volatile public static "
        "extern external interface flat long short double half fixed unsigned superp input output hvec2 hvec3 hvec4 "
        "dvec2 dvec3 dvec4 fvec2 fvec3 fvec4 sampler1D sampler3D sampler1DShadow sampler2DShadow sampler2DRect "
        "sampler3DRect sampler2DRectShadow sizeof cast namespace using")};
    static const Words keywords300=split(
        "const uniform layout centroid flat smooth break continue do for while switch case default if else in out inout "
        "float int void bool true false invariant discard return mat2 mat3 mat4 mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 "
        "mat3x4 mat4x2 mat4x3 mat4x4 vec2 vec3 vec4 ivec2 ivec3 ivec4 bvec2 bvec3 bvec4 uint uvec2 uvec3 uvec4 lowp "
        "mediump highp precision sampler2D sampler3D samplerCube sampler2DShadow samplerCubeShadow sampler2DArray "
        "sampler2DArrayShadow isampler2D isampler3D isamplerCube isampler2DArray usampler2D usampler3D usamplerCube "
        "usampler2DArray struct");
    static const Words reserved300=split(
        "attribute varying coherent volatile restrict readonly writeonly resource atomic_uint noperspective patch sample "
        "subroutine common partition active asm class union enum typedef template this goto inline noinline public "
        "static extern external interface long short double half fixed unsigned superp input output hvec2 hvec3 hvec4 "
        "dvec2 dvec3 dvec4 fvec2 fvec3 fvec4 sampler3DRect filter image1D image2D image3D imageCube iimage1D iimage2D "
        "iimage3D iimageCube uimage1D uimage2D uimage3D uimageCube image1DArray image2DArray iimage1DArray "
        "iimage2DArray uimage1DArray uimage2DArray imageBuffer iimageBuffer uimageBuffer sampler1D sampler1DShadow "
        "sampler1DArray sampler1DArrayShadow isampler1D isampler1DArray usampler1D usampler1DArray sampler2DRect "
        "sampler2DRectShadow isampler2DRect usampler2DRect samplerBuffer isamplerBuffer usamplerBuffer sampler2DMS "
        "isampler2DMS usampler2DMS sampler2DMSArray isampler2DMSArray usampler2DMSArray samplerCubeArray "
        "samplerCubeArrayShadow isamplerCubeArray usamplerCubeArray imageCubeArray iimageCubeArray uimageCubeArray "
        "sizeof cast namespace using");
    static const Words added310=split(
        "buffer shared coherent volatile restrict readonly writeonly atomic_uint sampler2DMS isampler2DMS usampler2DMS "
        "image2D iimage2D uimage2D image3D iimage3D uimage3D imageCube iimageCube uimageCube image2DArray "
        "iimage2DArray uimage2DArray");
    static const Words added320=split(
        "patch sample precise samplerBuffer isamplerBuffer usamplerBuffer imageBuffer iimageBuffer uimageBuffer "
        "samplerCubeArray samplerCubeArrayShadow isamplerCubeArray usamplerCubeArray imageCubeArray iimageCubeArray "
        "uimageCubeArray sampler2DMSArray isampler2DMSArray usampler2DMSArray");
    static const Tables es300{keywords300,reserved300};
    static const Tables es310{join(keywords300,added310),without(reserved300,added310)};
    static const Tables es320{join(es310.keywords,added320),without(es310.reserved,added320)};
    return version>=320?es320:version>=310?es310:version>=300?es300:es100;
}
}

WordClass classifyWord(std::string_view word, int version) {
    const auto& t=tables(version);
    if(t.keywords.count(word))return WordClass::Keyword;
    if(t.reserved.count(word))return WordClass::Reserved;
    return WordClass::Identifier;
}

bool isBuiltinTypeName(std::string_view word) {
    static const Words types=split(
        "void bool int uint float vec2 vec3 vec4 bvec2 bvec3 bvec4 ivec2 ivec3 ivec4 uvec2 uvec3 uvec4 mat2 mat3 mat4 "
        "mat2x2 mat2x3 mat2x4 mat3x2 mat3x3 mat3x4 mat4x2 mat4x3 mat4x4 sampler2D sampler3D samplerCube "
        "sampler2DShadow samplerCubeShadow sampler2DArray sampler2DArrayShadow isampler2D isampler3D isamplerCube "
        "isampler2DArray usampler2D usampler3D usamplerCube usampler2DArray sampler2DMS isampler2DMS usampler2DMS "
        "samplerBuffer isamplerBuffer usamplerBuffer samplerCubeArray samplerCubeArrayShadow isamplerCubeArray "
        "usamplerCubeArray sampler2DMSArray isampler2DMSArray usampler2DMSArray image2D iimage2D uimage2D image3D "
        "iimage3D uimage3D imageCube iimageCube uimageCube image2DArray iimage2DArray uimage2DArray imageBuffer "
        "iimageBuffer uimageBuffer imageCubeArray iimageCubeArray uimageCubeArray atomic_uint");
    return types.count(word)!=0;
}

bool isQualifierKeyword(std::string_view word) {
    static const Words qualifiers=split(
        "const in out inout uniform buffer shared attribute varying centroid flat smooth noperspective patch sample "
        "invariant precise coherent volatile restrict readonly writeonly lowp mediump highp layout");
    return qualifiers.count(word)!=0;
}

bool isKnownExtension(std::string_view name) {
    static const Words extensions=split(
        "GL_OES_standard_derivatives GL_EXT_shader_texture_lod GL_EXT_frag_depth GL_EXT_draw_buffers "
        "GL_OES_EGL_image_external GL_OES_EGL_image_external_essl3 GL_EXT_shader_framebuffer_fetch "
        "GL_EXT_shader_framebuffer_fetch_non_coherent GL_ARM_shader_framebuffer_fetch GL_EXT_blend_func_extended "
        "GL_OES_sample_variables GL_OES_shader_multisample_interpolation GL_OES_shader_image_atomic GL_EXT_gpu_shader5 "
        "GL_OES_gpu_shader5 GL_OES_texture_3D GL_EXT_shadow_samplers GL_KHR_blend_equation_advanced "
        "GL_EXT_texture_buffer GL_OES_texture_buffer GL_EXT_texture_cube_map_array GL_OES_texture_cube_map_array "
        "GL_EXT_geometry_shader GL_EXT_tessellation_shader GL_EXT_clip_cull_distance GL_EXT_YUV_target "
        "GL_OES_texture_storage_multisample_2d_array GL_NV_shader_noperspective_interpolation GL_OVR_multiview "
        "GL_OVR_multiview2 GL_EXT_shader_io_blocks GL_OES_shader_io_blocks GL_EXT_primitive_bounding_box "
        "GL_EXT_separate_shader_objects GL_EXT_multisampled_render_to_texture GL_EXT_shader_non_constant_global_initializers "
        "GL_ANGLE_multi_draw GL_EXT_conservative_depth GL_NV_EGL_stream_consumer_external");
    return extensions.count(name)!=0;
}

std::string versionName(int version) {
    switch(version) {
    case 300: return "GLSL ES 3.00";
    case 310: return "GLSL ES 3.10";
    case 320: return "GLSL ES 3.20";
    default: return "GLSL ES 1.00";
    }
}

NumberLiteral parseNumber(std::string_view s, int version) {
    NumberLiteral n;
    const std::string text(s);
    auto fail=[&](std::string message){n.kind=NumberLiteral::Invalid;n.message=std::move(message);return n;};
    auto digit=[&](std::size_t i){return i<s.size() && s[i]>='0' && s[i]<='9';};
    auto integerSuffix=[&](std::string_view rest)->bool {
        if(rest.empty())return true;
        if(rest=="u"||rest=="U") {
            if(version<300){fail("Invalid unsigned literal '"+text+"': the 'u' suffix requires GLSL ES 3.00");return false;}
            n.kind=NumberLiteral::Uint;return true;
        }
        fail("Invalid numeric literal '"+text+"'");return false;
    };
    if(s.size()>1 && s[0]=='0' && (s[1]=='x'||s[1]=='X')) {
        std::size_t i=2; std::uint64_t value=0; bool big=false;
        auto hex=[&](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
        for(;i<s.size() && hex(s[i])>=0;++i){value=value*16+static_cast<unsigned>(hex(s[i]));if(value>0xFFFFFFFFull)big=true;}
        if(i==2)return fail("Invalid hexadecimal literal '"+text+"': no digits after 0x");
        n.kind=NumberLiteral::Int;
        if(!integerSuffix(s.substr(i)))return n;
        if(big)return fail("Invalid integer literal '"+text+"': it does not fit in 32 bits");
        n.intValue=value;n.spelling=std::string(s.substr(0,i));
        return n;
    }
    std::size_t i=0;
    while(digit(i))++i;
    const std::size_t whole=i;
    bool dot=false, exponent=false;
    std::size_t fraction=0;
    if(i<s.size() && s[i]=='.') {dot=true;++i;while(digit(i)){++i;++fraction;}}
    if(whole+fraction==0)return fail("Invalid numeric literal '"+text+"'");
    if(i<s.size() && (s[i]=='e'||s[i]=='E')) {
        exponent=true;++i;
        if(i<s.size() && (s[i]=='+'||s[i]=='-'))++i;
        std::size_t digits=0;
        while(digit(i)){++i;++digits;}
        if(!digits)return fail("Invalid floating-point literal '"+text+"': the exponent has no digits");
    }
    if(!dot && !exponent) {
        n.kind=NumberLiteral::Int;
        if(!integerSuffix(s.substr(whole)))return n;
        const bool octal=whole>1 && s[0]=='0';
        std::uint64_t value=0; bool big=false;
        for(std::size_t k=0;k<whole;++k) {
            const unsigned d=static_cast<unsigned>(s[k]-'0');
            if(octal && d>7)return fail("Invalid octal literal '"+text+"': '"+std::string(1,s[k])+"' is not an octal digit");
            value=value*(octal?8:10)+d;
            if(value>0xFFFFFFFFull)big=true;
        }
        if(big)return fail("Invalid integer literal '"+text+"': it does not fit in 32 bits");
        n.intValue=value;n.spelling=std::string(s.substr(0,whole));
        return n;
    }
    const std::string_view rest=s.substr(i);
    if(rest=="lf"||rest=="LF")return fail("Invalid floating-point literal '"+text+"': double precision ('lf') does not exist in GLSL ES");
    if(rest=="f"||rest=="F") {
        if(version<300)return fail("Invalid floating-point literal '"+text+"': the 'f' suffix requires GLSL ES 3.00");
    } else if(!rest.empty()) return fail("Invalid numeric literal '"+text+"'");
    n.kind=NumberLiteral::Float;
    n.spelling=std::string(s.substr(0,i));
    double value=0;
    const auto parsed=std::from_chars(n.spelling.data(),n.spelling.data()+n.spelling.size(),value);
    if(parsed.ec==std::errc::result_out_of_range) {
        const auto e=n.spelling.find_first_of("eE");
        value=e!=std::string::npos && n.spelling.find('-',e)!=std::string::npos?0.0:HUGE_VAL;
    }
    n.floatValue=value;
    n.overflow=!(value<=FLT_MAX);
    return n;
}
}
