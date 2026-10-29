#include "glsl_sksl/transpiler.hpp"
#include <iostream>
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
    check(!shifts.ok());
    check(shifts.diagnostics.size()>=1 && shifts.diagnostics[0].code=="unsupported.operator");
    auto entry=glsl_sksl::transpile("#version 100\nprecision mediump float;void main(void){gl_FragColor=vec4(1.0);}");
    check(entry.ok());
    check(entry.source.find("entry()")!=std::string::npos);
    check(glsl_sksl::transpile("#version 100  // legacy\r\nvoid main(void){gl_FragColor=vec4(1.0);}").ok());
    check(glsl_sksl::transpile("# version 100\nvoid main(){gl_FragColor=vec4(1);}").ok());
    check(glsl_sksl::transpile("#version   100\nvoid main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("#version 100 extra\nvoid main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("void main(){gl_FragColor=gl_FragCoord;}").ok());
    check(!glsl_sksl::transpile("void main(){gl_FragColor=vec4(1.0);").ok());
    check(!glsl_sksl::transpile("/* unclosed").ok());
    check(glsl_sksl::transpile("#define X 1.0\nvoid main(){gl_FragColor=vec4(X);}").ok());
    check(!glsl_sksl::transpile("void main(void){int n=1;n<<=2;gl_FragColor=vec4(1.0);}").ok());
    check(!glsl_sksl::transpile("uniform sampler2D image; void main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("uniform samplerCube image; void main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("uniform isampler2D image; void main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("uniform usampler3D image; void main(){gl_FragColor=vec4(1);}").ok());
    check(!glsl_sksl::transpile("void main(){} void main(){}").ok());
    if(failures)std::cerr<<failures<<" failed checks\n";
    return failures?1:0;
}
