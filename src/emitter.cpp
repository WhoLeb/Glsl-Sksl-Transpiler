#include "emitter.hpp"
#include "helpers.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <unordered_map>
#ifdef _MSC_VER
#define GLSL_SKSL_NOINLINE __declspec(noinline)
#else
#define GLSL_SKSL_NOINLINE __attribute__((noinline))
#endif
namespace glsl_sksl {
namespace {
using ast::Expr;
using ast::ExprKind;
using ast::Stmt;
using ast::StmtKind;
using ast::DeclKind;
using sema::Symbol;
using sema::Storage;
using sema::Type;
using sema::Direction;

// Identifiers that GLSL ES allows but SkSL reserves for its own types and intrinsics.
const std::set<std::string> kReserved={
    "half","half2","half3","half4","half2x2","half3x3","half4x4","half2x3","half2x4","half3x2","half3x4","half4x2","half4x3",
    "float2","float3","float4","float2x2","float3x3","float4x4","float2x3","float2x4","float3x2","float3x4","float4x2","float4x3",
    "int2","int3","int4","uint2","uint3","uint4","bool2","bool3","bool4","short","short2","short3","short4",
    "ushort","ushort2","ushort3","ushort4","shader","colorFilter","blender","inline","noinline",
    "saturate","unpremul","toLinearSrgb","fromLinearSrgb"};

const char* const kComponents="xyzw";

const Symbol* root(const Expr& e) {
    const Expr* x=&e;
    while(x->kind==ExprKind::Index||x->kind==ExprKind::Field)x=x->operands[0].get();
    return x->kind==ExprKind::Name?x->symbol:nullptr;
}
bool isWrite(const Expr& e) {
    return e.kind==ExprKind::Assign||((e.kind==ExprKind::Unary||e.kind==ExprKind::Postfix) && (e.text=="++"||e.text=="--"));
}
bool isDerivativeCall(const Expr& e) {
    return e.kind==ExprKind::Call && e.function && e.function->builtin && sema::isDerivative(e.function->name);
}

int binaryPrecedence(const std::string& op) {
    if(op=="||")return 4;
    if(op=="^^")return 5;
    if(op=="&&")return 6;
    if(op=="|")return 7;
    if(op=="^")return 8;
    if(op=="&")return 9;
    if(op=="=="||op=="!=")return 10;
    if(op=="<"||op==">"||op=="<="||op==">=")return 11;
    if(op=="<<"||op==">>")return 12;
    if(op=="+"||op=="-")return 13;
    return 14;   // * / %
}
int precedence(const Expr& e) {
    switch(e.kind) {
    case ExprKind::Comma: return 1;
    case ExprKind::Assign: return 2;
    case ExprKind::Ternary: return 3;
    case ExprKind::Binary: return binaryPrecedence(e.text);
    case ExprKind::Unary: return 15;
    case ExprKind::Postfix: case ExprKind::Call: case ExprKind::Constructor: case ExprKind::Index:
    case ExprKind::Field: case ExprKind::Method: return 16;
    default: return 17;
    }
}

// Shortest decimal spelling that reads back as the same 32-bit float.
std::string floatText(float f) {
    char buffer[48];
    for(int digits=1;digits<=9;++digits) {
        std::snprintf(buffer,sizeof buffer,"%.*g",digits,static_cast<double>(f));
        if(std::strtof(buffer,nullptr)==f)break;
    }
    std::string s=buffer;
    if(s.find_first_of(".e")==std::string::npos)s+=".0";
    return s;
}
bool scalarText(const sema::Scalar& v, std::string& out) {
    std::string s;
    switch(v.base) {
    case Type::Float:
        if(!(v.f==v.f) || v.f>3.4e38f || v.f<-3.4e38f)return false;
        s=floatText(v.f);
        break;
    case Type::Int:
        s=v.i==INT32_MIN?"-2147483647 - 1":std::to_string(v.i);
        break;
    case Type::Bool: out+=v.b?"true":"false"; return true;
    default: return false;
    }
    out+=s[0]=='-'?"("+s+")":s;
    return true;
}
void intText(std::string& out, std::uint64_t raw) {
    const auto v=static_cast<std::uint32_t>(raw);
    if(v<=0x7FFFFFFFu)out+=std::to_string(v);
    else if(v==0x80000000u)out+="(-2147483647 - 1)";
    else out+="(-"+std::to_string(0x100000000ull-v)+")";
}

class Emitter {
public:
    Emitter(const ast::TranslationUnit& unit, const sema::Program& program, const std::vector<Token>& tokens, DiagnosticSink& sink)
        :unit(unit),program(program),helpers([this](const std::string& builtin){return fresh("glsl_"+builtin);}),sink(sink) {
        // Names the shader uses keep their spelling; generated names avoid all of them.
        for(const auto& t:tokens)if(t.kind==TokenKind::Identifier)taken.insert(t.text);
        taken.insert(kReserved.begin(),kReserved.end());
        taken.insert("main");
        colorName=program.outputs.empty()?fresh("fragColor"):userName(program.outputs[0]->name);
        coordName=fresh("coord"); fragCoordName=fresh("fragCoord"); quadName=fresh("quad");
        retName=fresh("ret"); doneName=fresh("done"); resultName=fresh("result");
    }

    std::string run() {
        if(!program.main)return {};
        if(!program.writesFragColor && program.outputs.empty())
            error(Loc{1,1},"output","shader without an output: it neither writes gl_FragColor nor declares an 'out vec4'",
                "write the color to gl_FragColor (GLSL ES 1.00) or to an 'out vec4' variable");
        nameStructs();
        analyzeFunctions();
        lanes=quad(program.main);
        std::vector<const sema::Function*> order;
        std::set<const sema::Function*> seen;
        visitOrder(program.main,seen,order);
        for(const sema::Function* f:order)if(functions[f].fragCoord)usesFragCoord=true;

        std::string structs, globals, code;
        out=&structs; structDefinitions();
        out=&globals; generatedGlobals(); globalDeclarations();
        out=&code;
        for(const sema::Function* f:order)function(*f);   // main last: it becomes the SkSL main
        return helpers.definitions()+structs+globals+code;
    }

private:
    struct FunctionInfo {
        std::vector<const sema::Function*> callees;     // user functions with a definition, in call order
        bool derivative=false, touches=false, writes=false, fragCoord=false;   // in the function itself
        bool quad=false, touchesAll=false, writesAll=false, closed=false;      // including callees
    };
    struct StmtInfo { bool sync=false, returns=false, jumps=false; };          // jumps: break/continue that leave it
    struct Access { std::set<const Symbol*> reads, writes; bool readsGlobals=false, writesGlobals=false; };

    const ast::TranslationUnit& unit;
    const sema::Program& program;
    mutable std::set<std::string> taken;                    // identifiers of the shader and generated names
    mutable std::map<std::string,std::string> renamed;      // SkSL-reserved identifiers -> replacement
    mutable std::map<std::pair<std::string,int>,std::string> laneNames;
    std::string colorName, coordName, fragCoordName, quadName, retName, doneName, resultName;
    BuiltinHelpers helpers;
    DiagnosticSink& sink;
    std::map<const sema::Function*,FunctionInfo> functions;
    std::map<const sema::StructDef*,std::string> structNames;
    std::unordered_map<const Stmt*,StmtInfo> stmtInfo;
    bool lanes=false, usesFragCoord=false;

    // State of the function being emitted.
    const sema::Function* current=nullptr;
    int tailLane=0;                         // lane whose returns become assignments (tail of a quad function)
    std::set<const Symbol*> shared;         // indices of loops that all lanes run together
    std::set<const Symbol*> varying;        // locals whose value can differ between the pixels of a quad
    std::unordered_map<const Expr*,std::string> hoisted;    // derivative or quad call -> temporary, all lanes
    std::unordered_map<const Expr*,std::string> values[4];  // per lane: values computed before the current statement
    std::vector<std::pair<int,const Expr*>> pending;        // entries of values, in the order computed
    std::set<const Expr*> reported;
    std::vector<const ast::Declarator*> globalArrays;       // global arrays initialized at the start of main
    int temps=0;
    std::string* out=nullptr;
    int indent=0;

    // ------------------------------------------------------------------ output
    void line(const std::string& s) {
        out->append(static_cast<std::size_t>(indent)*4,' ');
        *out+=s;
        *out+='\n';
    }
    void error(Loc at, const std::string& code, const std::string& message, const std::string& suggestion={}) {
        sink.error(at,"unsupported."+code,"Unsupported "+message,suggestion);
    }
    // A generated name that no identifier of the shader and no other generated name uses.
    std::string fresh(const std::string& base) const {
        std::string name=base;
        for(int n=2;taken.count(name);++n)name=base+"_"+std::to_string(n);
        taken.insert(name);
        return name;
    }
    std::string temp(){return fresh("tmp"+std::to_string(++temps));}
    // Name of a variable in another pixel of the quad: x, x_L1, x_L2, x_L3.
    std::string laneName(const std::string& name, int lane) const {
        if(lane==0)return name;
        const auto key=std::make_pair(name,lane);
        const auto it=laneNames.find(key);
        if(it!=laneNames.end())return it->second;
        return laneNames[key]=fresh(name+"_L"+std::to_string(lane));
    }
    std::string userName(const std::string& name) const {
        if(!kReserved.count(name) && name.compare(0,3,"sk_")!=0)return name;
        const auto it=renamed.find(name);
        if(it!=renamed.end())return it->second;
        return renamed[name]=fresh(name.compare(0,3,"sk_")==0?"_"+name:name+"_");
    }
    std::string typeName(const Type& t) const {
        if(t.base==Type::Struct) {
            const auto it=structNames.find(t.structDef);
            return it!=structNames.end()?it->second:(t.structDef?t.structDef->name:std::string());
        }
        Type b=t; b.arrays.clear();
        return skslType(b);
    }
    static std::string dims(const Type& t) {
        std::string s;
        for(int n:t.arrays)s+="["+std::to_string(n)+"]";
        return s;
    }
    std::string symbolName(const Symbol* s, int lane) const {
        if(!s)return "?";
        switch(s->storage) {
        case Storage::BuiltIn:
            if(s->name=="gl_FragColor")return laneName(colorName,lane);
            if(s->name=="gl_FragCoord")return laneName(fragCoordName,lane);
            return s->name;
        case Storage::Output: return laneName(colorName,lane);
        case Storage::Uniform: return s->name;
        case Storage::Global: return s->constQualified?userName(s->name):laneName(userName(s->name),lane);
        default: return shared.count(s)?userName(s->name):laneName(userName(s->name),lane);
        }
    }
    // A function that computes derivatives has one version, which takes all four lanes.
    std::string functionName(const sema::Function* f, int lane) {
        if(laneFunction(f))return laneName(userName(f->name),lane);
        return userName(f->name);
    }

    // ------------------------------------------------------------------ analysis
    template<class F> static void eachExpr(const Expr& e, F& f) {
        f(e);
        for(const auto& o:e.operands)eachExpr(*o,f);
    }
    template<class F> static void eachExpr(const Stmt& s, F& f) {
        for(const Expr* e:{s.expr.get(),s.step.get()})if(e)eachExpr(*e,f);
        if(s.decl)for(const auto& v:s.decl->declarators)if(v.init)eachExpr(*v.init,f);
        for(const auto& x:s.body)eachExpr(*x,f);
        for(const Stmt* x:{s.then.get(),s.otherwise.get(),s.init.get(),s.loop.get()})if(x)eachExpr(*x,f);
    }
    // Variables with one value per pixel outside any function: user globals, the output and the coordinate.
    static bool perPixel(const Symbol* s) {
        if(!s)return false;
        if(s->storage==Storage::Output)return true;
        if(s->storage==Storage::Global)return !s->constQualified;
        if(s->storage==Storage::BuiltIn)return s->name=="gl_FragColor"||s->name=="gl_FragCoord";
        return false;
    }

    void analyzeFunctions() {
        for(const auto& f:program.functions) {
            if(f.builtin || !f.definition || !f.definition->body)continue;
            FunctionInfo& info=functions[&f];
            auto visit=[&](const Expr& e) {
                if(e.kind==ExprKind::Name && perPixel(e.symbol)) {
                    info.touches=true;
                    if(e.symbol->name=="gl_FragCoord" && e.symbol->storage==Storage::BuiltIn)info.fragCoord=true;
                }
                if(isWrite(e) && perPixel(root(*e.operands[0])))info.writes=true;
                if(e.kind!=ExprKind::Call || !e.function)return;
                const sema::Function& callee=*e.function;
                for(std::size_t i=0;i<callee.params.size() && i<e.operands.size();++i)
                    if(callee.params[i].direction!=Direction::In && perPixel(root(*e.operands[i])))info.writes=true;
                if(callee.builtin) {
                    if(sema::isDerivative(callee.name))info.derivative=true;
                } else if(callee.definition && std::find(info.callees.begin(),info.callees.end(),&callee)==info.callees.end())
                    info.callees.push_back(&callee);
            };
            eachExpr(*f.definition->body,visit);
        }
        for(auto& f:functions)close(f.first);
    }
    FunctionInfo& close(const sema::Function* f) {
        FunctionInfo& info=functions[f];
        if(info.closed)return info;
        info.closed=true;
        info.quad=info.derivative; info.touchesAll=info.touches; info.writesAll=info.writes;
        for(const sema::Function* c:info.callees) {
            const FunctionInfo& ci=close(c);
            info.quad=info.quad||ci.quad;
            info.touchesAll=info.touchesAll||ci.touchesAll;
            info.writesAll=info.writesAll||ci.writesAll;
        }
        return info;
    }
    const FunctionInfo* infoOf(const sema::Function* f) const {
        if(!f || f->builtin)return nullptr;
        const auto it=functions.find(f);
        return it==functions.end()?nullptr:&it->second;
    }
    bool quad(const sema::Function* f) const {const FunctionInfo* i=infoOf(f);return i && i->quad;}
    bool touchesGlobals(const sema::Function* f) const {const FunctionInfo* i=infoOf(f);return i && i->touchesAll;}
    bool writesGlobals(const sema::Function* f) const {const FunctionInfo* i=infoOf(f);return i && i->writesAll;}
    // Functions without derivatives that use per-pixel globals get one copy per lane.
    bool laneFunction(const sema::Function* f) const {return lanes && !quad(f) && touchesGlobals(f);}
    bool isQuadCall(const Expr& e) const {return e.kind==ExprKind::Call && quad(e.function);}
    bool isSync(const Expr& e) const {return isDerivativeCall(e)||isQuadCall(e);}

    void visitOrder(const sema::Function* f, std::set<const sema::Function*>& seen, std::vector<const sema::Function*>& order) {
        if(!f || !seen.insert(f).second)return;
        const auto it=functions.find(f);
        if(it==functions.end())return;
        for(const sema::Function* c:it->second.callees)visitOrder(c,seen,order);
        order.push_back(f);
    }

    const StmtInfo& info(const Stmt& s) {
        const auto found=stmtInfo.find(&s);
        if(found!=stmtInfo.end())return found->second;
        StmtInfo i;
        auto visit=[&](const Expr& e){if(isSync(e))i.sync=true;};
        for(const Expr* e:{s.expr.get(),s.step.get()})if(e)eachExpr(*e,visit);
        if(s.decl)for(const auto& v:s.decl->declarators)if(v.init)eachExpr(*v.init,visit);
        if(s.kind==StmtKind::Return)i.returns=true;
        if(s.kind==StmtKind::Break||s.kind==StmtKind::Continue)i.jumps=true;
        const bool encloses=s.kind==StmtKind::For||s.kind==StmtKind::While||s.kind==StmtKind::DoWhile||s.kind==StmtKind::Switch;
        auto child=[&](const Stmt& c) {
            const StmtInfo ci=info(c);
            i.sync=i.sync||ci.sync;
            i.returns=i.returns||ci.returns;
            if(!encloses)i.jumps=i.jumps||ci.jumps;
        };
        for(const auto& x:s.body)child(*x);
        for(const Stmt* x:{s.then.get(),s.otherwise.get(),s.init.get(),s.loop.get()})if(x)child(*x);
        return stmtInfo[&s]=i;
    }

    bool sideEffects(const Expr& e) const {
        bool found=false;
        auto visit=[&](const Expr& x) {
            if(isWrite(x))found=true;
            if(x.kind==ExprKind::Call && x.function) {
                for(const auto& p:x.function->params)if(p.direction!=Direction::In)found=true;
                if(writesGlobals(x.function))found=true;
            }
        };
        eachExpr(e,visit);
        return found;
    }
    void access(const Expr& e, Access& a, const Expr* rootAssign, bool skipSync) const {
        if(skipSync && isSync(e))return;
        if(e.kind==ExprKind::Name && e.symbol)a.reads.insert(e.symbol);
        if(isWrite(e) && &e!=rootAssign)
            if(const Symbol* s=root(*e.operands[0]))a.writes.insert(s);
        if(e.kind==ExprKind::Call && e.function) {
            for(std::size_t i=0;i<e.function->params.size() && i<e.operands.size();++i)
                if(e.function->params[i].direction!=Direction::In)
                    if(const Symbol* s=root(*e.operands[i]))a.writes.insert(s);
            a.readsGlobals=a.readsGlobals||touchesGlobals(e.function);
            a.writesGlobals=a.writesGlobals||writesGlobals(e.function);
        }
        for(std::size_t i=0;i<e.operands.size();++i) {
            // The target of the statement's own assignment is written after the value is computed.
            if(&e==rootAssign && i==0 && e.operands[0]->kind==ExprKind::Name)continue;
            access(*e.operands[i],a,rootAssign,skipSync);
        }
    }
    void access(const Stmt& s, Access& a) const {
        for(const Expr* e:{s.expr.get(),s.step.get()})if(e)access(*e,a,nullptr,false);
        if(s.decl)for(const auto& v:s.decl->declarators) {
            if(v.symbol)a.writes.insert(v.symbol);
            if(v.init)access(*v.init,a,nullptr,false);
        }
        for(const auto& x:s.body)access(*x,a);
        for(const Stmt* x:{s.then.get(),s.otherwise.get(),s.init.get(),s.loop.get()})if(x)access(*x,a);
    }
    static bool readsPerPixel(const Access& a) {
        for(const Symbol* s:a.reads)if(perPixel(s))return true;
        return a.readsGlobals;
    }
    static bool writesPerPixel(const Access& a) {
        for(const Symbol* s:a.writes)if(perPixel(s))return true;
        return a.writesGlobals;
    }
    // Whether moving `moved` before `rest` can change what either computes. Variables are compared
    // directly; globals that called functions use are compared as a group.
    static bool conflict(const Access& moved, const Access& rest) {
        for(const Symbol* s:moved.reads)if(rest.writes.count(s))return true;
        for(const Symbol* s:moved.writes)if(rest.reads.count(s)||rest.writes.count(s))return true;
        if(rest.writesGlobals && readsPerPixel(moved))return true;
        if(moved.readsGlobals && writesPerPixel(rest))return true;
        if(moved.writesGlobals && (readsPerPixel(rest)||writesPerPixel(rest)))return true;
        if(rest.readsGlobals && writesPerPixel(moved))return true;
        return false;
    }

    // Uniformity: whether an expression has the same value in the four pixels of the quad.
    bool uniform(const Expr& e) const {
        if(e.constant)return true;
        switch(e.kind) {
        case ExprKind::Name: {
            const Symbol* s=e.symbol;
            if(!s)return false;
            switch(s->storage) {
            case Storage::Uniform: return true;
            case Storage::Global: return s->constQualified;
            case Storage::Local: case Storage::Parameter: return !varying.count(s);
            default: return false;
            }
        }
        case ExprKind::Call:
            if(!e.function)return false;
            if(!e.function->builtin && (quad(e.function)||touchesGlobals(e.function)))return false;
            break;
        default: break;
        }
        for(const auto& o:e.operands)if(!uniform(*o))return false;
        return true;
    }
    void markVarying(const Symbol* s, bool& changed) {
        if(s && (s->storage==Storage::Local||s->storage==Storage::Parameter) && varying.insert(s).second)changed=true;
    }
    void writesVarying(const Expr& e, bool control, bool& changed) {
        if(e.kind==ExprKind::Ternary) {
            writesVarying(*e.operands[0],control,changed);
            const bool u=control && uniform(*e.operands[0]);
            writesVarying(*e.operands[1],u,changed);
            writesVarying(*e.operands[2],u,changed);
            return;
        }
        if(e.kind==ExprKind::Binary && (e.text=="&&"||e.text=="||")) {
            writesVarying(*e.operands[0],control,changed);
            writesVarying(*e.operands[1],control && uniform(*e.operands[0]),changed);
            return;
        }
        if(e.kind==ExprKind::Assign && !(control && uniform(*e.operands[1]) && uniform(*e.operands[0])))markVarying(root(*e.operands[0]),changed);
        if(isWrite(e) && e.kind!=ExprKind::Assign && !(control && uniform(*e.operands[0])))markVarying(root(*e.operands[0]),changed);
        if(e.kind==ExprKind::Call && e.function) {
            const bool u=control && uniform(e);
            for(std::size_t i=0;i<e.function->params.size() && i<e.operands.size();++i)
                if(e.function->params[i].direction!=Direction::In && !u)markVarying(root(*e.operands[i]),changed);
        }
        for(const auto& o:e.operands)writesVarying(*o,control,changed);
    }
    // Returns 1 when some pixels of the quad may have returned, 2 when some may have left a loop.
    int walkVarying(const Stmt& s, bool control, bool& changed) {
        switch(s.kind) {
        case StmtKind::Block: case StmtKind::Switch: {
            int flags=0;
            bool c=control;
            if(s.kind==StmtKind::Switch) {
                writesVarying(*s.expr,control,changed);
                c=control && uniform(*s.expr);
            }
            for(const auto& x:s.body) {
                const int f=walkVarying(*x,c,changed);
                flags|=f;
                if(f&1)c=false;
            }
            return flags;
        }
        case StmtKind::Declaration:
            for(const auto& v:s.decl->declarators)
                if(v.init) {
                    writesVarying(*v.init,control,changed);
                    if(!(control && uniform(*v.init)))markVarying(v.symbol,changed);
                }
            return 0;
        case StmtKind::Expression: writesVarying(*s.expr,control,changed); return 0;
        case StmtKind::If: {
            writesVarying(*s.expr,control,changed);
            const bool u=control && uniform(*s.expr);
            int flags=walkVarying(*s.then,u,changed);
            if(s.otherwise)flags|=walkVarying(*s.otherwise,u,changed);
            return flags;
        }
        case StmtKind::For: {
            int flags=walkVarying(*s.loop,control,changed);
            // After a pixel-dependent exit the remaining iterations run for some pixels only.
            if(flags)flags|=walkVarying(*s.loop,false,changed);
            return flags&1;
        }
        case StmtKind::Return:
            if(s.expr)writesVarying(*s.expr,control,changed);
            return control?0:1;
        case StmtKind::Break: case StmtKind::Continue: return control?0:2;
        default: return 0;
        }
    }
    void computeVarying(const sema::Function& f, bool parametersVary) {
        varying.clear();
        if(parametersVary)for(const auto& p:f.definition->params)if(p.symbol)varying.insert(p.symbol);
        bool changed=true;
        while(changed) {
            changed=false;
            walkVarying(*f.definition->body,true,changed);
        }
    }

    // ------------------------------------------------------------------ expressions
    std::string expr(const Expr& e, int lane, int context=0) {
        std::string s;
        expr(s,e,lane,context);
        return s;
    }
    void expr(std::string& o, const Expr& e, int lane, int context) {
        const auto h=hoisted.find(&e);
        if(h!=hoisted.end()) {
            o+=laneName(h->second,lane);
            return;
        }
        if(const std::string* v=computed(e,lane)) {
            o+=*v;
            return;
        }
        const bool parens=e.parenthesized||precedence(e)<context;
        if(parens)o+='(';
        node(o,e,lane);
        if(parens)o+=')';
    }
    void node(std::string& o, const Expr& e, int lane) {
        switch(e.kind) {
        case ExprKind::IntLiteral: case ExprKind::UintLiteral: intText(o,e.intValue); break;
        case ExprKind::FloatLiteral: floatLiteral(o,e); break;
        case ExprKind::BoolLiteral: o+=e.intValue?"true":"false"; break;
        case ExprKind::Name: o+=symbolName(e.symbol,lane); break;
        case ExprKind::Unary: if(!folded(o,e))unary(o,e,lane); break;
        case ExprKind::Postfix: expr(o,*e.operands[0],lane,16); o+=e.text; break;
        case ExprKind::Binary:
            if(folded(o,e))break;
            if((e.text=="=="||e.text=="!=") && aggregate(e.operands[0]->type)){comparison(o,e,lane);break;}
            expr(o,*e.operands[0],lane,binaryPrecedence(e.text));
            o+=" "+e.text+" ";
            expr(o,*e.operands[1],lane,binaryPrecedence(e.text)+1);
            break;
        case ExprKind::Assign:
            if(aggregate(e.type))
                notExpressible(e,"assignment","SkSL cannot assign arrays (or structs containing arrays) as a whole");
            expr(o,*e.operands[0],lane,15);
            o+=" "+e.text+" ";
            expr(o,*e.operands[1],lane,2);
            break;
        case ExprKind::Ternary:
            if(aggregate(e.type))
                notExpressible(e,"conditional","the result of ?: cannot be an array (or a struct containing arrays) in SkSL");
            expr(o,*e.operands[0],lane,4);
            o+=" ? ";
            expr(o,*e.operands[1],lane,2);
            o+=" : ";
            expr(o,*e.operands[2],lane,3);
            break;
        case ExprKind::Call: call(o,e,lane); break;
        case ExprKind::Constructor:
            if(e.type.isArray())
                notExpressible(e,"array constructor","SkSL has no array constructors");
            constructor(o,e,lane);
            break;
        case ExprKind::Index:
            if(foldedElement(o,e))break;
            expr(o,*e.operands[0],lane,16);
            o+='[';
            expr(o,*e.operands[1],lane,0);
            o+=']';
            break;
        case ExprKind::Field: if(!foldedElement(o,e))field(o,e,lane); break;
        case ExprKind::Method: length(o,e); break;
        case ExprKind::Comma:
            if(unexpressible(e))
                notExpressible(e,"sequence","the operands of a comma expression cannot be arrays (or structs containing arrays) in SkSL");
            expr(o,*e.operands[0],lane,1);
            o+=", ";
            expr(o,*e.operands[1],lane,2);
            break;
        }
    }
    void arguments(std::string& o, const Expr& e, std::size_t first, int lane) {
        o+='(';
        for(std::size_t i=first;i<e.operands.size();++i) {
            if(i>first)o+=", ";
            expr(o,*e.operands[i],lane,2);
        }
        o+=')';
    }
    // GLSL ignores the components of the last argument that a vector, scalar or matrix constructor
    // does not need, and lets vector constructors take matrices; SkSL requires float3(v.xyz) and
    // the matrix elements.
    GLSL_SKSL_NOINLINE void constructor(std::string& o, const Expr& e, int lane) {
        o+=typeName(e.type)+dims(e.type);
        const bool resize=e.type.isMatrix() && e.operands.size()==1 && e.operands[0]->type.isMatrix();
        if(!e.type.isBasic() || resize){arguments(o,e,0,lane);return;}
        int remaining=e.type.components();
        o+='(';
        for(std::size_t i=0;i<e.operands.size();++i) {
            const Expr& a=*e.operands[i];
            const int k=a.type.components();
            if(i)o+=", ";
            if(a.type.isMatrix() && !e.type.isMatrix() && !sideEffects(a)) {
                const int take=std::min(k,remaining);
                for(int c=0;c<take;++c) {
                    if(c)o+=", ";
                    expr(o,a,lane,16);
                    o+="["+std::to_string(c/a.type.rows)+"]["+std::to_string(c%a.type.rows)+"]";
                }
                remaining-=take;
            } else if(i+1==e.operands.size() && a.type.isBasic() && !a.type.isMatrix() && k>remaining && remaining>0) {
                expr(o,a,lane,16);
                o+='.';
                o.append(kComponents,static_cast<std::size_t>(remaining));
                remaining=0;
            } else {
                expr(o,a,lane,2);
                remaining-=k;
            }
        }
        o+=')';
    }
    GLSL_SKSL_NOINLINE static void floatLiteral(std::string& o, const Expr& e) {
        std::string s=e.text;
        while(!s.empty() && (s.back()=='f'||s.back()=='F'))s.pop_back();
        o+=s;
    }
    GLSL_SKSL_NOINLINE void unary(std::string& o, const Expr& e, int lane) {
        std::string operand;
        expr(operand,*e.operands[0],lane,15);
        o+=e.text;
        // "- -x", not "--x"
        if(!operand.empty() && (operand[0]=='+'||operand[0]=='-') && (e.text[0]=='+'||e.text[0]=='-'))o+=' ';
        o+=operand;
    }
    // Constant integer remainders and bit operations, which SkSL lacks, are replaced by their value.
    GLSL_SKSL_NOINLINE bool folded(std::string& o, const Expr& e) {
        static const std::set<std::string> lowered={"%","&","|","^","~","<<",">>"};
        if(!e.constant || !e.value || !lowered.count(e.text))return false;
        return constantText(o,*e.value);
    }
    bool constantText(std::string& o, const sema::Constant& c) const {
        if(!c.type.isBasic() || c.values.empty())return false;
        std::string s;
        if(c.type.isScalar())return scalarText(c.values[0],o);
        s=typeName(c.type)+"(";
        for(std::size_t i=0;i<c.values.size();++i) {
            if(i)s+=", ";
            if(!scalarText(c.values[i],s))return false;
        }
        o+=s+")";
        return true;
    }
    GLSL_SKSL_NOINLINE void call(std::string& o, const Expr& e, int lane) {
        const sema::Function* f=e.function;
        if(aggregate(e.type))notExpressible(e,"call","SkSL functions cannot return arrays (or structs containing arrays)");
        if(!f)o+=e.text;
        else if(f->builtin) {
            if(sema::isDerivative(f->name))
                sink.error(e.loc,"internal","Internal error: derivative "+ast::toGlsl(e)+" was not lowered");
            o+=BuiltinHelpers::needed(*f)?helpers.use(*f):e.text;
        } else o+=functionName(f,lane);
        arguments(o,e,0,lane);
    }
    GLSL_SKSL_NOINLINE void field(std::string& o, const Expr& e, int lane) {
        const Expr& base=*e.operands[0];
        const Symbol* s=base.kind==ExprKind::Name?base.symbol:nullptr;
        if(s && s->storage==Storage::Uniform && s->type.base==Type::Struct && s->type.structDef && s->type.structDef->block && !s->type.isArray()) {
            o+=e.text;   // member of a flattened uniform block
            return;
        }
        expr(o,base,lane,16);
        o+='.';
        if(!e.swizzle.empty())for(int c:e.swizzle)o+=kComponents[c];
        else o+=userName(e.text);
    }
    GLSL_SKSL_NOINLINE void length(std::string& o, const Expr& e) const {
        if(e.value && constantText(o,*e.value))return;
        const Type& t=e.operands[0]->type;
        o+=std::to_string(t.isArray()?t.arrays.front():t.isMatrix()?t.columns:t.rows);
    }

    // ------------------------------------------------------------------ arrays
    // SkSL has no array constructors and cannot initialize, assign, compare, select or sequence
    // arrays and structs containing arrays, nor return them from functions. Such values are
    // computed before the statement that uses them, element by element and in GLSL's evaluation
    // order; comparisons compare the elements.
    static bool aggregate(const Type& t){return t.containsArray();}
    void valueError(const Expr& e, const std::string& code, const std::string& message, const std::string& suggestion) {
        if(reported.insert(&e).second)error(e.loc,code,message,suggestion);
    }
    // A value the transpiler would compute in statements before its own, where there are none.
    void notExpressible(const Expr& e, const std::string& what, const std::string& why) {
        valueError(e,"array-value",what+" "+quoted(e)+": "+why+", and the transpiler computes such values in statements before the one that uses them, "
            "which this position does not allow (a for loop header, a global initializer or an argument of a function that computes derivatives)",
            "compute the value into a variable before this statement and use the variable");
    }
    // Elements of constant arrays are written as their values: the arrays themselves become
    // variables, which global initializers cannot read.
    bool foldedElement(std::string& o, const Expr& e) const {
        return e.value && aggregate(e.operands[0]->type) && constantText(o,*e.value);
    }
    bool foldsToConstant(const Expr& e) const {
        std::string s;
        return (e.kind==ExprKind::Index||e.kind==ExprKind::Field) && foldedElement(s,e);
    }
    static std::size_t parts(const Type& t){return t.isArray()?static_cast<std::size_t>(t.arrays[0]):t.structDef->fields.size();}
    static Type partType(const Type& t, std::size_t i){return t.isArray()?t.element():t.structDef->fields[i].type;}
    std::string part(const Type& t, std::size_t i) const {
        return t.isArray()?"["+std::to_string(i)+"]":"."+userName(t.structDef->fields[i].name);
    }
    std::string declaration(const Type& t, const std::string& name) const {return typeName(t)+" "+name+dims(t);}

    // Values SkSL cannot write where they occur.
    static bool unexpressible(const Expr& e) {
        switch(e.kind) {
        case ExprKind::Constructor: return e.type.isArray();
        case ExprKind::Call: return aggregate(e.type);
        case ExprKind::Comma: return aggregate(e.type)||aggregate(e.operands[0]->type);
        case ExprKind::Ternary: case ExprKind::Assign: return aggregate(e.type);
        default: return false;
        }
    }
    // Values computed before the current statement, per lane, until release().
    const std::string* computed(const Expr& e, int lane) const {
        const auto it=values[lane].find(&e);
        return it==values[lane].end()?nullptr:&it->second;
    }
    void compute(const Expr& e, int lane, const std::string& text) {
        values[lane][&e]=text;
        pending.emplace_back(lane,&e);
    }
    std::size_t mark() const {return pending.size();}
    void release(std::size_t m) {
        for(;pending.size()>m;pending.pop_back())values[pending.back().first].erase(pending.back().second);
    }
    bool needsPrelude(const Expr& e, int lane) const {
        if(hoisted.count(&e) || computed(e,lane) || foldsToConstant(e) || e.kind==ExprKind::Method)return false;
        if(unexpressible(e))return true;
        for(const auto& o:e.operands)if(needsPrelude(*o,lane))return true;
        return false;
    }

    // Emits, before the statement being written, what SkSL cannot compute inside `e`.
    void prepare(const Expr& e, int lane) {
        if(!needsPrelude(e,lane))return;
        switch(e.kind) {
        case ExprKind::Binary:
            if(e.text=="&&"||e.text=="||"){shortCircuit(e,lane);return;}
            break;
        case ExprKind::Ternary:
            if(!aggregate(e.type)){conditional(e,lane);return;}
            prepare(*e.operands[0],lane);
            materialize(e,lane);
            return;
        case ExprKind::Comma:
            expressionStatement(*e.operands[0],lane);
            prepare(*e.operands[1],lane);
            compute(e,lane,"("+expr(*e.operands[1],lane,0)+")");
            return;
        case ExprKind::Constructor: case ExprKind::Assign:
            if(unexpressible(e)){materialize(e,lane);return;}
            break;
        default: break;
        }
        prepareOperands(e,lane);
        if(unexpressible(e))materialize(e,lane);   // a call that returns an array
    }
    static bool assignedOperand(const Expr& e, std::size_t i) {
        if(e.kind==ExprKind::Assign)return i==0 && e.text=="=";
        return e.kind==ExprKind::Call && e.function && i<e.function->params.size() && e.function->params[i].direction==Direction::Out;
    }
    static bool readWrittenOperand(const Expr& e, std::size_t i) {
        if(e.kind==ExprKind::Assign)return i==0;
        return e.kind==ExprKind::Call && e.function && i<e.function->params.size() && e.function->params[i].direction==Direction::InOut;
    }
    // Operands are evaluated left to right. An operand evaluated before a value that moves before
    // the statement is computed first itself when the order could change a result.
    void prepareOperands(const Expr& e, int lane) {
        std::size_t last=e.operands.size();
        for(std::size_t i=0;i<e.operands.size();++i)if(needsPrelude(*e.operands[i],lane))last=i;
        if(last==e.operands.size())return;
        for(std::size_t i=0;i<last;++i) {
            const Expr& o=*e.operands[i];
            Access before, after;
            access(o,before,nullptr,false);
            for(std::size_t j=i+1;j<=last;++j)access(*e.operands[j],after,nullptr,false);
            // The target of an assignment or out argument is written after everything is evaluated.
            if(assignedOperand(e,i) || !conflict(before,after))prepare(o,lane);
            else if(readWrittenOperand(e,i))
                valueError(o,"evaluation-order",quoted(o)+" in "+quoted(e)+": the transpiler must compute part of the expression before the statement, "
                    "and that part changes "+quoted(o)+", which the expression reads first",
                    "compute the later part in a separate statement before this one");
            else spill(o,lane);
        }
        prepare(*e.operands[last],lane);
    }
    void spill(const Expr& o, int lane) {
        prepare(o,lane);
        const std::string t=temp();
        if(aggregate(o.type)) {
            line(declaration(o.type,t)+";");
            assignParts(t,o.type,o,lane);
        } else line(declaration(o.type,t)+" = "+expr(o,lane,2)+";");
        compute(o,lane,t);
    }
    void materialize(const Expr& e, int lane) {
        const std::string t=temp();
        line(declaration(e.type,t)+";");
        assignParts(t,e.type,e,lane);
        compute(e,lane,t);
    }
    // && and || evaluate their right operand only when needed: when the part that moves before
    // the statement has side effects, the operator becomes an if.
    void shortCircuit(const Expr& e, int lane) {
        const Expr& right=*e.operands[1];
        if(!needsPrelude(right,lane) || !sideEffects(right)){prepareOperands(e,lane);return;}
        prepare(*e.operands[0],lane);
        const std::string t=temp();
        line("bool "+t+" = "+expr(*e.operands[0],lane,2)+";");
        line("if ("+std::string(e.text=="||"?"!":"")+t+") {");
        branch(right,t,lane);
        line("}");
        compute(e,lane,t);
    }
    // Likewise ?: evaluates one of its branches.
    void conditional(const Expr& e, int lane) {
        const Expr& a=*e.operands[1];
        const Expr& b=*e.operands[2];
        if(!(needsPrelude(a,lane)||needsPrelude(b,lane)) || !(sideEffects(a)||sideEffects(b))){prepareOperands(e,lane);return;}
        prepare(*e.operands[0],lane);
        const std::string t=e.type.base==Type::Void?std::string():temp();
        if(!t.empty())line(declaration(e.type,t)+";");
        line("if ("+expr(*e.operands[0],lane,0)+") {");
        branch(a,t,lane);
        line("} else {");
        branch(b,t,lane);
        line("}");
        compute(e,lane,t);
    }
    void branch(const Expr& value, const std::string& target, int lane) {
        ++indent;
        if(target.empty())expressionStatement(value,lane);
        else assignParts(target,value.type,value,lane);
        --indent;
    }

    // Writes `value` into `target`, of type `t`, in statements of their own.
    void assignParts(const std::string& target, const Type& t, const Expr& value, int lane) {
        if(!aggregate(t)) {
            const std::size_t m=mark();
            prepare(value,lane);
            line(target+" = "+expr(value,lane,2)+";");
            release(m);
            return;
        }
        if(const std::string* s=computed(value,lane)){copyParts(target,t,*s);return;}
        switch(value.kind) {
        case ExprKind::Constructor:
            for(std::size_t i=0;i<value.operands.size();++i)assignParts(target+part(t,i),partType(t,i),*value.operands[i],lane);
            return;
        case ExprKind::Ternary: {
            const std::size_t m=mark();
            prepare(*value.operands[0],lane);
            line("if ("+expr(*value.operands[0],lane,0)+") {");
            release(m);
            ++indent; assignParts(target,t,*value.operands[1],lane); --indent;
            line("} else {");
            ++indent; assignParts(target,t,*value.operands[2],lane); --indent;
            line("}");
            return;
        }
        case ExprKind::Comma:
            expressionStatement(*value.operands[0],lane);
            assignParts(target,t,*value.operands[1],lane);
            return;
        case ExprKind::Assign:
            assignStatement(value,lane);
            copyParts(target,t,expr(*value.operands[0],lane,16));
            return;
        case ExprKind::Call: {
            // The result is the function's last, out parameter.
            const std::size_t m=mark();
            prepareOperands(value,lane);
            std::string call=functionName(value.function,lane);
            arguments(call,value,0,lane);
            call.insert(call.size()-1,(value.operands.empty()?"":", ")+target);
            line(call+";");
            release(m);
            return;
        }
        default:
            copyParts(target,t,expr(value,lane,16));   // a variable, element or field
        }
    }
    void copyParts(const std::string& target, const Type& t, const std::string& source) {
        if(!aggregate(t)){line(target+" = "+source+";");return;}
        for(std::size_t i=0;i<parts(t);++i)copyParts(target+part(t,i),partType(t,i),source+part(t,i));
    }
    // Arrays and structs containing arrays are compared element by element. The operands are
    // variables, elements, fields or temporaries, so repeating them has no effect.
    void comparison(std::string& o, const Expr& e, int lane) {
        std::vector<std::string> leaves;
        compareParts(expr(*e.operands[0],lane,16),expr(*e.operands[1],lane,16),e.operands[0]->type,e.text,leaves);
        o+='(';
        for(std::size_t i=0;i<leaves.size();++i)o+=(i?(e.text=="=="?" && ":" || "):"")+leaves[i];
        o+=')';
    }
    void compareParts(const std::string& l, const std::string& r, const Type& t, const std::string& op, std::vector<std::string>& leaves) {
        if(!aggregate(t)){leaves.push_back(l+" "+op+" "+r);return;}
        for(std::size_t i=0;i<parts(t);++i)compareParts(l+part(t,i),r+part(t,i),partType(t,i),op,leaves);
    }

    // Assignment of a whole array or struct containing arrays, as a statement.
    void assignStatement(const Expr& e, int lane) {
        const Expr& target=*e.operands[0];
        const Expr& value=*e.operands[1];
        Access a;
        access(value,a,nullptr,false);
        const Symbol* r=root(target);
        // A value that reads or changes the target is computed into a temporary first, so no element
        // is overwritten before it is read. Variables are either the target itself or disjoint from it.
        const bool variable=root(value) && !sideEffects(value);
        const bool overlap=!variable && (a.reads.count(r)||a.writes.count(r)||((a.readsGlobals||a.writesGlobals) && perPixel(r)));
        const std::string text=expr(target,lane,16);
        if(!overlap){assignParts(text,target.type,value,lane);return;}
        const std::string t=temp();
        line(declaration(target.type,t)+";");
        assignParts(t,target.type,value,lane);
        copyParts(text,target.type,t);
    }
    void declare(const ast::Declarator& v, int lane) {
        const Symbol* s=v.symbol;
        if(v.init && aggregate(s->type)) {
            line(declaration(s->type,symbolName(s,lane))+";");
            assignParts(symbolName(s,lane),s->type,*v.init,lane);
            return;
        }
        const std::size_t m=mark();
        if(v.init)prepare(*v.init,lane);
        line(declarator(v,lane)+";");
        release(m);
    }
    void expressionStatement(const Expr& e, int lane) {
        if(e.kind==ExprKind::Assign && e.text=="=" && aggregate(e.type)){assignStatement(e,lane);return;}
        if(e.kind==ExprKind::Comma && needsPrelude(e,lane)) {
            expressionStatement(*e.operands[0],lane);
            expressionStatement(*e.operands[1],lane);
            return;
        }
        const std::size_t m=mark();
        prepare(e,lane);
        if(!computed(e,lane))line(expr(e,lane)+";");   // otherwise the prelude was the statement
        release(m);
    }

    // ------------------------------------------------------------------ statements, one lane
    std::string declarator(const ast::Declarator& v, int lane) {
        const Symbol* s=v.symbol;
        std::string text=s->constQualified?"const ":"";
        text+=typeName(s->type)+" "+symbolName(s,lane)+dims(s->type);
        if(v.init) {
            text+=" = ";
            expr(text,*v.init,lane,2);
        }
        return text;
    }
    std::string done(int lane) const {return laneName(doneName,lane);}
    bool rewritingReturns(int lane) const {return lane>0 && tailLane==lane;}

    void stmt(const Stmt& s, int lane) {
        switch(s.kind) {
        case StmtKind::Block:
            line("{");
            ++indent; block(s.body,0,lane); --indent;
            line("}");
            break;
        case StmtKind::Declaration:
            for(const auto& v:s.decl->declarators)if(v.symbol)declare(v,lane);
            break;
        case StmtKind::Expression: expressionStatement(*s.expr,lane); break;
        case StmtKind::If: ifStatement(s,lane); break;
        case StmtKind::Switch: switchStatement(s,lane,false); break;
        case StmtKind::For: forStatement(s,lane,false); break;
        case StmtKind::Return: returnStatement(s,lane); break;
        case StmtKind::Break: line("break;"); break;
        case StmtKind::Continue: line("continue;"); break;
        default: break;   // empty statements; while, do-while and discard are refused by the semantic pass
        }
    }
    void block(const std::vector<ast::StmtPtr>& list, std::size_t first, int lane) {
        int guards=0;
        for(std::size_t i=first;i<list.size();++i) {
            stmt(*list[i],lane);
            // In the tail of a quad function the lanes after a return must skip the rest.
            if(rewritingReturns(lane) && i+1<list.size() && info(*list[i]).returns) {
                line("if (!"+done(lane)+") {");
                ++indent; ++guards;
            }
        }
        for(;guards>0;--guards){--indent;line("}");}
    }
    void contents(const Stmt& s, int lane) {
        if(s.kind==StmtKind::Block)block(s.body,0,lane);
        else stmt(s,lane);
    }
    void ifStatement(const Stmt& s, int lane) {
        const std::size_t m=mark();
        prepare(*s.expr,lane);
        line("if ("+expr(*s.expr,lane)+") {");
        release(m);
        ++indent; contents(*s.then,lane); --indent;
        if(s.otherwise) {
            line("} else {");
            ++indent; contents(*s.otherwise,lane); --indent;
        }
        line("}");
    }
    void switchStatement(const Stmt& s, int lane, bool synced) {
        const std::size_t m=mark();
        prepare(*s.expr,lane);
        line("switch ("+expr(*s.expr,lane)+") {");
        release(m);
        ++indent;
        for(const auto& x:s.body) {
            if(x->kind==StmtKind::Case) {
                std::string label;
                if(!(x->expr->value && constantText(label,*x->expr->value)))label=expr(*x->expr,lane);
                line("case "+label+":");
            } else if(x->kind==StmtKind::Default)line("default:");
            else {
                ++indent;
                if(synced)sync(*x);
                else stmt(*x,lane);
                --indent;
            }
        }
        --indent;
        line("}");
    }
    void forStatement(const Stmt& s, int lane, bool synced) {
        std::string h="for (";
        if(s.init && s.init->kind==StmtKind::Declaration && s.init->decl->declarators.size()==1 && s.init->decl->declarators[0].symbol)
            h+=declarator(s.init->decl->declarators[0],lane);
        else if(s.init && s.init->expr)expr(h,*s.init->expr,lane,0);
        h+="; ";
        if(s.expr)expr(h,*s.expr,lane,0);
        h+="; ";
        if(s.step)expr(h,*s.step,lane,0);
        line(h+") {");
        ++indent;
        if(rewritingReturns(lane) && info(*s.loop).returns)line("if ("+done(lane)+") break;");
        if(synced)syncContents(*s.loop);
        else contents(*s.loop,lane);
        --indent;
        line("}");
    }
    void returnStatement(const Stmt& s, int lane) {
        if(current==program.main){line("return half4("+colorName+");");return;}
        if(s.expr && returnsAggregate(*current)) {
            assignParts(resultName,current->returnType,*s.expr,lane);
            line("return;");
            return;
        }
        const std::size_t m=mark();
        if(s.expr)prepare(*s.expr,lane);
        if(rewritingReturns(lane)) {
            if(s.expr)line(laneName(retName,lane)+" = "+expr(*s.expr,lane)+";");
            line(done(lane)+" = true;");
        } else line(s.expr?"return "+expr(*s.expr,lane)+";":"return;");
        release(m);
    }

    // ------------------------------------------------------------------ statements, all lanes together
    // Statements that compute derivatives run for the four pixels of the quad in lockstep; the rest
    // runs once per lane.
    void sync(const Stmt& s) {
        const StmtInfo i=info(s);
        if(!i.sync && !i.returns && !i.jumps) {
            for(int lane=0;lane<4;++lane)stmt(s,lane);
            return;
        }
        switch(s.kind) {
        case StmtKind::Block:
            line("{");
            ++indent; syncContents(s); --indent;
            line("}");
            return;
        case StmtKind::Declaration:
            for(const auto& v:s.decl->declarators) {
                if(!v.symbol)continue;
                if(v.init)hoistStatement(*v.init,nullptr,v.init.get(),false);
                for(int lane=0;lane<4;++lane)declare(v,lane);
            }
            return;
        case StmtKind::Expression: {
            const Expr& e=*s.expr;
            const bool assign=e.kind==ExprKind::Assign;
            hoistStatement(e,assign?&e:nullptr,assign?e.operands[1].get():&e,!assign);
            if(isQuadCall(e))return;   // the hoisted call is the statement
            for(int lane=0;lane<4;++lane)expressionStatement(e,lane);
            return;
        }
        case StmtKind::Return:
            if(s.expr)hoistStatement(*s.expr,nullptr,s.expr.get(),false);
            if(current==program.main){returnStatement(s,0);return;}
            if(s.expr)for(int lane=1;lane<4;++lane) {
                const std::size_t m=mark();
                prepare(*s.expr,lane);
                line(laneName(retName,lane)+" = "+expr(*s.expr,lane)+";");
                release(m);
            }
            returnStatement(s,0);
            return;
        case StmtKind::Break: line("break;"); return;
        case StmtKind::Continue: line("continue;"); return;
        case StmtKind::If: {
            hoistStatement(*s.expr,nullptr,nullptr,false);
            if(!uniform(*s.expr)){divergent(s);return;}
            const std::size_t m=mark();
            prepare(*s.expr,0);
            line("if ("+expr(*s.expr,0)+") {");
            release(m);
            ++indent; syncContents(*s.then); --indent;
            if(s.otherwise) {
                line("} else {");
                ++indent; syncContents(*s.otherwise); --indent;
            }
            line("}");
            return;
        }
        case StmtKind::Switch:
            hoistStatement(*s.expr,nullptr,nullptr,false);
            if(!uniform(*s.expr)){divergent(s);return;}
            switchStatement(s,0,true);
            return;
        case StmtKind::For: {
            const Symbol* index=nullptr;
            if(s.init && s.init->kind==StmtKind::Declaration && !s.init->decl->declarators.empty())index=s.init->decl->declarators[0].symbol;
            if(index)shared.insert(index);
            forStatement(s,0,true);
            if(index)shared.erase(index);
            return;
        }
        default:
            for(int lane=0;lane<4;++lane)stmt(s,lane);
            return;
        }
    }
    void syncContents(const Stmt& s) {
        if(s.kind==StmtKind::Block)for(const auto& x:s.body)sync(*x);
        else sync(s);
    }

    static void outermostSync(const Expr& e, std::vector<const Expr*>& nodes, const Emitter& self) {
        if(self.isSync(e)){nodes.push_back(&e);return;}
        for(const auto& o:e.operands)outermostSync(*o,nodes,self);
    }
    bool quadCallEffects(const Expr& call) const {
        for(const auto& p:call.function->params)if(p.direction!=Direction::In)return true;
        return writesGlobals(call.function);
    }
    // Computes the derivatives and quad calls of a statement's expression before the statement, in
    // evaluation order. `value` is the position whose evaluation may have side effects (the whole
    // expression, or the value of the statement's assignment).
    void hoistStatement(const Expr& e, const Expr* rootAssign, const Expr* value, bool voidAllowed) {
        std::vector<const Expr*> nodes;
        outermostSync(e,nodes,*this);
        if(nodes.empty())return;
        Access rest;
        access(e,rest,rootAssign,true);
        bool ok=true;
        for(const Expr* n:nodes) {
            bool movable=true;
            for(const auto& o:n->operands)if(sideEffects(*o))movable=false;
            if(isQuadCall(*n)) {
                if(quadCallEffects(*n) && n!=value)movable=false;
                if(n->function->returnType.base==Type::Void && !(voidAllowed && n==value))movable=false;
            }
            Access a;
            access(*n,a,nullptr,false);
            if(conflict(a,rest))movable=false;
            if(!movable) {
                ok=false;
                derivativeError(*n,"it cannot be computed apart from the side effects of "+quoted(e),
                    "compute "+quoted(*n)+" in a separate statement before this one");
            }
        }
        if(ok)for(const Expr* n:nodes)hoistIn(*n);
    }
    static std::string quoted(const Expr& e){return "'"+ast::toGlsl(e)+"'";}
    GLSL_SKSL_NOINLINE void derivativeError(const Expr& n, const std::string& why, const std::string& suggestion) {
        const std::string what=isDerivativeCall(n)?"derivative "+quoted(n):"call "+quoted(n)+" (the function computes derivatives)";
        error(n.loc,"derivative",what+": "+why,suggestion);
    }
    void hoistIn(const Expr& e) {
        if(hoisted.count(&e))return;
        for(const auto& o:e.operands)hoistIn(*o);
        if(isDerivativeCall(e))hoistDerivative(e);
        else if(isQuadCall(e))hoistQuadCall(e);
    }
    // GPUs compute fine derivatives as differences within the 2x2 quad: dFdx is the right minus
    // the left pixel of the row, dFdy the upper minus the lower pixel of the column. Lane 1 is the
    // horizontal neighbour, lane 2 the vertical one and lane 3 the diagonal one; quad holds +1 or -1
    // per axis, depending on which side the neighbour is on.
    GLSL_SKSL_NOINLINE void hoistDerivative(const Expr& e) {
        const Expr& a=*e.operands[0];
        const std::string T=typeName(a.type);
        std::string v[4];
        if(a.kind==ExprKind::Name && !hoisted.count(&a))for(int lane=0;lane<4;++lane)v[lane]=expr(a,lane);
        else {
            const std::string t=temp();
            for(int lane=0;lane<4;++lane) {
                v[lane]=laneName(t,lane);
                const std::size_t m=mark();
                prepare(a,lane);
                line(T+" "+v[lane]+" = "+expr(a,lane,2)+";");
                release(m);
            }
        }
        auto difference=[&](const std::string& name, int hi, int lo, const char* axis) {
            line(T+" "+name+" = ("+v[hi]+" - "+v[lo]+") * "+quadName+"."+axis+";");
        };
        auto dx=[&](const std::string& t) {
            difference(t,1,0,"x"); line(T+" "+laneName(t,1)+" = "+t+";");
            difference(laneName(t,2),3,2,"x"); line(T+" "+laneName(t,3)+" = "+laneName(t,2)+";");
        };
        auto dy=[&](const std::string& t) {
            difference(t,2,0,"y"); line(T+" "+laneName(t,2)+" = "+t+";");
            difference(laneName(t,1),3,1,"y"); line(T+" "+laneName(t,3)+" = "+laneName(t,1)+";");
        };
        const std::string& name=e.function->name;
        const std::string result=temp();
        if(name=="dFdx")dx(result);
        else if(name=="dFdy")dy(result);
        else {
            const std::string x=temp(), y=temp();
            dx(x); dy(y);
            for(int lane=0;lane<4;++lane)line(T+" "+laneName(result,lane)+" = abs("+laneName(x,lane)+") + abs("+laneName(y,lane)+");");
        }
        hoisted[&e]=result;
    }
    GLSL_SKSL_NOINLINE void hoistQuadCall(const Expr& e) {
        const sema::Function& f=*e.function;
        const std::size_t m=mark();
        for(int lane=0;lane<4;++lane)for(const auto& o:e.operands)prepare(*o,lane);
        std::string args;
        for(std::size_t i=0;i<e.operands.size();++i)
            for(int lane=0;lane<4;++lane) {
                if(!args.empty())args+=", ";
                expr(args,*e.operands[i],lane,2);
            }
        release(m);
        const std::string name=functionName(&f,0);
        if(f.returnType.base==Type::Void) {
            line(name+"("+args+");");
            hoisted[&e]="";
            return;
        }
        const std::string T=typeName(f.returnType), result=temp();
        for(int lane=1;lane<4;++lane) {
            line(T+" "+laneName(result,lane)+";");
            args+=(args.empty()?"":", ")+laneName(result,lane);
        }
        line(T+" "+result+" = "+name+"("+args+");");
        hoisted[&e]=result;
    }

    // An if or switch whose condition differs between the pixels of the quad. Derivatives in its
    // branches are computed before it, which gives the value GPUs produce when the argument does
    // not change inside the branches; GLSL itself leaves derivatives there undefined.
    void divergent(const Stmt& s) {
        const StmtInfo i=info(s);
        if(i.returns||i.jumps) {
            error(s.loc,"derivative-control","early exit (return, break or continue) under a condition that depends on the pixel position, before a derivative: "
                "GLSL leaves derivatives undefined once the pixels of a 2x2 quad take different paths",
                "compute the derivatives before this statement, or move the return, break or continue after the last derivative");
            return;
        }
        std::vector<const Expr*> nodes;
        bool inLoop=false;
        branchSync(s,true,false,nodes,inLoop);
        Access region;
        access(s,region);
        bool ok=true;
        for(const Expr* n:nodes) {
            Access a;
            access(*n,a,nullptr,false);
            bool movable=!inLoop && !conflict(a,region) && !(isQuadCall(*n) && (quadCallEffects(*n)||n->function->returnType.base==Type::Void));
            for(const auto& o:n->operands)if(sideEffects(*o))movable=false;
            if(!movable) {
                ok=false;
                derivativeError(*n,"it is under a condition that depends on the pixel position, and it "+std::string(inLoop?"is inside a loop there":"reads values the condition's branches change")+
                    "; GLSL leaves derivatives undefined when the pixels of a 2x2 quad take different paths",
                    "compute "+quoted(*n)+" before the "+(s.kind==StmtKind::If?"if":"switch")+" and use the result inside it");
            }
        }
        if(!ok)return;
        for(const Expr* n:nodes)hoistIn(*n);
        for(int lane=0;lane<4;++lane)stmt(s,lane);
    }
    void branchSync(const Stmt& s, bool top, bool loop, std::vector<const Expr*>& nodes, bool& inLoop) {
        if(!info(s).sync)return;
        if(!top) {
            std::vector<const Expr*> found;
            for(const Expr* e:{s.expr.get(),s.step.get()})if(e)outermostSync(*e,found,*this);
            if(s.decl)for(const auto& v:s.decl->declarators)if(v.init)outermostSync(*v.init,found,*this);
            if(!found.empty() && loop)inLoop=true;
            nodes.insert(nodes.end(),found.begin(),found.end());
        }
        const bool inner=loop||s.kind==StmtKind::For||s.kind==StmtKind::While||s.kind==StmtKind::DoWhile;
        for(const auto& x:s.body)branchSync(*x,false,inner,nodes,inLoop);
        for(const Stmt* x:{s.then.get(),s.otherwise.get(),s.init.get(),s.loop.get()})if(x)branchSync(*x,false,inner,nodes,inLoop);
    }

    // ------------------------------------------------------------------ functions
    std::map<const ast::Param*,std::string> unnamed;
    std::string parameterName(const ast::Param& p, std::size_t i, int lane) {
        if(p.symbol)return symbolName(p.symbol,lane);
        auto it=unnamed.find(&p);
        if(it==unnamed.end())it=unnamed.emplace(&p,fresh("p"+std::to_string(i))).first;
        return laneName(it->second,lane);
    }
    std::string parameter(const sema::Function& f, std::size_t i, int lane) {
        const sema::Param& p=f.params[i];
        const char* direction=p.direction==Direction::Out?"out ":p.direction==Direction::InOut?"inout ":"";
        return direction+typeName(p.type)+" "+parameterName(f.definition->params[i],i,lane)+dims(p.type);
    }
    // SkSL functions cannot return arrays or structs containing arrays: the value is returned
    // through an out parameter instead.
    static bool returnsAggregate(const sema::Function& f){return aggregate(f.returnType);}
    // The GLSL main becomes the SkSL main, which returns the color.
    void mainSignature() {
        line("half4 main(float2 "+coordName+") {");
        ++indent;
        mainPrologue();
    }
    void function(const sema::Function& f) {
        current=&f;
        shared.clear();
        if(quad(&f)) {
            quadFunction(f);
            return;
        }
        if(&f==program.main) {
            mainSignature();
            block(f.definition->body->body,0,0);
            line("return half4("+colorName+");");
            --indent;
            line("}");
            return;
        }
        const int copies=laneFunction(&f)?4:1;
        for(int lane=0;lane<copies;++lane) {
            std::string signature=(returnsAggregate(f)?"void":typeName(f.returnType))+" "+functionName(&f,lane)+"(";
            for(std::size_t i=0;i<f.params.size();++i)signature+=(i?", ":"")+parameter(f,i,lane);
            if(returnsAggregate(f))signature+=(f.params.empty()?"out ":", out ")+declaration(f.returnType,resultName);
            line(signature+") {");
            ++indent;
            unwrittenOutputs(f,lane);
            block(f.definition->body->body,0,lane);
            --indent;
            line("}");
        }
    }
    // GLSL leaves an out parameter that the function never writes undefined; SkSL rejects it, so it
    // is set to zero.
    void unwrittenOutputs(const sema::Function& f, int lane) {
        Access a;
        access(*f.definition->body,a);
        for(std::size_t i=0;i<f.params.size();++i) {
            const sema::Param& p=f.params[i];
            const Symbol* s=f.definition->params[i].symbol;
            if(p.direction!=Direction::Out || !p.type.isBasic() || (s && a.writes.count(s)))continue;
            const char* zero=p.type.base==Type::Bool?"(false)":p.type.base==Type::Float?"(0.0)":"(0)";
            line(parameterName(f.definition->params[i],i,lane)+" = "+typeName(p.type)+zero+";");
        }
    }
    // The four lanes of every parameter, and out parameters for the results of lanes 1-3.
    void quadFunction(const sema::Function& f) {
        const bool isMain=&f==program.main;
        computeVarying(f,!isMain);
        if(returnsAggregate(f))
            error(f.loc,"array-return","function "+quotedName(f)+": it computes derivatives and returns "+quoted(toString(f.returnType))+
                ", which SkSL functions cannot return; the transpiler returns such values through an out parameter only in functions without derivatives",
                "return the value through an out parameter");
        if(isMain)mainSignature();
        else {
            std::string params;
            for(std::size_t i=0;i<f.params.size();++i)
                for(int lane=0;lane<4;++lane)params+=(params.empty()?"":", ")+parameter(f,i,lane);
            if(f.returnType.base!=Type::Void)
                for(int lane=1;lane<4;++lane)params+=std::string(params.empty()?"":", ")+"out "+typeName(f.returnType)+" "+laneName(retName,lane);
            line(typeName(f.returnType)+" "+functionName(&f,0)+"("+params+") {");
            ++indent;
        }
        for(int lane=0;lane<4;++lane)unwrittenOutputs(f,lane);
        const auto& body=f.definition->body->body;
        std::size_t last=0;
        for(std::size_t i=0;i<body.size();++i)if(info(*body[i]).sync)last=i;
        for(std::size_t i=0;i<=last && i<body.size();++i)sync(*body[i]);
        const std::size_t tail=last+1;
        if(!isMain && tail<body.size()) {
            // After the last derivative, lanes 1-3 finish separately (their returns become
            // assignments and flags), then lane 0 runs with real returns.
            bool returns=false;
            for(std::size_t i=tail;i<body.size();++i) {
                returns=returns||info(*body[i]).returns;
                if(const Stmt* sw=switchWithReturn(*body[i]))
                    error(sw->loc,"derivative-control","return inside a switch in "+quotedName(f)+" after a derivative",
                        "set a result variable in the switch and return it after the switch");
            }
            for(int lane=1;lane<4;++lane) {
                if(returns)line("bool "+done(lane)+" = false;");
                tailLane=lane;
                block(body,tail,lane);
                tailLane=0;
            }
        }
        if(tail<body.size())block(body,tail,0);
        if(isMain)line("return half4("+colorName+");");
        --indent;
        line("}");
    }
    static std::string quotedName(const sema::Function& f){return "'"+f.name+"'";}
    const Stmt* switchWithReturn(const Stmt& s) {
        if(!info(s).returns)return nullptr;
        if(s.kind==StmtKind::Switch)return &s;
        for(const auto& x:s.body)if(const Stmt* r=switchWithReturn(*x))return r;
        for(const Stmt* x:{s.then.get(),s.otherwise.get(),s.loop.get()})if(x)if(const Stmt* r=switchWithReturn(*x))return r;
        return nullptr;
    }

    // ------------------------------------------------------------------ declarations
    void nameStructs() {
        std::set<std::string> used;
        for(const auto& def:program.structs) {
            if(def.block)continue;
            std::string name=def.name.empty()?fresh("S"):userName(def.name);
            if(used.count(name))name=fresh(name);   // structs of the same name in different scopes
            used.insert(name);
            structNames[&def]=name;
        }
    }
    void structDefinitions() {
        for(const auto& def:program.structs) {
            if(def.block)continue;
            line("struct "+structNames[&def]+" {");
            ++indent;
            for(const auto& f:def.fields)line(typeName(f.type)+" "+userName(f.name)+dims(f.type)+";");
            --indent;
            line("};");
        }
    }
    void generatedGlobals() {
        const int copies=lanes?4:1;
        for(int lane=0;lane<copies;++lane)line("float4 "+laneName(colorName,lane)+";");
        if(usesFragCoord||lanes)for(int lane=0;lane<copies;++lane)line("float4 "+laneName(fragCoordName,lane)+";");
        if(lanes)line("float2 "+quadName+";");
    }
    void uniformName(const std::string& name, Loc at) {
        if(userName(name)!=name)
            error(at,"reserved-name","uniform name "+quoted(name)+": SkSL reserves it, and uniforms keep their names so the application can set them",
                "rename the uniform");
    }
    static std::string quoted(const std::string& s){return "'"+s+"'";}
    int globalCopies(const Symbol& s) const {return lanes && !s.constQualified?4:1;}
    static bool rowMajor(const ast::Qualifiers& q) {
        for(const auto& x:q.list)for(const auto& id:x.layout)if(id.name=="row_major")return true;
        return false;
    }
    void globalDeclarations() {
        bool rowMajorDefault=false;
        for(const auto& d:unit.decls) {
            if(d->kind==DeclKind::Qualifier && d->qualifiers.has("uniform") && rowMajor(d->qualifiers))rowMajorDefault=true;
            if(d->kind==DeclKind::Block && d->block->qualifiers.has("uniform")) {
                bool row=rowMajorDefault||rowMajor(d->block->qualifiers), matrices=false;
                for(const auto& g:d->block->members) {
                    row=row||rowMajor(g.qualifiers);
                    Type t;
                    if(sema::keywordType(g.type.name,t) && t.isMatrix())matrices=true;
                }
                if(row && matrices)
                    error(d->loc,"matrix-layout","row_major uniform block "+quoted(d->block->name)+": SkSL runtime shaders have no uniform blocks, and the "
                        "application sets the flattened matrix uniforms in column-major order, so data laid out row by row would arrive transposed",
                        "remove row_major and upload the matrices column by column");
            }
            if(d->kind==DeclKind::Variable) {
                for(const auto& v:d->declarators) {
                    const Symbol* s=v.symbol;
                    if(!s)continue;
                    if(s->storage==Storage::Uniform) {
                        uniformName(s->name,s->loc);
                        line("uniform "+typeName(s->type)+" "+s->name+dims(s->type)+";");
                    } else if(s->storage==Storage::Global && v.init && aggregate(s->type)) {
                        for(int lane=0;lane<globalCopies(*s);++lane)line(declaration(s->type,symbolName(s,lane))+";");
                        globalArrays.push_back(&v);
                    } else if(s->storage==Storage::Global) {
                        for(int lane=0;lane<globalCopies(*s);++lane)line(declarator(v,lane)+";");
                    }
                }
            } else if(d->kind==DeclKind::Block && d->block->qualifiers.has("uniform")) {
                for(const auto& def:program.structs) {
                    if(!def.block || def.name!=d->block->name || def.loc.line!=d->block->loc.line || def.loc.column!=d->block->loc.column)continue;
                    for(const auto& f:def.fields) {
                        uniformName(f.name,f.loc);
                        line("uniform "+typeName(f.type)+" "+f.name+dims(f.type)+";");
                    }
                }
            }
        }
    }
    void mainPrologue() {
        const std::string& c=coordName;
        const std::string& q=quadName;
        if(lanes)line(q+" = 1.0 - 2.0 * mod(floor("+c+"), 2.0);");
        if(usesFragCoord||lanes) {
            line(fragCoordName+" = float4("+c+", 0.0, 1.0);");
            if(lanes) {
                line(laneName(fragCoordName,1)+" = float4("+c+".x + "+q+".x, "+c+".y, 0.0, 1.0);");
                line(laneName(fragCoordName,2)+" = float4("+c+".x, "+c+".y + "+q+".y, 0.0, 1.0);");
                line(laneName(fragCoordName,3)+" = float4("+c+" + "+q+", 0.0, 1.0);");
            }
        }
        for(int lane=0;lane<(lanes?4:1);++lane)line(laneName(colorName,lane)+" = float4(0);");
        // SkSL global arrays cannot have initializers (const ones become plain variables).
        for(const ast::Declarator* v:globalArrays)
            for(int lane=0;lane<globalCopies(*v->symbol);++lane)
                assignParts(symbolName(v->symbol,lane),v->symbol->type,*v->init,lane);
    }
};
}

std::string emitSkSL(const ast::TranslationUnit& unit, const sema::Program& program, const std::vector<Token>& tokens, DiagnosticSink& sink) {
    return Emitter(unit,program,tokens,sink).run();
}
}
