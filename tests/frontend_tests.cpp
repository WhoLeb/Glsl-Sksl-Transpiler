#include "parser.hpp"
#include "glsl_sksl/transpiler.hpp"
#include <iostream>
#include <random>
using namespace glsl_sksl;
namespace {
int failures=0;
void check(bool value, const char* what, int line) {
    if(value)return;
    ++failures;
    std::cerr<<"frontend_tests.cpp:"<<line<<": failed: "<<what<<"\n";
}
#define CHECK(x) check((x),#x,__LINE__)
#define CHECK_EQ(a,b) do{const auto& a_=(a);const auto& b_=(b);if(!(a_==b_)){++failures;std::cerr<<"frontend_tests.cpp:"<<__LINE__<<": expected\n  "<<b_<<"\ngot\n  "<<a_<<"\n";}}while(0)

struct Parsed { DiagnosticSink sink; ParsedShader shader; };
Parsed run(const std::string& source) {Parsed p; p.shader=parseShader(source,p.sink); return p;}
std::string tree(const std::string& source) {
    auto p=run(source);
    for(const auto& d:p.sink.list)if(d.severity==Severity::Error)std::cerr<<"  unexpected: "<<d.line<<":"<<d.column<<" "<<d.message<<"\n";
    return p.shader.unit && !p.sink.errors?ast::dump(*p.shader.unit):"<error>";
}
// Dump of the initializer of `int v = <text>;` in a GLSL ES 3.00 shader.
std::string expr(const std::string& text, const std::string& prelude="") {
    const std::string d=tree("#version 300 es\n"+prelude+"\nint v="+text+";");
    const auto at=d.rfind("(var int v=");
    if(at==std::string::npos)return d;
    return d.substr(at+11,d.size()-at-11-2);
}
bool error(const std::string& source, const std::string& code) {
    auto p=run(source);
    for(const auto& d:p.sink.list)if(d.severity==Severity::Error && d.code==code)return true;
    std::cerr<<"  diagnostics for expected "<<code<<":";
    for(const auto& d:p.sink.list)std::cerr<<" ["<<d.code<<"] "<<d.message<<";";
    std::cerr<<"\n";
    return false;
}
bool warning(const std::string& source, const std::string& code) {
    auto p=run(source);
    if(p.sink.errors)return false;
    for(const auto& d:p.sink.list)if(d.severity==Severity::Warning && d.code==code)return true;
    return false;
}
bool clean(const std::string& source) {
    auto p=run(source);
    for(const auto& d:p.sink.list)if(d.severity==Severity::Error)std::cerr<<"  unexpected: "<<d.line<<":"<<d.column<<" "<<d.message<<"\n";
    return !p.sink.errors && p.shader.unit;
}
const std::string es3="#version 300 es\n";

void lexer() {
    CHECK_EQ(expr("1\\\n2"),"12");
    CHECK(error("#version 100\nint v=1\\\n2;","lex.line-continuation"));
    CHECK(clean("#version 300 es\r\nint v=1;\r\nint w=2;\r\n"));
    {
        auto p=run("#version 300 es\r\nint v=1;\r\nint w=$;\r\n");
        CHECK(p.sink.errors==1 && p.sink.list[0].line==3 && p.sink.list[0].column==7);
    }
    CHECK(clean(es3+"// caf\xC3\xA9\n/* \xE2\x82\xAC */int v=1;"));
    CHECK(error(es3+"int caf\xC3\xA9=1;","lex.invalid-character"));
    CHECK(error("\xEF\xBB\xBF#version 300 es\nint v=1;","lex.invalid-character"));
    CHECK(error(es3+"int v=1;\n/* never closed","lex.unterminated-comment"));
    CHECK(error(es3+"int v=\"a\";","lex.invalid-character"));
    CHECK(error(es3+std::string("int v=1;\0",9),"lex.invalid-character"));
    CHECK(error(es3+"int v=0x;","lex.invalid-number"));
    CHECK(error(es3+"int v=08;","lex.invalid-number"));
    CHECK(error(es3+"int v=4294967296;","lex.invalid-number"));
    CHECK(error(es3+"float v=1e;","lex.invalid-number"));
    CHECK(error(es3+"float v=1.0lf;","lex.invalid-number"));
    CHECK(error(es3+"float v=1.0.x;","lex.invalid-number"));
    CHECK(error(es3+"int v=1x;","lex.invalid-number"));
    CHECK(error("#version 100\nfloat v=1.5f;","lex.invalid-number"));
    CHECK(error("#version 100\nint v=1u;","lex.invalid-number"));
    CHECK(error(es3+"float v=1e39;","lex.float-range"));
    CHECK_EQ(expr("0x1F"),"0x1F");
    CHECK_EQ(expr("0x1e+1"),"(+ 0x1e 1)");
    CHECK_EQ(expr("017"),"017");
    CHECK_EQ(expr("4294967295u"),"4294967295u");
    {
        auto p=run(es3+"int a=0x1F; int b=017; float c=1.5e1f; float d=.5;");
        CHECK(!p.sink.errors);
        CHECK(p.shader.tokens[3].intValue==31 && p.shader.tokens[8].intValue==15);
        CHECK(p.shader.tokens[13].floatValue==15.0 && p.shader.tokens[13].text=="1.5e1");
    }
    CHECK(error(es3+"float half=1.;","lex.reserved-word"));
    CHECK(error("#version 100\nvoid main(){int x=1; switch(x){}}","lex.reserved-word"));
    CHECK(clean("#version 100\nfloat uint=1.;"));   // not a keyword before 3.00
    CHECK(error("#version 100\nint v=5%2;","lex.reserved-operator"));
    CHECK(clean(es3+"int v=5%2;"));
    CHECK(error(es3+"int a; int v=a--.5;","syntax.expected-token"));
}

void preprocessor() {
    CHECK_EQ(expr("TWO*3","#define TWO 2"),"(* 2 3)");
    CHECK_EQ(expr("SQ(1+1)","#define SQ(x) ((x)*(x))"),"(* (+ 1 1) (+ 1 1))");
    CHECK_EQ(expr("ADD(SQ(2),3)","#define SQ(x) ((x)*(x))\n#define ADD(a,b) a+b"),"(+ (* 2 2) 3)");
    CHECK_EQ(expr("A","int A;\n#define A A+1"),"(+ A 1)");
    CHECK_EQ(expr("f(1)","int f(int x){return x;}\n#define f(x) x+f(x)"),"(+ 1 (call f 1))");
    CHECK_EQ(expr("G","int F;\n#define F(x) x\n#define G F"),"F");   // a function-like macro name without '(' is not expanded
    CHECK_EQ(expr("F (2)","#define F(x) x*2"),"(* 2 2)");
    CHECK_EQ(expr("NONE()","#define NONE() 7"),"7");
    CHECK_EQ(expr("EMPTY 1","#define EMPTY"),"1");
    CHECK_EQ(expr("ONE","#if (1<<4)==16 && !defined(NOPE) && defined GL_ES\n#define ONE 1\n#else\n#define ONE 2\n#endif"),"1");
    CHECK_EQ(expr("X","#if 0\nthis is not glsl $ @\n#error skipped\n#define X 1\n#elif 0\n#define X 2\n#else\n#define X 3\n#endif"),"3");
    CHECK_EQ(expr("X","#if 1\n#if 0\n#define X 1\n#else\n#define X 2\n#endif\n#else\n#define X 3\n#endif"),"2");
    CHECK_EQ(expr("X","#ifdef GL_ES\n#define X __VERSION__\n#endif"),"300");
    CHECK_EQ(expr("X","#if GL_FRAGMENT_PRECISION_HIGH == 1 && -1 < 0 && (~0) == -1 && 7/2 == 3 && -7%3 == -1\n#define X 1\n#endif"),"1");
    CHECK_EQ(expr("X","#if 0 && 1/0\n#else\n#define X 4\n#endif"),"4");
    CHECK_EQ(expr("X","#if 0x7fffffff + 1 < 0\n#define X 5\n#endif"),"5");
    CHECK(error(es3+"#if UNDEFINED\n#endif","pp.if-expression"));
    CHECK(error(es3+"#if 1/0\n#endif","pp.if-expression"));
    CHECK(error(es3+"#if 1 +\n#endif","pp.if-expression"));
    CHECK(error(es3+"#if 1\n","pp.unterminated-conditional"));
    CHECK(error(es3+"#endif","pp.unbalanced-conditional"));
    CHECK(error(es3+"#if 1\n#else\n#else\n#endif","pp.unbalanced-conditional"));
    CHECK(error(es3+"#if 1\n#else\n#elif 1\n#endif","pp.unbalanced-conditional"));
    CHECK(error(es3+"#if 1\n#endif junk","pp.extra-tokens"));
    CHECK(error(es3+"#error stop here","pp.error"));
    CHECK(clean(es3+"#if 0\n#error not reached\n#endif\nint v=1;"));
    CHECK(error(es3+"#foo","pp.unknown-directive"));
    CHECK(clean(es3+"#\n#pragma optimize(off)\n#pragma STDGL invariant(all)\n#pragma anything 'goes' here\nint v=1;"));
    CHECK_EQ(expr("__LINE__","#line 10"),"10");                 // GLSL ES 3.00: the next line is numbered 10
    CHECK_EQ(tree("#version 100\n#line 10\nint v=__LINE__;"),"(var int v=11)\n");   // GLSL ES 1.00: line+1
    CHECK_EQ(expr("__FILE__","#line 10 4"),"4");
    CHECK_EQ(expr("__LINE__"),"3");
    CHECK(clean(es3+"#define X 1\n#define X 1\nint v=X;"));
    CHECK(error(es3+"#define X 1\n#define X 2","pp.macro-redefined"));
    CHECK(error(es3+"#define F(a) a\n#define F(b) b","pp.macro-redefined"));
    CHECK(clean(es3+"#define X 1\n#undef X\n#define X 2\nint v=X;"));
    CHECK(error(es3+"#define GL_MINE 1","pp.reserved-macro"));
    CHECK(error(es3+"#undef __LINE__","pp.reserved-macro"));
    CHECK(error(es3+"#define defined 1","pp.reserved-macro"));
    CHECK(error("#version 100\n#define MY__X 1","pp.reserved-macro"));
    CHECK(warning(es3+"#define MY__X 1\nint v=MY__X;","pp.reserved-macro"));
    CHECK(error(es3+"#define F(a,b) a\nint v=F(1);","pp.macro-arguments"));
    CHECK(error(es3+"#define F(a) a\nint v=F(1\n#define Y\n);","pp.macro-arguments"));
    CHECK(error(es3+"#define F(a,a) a","pp.syntax"));
    CHECK(error(es3+"#define CAT(a,b) a##b","pp.token-pasting"));
    CHECK(error(es3+"#define S(a) #a","pp.stringify"));
    CHECK(error(es3+"int v=1; # define X","lex.misplaced-hash"));
    std::string bomb=es3;
    for(int k=0;k<30;++k)bomb+="#define M"+std::to_string(k)+" M"+std::to_string(k+1)+" M"+std::to_string(k+1)+"\n";
    CHECK(error(bomb+"int v=M0;","pp.expansion-limit"));
    // #version
    CHECK(error("#version 300\nint v=1;","version.missing-profile"));
    CHECK(error("#version 320 ES\nint v=1;","version.invalid-profile"));
    CHECK(error("#version 330 core\nint v=1;","version.desktop"));
    CHECK(error("#version 999 es\nint v=1;","version.invalid"));
    CHECK(error("#version 100 extra\nint v=1;","pp.extra-tokens"));
    CHECK(error("#version 300 es\n#version 300 es\n","version.misplaced"));
    CHECK(error("int v=1;\n#version 300 es\n","version.misplaced"));
    CHECK(clean("/* licence */\n// header\n#version 310 es\nint v=1;"));
    CHECK(run("# version 100\nint v=1;").shader.version==100);
    CHECK(run("int v=1;").shader.version==100 && !run("int v=1;").shader.versionDeclared);
    // #extension
    CHECK(clean(es3+"#extension all : warn\n#extension all : disable\nint v=1;"));
    CHECK(error(es3+"#extension all : require","pp.extension"));
    CHECK(error(es3+"#extension GL_FAKE_thing : require","pp.extension"));
    CHECK(error(es3+"#extension GL_OES_standard_derivatives : require","pp.extension"));
    CHECK(warning(es3+"#extension GL_OES_standard_derivatives : enable\nint v=1;","pp.extension"));
    CHECK(error(es3+"#extension GL_OES_standard_derivatives : on","pp.extension"));
    CHECK(error(es3+"#extension GL_OES_standard_derivatives","pp.syntax"));
    CHECK(warning(es3+"int v=1;\n#extension all : warn","pp.extension-position"));
    CHECK(clean("#version 100\nprecision mediump float;\n#extension all : warn\nint v=1;"));
}

void parser() {
    CHECK_EQ(expr("1+2*3"),"(+ 1 (* 2 3))");
    CHECK_EQ(expr("(1+2)*3"),"(* (+ 1 2) 3)");
    CHECK_EQ(expr("1-2-3"),"(- (- 1 2) 3)");
    CHECK_EQ(expr("a=b=c","int a,b,c;"),"(= a (= b c))");
    CHECK_EQ(expr("a+=b*=2","int a,b;"),"(+= a (*= b 2))");
    CHECK_EQ(expr("a?1:b?2:3","bool a,b;"),"(? a 1 (? b 2 3))");
    CHECK_EQ(expr("a?b=1:c","bool a; int b,c;"),"(? a (= b 1) c)");
    CHECK_EQ(expr("a?b:c=1","bool a; int b,c;"),"(? a b (= c 1))");
    CHECK_EQ(expr("int(a||b^^c&&d)","bool a,b,c,d;"),"(ctor int (|| a (^^ b (&& c d))))");
    CHECK_EQ(expr("a|b^c&d","int a,b,c,d;"),"(| a (^ b (& c d)))");
    CHECK_EQ(expr("int(a==b<c)","int a,b,c;"),"(ctor int (== a (< b c)))");
    CHECK_EQ(expr("a<<1+2","int a;"),"(<< a (+ 1 2))");
    CHECK_EQ(expr("-a.x++","ivec2 a;"),"(- (post++ (. a x)))");
    CHECK_EQ(expr("~-!a","bool a;"),"(~ (- (! a)))");
    CHECK_EQ(expr("++a[0].y","ivec2 a[2];"),"(++ (. ([] a 0) y))");
    CHECK_EQ(expr("(a,b)","int a,b;"),"(, a b)");
    CHECK_EQ(expr("a.length()","int a[3];"),"(method length a)");
    CHECK_EQ(expr("f(void)","int f(void){return 1;}"),"(call f void)");
    CHECK_EQ(expr("f(1,2)","int f(int a,int b){return a;}"),"(call f 1 2)");
    CHECK_EQ(expr("int[2](1,2)[0]"),"([] (ctor int[2] 1 2) 0)");
    CHECK_EQ(expr("int[](1,2).length()"),"(method length (ctor int[] 1 2))");
    CHECK_EQ(expr("S(1).x","struct S{int x;};"),"(. (ctor S 1) x)");
    CHECK_EQ(expr("ivec2(1).y"),"(. (ctor ivec2 1) y)");
    CHECK_EQ(tree(es3+"struct S{float x; vec2 y[2], z;} s, t[2];"),"(var struct S{(float x) (vec2 y[2] z)} s t[2])\n");
    CHECK_EQ(tree(es3+"struct{lowp float x;} anon;"),"(var struct {(lowp float x)} anon)\n");
    CHECK_EQ(tree(es3+"float a[2], b=1., c[]=float[](1.,2.);"),"(var float a[2] b=1. c[]=(ctor float[] 1. 2.))\n");
    CHECK_EQ(tree(es3+"const float[3] a=float[3](1.,2.,3.);"),"(var const float[3] a=(ctor float[3] 1. 2. 3.))\n");
    CHECK_EQ(tree(es3+"layout(location=0) out highp vec4 c;"),"(var layout(location=0) out highp vec4 c)\n");
    CHECK_EQ(tree("#version 310 es\nlayout(std140, binding=2) uniform Block { mediump vec4 c; float d[2]; } b[2];"),
        "(interface layout(std140,binding=2) uniform Block {(mediump vec4 c) (float d[2])} b[2])\n");
    CHECK_EQ(tree(es3+"uniform Block { vec4 c; };"),"(interface uniform Block {(vec4 c)})\n");
    CHECK_EQ(tree(es3+"layout(shared) uniform;"),"(qualifier layout(shared) uniform)\n");
    CHECK_EQ(tree("#version 310 es\nlayout(early_fragment_tests) in;"),"(qualifier layout(early_fragment_tests) in)\n");
    CHECK_EQ(tree(es3+"out vec4 c; invariant c;"),"(var out vec4 c)\n(qualifier invariant c)\n");
    CHECK_EQ(tree("#version 100\ninvariant gl_FragColor;"),"(qualifier invariant gl_FragColor)\n");
    CHECK_EQ(tree(es3+"precision highp float; precision mediump int;"),"(precision highp float)\n(precision mediump int)\n");
    CHECK_EQ(tree(es3+"float f(in float x, out float y[2], const highp vec2);"),"(function float f (in float x, out float y[2], const highp vec2))\n");
    CHECK_EQ(tree(es3+"highp float f(void){return 1.;}"),"(function highp float f (void) (body (return 1.)))\n");
    CHECK_EQ(tree(es3+"void main(){for(int i=0;i<4;++i){continue;}for(;;)break;}"),
        "(function void main () (body (for (var int i=0) (< i 4) (++ i) (body (continue))) (for (empty) _ _ (break))))\n");
    CHECK_EQ(tree(es3+"void main(){bool c; while(bool b=c){break;} do{discard;}while(c);}"),
        "(function void main () (body (var bool c) (while (var bool b=c) (body (break))) (do (block (discard)) c)))\n");
    CHECK_EQ(tree(es3+"void main(){int x; for(x=0;bool b=x<3;x++);}"),
        "(function void main () (body (var int x) (for (= x 0) (var bool b=(< x 3)) (post++ x) (empty))))\n");
    CHECK_EQ(tree(es3+"void main(){int x; switch(x){case 1: case 2: x=3; break; default: {x=4;}}}"),
        "(function void main () (body (var int x) (switch x (case 1) (case 2) (= x 3) (break) (default) (block (= x 4)))))\n");
    CHECK_EQ(tree(es3+"void main(){if(true)return; else if(false){;}else discard;}"),
        "(function void main () (body (if true (return) (if false (block (empty)) (discard)))))\n");
    CHECK_EQ(tree(es3+"void main(){vec3(1); float[2](1.,2.); precision lowp float; const int k=1;}"),
        "(function void main () (body (ctor vec3 1) (ctor float[2] 1. 2.) (precision lowp float) (var const int k=1)))\n");
    // struct names follow scoping: a variable may shadow a type
    CHECK(clean(es3+"struct S{float x;}; void main(){S s; {float S=1.; S+=s.x;} S t=S(2.);}"));
    CHECK(error(es3+"struct S{float x;}; void main(){float S=1.; S t;}","syntax.expected-token"));
    CHECK(clean(es3+"void main(){ {struct T{int a;}; T t;} }"));
    CHECK(error(es3+"void main(){ {struct T{int a;};} T t; }","syntax.expected-token"));
    // errors
    {
        auto p=run(es3+"void main(){int a=1\nint b=2;}");
        CHECK(p.sink.errors==1 && p.sink.list[0].code=="syntax.expected-token" && p.sink.list[0].line==3);
        CHECK(p.sink.list[0].suggestion=="add ';' after '1'");
    }
    CHECK(error(es3+"int a; a=1;","syntax.global-statement"));
    CHECK(error(es3+"if(true){}","syntax.global-statement"));
    CHECK(error(es3+"void f(){};","syntax.stray-semicolon"));
    CHECK(error(es3+"void main(){case 1: ;}","syntax.misplaced-label"));
    CHECK(error(es3+"void main(){int x; switch(x){case 0: {case 1: break;}}}","syntax.misplaced-label"));
    CHECK(error(es3+"void main(){int x; switch(x){case 0: if(true) default: break;}}","syntax.misplaced-label"));
    CHECK(error(es3+"void main(){break;}","syntax.misplaced-jump"));
    CHECK(error(es3+"void main(){int x; switch(x){default: continue;}}","syntax.misplaced-jump"));
    CHECK(clean(es3+"void main(){int x; for(;;){switch(x){default: continue;}}}"));
    CHECK(error(es3+"void main(){float a,b; a+b=1.;}","syntax.invalid-assignment"));
    CHECK(clean(es3+"void main(){float a,b; true?a:b=1.;}"));  // parses as true?a:(b=1.)
    CHECK(clean(es3+"void main(){float a,b; (a)=1.; (true?a:b)=1.;}"));   // grammatically unary; rejected by the semantic pass
    CHECK(error(es3+"struct S{};","syntax.empty-struct"));
    CHECK(error(es3+"struct S{float x=1.;};","syntax.member-initializer"));
    CHECK(error(es3+"struct S{struct T{float y;} t;};","syntax.nested-struct"));
    CHECK(error(es3+"struct S{const float x;};","syntax.member-qualifier"));
    CHECK(error(es3+"uniform B{};","syntax.empty-block"));
    CHECK(error(es3+"void main(){if(bool b=true){}}","syntax.expected-token"));
    CHECK(error(es3+"void main(){void f(){}}","syntax.nested-function"));
    CHECK(clean(es3+"void main(){float f(float x);}"));     // local prototypes are a semantic question
    CHECK(error(es3+"float float;","syntax.expected-name"));
    CHECK(error(es3+"Undeclared x;","syntax.global-statement"));
    CHECK(error(es3+"void main(){Undeclared x;}","syntax.expected-token"));
    CHECK(error(es3+"void main(){float x=vec3;}","syntax.expected-token"));
    CHECK(error(es3+"float gl_Mine;","syntax.reserved-name"));
    CHECK(error(es3+"void gl_f(){}","syntax.reserved-name"));
    CHECK(error("#version 100\nfloat a__b;","syntax.reserved-name"));
    CHECK(warning(es3+"float a__b;","syntax.reserved-name"));
    CHECK(error(es3+"void main(){goto x;}","lex.reserved-word"));
    CHECK(error(es3+"void main(){}}","syntax.unbalanced-delimiter"));
    CHECK(error(es3+"void main(){(]}","syntax.unbalanced-delimiter"));
    CHECK(error(es3+"void main(){","syntax.unclosed-delimiter"));
    CHECK(error(es3+"void main(){float x=1.0.xxxx;}","lex.invalid-number"));
    // recovery reports independent errors and keeps going
    {
        auto p=run(es3+"void a(){int x=;}\nvoid b(){int y=1 int z;}\nfloat c=;\nvoid d(){}");
        CHECK(p.sink.errors==3);
        CHECK(p.shader.unit && p.shader.unit->decls.size()==3);
    }
    // limits: deep nesting is diagnosed instead of exhausting the stack
    CHECK(error(es3+"int v="+std::string(200,'(')+"1"+std::string(200,')')+";","syntax.nesting-limit"));
    CHECK(clean(es3+"int v="+std::string(100,'(')+"1"+std::string(100,')')+";"));
    {
        std::string negations=es3+"int v=";
        for(int k=0;k<300;++k)negations+="- ";
        CHECK(error(negations+"1;","syntax.nesting-limit"));
    }
    {
        std::string blocks=es3+"void main(){";
        for(int k=0;k<1000;++k)blocks+="{";
        for(int k=0;k<1000;++k)blocks+="}";
        CHECK(error(blocks+"}","syntax.nesting-limit"));
    }
    {
        std::string sum=es3+"int v=1";
        for(int k=0;k<2000;++k)sum+="+1";
        CHECK(error(sum+";","syntax.nesting-limit"));
        std::string ok=es3+"int v=1";
        for(int k=0;k<200;++k)ok+="+1";
        CHECK(clean(ok+";"));
    }
    {
        std::string ifs=es3+"void main(){bool c;";
        for(int k=0;k<100;++k)ifs+="if(c){";
        for(int k=0;k<100;++k)ifs+="}";
        CHECK(clean(ifs+"}"));
    }
}

void api() {
    auto r=transpile("#version 100\nvoid main(){\n  gl_FragColor = vec4(1.0)\n}\n");
    CHECK(!r.ok() && r.source.empty() && r.diagnostics.size()==1);
    const auto& d=r.diagnostics[0];
    CHECK(d.line==4 && d.column==1 && d.sourceLine=="}" && d.skslLine==0 && !d.suggestion.empty());
    const std::string text=formatDiagnostic(d);
    CHECK(text.find("4:1: error: Invalid syntax: expected ';'")==0);
    CHECK(text.find("GLSL 4 | }")!=std::string::npos && text.find("SkSL - | (no SkSL generated)")!=std::string::npos);
    CHECK(text.find("suggestion: add ';' after '(1.0)'")!=std::string::npos || text.find("suggestion: add ';' after ')'")!=std::string::npos);
    const auto byLine=r.diagnosticsByLine();
    CHECK(byLine.size()==1 && byLine.count(4));
    auto warn=transpile("#version 100\n#extension GL_EXT_shader_texture_lod : enable\nvoid main(){gl_FragColor=vec4(1.0);}");
    CHECK(warn.ok() && !warn.source.empty() && warn.diagnostics.size()==1 && warn.diagnostics[0].severity==Severity::Warning);
    auto derivativesExt=transpile("#version 100\n#extension GL_OES_standard_derivatives : enable\nvoid main(){gl_FragColor=vec4(1.0);}");
    CHECK(derivativesExt.ok() && derivativesExt.diagnostics.empty());
    auto macro=transpile("#version 100\n#define WHITE vec4(1.0)\nvoid main(){gl_FragColor=WHITE;}");
    CHECK(macro.ok() && macro.source.find("float4(1.0)")!=std::string::npos);
    auto suffix=transpile("#version 300 es\nprecision highp float;\nout vec4 c;\nvoid main(){c=vec4(1.5f);}");
    CHECK(suffix.ok() && suffix.source.find("1.5f")==std::string::npos && suffix.source.find("1.5")!=std::string::npos);
}

void fuzz() {
    // Token soup must never crash, hang or throw; only diagnostics are expected.
    static const char* const pieces[]={"#version 300 es\n","#define F(x) x\n","#if 1\n","#endif\n","\n","void","main","(",")","{","}","[","]",";",",",
        "float","vec3","struct","S","x","=","+","-","*","?",":","if","else","for","while","do","switch","case","default","break","return",
        "1","1.5","0x","uniform","layout","in","out","F","(1)","/*","*/","//","\\\n","$","precision","highp","int","++",".","length"};
    std::mt19937 random(12345);
    for(int round=0;round<3000;++round) {
        std::string source;
        const int count=static_cast<int>(random()%60);
        for(int k=0;k<count;++k){source+=pieces[random()%(sizeof pieces/sizeof *pieces)];source+=' ';}
        const auto r=transpile(source);
        for(const auto& d:r.diagnostics)if(d.code=="internal"){++failures;std::cerr<<"internal error for: "<<source<<"\n";}
    }
}
}

int main() {
    lexer();
    preprocessor();
    parser();
    api();
    fuzz();
    if(failures)std::cerr<<failures<<" failed checks\n";
    else std::cout<<"frontend tests passed\n";
    return failures?1:0;
}
