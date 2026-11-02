#include "glsl_sksl/transpiler.hpp"
#include <iostream>
#include <regex>
#include <vector>
// The target application's SkiaRuntimeEffect::GlslToSksl, verbatim: regular expressions it runs
// over the SkSL. The transpiler's output must come through unchanged.
static std::string hostFixups(std::string glsl) {
    auto fixup = [&glsl](const char* input, const char* replacement) {
        glsl = std::regex_replace(glsl, std::regex(input), replacement);
    };
    std::vector<std::string> childNames;
    std::regex functionRegex(R"(uniform\s+shader\s+(.*);)");
    std::smatch match;
    std::string::const_iterator searchStart(glsl.cbegin());
    while (std::regex_search(searchStart, glsl.cend(), match, functionRegex)) {
        childNames.emplace_back(match[1].str());
        searchStart = match.suffix().first;
    }
    for (const auto& childName : childNames) {
        std::string pattern(childName + R"(\((.*?)\))");
        std::string replacement = childName + ".eval($1)";
        fixup(pattern.c_str(), replacement.c_str());
    }
    fixup(R"(\buniform\s+sampler2D\b)", "uniform shader ");
    fixup(R"(texture\(\s*(\w+)\s*,\s*(\w+)\s*\))", "$01.eval($02)");
    fixup(R"((\d+\.\d+)f)", "$1");
    return glsl;
}
int main(){
    int failures=0;
    auto check=[&](bool value){if(!value)++failures;};
    auto r=glsl_sksl::transpile("#version 100\nprecision mediump float; void main(){gl_FragColor=vec4(1.0);return;}");
    check(r.ok()); check(r.source.find("half4 main(float2 ")!=std::string::npos);
    check(r.source.find("1.0")!=std::string::npos);
    auto numbers=glsl_sksl::transpile("precision mediump float;void main(){float n=.5+1e-3; for(int i=0;i<=2;++i){n+=.1;} gl_FragColor=vec4(n);}");
    check(numbers.ok());
    check(numbers.source.find("1e-3")!=std::string::npos);
    check(numbers.source.find(".5")!=std::string::npos);
    check(numbers.source.find("<=")!=std::string::npos);
    check(numbers.source.find("++")!=std::string::npos);
    check(numbers.source.find("+=")!=std::string::npos);
    auto shifts=glsl_sksl::transpile("#version 300 es\nprecision highp float;out vec4 c;void main(void){int n=1;n<<=2;n>>=1;c=vec4(n);}");
    check(shifts.ok());
    check(shifts.diagnostics.size()>=1 && shifts.diagnostics[0].code=="software.operator");
    auto entry=glsl_sksl::transpile("#version 100\nprecision mediump float;void main(void){gl_FragColor=vec4(1.0);}");
    check(entry.ok());
    check(entry.source.find("return half4(fragColor);")!=std::string::npos);
    check(entry.source.find("transpiler_")==std::string::npos);
    check(glsl_sksl::transpile("#version 100  // legacy\r\nvoid main(void){gl_FragColor=vec4(1.0);}").ok());
    check(glsl_sksl::transpile("# version 100\nvoid main(){gl_FragColor=vec4(1);}").ok());
    check(glsl_sksl::transpile("#version   100\nvoid main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("#version 100 extra\nvoid main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("void main(){gl_FragColor=gl_FragCoord;}").ok());
    check(!glsl_sksl::transpile("void main(){gl_FragColor=vec4(1.0);").ok());
    check(!glsl_sksl::transpile("/* unclosed").ok());
    check(glsl_sksl::transpile("#define X 1.0\nvoid main(){gl_FragColor=vec4(X);}").ok());
    check(!glsl_sksl::transpile("void main(void){int n=1;n<<=2;gl_FragColor=vec4(1.0);}").ok());
    check(glsl_sksl::transpile("uniform sampler2D image; void main(){gl_FragColor=vec4(1);}").ok());
    check(glsl_sksl::transpile("uniform samplerCube image; void main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("uniform isampler2D image; void main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("uniform usampler3D image; void main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("void main(){} void main(){}").ok());
    {
        // A child 's' would turn 'abs(' into 'ab.eval(', and 'blurtexture(a, b)' would become
        // 'blura.eval(b)': such children and called names are renamed.
        const auto host=glsl_sksl::transpile(
            "#version 300 es\nprecision highp float;\nuniform sampler2D s;\nuniform samplerCube t;\nuniform vec2 iResolution;\nout vec4 o;\n"
            "struct Mytexture { vec4 c; };\n"
            "vec4 blurtexture(vec2 a, vec2 b) { return texture(s, a) + texture(s, b); }\n"
            "vec4 cubetexture(samplerCube c, vec3 d) { return texture(c, d); }\n"
            "void main() {\n    vec2 a = gl_FragCoord.xy, b = a * 2.0;\n"
            "    Mytexture m = Mytexture(vec4(abs(a.x), cos(b.y), 0.0, 1.0));\n"
            "    o = blurtexture(a, b) + cubetexture(t, vec3(a, 1.0)) + m.c + sqrt(vec4(dot(a, b)));\n}\n");
        check(host.ok());
        check(hostFixups(host.source)==host.source);
        check(host.source.find("uniform shader s_;")!=std::string::npos);
        check(host.source.find("texture(")==std::string::npos);
        bool reported=false;
        for(const auto& [from,to]:host.renamedIdentifiers)if(from=="s" && to=="s_")reported=true;
        check(reported);
    }
    {
        // Every sampler uniform is reported in Result::textures with its child shaders and size uniform.
        auto tex=glsl_sksl::transpile("#version 300 es\nprecision highp float;\nuniform sampler2D inputTex;\nuniform vec2 inputTexSize;\nuniform samplerCube env;\nuniform sampler2D arr[2];\nout vec4 o;\n"
            "void main() {\n    o = texture(inputTex, gl_FragCoord.xy / inputTexSize) + vec4(vec2(textureSize(inputTex, 0)), 0.0, 0.0)\n      + texture(env, vec3(1.0)) + texelFetch(arr[1], ivec2(0), 0);\n}\n");
        check(tex.ok());
        check(tex.textures.size()==4);
        if(tex.textures.size()==4){
            const auto& t0=tex.textures[0];
            check(t0.sampler=="inputTex" && t0.element==-1 && t0.type=="sampler2D");
            check(t0.children==std::vector<std::string>{"inputTex"} && t0.sizeUniform=="inputTexSize_2");
            check(tex.source.find("uniform float2 inputTexSize_2;")!=std::string::npos);
            check(tex.source.find("uniform float2 inputTexSize;")!=std::string::npos);
            const auto& t1=tex.textures[1];
            check(t1.sampler=="env" && t1.type=="samplerCube" && t1.children.size()==6 && t1.sizeUniform.empty());
            for(int i=2;i<4;++i){
                const auto& t=tex.textures[i];
                check(t.sampler=="arr" && t.element==i-2 && t.type=="sampler2D" && t.children.size()==1 && !t.sizeUniform.empty());
                if(t.children.size()==1)check(tex.source.find("uniform shader "+t.children[0]+";")!=std::string::npos);
                check(tex.source.find("uniform float2 "+t.sizeUniform+";")!=std::string::npos);
            }
        }
    }
    if(failures)std::cerr<<failures<<" failed checks\n";
    return failures?1:0;
}
