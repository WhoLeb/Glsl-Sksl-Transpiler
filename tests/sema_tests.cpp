#include "parser.hpp"
#include "sema.hpp"
#include "glsl_sksl/transpiler.hpp"
#include <iostream>
#include <string>
#include <utility>
#include <vector>
using namespace glsl_sksl;
namespace {
int failures=0;
void check(bool value, const char* what, int line) {
    if(value)return;
    ++failures;
    std::cerr<<"sema_tests.cpp:"<<line<<": failed: "<<what<<"\n";
}
#define CHECK(x) check((x),#x,__LINE__)

const std::string P="#version 300 es\nprecision highp float;\nout vec4 color;\n";

// Parses the source and, when parsing succeeded, runs semantic analysis into the same sink.
DiagnosticSink analyzeSource(const std::string& source) {
    DiagnosticSink sink;
    auto shader=parseShader(source,sink);
    if(shader.unit && !sink.errors)sema::analyze(*shader.unit,sink);
    else if(!shader.unit && !sink.errors)sink.error({},"internal","parser produced no translation unit");
    return sink;
}
void printDiagnostics(const std::string& source, const DiagnosticSink& sink) {
    std::cerr<<"  source: "<<source<<"\n  diagnostics:";
    for(const auto& d:sink.list)
        std::cerr<<" ["<<(d.severity==Severity::Error?"error":"warning")<<" "<<d.code<<"] "<<d.line<<":"<<d.column<<" "<<d.message<<";";
    std::cerr<<"\n";
}
// True when analysis produces no Error diagnostics.
bool ok(const std::string& source) {
    auto sink=analyzeSource(source);
    for(const auto& d:sink.list)if(d.severity==Severity::Error){
        std::cerr<<"  expected no errors\n";
        printDiagnostics(source,sink);
        return false;
    }
    return true;
}
// True when some Error diagnostic has exactly this code.
bool error(const std::string& source, const std::string& code) {
    auto sink=analyzeSource(source);
    for(const auto& d:sink.list)if(d.severity==Severity::Error && d.code==code)return true;
    std::cerr<<"  expected error "<<code<<"\n";
    printDiagnostics(source,sink);
    return false;
}
// True when there are no errors but some Warning has this code.
bool warns(const std::string& source, const std::string& code) {
    auto sink=analyzeSource(source);
    bool found=false;
    for(const auto& d:sink.list){
        if(d.severity==Severity::Error){printDiagnostics(source,sink);return false;}
        if(d.code==code)found=true;
    }
    if(!found){
        std::cerr<<"  expected warning "<<code<<"\n";
        printDiagnostics(source,sink);
    }
    return found;
}

void mustBeOk() {
    const std::vector<std::string> cases={
        P+"void main(){color=vec4(1.);}",
        P+"void main(){vec3 a=vec3(1.); vec4 b=vec4(a,1.); mat2 m=mat2(1.,2.,3.,4.); vec2 v=m*vec2(1.); color=vec4(v,b.x,1.);}",
        P+"float f(float x); void main(){color=vec4(f(1.));} float f(float x){return x;}",
        P+"struct S{float x; vec2 y;}; void main(){S s=S(1.,vec2(2.)); color=vec4(s.x,s.y,1.);}",
        P+"const int N=3; float a[N]; void main(){color=vec4(float(a.length()));}",
        P+"void main(){int i=7/2; float f=float(i); color=vec4(f);}",
        P+"void main(){for(int i=0;i<4;++i){color=vec4(float(i));}}",
        P+"void main(){vec4 c=vec4(1.); c.xy=vec2(0.); color=c.rgba;}",
        P+"void main(){int x=1; switch(x){case 0: color=vec4(0.); break; default: color=vec4(1.);}}",
        P+"float a[3]=float[3](1.,2.,3.); void main(){color=vec4(a[2]);}",
        P+"void main(){float a[]=float[](1.,2.); color=vec4(a[1]);}",
        "#version 100\nprecision mediump float;\nvoid main(){gl_FragColor=vec4(1.);}",
        P+"int f(int x){return x+1;} void main(){color=vec4(float(f(1)));}",
        P+"void main(){color=vec4(mat2(1.));}",
        P+"void main(){vec4 v=vec4(1.); int n=v.length(); color=vec4(float(n));}",
        P+"void main(){float a[3]; for(int i=0;i<3;++i){a[i]=1.;} color=vec4(a[0]);}",
        P+"void main(){int x=7%2; color=vec4(float(x));}",
        P+"void main(){for(int i=0;i<3;i+=2){} color=vec4(1.);}",
        P+"void main(){float a,b; a=b=1.; color=vec4(true?a:b);}",
        P+"void f(out float x){x=1.;} void main(){float y; f(y); color=vec4(y);}",
        // Full sources: 3.10 allows any qualifier order.
        "#version 310 es\nprecision highp float;\nout vec4 color;\nhighp uniform float b; void main(){color=vec4(b);}",
    };
    for(const auto& s:cases)CHECK(ok(s));
}

void mustReportError() {
    const std::vector<std::pair<std::string,std::string>> cases={
        {P+"void main(){float x=1;color=vec4(x);}","sema.type-mismatch"},
        {P+"void main(){color=vec4(1.)+vec3(1.);}","sema.operator"},
        {P+"void main(){int i=1; float f=i*2.0; color=vec4(f);}","sema.operator"},
        {P+"void main(){color=vec4(1.,2.,3.,4.,5.);}","sema.constructor"},
        {P+"void main(){color=vec4(1.,2.);}","sema.constructor"},
        {P+"void main(){color=y;}","sema.undeclared"},
        {P+"void main(){color=vec4(f(1.));}","sema.undeclared"},
        {P+"float f(float x){return x;} void main(){color=vec4(f(1));}","sema.no-overload"},
        {P+"void main(){const float c; color=vec4(c);}","sema.const-initializer"},
        {P+"uniform float u; void main(){const float c=u; color=vec4(c);}","sema.const-initializer"},
        {P+"uniform float u; void main(){u=1.; color=vec4(u);}","sema.not-lvalue"},
        {P+"void main(){vec4 v; v.xx=vec2(1.); color=v;}","sema.not-lvalue"},
        {P+"void main(){vec2 v; color=vec4(v.xyz,1.);}","sema.swizzle"},
        {P+"void main(){vec4 v; color=vec4(v.xg,v.zw);}","sema.swizzle"},
        {P+"void main(){float a[2]; color=vec4(a[2]);}","sema.index"},
        {P+"void main(){float a[2]; color=vec4(a[-1]);}","sema.index"},
        {P+"void main(){float a[0]; color=vec4(1.);}","sema.array-size"},
        {P+"void main(){float a[1.5]; color=vec4(1.);}","sema.array-size"},
        {P+"void main(){if(1){} color=vec4(1.);}","sema.condition"},
        {P+"void main(){float x=1.; switch(x){default: break;} color=vec4(1.);}","sema.switch"},
        {P+"void main(){int x=0; switch(x){case 1: break; case 1: break;} color=vec4(1.);}","sema.case"},
        {P+"void main(){int x=0; switch(x){case 1: break; case 2:} color=vec4(1.);}","sema.switch"},
        {P+"void main(){int x=0; switch(x){x=1; case 1: break;} color=vec4(1.);}","sema.switch"},
        {P+"void main(){return 1;}","sema.return"},
        {P+"float f(){return;} void main(){color=vec4(f());}","sema.return"},
        {P+"float f(){return 1;} void main(){color=vec4(f());}","sema.return"},
        {P+"float f(float x){return f(x);} void main(){color=vec4(f(1.));}","sema.recursion"},
        {P+"float f(float x); float g(float x){return f(x);} float f(float x){return g(x);} void main(){color=vec4(f(1.));}","sema.recursion"},
        {P+"float f(float x); void main(){color=vec4(f(1.));}","sema.undefined-function"},
        {P+"void main(){} void main(){}","sema.redefinition"},
        {P+"void main(int x){color=vec4(1.);}","sema.main"},
        {P+"float main(){return 1.;}","sema.main"},
        {P+"void main(){color=vec4(1.);} void f(){main();}","sema.main"},
        {P+"void helper(){}","sema.missing-main"},
        {P+"float f(float x); int f(float x){return 1;} void main(){color=vec4(1.);}","sema.overload"},
        {P+"float f(in float x); float f(out float x){x=1.;return x;} void main(){color=vec4(1.);}","sema.overload"},
        {P+"float sin(float x){return x;} void main(){color=vec4(1.);}","sema.builtin-redefinition"},
        {P+"void main(){int x; int x; color=vec4(1.);}","sema.redefinition"},
        {P+"void f(float a){float a;} void main(){color=vec4(1.);}","sema.redefinition"},
        {P+"void main(){for(int i=0;i<2;++i){} color=vec4(float(i));}","sema.undeclared"},
        {P+"void f(const float x){x=1.;} void main(){color=vec4(1.);}","sema.not-lvalue"},
        {P+"void f(out float x){x=1.;} void main(){f(1.); color=vec4(1.);}","sema.not-lvalue"},
        {P+"void f(out vec2 x){x=vec2(1.);} void main(){vec4 v; f(v.xx); color=v;}","sema.not-lvalue"},
        {P+"uniform float u=1.; void main(){color=vec4(u);}","sema.initializer"},
        {P+"uniform float u; float g=u; void main(){color=vec4(g);}","sema.global-initializer"},
        {P+"inout float g; void main(){color=vec4(1.);}","sema.qualifier"},
        {P+"void main(){uniform float x; color=vec4(1.);}","sema.qualifier"},
        {P+"void f(const out float x){x=1.;} void main(){color=vec4(1.);}","sema.qualifier"},
        {P+"highp uniform float b; void main(){color=vec4(b);}","sema.qualifier-order"},
        {"#version 310 es\nprecision highp float;\nout vec4 color;\ncoherent uniform float v; void main(){color=vec4(v);}","sema.qualifier"},
        {P+"void main(){(true?color:color)=vec4(1.);}","sema.not-lvalue"},
        {P+"void main(){bool b=1.<2.<3.; color=vec4(1.);}","sema.operator"},
        {P+"void main(){float x=5.%2.; color=vec4(x);}","sema.operator"},
        {P+"struct S{float x;}; void main(){S s=S(1); color=vec4(s.x);}","sema.constructor"},
        {P+"struct S{vec2 v;}; void main(){S s=S(1.); color=vec4(1.);}","sema.constructor"},
        {P+"struct S{float x;}; void main(){S s; color=vec4(s.y);}","sema.field"},
        {P+"struct S{float x;}; struct S{float y;}; void main(){color=vec4(1.);}","sema.redefinition"},
        {P+"void main(){float f=1.; color=vec4(f.x);}","sema.field"},
        {P+"void main(){gl_FragColor=vec4(1.);}","sema.undeclared"},
        {P+"void main(){gl_FragCoord=vec4(1.); color=vec4(1.);}","sema.not-lvalue"},
        {P+"uniform sampler2D s; void main(){color=texture2D(s,vec2(0.));}","sema.undeclared"},
        {P+"invariant uniform float u; void main(){color=vec4(u);}","sema.qualifier"},
        // Full sources.
        {"#version 300 es\nprecision highp float;\nout vec4 a; out vec4 b; void main(){a=b=vec4(1.);}","sema.output-location"},
        {"#version 300 es\nprecision highp float;\nlayout(location=0) out vec4 a; layout(location=0) out vec4 b; void main(){a=b=vec4(1.);}","sema.output-location"},
        {"#version 300 es\nprecision highp float;\nout mat2 m; void main(){m=mat2(1.);}","sema.output-type"},
        {"#version 300 es\nout vec4 color; void main(){color=vec4(1.);}","sema.precision"},
        {"#version 100\nprecision mediump float;\nvoid main(){gl_FragColor=vec4(1.); gl_FragData[0]=vec4(1.);}","sema.fragment-output"},
        {"#version 100\nprecision mediump float;\nfloat a[2]; void main(){float b[2]; b=a; gl_FragColor=vec4(1.);}","sema.array-assignment"},
        {"#version 100\nprecision mediump float;\nvoid main(){gl_FragColor=vec4(sinh(1.));}","sema.undeclared"},
        {"#version 100\nprecision mediump float;\nvoid main(){int a[2]; gl_FragColor=vec4(float(a.length()));}","sema.method"},
        {"#version 100\nprecision mediump float;\nout vec4 c; void main(){c=vec4(1.);}","sema.qualifier"},
    };
    for(const auto& c:cases)CHECK(error(c.first,c.second));
}

// Constant folding: each array size must be computed, so the out-of-range index is reported.
void constantFolding() {
    const std::vector<std::string> cases={
        P+"const int N=2*3; float a[N]; void main(){color=vec4(a[6]);}",
        P+"const ivec2 v=ivec2(3,4); float a[v.y]; void main(){color=vec4(a[4]);}",
        P+"float a[int(2.5*2.)]; void main(){color=vec4(a[5]);}",
        P+"float a[max(2,3)]; void main(){color=vec4(a[3]);}",
        P+"const mat2 m=mat2(1.,2.,3.,4.); float a[int(m[1][0])]; void main(){color=vec4(a[3]);}",
        P+"float a[ivec2(1,2).y+1]; void main(){color=vec4(a[3]);}",
        // Built-ins the transpiler replaces by generated functions are folded too (SkSL needs the value in const initializers).
        P+"float a[int(tanh(1.)*10.)]; void main(){color=vec4(a[7]);}",
        P+"float a[int(determinant(mat2(2.,0.,0.,3.)))]; void main(){color=vec4(a[6]);}",
        P+"float a[int(transpose(mat2(1.,2.,3.,4.))[0][1])]; void main(){color=vec4(a[3]);}",
        P+"float a[int(outerProduct(vec2(1.,2.),vec3(3.,4.,5.))[2][1])]; void main(){color=vec4(a[10]);}",
        P+"float a[int(pow(2.,3.))]; void main(){color=vec4(a[8]);}",
        P+"float a[int(length(vec2(3.,4.)))]; void main(){color=vec4(a[5]);}",
        P+"float a[int(smoothstep(0.,1.,.5)*8.)]; void main(){color=vec4(a[4]);}",
        P+"float a[int(mod(7.,3.))]; void main(){color=vec4(a[1]);}",
    };
    for(const auto& s:cases)CHECK(error(s,"sema.index"));
}

void mustReportUnsupported() {
    const std::vector<std::pair<std::string,std::string>> cases={
        {P+"uniform int n; void main(){for(int i=0;i<n;++i){} color=vec4(1.);}","unsupported.loop"},
        {P+"uniform float e; void main(){float d=1.; while(d>e){d*=.5;} color=vec4(d);}","unsupported.loop"},
        {P+"void main(){for(int i=0;i<3;++i){i+=1;} color=vec4(1.);}","unsupported.loop"},
        {P+"uniform int n; void main(){float a[3]; color=vec4(a[n]);}","unsupported.dynamic-index"},
        {P+"void main(){uint u=1u; color=vec4(float(u));}","unsupported.uint"},
        {P+"uniform bool b; void main(){color=vec4(b?1.:0.);}","unsupported.bool-uniform"},
        {P+"layout(location=0) in vec2 uv; layout(location=1) in vec2 other; void main(){color=vec4(uv+other,0.,1.);}","unsupported.input"},
        {P+"void main(){color=vec4(isnan(1.)?1.:0.);}","unsupported.builtin"},
        {P+"uniform int n; void main(){switch(n){default: float t=1.; color=vec4(t); break; case 1: break;}}","unsupported.switch-default"},
        {P+"uniform int n; void main(){for(int i=0;i<2;++i){switch(n){case 0: continue;}} color=vec4(1.);}","unsupported.continue-in-switch"},
        {P+"void main(){color=vec4(gl_FragCoord.z);}","unsupported.frag-coord"},
        // Full source.
        {"#version 310 es\nprecision highp float;\nout vec4 color;\nvoid main(){float a[2][2]; color=vec4(a[0][0]);}","unsupported.array-of-arrays"},
    };
    for(const auto& c:cases)CHECK(error(c.first,c.second));
}

void details() {
    // The loop refusal must suggest a @minmax annotation.
    auto sink=analyzeSource(P+"uniform int n; void main(){for(int i=0;i<n;++i){} color=vec4(1.);}");
    const Diagnostic* loop=nullptr;
    for(const auto& d:sink.list)if(d.code=="unsupported.loop" && !loop)loop=&d;
    CHECK(loop!=nullptr);
    if(loop)CHECK(loop->suggestion.find("@minmax")!=std::string::npos);

    // The public API must render the refusal with the GLSL line and suggestion.
    auto r=transpile(P+"uniform int n; void main(){for(int i=0;i<n;++i){} color=vec4(1.);}");
    CHECK(!r.ok());
    if(!r.diagnostics.empty()){
        const std::string text=formatDiagnostic(r.diagnostics[0]);
        CHECK(text.find("GLSL 4 |")!=std::string::npos);
        CHECK(text.find("suggestion:")!=std::string::npos);
    }

    // Reserved identifiers are a warning, not an error.
    CHECK(warns(P+"float a__b; void main(){color=vec4(1.);}","syntax.reserved-name"));
}
}

// Built-in replacement helpers: GLSL built-ins without an exact SkSL overload are emitted as
// generated functions named glsl_<builtin>, defined before the code that calls them.
const std::string HP="#version 300 es\nprecision highp float;\nuniform vec4 u;\nout vec4 color;\n";

size_t countOf(const std::string& text, const std::string& needle) {
    size_t n=0;
    for(size_t p=text.find(needle); p!=std::string::npos; p=text.find(needle,p+needle.size()))++n;
    return n;
}
bool hasDiagnostic(const Result& r, const std::string& code) {
    for(const auto& d:r.diagnostics)if(d.code==code)return true;
    return false;
}
void printResult(const std::string& shader, const Result& r) {
    std::cerr<<"  shader: "<<shader<<"\n";
    if(!r.source.empty())std::cerr<<"  source:\n"<<r.source<<"\n";
    for(const auto& d:r.diagnostics)std::cerr<<"  diagnostic ["<<d.code<<"] "<<d.message<<"\n";
}
// Transpiles the shader; it must be accepted, and its source must contain every needle in
// `present`, none in `absent`, and each needle in `order` in the given sequence.
void expectSource(const std::string& shader, int line,
                  const std::vector<std::string>& present,
                  const std::vector<std::string>& absent={},
                  const std::vector<std::string>& order={}) {
    Result r=transpile(shader);
    if(!r.ok()){
        ++failures;
        std::cerr<<"sema_tests.cpp:"<<line<<": failed: shader is accepted\n";
        printResult(shader,r);
        return;
    }
    for(const auto& n:present)if(r.source.find(n)==std::string::npos){
        ++failures;
        std::cerr<<"sema_tests.cpp:"<<line<<": failed: source contains \""<<n<<"\"\n";
        printResult(shader,r);
    }
    for(const auto& n:absent)if(r.source.find(n)!=std::string::npos){
        ++failures;
        std::cerr<<"sema_tests.cpp:"<<line<<": failed: source does not contain \""<<n<<"\"\n";
        printResult(shader,r);
    }
    size_t last=0;
    for(const auto& n:order){
        size_t p=r.source.find(n,last);
        if(p==std::string::npos){
            ++failures;
            std::cerr<<"sema_tests.cpp:"<<line<<": failed: \""<<n<<"\" missing or out of order\n";
            printResult(shader,r);
            return;
        }
        last=p+1;
    }
}
// Transpiles the shader; it must be refused with an error whose code is `code`.
void expectRefused(const std::string& shader, const std::string& code, int line) {
    Result r=transpile(shader);
    if(r.ok() || !hasDiagnostic(r,code)){
        ++failures;
        std::cerr<<"sema_tests.cpp:"<<line<<": failed: shader is refused with \""<<code<<"\"\n";
        printResult(shader,r);
    }
}

void helpers() {
    // sinh is replaced by a helper; the native call must not remain.
    expectSource(HP+"void main(){color=vec4(sinh(u.x));}", __LINE__,
                 {"float glsl_sinh(float x)","glsl_sinh("},
                 {" sinh("});

    // Float abs stays native; int abs gets a helper.
    expectSource(HP+"void main(){color=vec4(abs(u.x), float(abs(int(u.y))), 0., 1.);}", __LINE__,
                 {"abs(u.x)","int glsl_abs(int x)"});

    // Helper dependencies come before their users: min is used by clamp.
    expectSource(HP+"void main(){ivec2 a=clamp(ivec2(u.xy),0,3); color=vec4(vec2(a),0.,1.);}", __LINE__,
                 {},{},
                 {"int glsl_min(int a, int b)",
                  "int glsl_clamp(int x, int lo, int hi)",
                  "int2 glsl_clamp(int2 x, int lo, int hi)"});

    // trunc is defined before modf, which calls it.
    expectSource(HP+"void main(){float i; float f=modf(u.x,i); color=vec4(f,i,0.,1.);}", __LINE__,
                 {},{},
                 {"float glsl_trunc(float x)","float glsl_modf(float x, out float i)"});

    // mix with a bool vector weight gets a helper; a float weight stays native.
    expectSource(HP+"void main(){color=vec4(mix(u.xy,u.zw,bvec2(true,false)),mix(u.z,u.w,0.5),1.);}", __LINE__,
                 {"float2 glsl_mix(float2 x, float2 y, bool2 a)","mix(u.z"});

    // Matrix helpers use SkSL's square-matrix types.
    expectSource(HP+"void main(){mat3 m=transpose(mat3(u.x)); color=vec4(determinant(m),outerProduct(u.xy,u.zw)[0],1.);}", __LINE__,
                 {"float3x3 glsl_transpose(float3x3 m)",
                  "float glsl_determinant(float3x3 m)",
                  "float2x2 glsl_outerProduct(float2 c, float2 r)"});

    // A helper used twice is defined once.
    {
        const std::string s=HP+"void main(){color=vec4(sinh(u.x)+sinh(u.y),0.,0.,1.);}";
        Result r=transpile(s);
        if(!r.ok() || countOf(r.source,"float glsl_sinh(float x)")!=1){
            ++failures;
            std::cerr<<"sema_tests.cpp:"<<__LINE__<<": failed: \"float glsl_sinh(float x)\" is defined exactly once\n";
            printResult(s,r);
        }
    }

    // A user identifier with a helper's name keeps it; the helper gets another name.
    expectSource(HP+"float glsl_sinh=1.; void main(){color=vec4(sinh(u.x)+glsl_sinh);}", __LINE__,
                 {"float glsl_sinh = 1.","float glsl_sinh_2(float x)","glsl_sinh_2(u.x) + glsl_sinh"},
                 {"float glsl_sinh(float x)"});

    // Non-square matrices have no SkSL type and are refused.
    expectRefused(HP+"void main(){mat2x3 m=mat2x3(1.); color=vec4(1.);}", "unsupported.matrix-shape", __LINE__);
    expectRefused(HP+"void main(){color=vec4(outerProduct(u.xyz,u.xy)[0],1.);}", "unsupported.matrix-shape", __LINE__);

    // Functions return arrays through an out parameter.
    expectSource(HP+"float[2] f(){return float[2](1.,2.);} void main(){color=vec4(f()[0]);}", __LINE__,
                 {"void f(out float result[2])","result[0] = 1.;","f(tmp1);"},
                 {"float[2]("});
    // Names of the shader are kept; generated names avoid them.
    expectSource(HP+"float coord=.5; void main(){float tmp1=coord; color=vec4(tmp1)+u;}", __LINE__,
                 {"float4 color;","float coord = .5;","half4 main(float2 coord_2)","float tmp1 = coord;"},
                 {"transpiler_"});
}

// Software derivatives: dFdx/dFdy/fwidth are computed from the 2x2 pixel quad, so the shader
// needs quad lanes only when a derivative is used; refusals cover what cannot be computed.
void derivatives() {
    expectSource(HP+"void main(){float v=gl_FragCoord.x*u.x; color=vec4(dFdx(v),dFdy(v),fwidth(v),1.);}", __LINE__,
                 {"float2 quad;","float4 fragCoord_L3;","half4 main(float2 coord)"},
                 {"dFdx(","dFdy(","fwidth("});

    // A derivative inside a helper that returns early after fwidth keeps its statement order.
    expectSource(HP+"float aa(float d){float w=fwidth(d); if(d>w) return 0.; return w;} void main(){color=vec4(aa(gl_FragCoord.x));}", __LINE__,
                 {"float aa(float d, float d_L1, float d_L2, float d_L3, out float ret_L1","ret_L1 = w_L1;","bool done_L1 = false;"});

    // Coordinate without derivatives: no quad lanes.
    expectSource(HP+"void main(){color=vec4(gl_FragCoord.xy,0.,1.);}", __LINE__,
                 {"float4 fragCoord;"}, {"float2 quad;"});
    // GLSL ES 1.00 with the extension enabled.
    expectSource("#version 100\n#extension GL_OES_standard_derivatives : enable\nprecision highp float;\nvoid main(){gl_FragColor=vec4(dFdx(gl_FragCoord.x));}", __LINE__,
                 {"float2 quad;"});
    // GLSL ES 1.00 without the extension: derivatives are undeclared.
    expectRefused("#version 100\nprecision highp float;\nvoid main(){gl_FragColor=vec4(dFdx(gl_FragCoord.x));}", "sema.undeclared", __LINE__);

    // A return under a pixel-dependent condition before a derivative is refused.
    expectRefused(HP+"void main(){float v=gl_FragCoord.x; if(v>4.){color=vec4(1.);return;} color=vec4(dFdx(v));}", "unsupported.derivative-control", __LINE__);
    // A derivative of an expression that cannot be computed separately is refused.
    expectRefused(HP+"void main(){float v=gl_FragCoord.x; color=vec4(dFdx(v++));}", "unsupported.derivative", __LINE__);
    expectRefused(HP+"void main(){float v=gl_FragCoord.x; float r=0.; if(v>4.){v*=2.; r=dFdx(v);} color=vec4(r);}", "unsupported.derivative", __LINE__);

    // gl_FragCoord.z, .w and the whole vector are refused.
    expectRefused(HP+"void main(){color=gl_FragCoord;}", "unsupported.frag-coord", __LINE__);
    // Struct uniforms, a single output at a non-zero location, and row_major blocks with matrices are refused.
    expectRefused("#version 300 es\nprecision highp float;\nstruct S{float a;}; uniform S s; out vec4 color;\nvoid main(){color=vec4(s.a);}", "unsupported.struct-uniform", __LINE__);
    expectRefused("#version 300 es\nprecision highp float;\nlayout(location=1) out vec4 color;\nvoid main(){color=vec4(1.);}", "unsupported.output-location", __LINE__);
    expectRefused("#version 300 es\nprecision highp float;\nlayout(row_major) uniform B{mat2 m;}; out vec4 color;\nvoid main(){color=vec4(m[0],0.,1.);}", "unsupported.matrix-layout", __LINE__);

    // Constructors are emitted with only the components they need.
    expectSource(HP+"void main(){vec3 a=vec3(u); vec2 b=vec2(mat2(1.)); color=vec4(a.x,b,1.);}", __LINE__,
                 {"float3(u.xyz)","[0][0]"});

    // Arrays returned from a function that computes derivatives are refused.
    expectRefused(HP+"float[2] g(float x){return float[2](dFdx(x),1.);} void main(){color=vec4(g(gl_FragCoord.x)[0]);}", "unsupported.array-return", __LINE__);
}

// Loop ranges: an annotated uniform bound is clamped to its range with a guard and a warning;
// constant and enclosing-index bounds are exact. Refusals list every error, and invalid
// annotations warn.
void loopRanges() {
    auto has=[](const Result& r, const std::string& code, Severity severity) {
        for(const auto& d:r.diagnostics)if(d.code==code && d.severity==severity)return true;
        return false;
    };
    auto contains=[](const std::string& text, const std::string& needle) {
        return text.find(needle)!=std::string::npos;
    };
    auto byCode=[](const Result& r, const std::string& code) -> const Diagnostic* {
        for(const auto& d:r.diagnostics)if(d.code==code)return &d;
        return nullptr;
    };

    Result a=transpile(P+"uniform int n; // @minmax{0, 8}\nvoid main(){float a=0.;for(int i=0;i<n;++i){a+=.1;} color=vec4(a);}");
    CHECK(a.ok());
    CHECK(a.state==Result::State::Success);
    CHECK(has(a,"software.loop",Severity::Warning));
    const Diagnostic* sl=byCode(a,"software.loop");
    CHECK(sl!=nullptr);
    if(sl){
        CHECK(sl->skslLine>0);
        CHECK(sl->skslColumn>0);
        CHECK(sl->skslSourceLine.find("for (")!=std::string::npos);
        CHECK(sl->skslColumn>0 && sl->skslColumn<=sl->skslSourceLine.size() && sl->skslSourceLine.substr(sl->skslColumn-1,4)=="for ");
    }
    CHECK(contains(a.source,"i < 8"));
    CHECK(contains(a.source,"if (!(i < n))"));
    CHECK(contains(a.source,"// @minmax{0, 8}"));

    Result b=transpile(P+"uniform int n; void main(){float a=0.;for(int i=0;i<min(n,16);++i){a+=.1;} color=vec4(a);}");
    CHECK(b.ok());
    // Integer min is also a generated function, since SkSL only has the float overloads.
    CHECK(b.diagnostics.size()==2 && has(b,"software.loop",Severity::Warning) && has(b,"software.builtin",Severity::Warning));
    CHECK(contains(b.source,"i < 16"));

    Result c=transpile(P+"uniform int n; void main(){float a=0.;for(int i=0;i<4;++i){for(int j=0;j<i;++j){a+=.1;}} color=vec4(a);}");
    CHECK(c.ok());
    CHECK(c.diagnostics.size()==1 && c.diagnostics[0].code=="software.loop" && c.diagnostics[0].severity==Severity::Warning);
    CHECK(contains(c.source,"j < 4"));
    CHECK(contains(c.source,"if (!(j < i))"));

    Result d=transpile(P+"uniform int n; void main(){float a=0.;for(int i=0;i<n;++i){a+=.1;} color=vec4(a);}");
    CHECK(!d.ok());
    CHECK(d.state==Result::State::Failure);
    CHECK(d.source.rfind("#error GLSL ",0)==0);
    CHECK(contains(d.source,"half4 main"));
    CHECK(has(d,"unsupported.loop",Severity::Error));
    const Diagnostic* ul=byCode(d,"unsupported.loop");
    CHECK(ul!=nullptr);
    if(ul){
        CHECK(ul->skslLine>0);
        CHECK(ul->skslSourceLine.find("for (")!=std::string::npos);
    }

    Result e=transpile(P+"void main(){color=vec4(undeclaredName);}");
    CHECK(!e.ok());
    CHECK(e.source.rfind("#error GLSL ",0)==0 && contains(e.source,"undeclaredName") && e.state==Result::State::Failure);
    Result y=transpile(P+"void main(){color=vec4(1.)");
    CHECK(!y.ok() && y.source.empty());

    Result f=transpile(P+"void main(){if(gl_FragCoord.x>1.)discard; color=vec4(gl_FragCoord.z);}");
    CHECK(!f.ok());
    CHECK(has(f,"software.discard",Severity::Warning));
    CHECK(has(f,"unsupported.frag-coord",Severity::Error));
    CHECK(countOf(f.source,"#error GLSL ")>=1);

    Result g=transpile(P+"uniform int n; // @minmax{5, 1}\nvoid main(){color=vec4(1.);}");
    CHECK(g.ok());
    CHECK(has(g,"annotation.invalid",Severity::Warning));

    // sinh is replaced by a generated function; the warning points at the glsl_sinh call on GLSL line 4.
    Result h=transpile(P+"uniform float t; void main(){color=vec4(sinh(t));}");
    CHECK(h.ok());
    const Diagnostic* sb=byCode(h,"software.builtin");
    CHECK(sb!=nullptr);
    if(sb){
        CHECK(sb->severity==Severity::Warning);
        CHECK(sb->skslLine>0);
        CHECK(sb->skslColumn>0 && sb->skslColumn<=sb->skslSourceLine.size() && sb->skslSourceLine.substr(sb->skslColumn-1,10)=="glsl_sinh(");
        CHECK(sb->line==4);
    }

    // Derivatives are computed in software; the warning points at the replacement.
    Result k=transpile(HP+"void main(){color=vec4(dFdx(gl_FragCoord.x));}");
    CHECK(k.ok());
    CHECK(has(k,"software.derivative",Severity::Warning));
    const Diagnostic* sd=byCode(k,"software.derivative");
    CHECK(sd!=nullptr);
    if(sd)CHECK(sd->skslLine>0);

    // A shader with nothing replaced has no diagnostics.
    Result z=transpile(P+"void main(){color=vec4(0.5);}");
    CHECK(z.ok() && z.diagnostics.empty() && z.state==Result::State::Success);
}

// Fragment inputs: a float-vector input at location 0 (or the only input) is the normalized texture
// coordinate, computed from gl_FragCoord and iResolution with a warning.
void textureCoordinates() {
    auto has=[](const Result& r, const std::string& code, Severity severity) {
        for(const auto& d:r.diagnostics)if(d.code==code && d.severity==severity)return true;
        return false;
    };
    auto contains=[](const std::string& text, const std::string& needle) {
        return text.find(needle)!=std::string::npos;
    };

    Result t1=transpile(P+"in vec4 uv_coords; void main(){color=uv_coords;}");
    CHECK(t1.ok());
    CHECK(has(t1,"software.input",Severity::Warning));
    CHECK(contains(t1.source,"uniform float2 iResolution;"));
    CHECK(contains(t1.source,"uv_coords = float4(coord / iResolution, 0.0, 1.0);"));

    Result t2=transpile(P+"uniform vec2 iResolution; in vec2 uv; void main(){color=vec4(uv,0.,1.);}");
    CHECK(t2.ok());
    CHECK(contains(t2.source,"uv = coord / iResolution;"));
    CHECK(countOf(t2.source,"uniform float2 iResolution;")==1);   // the shader's own, not a second one
}

// 2D textures become shaders sampled with eval(); other samplers, unsupported texture
// functions and sampler parameters are refused with a code naming the cause.
void textures() {
    auto has=[](const Result& r, const std::string& code, Severity severity) {
        for(const auto& d:r.diagnostics)if(d.code==code && d.severity==severity)return true;
        return false;
    };
    auto contains=[](const std::string& text, const std::string& needle) {
        return text.find(needle)!=std::string::npos;
    };

    Result a=transpile(P+"uniform sampler2D t; uniform vec2 iResolution; in vec2 uv; void main(){color=texture(t,uv);}");
    CHECK(a.ok());
    CHECK(has(a,"software.texture",Severity::Warning));
    CHECK(contains(a.source,"uniform shader t;"));
    CHECK(contains(a.source,"float4(t.eval(uv * tSize))"));

    Result b=transpile(P+"uniform sampler2D t; void main(){color=textureProj(t,vec3(1.,1.,2.));}");
    CHECK(b.ok());
    CHECK(contains(b.source,"t.eval(glsl_textureProj("));
    CHECK(contains(b.source,"return p.xy / p.z;"));

    Result c=transpile(P+"uniform sampler2D t; void main(){color=textureLod(t,vec2(.5),0.);}");
    CHECK(c.ok());
    CHECK(has(c,"software.texture",Severity::Warning));
    CHECK(contains(c.source,"float4(t.eval(float2(.5) * tSize))"));

    Result d=transpile(P+"uniform sampler2D t; void main(){color=texture(t,vec2(.5),1.);}");
    CHECK(d.ok());
    CHECK(contains(d.source,"float4(t.eval(float2(.5) * tSize))"));

    Result e=transpile(P+"uniform sampler2D t; vec4 f(sampler2D s){return texture(s,vec2(.5));} void main(){color=f(t);}");
    CHECK(e.ok());
    // 'f_t(' would end in the child 't(', which the application rewrites to 't.eval('.
    CHECK(contains(e.source,"f_t_("));
    CHECK(!contains(e.source,"f_t("));
    CHECK(!contains(e.source,"sampler2D"));

    Result f=transpile(P+"uniform samplerCube t; void main(){color=vec4(1.);}");
    CHECK(f.ok());
    CHECK(contains(f.source,"uniform shader t_px;"));
    CHECK(contains(f.source,"uniform shader t_nz;"));

    Result sh=transpile(P+"uniform highp sampler2DShadow t; void main(){color=vec4(1.);}");
    CHECK(!sh.ok());
    CHECK(has(sh,"unsupported.sampler",Severity::Error));

    Result sz=transpile(P+"uniform sampler2D t; void main(){color=texelFetch(t,ivec2(1),0)+vec4(vec2(textureSize(t,0)),0.,0.);}");
    CHECK(sz.ok());
    CHECK(contains(sz.source,"uniform float2 tSize;"));

    Result g=transpile("#version 100\nprecision mediump float;\nuniform sampler2D t;\nvoid main(){gl_FragColor=texture2D(t,vec2(.5));}");
    CHECK(g.ok());
    CHECK(contains(g.source,"float4(t.eval(float2(.5) * tSize))"));

    // texture2D is GLSL ES 1.00 only; in a 3.00 shader it is an undeclared function.
    Result h=transpile(P+"uniform sampler2D t; void main(){color=texture2D(t,vec2(.5));}");
    CHECK(!h.ok());
    CHECK(has(h,"sema.undeclared",Severity::Error));

    // Invalid layout qualifiers are dropped with a warning: the SkSL has none.
    Result l1=transpile(P+"layout(location=0) uniform float u; void main(){color=vec4(u);}");
    CHECK(l1.ok() && has(l1,"sema.layout",Severity::Warning));
    Result l2=transpile(P+"layout(foo) uniform float u; void main(){color=vec4(u);}");
    CHECK(l2.ok() && has(l2,"sema.layout",Severity::Warning));
    Result l3=transpile("#version 320 es\nprecision highp float;\nlayout(locaiton = 0) out vec4 c;\nvoid main(){c=vec4(1.);}");
    CHECK(l3.ok() && has(l3,"sema.layout",Severity::Warning));
    for(const auto& d:l3.diagnostics)if(d.code=="sema.layout")CHECK(d.suggestion.find("'location'")!=std::string::npos);
}

// Constructs that used to be refused and are now translated with a software.* warning.
void softwareFallbacks() {
    auto has=[](const Result& r, const std::string& code, Severity severity) {
        for(const auto& d:r.diagnostics)if(d.code==code && d.severity==severity)return true;
        return false;
    };
    Result discard=transpile(P+"void main(){discard;}");
    CHECK(discard.ok());
    CHECK(has(discard,"software.discard",Severity::Warning));
    CHECK(countOf(discard.source,"return half4(0.0);")>=1);

    Result missing=transpile(P+"float f(bool b){if(b) return 1.;} void main(){color=vec4(f(true));}");
    CHECK(missing.ok());
    CHECK(has(missing,"software.missing-return",Severity::Warning));

    auto contains=[](const std::string& text, const std::string& needle) {
        return text.find(needle)!=std::string::npos;
    };
    Result whileCounter=transpile(P+"void main(){int i=0; while(i<3){++i;} color=vec4(1.);}");
    CHECK(whileCounter.ok());
    CHECK(has(whileCounter,"software.loop",Severity::Warning));
    CHECK(contains(whileCounter.source,"for (int iteration = 0; iteration < 4; iteration++)"));

    Result andLoop=transpile(P+"void main(){float t=1.;for(int i=0;i<16&&t<2.;i++){t+=.5;} color=vec4(t);}");
    CHECK(andLoop.ok());
    CHECK(has(andLoop,"software.loop",Severity::Warning));
    CHECK(contains(andLoop.source,"for (int i = 0; i < 16; i++)"));
    CHECK(contains(andLoop.source,"if (!(t < 2.))"));

    Result maxiter=transpile(P+"uniform float e; void main(){float d=1.; while(d>e){ // @maxiter{64}\nd*=.5;} color=vec4(d);}");
    CHECK(maxiter.ok());
    CHECK(has(maxiter,"software.loop",Severity::Warning));
    CHECK(contains(maxiter.source,"iteration < 65"));

    CHECK(transpile(P+"void main(){int k=0; do{k++;}while(false); color=vec4(float(k));}").ok());

    Result ops=transpile(P+"uniform int n; void main(){int x=n%2; ivec2 v=ivec2(n)<<2; v>>=1; color=vec4(float(x),float(v.x&3),float(~n),1.);}");
    CHECK(ops.ok());
    CHECK(has(ops,"software.operator",Severity::Warning));
    CHECK(contains(ops.source,"glsl_mod("));
    CHECK(contains(ops.source,"glsl_shl("));
    CHECK(contains(ops.source,"glsl_shrAssign("));
    CHECK(contains(ops.source,"glsl_and("));

    // A vec4 texture-coordinate input used where a vec2 is needed is retried as vec2.
    const std::string UV="#version 310 es\nprecision highp float;\nlayout(location = 0) in highp vec4 uv_coords;\nuniform vec2 iResolution;\nout vec4 out_color;\n";
    Result uvValid=transpile(UV+"void main(){ out_color = vec4(uv_coords.xy, uv_coords.w, 1.0); }");
    CHECK(uvValid.ok());
    CHECK(!has(uvValid,"sema.input-vec2",Severity::Warning));
    CHECK(contains(uvValid.source,"float4 uv_coords;"));

    Result uvAsVec2=transpile(UV+"vec3 f(vec2 p){ return vec3(p, 0.0); } void main(){ vec2 d = uv_coords * 2.0 - vec2(1.0); out_color = vec4(f(uv_coords).xy, d); }");
    CHECK(uvAsVec2.ok());
    CHECK(has(uvAsVec2,"sema.input-vec2",Severity::Warning));
    CHECK(contains(uvAsVec2.source,"float2 uv_coords;"));

    Result uvUsesZw=transpile(UV+"void main(){ vec2 d = uv_coords - vec2(0.5); out_color = vec4(d, uv_coords.zw); }");
    CHECK(!uvUsesZw.ok());
    CHECK(has(uvUsesZw,"sema.operator",Severity::Error));
    CHECK(!has(uvUsesZw,"sema.input-vec2",Severity::Warning));

    Result uvOtherError=transpile(UV+"void main(){ vec2 d = uv_coords * 2.0 - 1.0; out_color = vec4(d, 0.0, undeclared); }");
    CHECK(!uvOtherError.ok());
    CHECK(has(uvOtherError,"sema.input-vec2",Severity::Warning));
    CHECK(has(uvOtherError,"sema.undeclared",Severity::Error));
    CHECK(!has(uvOtherError,"sema.type-mismatch",Severity::Error));
}

int main() {
    mustBeOk();
    mustReportError();
    constantFolding();
    mustReportUnsupported();
    details();
    helpers();
    derivatives();
    loopRanges();
    textureCoordinates();
    textures();
    softwareFallbacks();
    if(failures)std::cerr<<failures<<" failed checks\n";
    else std::cout<<"sema tests passed\n";
    return failures?1:0;
}
