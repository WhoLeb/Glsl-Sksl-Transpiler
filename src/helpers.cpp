#include "helpers.hpp"
namespace glsl_sksl {
using sema::Type;

std::string skslType(const Type& t) {
    std::string s;
    switch(t.base) {
    case Type::Void: s="void"; break;
    case Type::Bool: s="bool"; break;
    case Type::Int: s="int"; break;
    case Type::Uint: s="uint"; break;
    case Type::Float: s="float"; break;
    case Type::Struct: s=t.structDef?t.structDef->name:""; break;
    case Type::Opaque: s=t.opaque; break;
    default: s="<error>"; break;
    }
    if(t.base>=Type::Bool && t.base<=Type::Float) {
        if(t.columns>1)s="float"+std::to_string(t.columns)+"x"+std::to_string(t.rows);
        else if(t.rows>1)s+=std::to_string(t.rows);
    }
    for(int n:t.arrays)s+="["+std::to_string(n)+"]";
    return s;
}

namespace {
const char* const kComponents[]={"x","y","z","w"};
const std::set<std::string> kAlways={"sinh","cosh","tanh","asinh","acosh","atanh","trunc","round","roundEven","modf","fma",
    "determinant","transpose","outerProduct"};
const std::set<std::string> kIntegerOnly={"abs","sign","min","max","clamp"};

// Bodies of the scalar helpers. The hyperbolic functions follow the definitions in section 8.1 of
// the GLSL ES 3.00 specification, rearranged where the literal formula overflows for large |x|.
const std::map<std::string,std::string> kFloatBodies={
    {"sinh","return (exp(x) - exp(-x)) * 0.5;"},
    {"cosh","return (exp(x) + exp(-x)) * 0.5;"},
    {"tanh","float t = exp(-2.0 * abs(x)); return sign(x) * ((1.0 - t) / (1.0 + t));"},
    {"asinh","float a = abs(x); return sign(x) * (a > 1e18 ? log(a) + 0.6931472 : log(a + sqrt(a * a + 1.0)));"},
    {"acosh","return x > 1e18 ? log(x) + 0.6931472 : log(x + sqrt(x * x - 1.0));"},
    {"atanh","return 0.5 * log((1.0 + x) / (1.0 - x));"},
    {"trunc","return x < 0.0 ? -floor(-x) : floor(x);"},
    // GLSL ES leaves the direction of a .5 fraction to the implementation; this rounds it up.
    {"round","float f = floor(x); return x - f >= 0.5 ? f + 1.0 : f;"},
    {"roundEven","float f = floor(x); float d = x - f; if (d > 0.5) return f + 1.0; if (d < 0.5) return f; return mod(f, 2.0) == 0.0 ? f : f + 1.0;"},
};
const std::map<std::string,std::string> kIntBodies={
    {"abs","return x < 0 ? -x : x;"},
    {"sign","return x > 0 ? 1 : (x < 0 ? -1 : 0);"},
};

std::string join(const std::vector<std::string>& parts) {
    std::string s;
    for(std::size_t i=0;i<parts.size();++i)s+=(i?", ":"")+parts[i];
    return s;
}
// arg.x or arg, for an argument that is a vector or a broadcast scalar.
std::string component(const std::string& arg, const Type& t, int i) {
    return t.isVector()?arg+"."+kComponents[i]:arg;
}
}

bool BuiltinHelpers::Key::operator<(const Key& o) const {
    if(name!=o.name)return name<o.name;
    if(params.size()!=o.params.size())return params.size()<o.params.size();
    for(std::size_t i=0;i<params.size();++i) {
        const std::string a=skslType(params[i]), b=skslType(o.params[i]);
        if(a!=b)return a<b;
    }
    return false;
}

bool BuiltinHelpers::needed(const sema::Function& f) {
    if(!f.builtin || f.params.empty())return false;
    if(kAlways.count(f.name))return true;
    if(kIntegerOnly.count(f.name))return f.params[0].type.base==Type::Int;
    if(f.name=="mix")return f.params.size()==3 && f.params[2].type.base==Type::Bool;
    return false;
}

std::string BuiltinHelpers::use(const sema::Function& f) {
    std::vector<Type> params;
    for(const auto& p:f.params)params.push_back(p.type);
    return request(f.name,params);
}

std::string BuiltinHelpers::request(const std::string& name, const std::vector<Type>& params) {
    Key key{name,params};
    if(!requested.count(key)) {
        requested.insert(key);
        std::string source=generate(name,params);   // dependencies are requested, and emitted, first
        sources.push_back(std::move(source));
    }
    return nameOf(name);
}

std::string BuiltinHelpers::nameOf(const std::string& builtin) {
    const auto it=names.find(builtin);
    return it!=names.end()?it->second:names[builtin]=naming(builtin);
}

std::string BuiltinHelpers::definitions() const {
    std::string s;
    for(const auto& d:sources)s+=d+"\n";
    return s;
}

std::string BuiltinHelpers::generate(const std::string& name, const std::vector<Type>& params) {
    const std::string fn=nameOf(name);
    const Type& t=params[0];
    const std::string T=skslType(t);
    // Vector overload that applies the scalar overload to each component.
    auto perComponent=[&](const Type& result, const std::vector<std::string>& names) {
        std::vector<Type> scalars;
        for(const auto& p:params)scalars.push_back(p.component());
        request(name,scalars);
        std::vector<std::string> declarations, calls;
        for(std::size_t i=0;i<params.size();++i)declarations.push_back(skslType(params[i])+" "+names[i]);
        for(int c=0;c<t.rows;++c) {
            std::vector<std::string> args;
            for(std::size_t i=0;i<params.size();++i)args.push_back(component(names[i],params[i],c));
            calls.push_back(fn+"("+join(args)+")");
        }
        return skslType(result)+" "+fn+"("+join(declarations)+") { return "+skslType(result)+"("+join(calls)+"); }";
    };
    const int n=t.isMatrix()?t.columns:t.rows;
    if(kFloatBodies.count(name)||kIntBodies.count(name)) {
        if(t.isVector())return perComponent(t,{"x"});
        return T+" "+fn+"("+T+" x) { "+(t.base==Type::Int?kIntBodies.at(name):kFloatBodies.at(name))+" }";
    }
    if(name=="min"||name=="max") {
        if(t.isVector())return perComponent(t,{"a","b"});
        return "int "+fn+"(int a, int b) { return a "+(name=="min"?"<":">")+" b ? a : b; }";
    }
    if(name=="clamp") {
        if(t.isVector())return perComponent(t,{"x","lo","hi"});
        const std::string min=request("min",{t,t}), max=request("max",{t,t});
        return "int "+fn+"(int x, int lo, int hi) { return "+min+"("+max+"(x, lo), hi); }";
    }
    if(name=="mix") {
        if(t.isVector())return perComponent(t,{"x","y","a"});
        return T+" "+fn+"("+T+" x, "+T+" y, bool a) { return a ? y : x; }";
    }
    if(name=="modf") {
        const std::string trunc=request("trunc",{t});
        return T+" "+fn+"("+T+" x, out "+T+" i) { i = "+trunc+"(x); return x - i; }";
    }
    if(name=="fma")return T+" "+fn+"("+T+" a, "+T+" b, "+T+" c) { return a * b + c; }";
    if(name=="transpose") {
        std::vector<std::string> elements;
        for(int c=0;c<n;++c)for(int r=0;r<n;++r)elements.push_back("m["+std::to_string(r)+"]["+std::to_string(c)+"]");
        return T+" "+fn+"("+T+" m) { return "+T+"("+join(elements)+"); }";
    }
    if(name=="outerProduct") {
        const std::string M=skslType(Type::of(Type::Float,n,n));
        std::vector<std::string> columns;
        for(int c=0;c<n;++c)columns.push_back("c * r."+std::string(kComponents[c]));
        return M+" "+fn+"("+T+" c, "+T+" r) { return "+M+"("+join(columns)+"); }";
    }
    if(name=="determinant") {
        if(n==2)return "float "+fn+"(float2x2 m) { return m[0][0] * m[1][1] - m[1][0] * m[0][1]; }";
        if(n==3)return "float "+fn+"(float3x3 m) { return m[0][0] * (m[1][1] * m[2][2] - m[2][1] * m[1][2])"
            " - m[1][0] * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) + m[2][0] * (m[0][1] * m[1][2] - m[1][1] * m[0][2]); }";
        return "float "+fn+"(float4x4 m) {"
            " float b00 = m[0][0] * m[1][1] - m[0][1] * m[1][0]; float b01 = m[0][0] * m[1][2] - m[0][2] * m[1][0];"
            " float b02 = m[0][0] * m[1][3] - m[0][3] * m[1][0]; float b03 = m[0][1] * m[1][2] - m[0][2] * m[1][1];"
            " float b04 = m[0][1] * m[1][3] - m[0][3] * m[1][1]; float b05 = m[0][2] * m[1][3] - m[0][3] * m[1][2];"
            " float b06 = m[2][0] * m[3][1] - m[2][1] * m[3][0]; float b07 = m[2][0] * m[3][2] - m[2][2] * m[3][0];"
            " float b08 = m[2][0] * m[3][3] - m[2][3] * m[3][0]; float b09 = m[2][1] * m[3][2] - m[2][2] * m[3][1];"
            " float b10 = m[2][1] * m[3][3] - m[2][3] * m[3][1]; float b11 = m[2][2] * m[3][3] - m[2][3] * m[3][2];"
            " return b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06; }";
    }
    return "";
}
}
