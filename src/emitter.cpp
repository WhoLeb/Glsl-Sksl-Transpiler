#include "emitter.hpp"
#include <charconv>
#include "helpers.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <tuple>
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
// Mirrors Skia's src/sksl/lex/sksl.lex (keywords, RESERVED) and SkSLBuiltinTypes.cpp; GLSL ES 1.00
// leaves many of them free (uint, mat2x3, layout, buffer, case, ...).
const std::set<std::string> kReserved=[] {
    std::set<std::string> s={
        "case","flat","noperspective","inline","noinline","readonly","writeonly","buffer","layout","workgroup","pixel_local",
        "atomic","attribute","varying","precision","invariant","asm","class","union","enum","typedef","template","this",
        "packed","goto","volatile","public","static","extern","external","interface","long","double","fixed","unsigned",
        "superp","input","output","sizeof","cast","namespace","using",
        "half","uint","short","ushort","shader","colorFilter","blender","sampler","sampler2D","samplerExternalOES",
        "sampler2DRect","texture2D","textureExternalOES","texture2DRect","readonlyTexture2D","writeonlyTexture2D",
        "subpassInput","subpassInputMS","isubpassInput","isubpassInputMS","usubpassInput","usubpassInputMS",
        "atomicUint","atomic_uint","saturate","unpremul","toLinearSrgb","fromLinearSrgb",
        // Skia's GPU backend appends '_S1_c0' to global names, which makes 'gl' the reserved 'gl_S1_c0'.
        "gl"};
    const std::string n[]={"2","3","4"};
    for(const auto& a:n) {
        for(const char* v:{"half","float","int","uint","short","ushort","bool","uvec","hvec","dvec","fvec","dmat"})s.insert(v+a);
        for(const auto& b:n)for(const char* m:{"half","float","mat","dmat"})s.insert(m+a+"x"+b);
    }
    const char* dims[]={"1D","2D","3D","1DArray","2DArray","2DRect","2DMS","2DMSArray","Buffer","Cube","CubeArray"};
    for(const char* p:{"iimage","image","isampler","itexture","uimage","usampler","utexture"})for(const char* d:dims)s.insert(std::string(p)+d);
    for(const char* p:{"sampler","texture"})for(const char* d:{"1D","1DArray","2DArray","2DMS","2DMSArray","3D","Buffer","Cube","CubeArray"})
        s.insert(std::string(p)+d);
    for(const char* d:{"1DArrayShadow","1DShadow","2DArrayShadow","2DShadow","2DRectShadow","3DRect","CubeArrayShadow","CubeShadow","Shadow"})
        s.insert(std::string("sampler")+d);
    return s;
}();

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
    char buffer[64];
    const auto result=std::to_chars(buffer,buffer+sizeof buffer,f);
    std::string s(buffer,result.ptr);
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

    std::string run(std::vector<SourceSpan>& origins) {
        if(!program.main)return {};
        if(!program.writesFragColor && !program.writesFragData && program.outputs.empty())
            error(Loc{1,1},"output","shader without an output: it neither writes gl_FragColor nor declares an 'out vec4'",
                "write the color to gl_FragColor (GLSL ES 1.00) or to an 'out vec4' variable");
        nameStructs();
        analyzeFunctions();
        {
            std::set<const sema::Function*> plain;
            collectSpecs(program.main,{},plain);
        }
        lanes=quad(program.main);
        std::vector<const sema::Function*> order;
        std::set<const sema::Function*> seen;
        visitOrder(program.main,seen,order);
        for(const sema::Function* f:order) {
            if(functions[f].fragCoord)usesFragCoord=true;
            if(f!=program.main && f->definition && discards(*f->definition->body) && discardedName.empty())discardedName=fresh("discarded");
        }

        std::string structs, globals, code;
        std::vector<SourceSpan> structOrigins, globalOrigins, codeOrigins;
        out=&structs; outOrigins=&structOrigins; structDefinitions();
        out=&globals; outOrigins=&globalOrigins; generatedGlobals(); globalDeclarations();
        out=&code; outOrigins=&codeOrigins;
        for(const sema::Function* f:order)function(*f);   // main last: it becomes the SkSL main
        const std::string definitions=helpers.definitions()+integerDefinitionsText+textureDefinitions();
        origins.assign(static_cast<std::size_t>(std::count(definitions.begin(),definitions.end(),'\n')),SourceSpan{});
        for(const auto* part:{&structOrigins,&globalOrigins,&codeOrigins})origins.insert(origins.end(),part->begin(),part->end());
        return definitions+structs+globals+code;
    }

    const std::map<std::string,std::string>& identifierMap() const { return renamed; }
private:
    struct FunctionInfo {
        std::vector<const sema::Function*> callees;     // user functions with a definition, in call order
        bool derivative=false, touches=false, writes=false, fragCoord=false;   // in the function itself
        bool quad=false, touchesAll=false, writesAll=false, closed=false;      // including callees
    };
    struct StmtInfo { bool sync=false, returns=false, jumps=false; };          // jumps: break/continue that leave it
    struct Access { std::set<const Symbol*> reads, writes; bool readsGlobals=false, writesGlobals=false; };
    // A sampler as the uniform (and element) it names; index: a non-constant index into an array of samplers.
    struct SamplerRef { const Symbol* uniform=nullptr; int element=-1; const Expr* index=nullptr; };
    using Binding=std::map<const Symbol*,SamplerRef>;         // sampler parameter -> sampler
    struct Spec { std::vector<SamplerRef> args; Binding binding; std::string name; };   // copy of a function for its samplers

    const ast::TranslationUnit& unit;
    const sema::Program& program;
    mutable std::set<std::string> taken;                    // identifiers of the shader and generated names
    mutable std::map<std::string,std::string> renamed;      // SkSL-reserved identifiers -> replacement
    mutable std::map<std::pair<std::string,int>,std::string> laneNames;
    std::string colorName, coordName, fragCoordName, quadName, retName, doneName, resultName;
    std::string discardedName;              // set by discard in functions other than main; empty when none does
    BuiltinHelpers helpers;
    DiagnosticSink& sink;
    std::map<const sema::Function*,FunctionInfo> functions;
    std::map<const sema::StructDef*,std::string> structNames;
    std::unordered_map<const Stmt*,StmtInfo> stmtInfo;
    bool lanes=false, usesFragCoord=false, usesPointCoord=false;
    std::string zeroName;                   // a float global that is 0.0 at run time (see zeroDivisor)

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
    int indexDepth=0;                       // > 0 while writing an array index
    std::string* out=nullptr;
    std::vector<SourceSpan>* outOrigins=nullptr;   // GLSL source per line of *out
    int indent=0;
    SourceSpan origin;                      // GLSL source of the statement or declaration being written
    struct From {
        Emitter& e; SourceSpan saved;
        From(Emitter& e, Loc at, unsigned endLine):e(e),saved(e.origin){e.origin={at,std::max(at.line,endLine)};}
        ~From(){e.origin=saved;}
    };
    static unsigned lastLine(const Expr& e) {
        unsigned l=e.loc.line;
        for(const auto& o:e.operands)l=std::max(l,lastLine(*o));
        return l;
    }
    std::unordered_map<const Stmt*,unsigned> lastLines;
    unsigned lastLine(const Stmt& s) {
        const auto found=lastLines.find(&s);
        if(found!=lastLines.end())return found->second;
        unsigned l=s.loc.line;
        for(const Expr* e:{s.expr.get(),s.step.get()})if(e)l=std::max(l,lastLine(*e));
        if(s.decl)for(const auto& v:s.decl->declarators)l=std::max(l,v.init?std::max(v.loc.line,lastLine(*v.init)):v.loc.line);
        for(const auto& x:s.body)l=std::max(l,lastLine(*x));
        for(const Stmt* x:{s.then.get(),s.otherwise.get(),s.init.get(),s.loop.get()})if(x)l=std::max(l,lastLine(*x));
        return lastLines[&s]=l;
    }
    // Where the declaration of a global variable starts, qualifiers included.
    Loc declarationStart(const Symbol* s) const {
        for(const auto& d:unit.decls)
            if(d->kind==DeclKind::Variable)for(const auto& v:d->declarators)if(v.symbol==s)return d->loc;
        return s?s->loc:Loc{};
    }

    // ------------------------------------------------------------------ output
    void line(const std::string& s) {
        out->append(static_cast<std::size_t>(indent)*4,' ');
        *out+=s;
        *out+='\n';
        outOrigins->insert(outOrigins->end(),static_cast<std::size_t>(std::count(s.begin(),s.end(),'\n'))+1,origin);
    }
    // A construct SkSL lacks was replaced by a software implementation; `replacement` is SkSL text
    // that marks the replacement in the generated code.
    void replaced(const Expr& e, const std::string& code, const std::string& message, const std::string& suggestion, const std::string& replacement) {
        if(!notedReplacements.insert(&e).second)return;
        sink.warning(e.loc,"software."+code,message,suggestion);
        sink.hint(replacement);
    }
    std::set<const Expr*> notedReplacements;
    void error(Loc at, const std::string& code, const std::string& message, const std::string& suggestion={}) {
        sink.error(at,"unsupported."+code,"Unsupported "+message,suggestion);
        if(code.compare(0,10,"derivative")==0)refusedDerivative=true;
    }
    // A refused derivative stays a plain dFdx call in the SkSL returned with the error.
    bool refusedDerivative=false;
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
        if(!kReserved.count(name) && !sema::isBuiltinFunctionName(name,320) && name.compare(0,3,"sk_")!=0)return name;
        const auto it=renamed.find(name);
        if(it!=renamed.end())return it->second;
        return renamed[name]=fresh(name.compare(0,3,"sk_")==0||name=="gl"?"_"+name:name+"_");   // "gl_" would be reserved too
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
            if(s->name=="gl_FragColor"||s->name=="gl_FragData")return laneName(colorName,lane);
            if(s->name=="gl_FragCoord")return laneName(fragCoordName,lane);
            if(s->name=="gl_FrontFacing")return "true";
            if(s->name=="gl_PointCoord")return "("+laneName(fragCoordName,lane)+".xy / "+resolution+" * float2(1.0, -1.0) + float2(0.0, 1.0))";
            if(s->name=="gl_DepthRange")return typeName(s->type)+"(0.0, 1.0, 1.0)";
            return s->name;
        case Storage::Output: return laneName(colorName,lane);
        case Storage::Uniform: return userName(s->name);
        case Storage::Global: return s->constQualified?userName(s->name):laneName(userName(s->name),lane);
        default: {
            const std::string name=s->generated?generatedName(s):userName(s->name);
            return shared.count(s)?name:laneName(name,lane);
        }
        }
    }
    // Variables the semantic pass introduces get names no identifier of the shader uses.
    std::string generatedName(const Symbol* s) const {
        auto& n=generatedNames[s];
        if(n.empty())n=fresh(s->name);
        return n;
    }
    mutable std::map<const Symbol*,std::string> generatedNames;
    // A function that computes derivatives has one version, which takes all four lanes.
    // Functions that take samplers have a copy per sampler (spec).
    std::string functionName(const sema::Function* f, int lane, const Spec* spec) {
        const std::string name=spec?spec->name:userFunctionName(f->name);
        return laneFunction(f)?laneName(name,lane):name;
    }
    // GLSL ES 1.00 lets shaders define round, inverse, abs(int), ...: names SkSL gives to its intrinsics.
    std::string userFunctionName(const std::string& name) const {
        if(!sema::isBuiltinFunctionName(name,320))return userName(name);
        const auto it=renamed.find(name);
        if(it!=renamed.end())return it->second;
        return renamed[name]=fresh(name+"_");
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
        if(s->storage==Storage::Output||s->storage==Storage::Input)return true;
        if(s->storage==Storage::Global)return !s->constQualified;
        if(s->storage==Storage::BuiltIn)return s->name=="gl_FragColor"||s->name=="gl_FragData"||s->name=="gl_FragCoord"||s->name=="gl_PointCoord";
        return false;
    }

    void analyzeFunctions() {
        for(const auto& f:program.functions) {
            if(f.builtin || !f.definition || !f.definition->body)continue;
            FunctionInfo& info=functions[&f];
            auto visit=[&](const Expr& e) {
                if(e.kind==ExprKind::Name && perPixel(e.symbol)) {
                    info.touches=true;
                    if((e.symbol->name=="gl_FragCoord"||e.symbol->name=="gl_PointCoord") && e.symbol->storage==Storage::BuiltIn)info.fragCoord=true;
                    if(e.symbol->name=="gl_PointCoord" && e.symbol->storage==Storage::BuiltIn)usesPointCoord=true;
                }
                if(isWrite(e) && perPixel(root(*e.operands[0])))info.writes=true;
                if(zeroDivisor(e) && zeroName.empty())zeroName=fresh("zero");
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
        case ExprKind::Name:
            if(e.symbol && e.symbol->storage==Storage::BuiltIn)builtinVariable(e);
            if(e.symbol && e.symbol->storage==Storage::BuiltIn && e.symbol->value && constantText(o,*e.symbol->value))break;
            o+=e.symbol?symbolName(e.symbol,lane):e.text;   // undeclared names stay as written
            break;
        case ExprKind::Unary: if(!folded(o,e) && !integerOperator(o,e,lane))unary(o,e,lane); break;
        case ExprKind::Postfix: expr(o,*e.operands[0],lane,16); o+=e.text; break;
        case ExprKind::Binary:
            if(folded(o,e)||integerOperator(o,e,lane))break;
            if((e.text=="=="||e.text=="!=") && aggregate(e.operands[0]->type)){comparison(o,e,lane);break;}
            expr(o,*e.operands[0],lane,binaryPrecedence(e.text));
            o+=" "+e.text+" ";
            if(zeroDivisor(e))divisor(o,e,lane);
            else expr(o,*e.operands[1],lane,binaryPrecedence(e.text)+1);
            break;
        case ExprKind::Assign:
            if(aggregate(e.type))
                notExpressible(e,"assignment","SkSL cannot assign arrays (or structs containing arrays) as a whole");
            if(integerOperator(o,e,lane))break;
            expr(o,*e.operands[0],lane,15);
            o+=" "+e.text+" ";
            if(zeroDivisor(e))divisor(o,e,lane);
            else expr(o,*e.operands[1],lane,2);
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
            if(isBuiltin(*e.operands[0],"gl_FragData")) {
                // gl_FragData has gl_MaxDrawBuffers = 1 element: any valid index selects the color.
                builtinVariable(*e.operands[0]);
                o+=laneName(colorName,lane);
                break;
            }
            expr(o,*e.operands[0],lane,16);
            o+='[';
            ++indexDepth;
            expr(o,*e.operands[1],lane,0);
            --indexDepth;
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
        bool any=false;
        for(std::size_t i=first;i<e.operands.size();++i) {
            if(samplerType(e.operands[i]->type))continue;   // functions get a copy per sampler instead
            if(any)o+=", ";
            any=true;
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
            if(a.type.isMatrix() && !e.type.isMatrix() && (!sideEffects(a) || hoisted.count(&a) || computed(a,lane))) {
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
    // A float division by a constant with a zero component: GLSL leaves the result unspecified and
    // GPUs give infinity or NaN, but Skia rejects the division. The divisor gets a zero that is only
    // known at run time (a global set at the start of main), so the division happens as on a GPU.
    static bool zeroDivisor(const Expr& e) {
        if(!((e.kind==ExprKind::Binary && e.text=="/")||(e.kind==ExprKind::Assign && e.text=="/=")) || e.type.base!=Type::Float)return false;
        const Expr& d=*e.operands[1];
        if(!d.value)return false;
        for(const auto& v:d.value->values)if(v.base==Type::Float && v.f==0.0f)return true;
        return false;
    }
    void divisor(std::string& o, const Expr& e, int lane) {
        o+="(";
        expr(o,*e.operands[1],lane,binaryPrecedence("+"));
        o+=" + "+zeroName+")";
        replaced(e,"operator","division by "+quoted(*e.operands[1])+", which is zero: Skia rejects a division by a constant zero, so the divisor gets a zero "
            "known only at run time and the result is the infinity or NaN that GPUs give (GLSL leaves it unspecified)","no change needed",zeroName);
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
        else if(isTextureCall(e)) {
            textureLookup(o,e,lane);
            return;
        } else if(f->builtin) {
            if(sema::isDerivative(f->name) && !refusedDerivative)
                sink.error(e.loc,"internal","Internal error: derivative "+ast::toGlsl(e)+" was not lowered");
            // A constant argument list is folded: SkSL does not accept a call of a generated
            // function where GLSL has a constant expression (a const initializer).
            if(BuiltinHelpers::needed(*f) && e.value && constantText(o,*e.value))return;
            if(BuiltinHelpers::needed(*f)) {
                const std::string helper=helpers.use(*f);
                replaced(e,"builtin","built-in "+quoted(sema::signature(*f))+" is not available in SkSL runtime shaders; it is replaced by the generated function "+
                    quoted(helper)+", which computes the result the GLSL ES specification defines","no change needed",helper+"(");
                o+=helper;
            } else o+=e.text;
        } else if(dynamicSamplerCall(e)) {
            if(f->returnType.base==Type::Void)
                valueError(e,"sampler-argument","call "+quoted(e)+": it passes an element of an array of samplers chosen at run time to a void function, "
                    "which the transpiler calls through a selection among copies of the function, and SkSL selects void calls only in a statement of their own",
                    "make the call a statement of its own");
            o+=dynamicCall(e,lane,samplerArgs(e));
            return;
        } else o+=functionName(f,lane,calleeSpec(e));
        arguments(o,e,0,lane);
    }
    // ------------------------------------------------------------------ integer operators
    // SkSL runtime shaders have no integer remainder or bit operations. Generated functions compute
    // them on the 32-bit two's complement value with the integer arithmetic SkSL has, without
    // overflow and without relying on how negative numbers are divided: the sign bit is handled
    // apart, and the other 31 bits, as a non-negative number, through division by 2. ~a is -1 - a.
    // Compound assignments (a &= b) call a version that takes the target as an inout parameter
    // and returns the new value, so the target is evaluated once.
    GLSL_SKSL_NOINLINE bool integerOperator(std::string& o, const Expr& e, int lane) {
        static const std::map<std::string,std::string> ops={{"%","mod"},{"&","and"},{"|","or"},{"^","xor"},{"<<","shl"},{">>","shr"},
            {"%=","mod"},{"&=","and"},{"|=","or"},{"^=","xor"},{"<<=","shl"},{">>=","shr"}};
        if(e.type.base!=Type::Int || !e.type.isBasic())return false;
        if(e.kind==ExprKind::Unary && e.text=="~") {
            o+="("+(e.type.isScalar()?std::string("-1"):typeName(e.type)+"(-1)")+" - ";
            expr(o,*e.operands[0],lane,binaryPrecedence("-")+1);
            o+=")";
            noteInteger(e);
            return true;
        }
        const auto it=ops.find(e.text);
        if(it==ops.end() || (e.kind!=ExprKind::Binary && e.kind!=ExprKind::Assign))return false;
        // An index must be built from loop indices and constants with operators, so a remainder
        // by a positive constant there is written with them: a - n * (a / n) is exact for a >= 0,
        // and GLSL leaves the remainder of a negative operand undefined.
        const Expr& divisor=*e.operands[1];
        if(e.text=="%" && e.kind==ExprKind::Binary && indexDepth>0 && e.type.isScalar() && divisor.value && !divisor.value->values.empty()
           && divisor.value->values[0].base==Type::Int && divisor.value->values[0].i>0 && !sideEffects(*e.operands[0])) {
            std::string a, n;
            expr(a,*e.operands[0],lane,binaryPrecedence("/"));
            expr(n,divisor,lane,binaryPrecedence("/")+1);
            o+="("+a+" - "+n+" * ("+a+" / "+n+"))";
            return true;
        }
        const bool assign=e.kind==ExprKind::Assign;
        const std::string name=integerHelper(it->second,e.type.rows,assign);
        // A scalar operand of a vector operation is widened.
        auto operand=[&](const Expr& x) {
            std::string t;
            expr(t,x,lane,2);
            return x.type.rows<e.type.rows?typeName(e.type)+"("+t+")":t;
        };
        o+=name+"("+operand(*e.operands[0])+", "+operand(*e.operands[1])+")";
        noteInteger(e);
        return true;
    }
    void noteInteger(const Expr& e) {
        replaced(e,"operator","operator "+quoted(e.text)+" is computed by a generated function: SkSL runtime shaders have no integer remainder or bit "
            "operations, so it works on the 32-bit two's complement bits with integer arithmetic, with the result GLSL defines","no change needed",
            e.text=="~"?"-1 - ":"glsl_");
    }
    std::map<std::string,std::string> integerNames;      // operation -> generated name
    std::set<std::string> integerDefined;                // operation and width already written
    std::string integerDefinitionsText;
    std::string integerHelper(const std::string& op, int rows, bool assign) {
        const std::string scalarName=integerName(op);
        if(integerDefined.insert(op+"1").second) {
            std::string& d=integerDefinitionsText;
            const std::string low="2147483647 + 1";   // a + 2^31 without overflow for a < 0
            // Skia's CPU backend also evaluates lanes that a branch or return has left, with
            // arbitrary values there, and an integer division that overflows (INT_MIN / -1)
            // stops the process. So the functions have no early returns and divide by constants
            // only; the remainder replaces the divisors 0 (undefined in GLSL) and -1 by 1.
            if(op=="mod")d+="int "+scalarName+"(int a, int b) {\n    int d = b == 0 || b == -1 ? 1 : b;\n    return a - d * (a / d);\n}\n";
            else if(op=="and"||op=="or"||op=="xor") {
                const std::string combine=op=="and"?"u * v":op=="or"?"u + v - u * v":"u + v - 2 * u * v";
                const std::string sign=op=="and"?"a < 0 && b < 0":op=="or"?"a < 0 || b < 0":"(a < 0) != (b < 0)";
                d+="int "+scalarName+"(int a, int b) {\n"
                   "    int x = a < 0 ? a + "+low+" : a;\n"
                   "    int y = b < 0 ? b + "+low+" : b;\n"
                   "    int r = 0;\n"
                   "    int p = 1;\n"
                   "    for (int i = 0; i < 31; i++) {\n"
                   "        int u = x - x / 2 * 2;\n"
                   "        int v = y - y / 2 * 2;\n"
                   "        r += p * ("+combine+");\n"
                   "        x /= 2;\n"
                   "        y /= 2;\n"
                   "        if (i < 30) { p *= 2; }\n"
                   "    }\n"
                   "    return "+sign+" ? r - 2147483647 - 1 : r;\n"
                   "}\n";
            } else if(op=="shl")
                // One bit at a time: bit 30 of the low bits becomes the sign bit.
                d+="int "+scalarName+"(int a, int b) {\n"
                   "    int x = a < 0 ? a + "+low+" : a;\n"
                   "    bool s = a < 0;\n"
                   "    for (int i = 0; i < 31; i++) {\n"
                   "        if (i < b) {\n"
                   "            s = x / 1073741824 == 1;\n"
                   "            x = (x - x / 1073741824 * 1073741824) * 2;\n"
                   "        }\n"
                   "    }\n"
                   "    return s ? x - 2147483647 - 1 : x;\n"
                   "}\n";
            else
                // The low bits shift down; a negative value then loses 2^31 / 2^b.
                d+="int "+scalarName+"(int a, int b) {\n"
                   "    int x = a < 0 ? a + "+low+" : a;\n"
                   "    int t = 1073741824;\n"
                   "    for (int i = 0; i < 31; i++) {\n"
                   "        if (i < b) {\n"
                   "            x /= 2;\n"
                   "            if (i > 0) { t /= 2; }\n"
                   "        }\n"
                   "    }\n"
                   "    return b <= 0 ? a : a < 0 ? x - t : x;\n"
                   "}\n";
        }
        const std::string T=rows==1?"int":"int"+std::to_string(rows);
        if(rows>1 && integerDefined.insert(op+std::to_string(rows)).second) {
            std::string parts;
            for(int i=0;i<rows;++i)parts+=std::string(i?", ":"")+scalarName+"(a."+kComponents[i]+", b."+kComponents[i]+")";
            integerDefinitionsText+=T+" "+scalarName+"("+T+" a, "+T+" b) { return "+T+"("+parts+"); }\n";
        }
        if(!assign)return scalarName;
        const std::string assignName=integerName(op+"Assign");
        if(integerDefined.insert(op+"Assign"+std::to_string(rows)).second)
            integerDefinitionsText+=T+" "+assignName+"(inout "+T+" a, "+T+" b) {\n    a = "+scalarName+"(a, b);\n    return a;\n}\n";
        return assignName;
    }
    std::string integerName(const std::string& op) {
        auto& n=integerNames[op];
        if(n.empty())n=fresh("glsl_"+op);
        return n;
    }

    // ------------------------------------------------------------------ textures
    // SkSL runtime shaders have no textures; they sample child shaders, which the application
    // creates so that eval takes GLSL texture coordinates (0 to 1 across the image; see README):
    //  - sampler2D: one child; eval filters and wraps as the GL sampler does.
    //  - samplerCube: one child per face (_px, _nx, _py, _ny, _pz, _nz), chosen as GL chooses the
    //    face; eval filters within the face.
    //  - sampler3D, sampler2DArray: one child holding the slices or layers stacked from top to
    //    bottom. The shader reads texel centres, where any filter returns the texel itself, and
    //    filters and wraps by hand as the annotation of the sampler states.
    // Lookups that need the size of the texture (textureSize, texelFetch, offsets, gathers, and
    // every 3D and array lookup) read a generated size uniform. Mipmaps are not available: lookups
    // with a level of detail, bias or gradients sample the base level.
    std::map<const sema::Function*,std::deque<Spec>> specs;     // copies of functions that take samplers
    const Spec* currentSpec=nullptr;
    std::set<const Symbol*> sizedSamplers;                       // samplers whose size uniform is used
    std::map<std::pair<const Symbol*,int>,std::string> childNames, sizeNames;
    std::map<std::tuple<const Symbol*,int,int>,std::string> faceNames;     // (sampler, element, face)

    static bool samplerType(const Type& t){return t.base==Type::Opaque && t.opaque.compare(0,7,"sampler")==0;}
    static bool samplerParams(const sema::Function& f) {
        for(const auto& p:f.params)if(samplerType(p.type))return true;
        return false;
    }
    static bool isTextureCall(const Expr& e) {
        return e.kind==ExprKind::Call && e.function && e.function->builtin && !e.function->params.empty() && samplerType(e.function->params[0].type);
    }
    // The uniform sampler (and element) an expression of sampler type names under a binding of
    // sampler parameters; GLSL only allows names and array elements there.
    static SamplerRef resolveSampler(const Expr& e, const Binding& binding) {
        if(e.kind==ExprKind::Name && e.symbol) {
            if(e.symbol->storage==Storage::Uniform)return {e.symbol,-1,nullptr};
            const auto it=binding.find(e.symbol);
            return it==binding.end()?SamplerRef{}:it->second;
        }
        if(e.kind==ExprKind::Index) {
            SamplerRef r=resolveSampler(*e.operands[0],binding);
            const Expr& i=*e.operands[1];
            if(i.value && !i.value->values.empty())r.element=i.value->values[0].i;
            else r.index=&i;
            return r;
        }
        return {};
    }
    const Binding& binding() const {static const Binding none; return currentSpec?currentSpec->binding:none;}
    static bool sameSampler(const SamplerRef& a, const SamplerRef& b){return a.uniform==b.uniform && a.element==b.element && !a.index && !b.index;}

    // Every function reachable from main with the samplers it receives; sizes the lookups need.
    void collectSpecs(const sema::Function* f, const Binding& bound, std::set<const sema::Function*>& plain) {
        if(!f || !f->definition || !f->definition->body)return;
        auto visit=[&](const Expr& e) {
            if(e.kind!=ExprKind::Call || !e.function)return;
            if(isTextureCall(e)) {
                const SamplerRef r=resolveSampler(*e.operands[0],bound);
                if(r.uniform && needsSize(e,r.uniform->type))sizedSamplers.insert(r.uniform);
                return;
            }
            const sema::Function* g=e.function;
            if(g->builtin)return;
            if(!samplerParams(*g)) {
                if(plain.insert(g).second)collectSpecs(g,{},plain);
                return;
            }
            // An element of an array of samplers chosen by a value that is not constant: every
            // element gets its copy, and the call selects among them (see dynamicCall).
            std::vector<std::vector<SamplerRef>> choices(1);
            for(std::size_t i=0;i<g->params.size() && i<e.operands.size();++i) {
                if(!samplerType(g->params[i].type))continue;
                const SamplerRef r=resolveSampler(*e.operands[i],bound);
                if(!r.uniform)return;
                std::vector<std::vector<SamplerRef>> next;
                for(const auto& c:choices) {
                    if(!r.index){next.push_back(c);next.back().push_back(r);continue;}
                    for(int k=0;k<r.uniform->type.arrays[0];++k){next.push_back(c);next.back().push_back({r.uniform,k,nullptr});}
                }
                choices=std::move(next);
            }
            for(const auto& args:choices) {
                if(findSpec(g,args))continue;
                Spec spec;
                spec.args=args;
                std::size_t k=0;
                for(std::size_t i=0;i<g->params.size();++i)
                    if(samplerType(g->params[i].type)) {
                        if(const Symbol* p=g->definition?g->definition->params[i].symbol:nullptr)spec.binding[p]=args[k];
                        ++k;
                    }
                std::string name=userFunctionName(g->name);
                for(const SamplerRef& r:args)name+="_"+r.uniform->name+(r.element>=0?std::to_string(r.element):std::string());
                spec.name=fresh(name);
                specs[g].push_back(spec);
                collectSpecs(g,specs[g].back().binding,plain);
            }
        };
        for(const auto& s:f->definition->body->body)eachExpr(*s,visit);
    }
    const Spec* findSpec(const sema::Function* f, const std::vector<SamplerRef>& args) const {
        const auto it=specs.find(f);
        if(it==specs.end())return nullptr;
        for(const Spec& s:it->second) {
            bool same=s.args.size()==args.size();
            for(std::size_t i=0;same && i<args.size();++i)same=sameSampler(s.args[i],args[i]);
            if(same)return &s;
        }
        return nullptr;
    }
    std::vector<SamplerRef> samplerArgs(const Expr& call) const {
        std::vector<SamplerRef> args;
        for(std::size_t i=0;i<call.function->params.size() && i<call.operands.size();++i)
            if(samplerType(call.function->params[i].type))args.push_back(resolveSampler(*call.operands[i],binding()));
        return args;
    }
    static bool dynamicSampler(const std::vector<SamplerRef>& args) {
        for(const SamplerRef& r:args)if(r.index)return true;
        return false;
    }
    bool dynamicSamplerCall(const Expr& e) const {
        return e.kind==ExprKind::Call && e.function && !e.function->builtin && samplerParams(*e.function) && dynamicSampler(samplerArgs(e));
    }
    // The copy of the called function for the samplers of the call.
    const Spec* calleeSpec(const Expr& call) {
        if(!call.function || !samplerParams(*call.function))return nullptr;
        const std::vector<SamplerRef> args=samplerArgs(call);
        if(dynamicSampler(args))
            valueError(call,"sampler-argument","call "+quoted(call)+": it passes an element of an array of samplers chosen at run time to a function "
                "that computes derivatives or returns an array, which the transpiler calls in statements of their own","pass an element chosen by a constant index");
        return findSpec(call.function,args);
    }
    // A call that passes an element of an array of samplers chosen at run time: a selection among
    // the copies of the function for each element (the condition picks exactly one).
    std::string dynamicCall(const Expr& e, int lane, std::vector<SamplerRef> args) {
        for(std::size_t i=0;i<args.size();++i) {
            if(!args[i].index)continue;
            const std::string index=expr(*args[i].index,lane,6);
            const int count=args[i].uniform->type.arrays[0];
            std::string s="(";
            for(int k=0;k<count;++k) {
                std::vector<SamplerRef> a=args;
                a[i].index=nullptr; a[i].element=k;
                s+=k+1<count?index+" == "+std::to_string(k)+" ? "+dynamicCall(e,lane,a)+" : ":dynamicCall(e,lane,a);
            }
            return s+")";
        }
        std::string call=functionName(e.function,lane,findSpec(e.function,args));
        arguments(call,e,0,lane);
        return call;
    }
    // The same for a call of a void function, which SkSL cannot select with ?:, as a statement.
    void dynamicCallStatement(const Expr& e, int lane, std::vector<SamplerRef> args) {
        for(std::size_t i=0;i<args.size();++i) {
            if(!args[i].index)continue;
            const std::string index=expr(*args[i].index,lane,6);
            const int count=args[i].uniform->type.arrays[0];
            for(int k=0;k<count;++k) {
                std::vector<SamplerRef> a=args;
                a[i].index=nullptr; a[i].element=k;
                line(k==0?"if ("+index+" == 0) {":k+1<count?"} else if ("+index+" == "+std::to_string(k)+") {":"} else {");
                ++indent; dynamicCallStatement(e,lane,a); --indent;
            }
            line("}");
            return;
        }
        line(dynamicCall(e,lane,args)+";");
    }
    static std::string kind(const Type& t){return t.opaque;}
    static bool softwareFiltered(const Type& t){return t.opaque=="sampler3D"||t.opaque=="sampler2DArray";}
    static bool needsSize(const Expr& call, const Type& sampler) {
        const std::string& n=call.function->name;
        return softwareFiltered(sampler) || n=="textureSize" || n.compare(0,10,"texelFetch")==0 || n.find("Offset")!=std::string::npos || n.compare(0,13,"textureGather")==0;
    }
    // Lookups that repeat parts of their computation: cube maps, 3D and array textures, gathers.
    bool complexLookup(const Expr& e) const {
        if(!isTextureCall(e))return false;
        const std::string& n=e.function->name;
        if(n=="textureSize"||n.compare(0,10,"texelFetch")==0)return false;
        const SamplerRef r=resolveSampler(*e.operands[0],binding());
        return r.uniform && (r.uniform->type.opaque!="sampler2D" || n.compare(0,13,"textureGather")==0);
    }
    bool lookupPrelude=false;
    std::string elementName(const Symbol* u, int element) {
        if(element<0)return userName(u->name);
        auto& n=childNames[{u,element}];
        if(n.empty())n=fresh(userName(u->name)+"_"+std::to_string(element));
        return n;
    }
    std::string sizeName(const Symbol* u, int element) {
        auto& n=sizeNames[{u,element}];
        if(n.empty())n=fresh(elementName(u,element)+"Size");
        return n;
    }
    std::string faceName(const Symbol* u, int element, int face) {
        static const char* const faces[6]={"px","nx","py","ny","pz","nz"};
        auto& n=faceNames[{u,element,face}];
        if(n.empty())n=fresh(elementName(u,element)+"_"+faces[face]);
        return n;
    }
    // Declarations of the child shaders and size uniforms that replace a sampler uniform.
    void samplerDeclarations(const Symbol& s) {
        const std::string k=kind(s.type);
        const int count=s.type.isArray()?s.type.arrays[0]:1;
        const int first=s.type.isArray()?0:-1;
        const bool sized=sizedSamplers.count(&s)!=0;
        for(int i=0;i<count;++i) {
            const int element=s.type.isArray()?i:-1;
            if(k=="samplerCube")for(int face=0;face<6;++face)line("uniform shader "+faceName(&s,element,face)+";");
            else line("uniform shader "+elementName(&s,element)+";");
            if(sized)line("uniform "+std::string(softwareFiltered(s.type)?"float3 ":"float2 ")+sizeName(&s,element)+";");
        }
        const std::string children=count>1?"the child shaders "+quoted(elementName(&s,0))+" to "+quoted(elementName(&s,count-1)):"the child shader "+quoted(elementName(&s,first));
        const std::string size=sized?"; set "+quoted(sizeName(&s,first))+(count>1?" (one per element)":"")+" to the "+
            (softwareFiltered(s.type)?"width, height and "+std::string(k=="sampler3D"?"depth":"number of layers")+" of the texture":
             k=="samplerCube"?"width and height of a face":"width and height of the texture")+" in texels":"";
        const std::string raw="SkImage::makeRawShader(tileX, tileY, sampling, &SkMatrix::Scale(1.0f / width, 1.0f / height))";
        std::string message, suggestion;
        if(k=="sampler2D") {
            message="sampler "+quoted(s.name)+" becomes "+children+", since SkSL runtime shaders have no textures; the shader samples it with the GLSL "
                "texture coordinates (0 to 1)";
            suggestion="create the child with "+raw+": the scale maps 0 to 1 onto the image, and a raw shader returns the texels unconverted, as GLSL "
                "does; use the wrap and filter modes of the GL sampler, without mipmaps"+size;
        } else if(k=="samplerCube") {
            message="cube sampler "+quoted(s.name)+" becomes six child shaders, one per face ("+quoted(faceName(&s,first,0))+" for +X, then -X, +Y, "
                "-Y, +Z, -Z), since SkSL runtime shaders have no textures; the shader chooses the face and the coordinates in it as GL does";
            suggestion="create each face like a 2D texture with "+raw+", clamped at the edges; with linear filtering, texels within half a texel of "
                "a face edge differ from GL, which filters across the edge"+size;
        } else {
            message=std::string(k=="sampler3D"?"3D sampler ":"array sampler ")+quoted(s.name)+" becomes "+children+" holding the "+
                (k=="sampler3D"?"slices":"layers")+" stacked from top to bottom (slice or layer k in rows k * height to (k + 1) * height - 1), since SkSL "
                "runtime shaders have no textures; the shader reads texel centres and filters ("+(s.nearest?"nearest":"linear")+") and wraps ("+s.wrap+
                ") them itself";
            suggestion="create the child from the stacked image with "+raw+" where height is the height of the whole stack; any filter mode works. "
                "State the GL sampler with '// @filter{nearest|linear} @wrap{repeat|clamp|mirror}' on the sampler (linear and repeat by default)"+size;
        }
        sink.warning(s.loc,"software.texture",message,suggestion);
        sink.hint("uniform shader ");
    }

    // Generated functions that compute texture coordinates (children cannot be passed to functions).
    std::map<std::string,std::pair<std::string,std::string>> textureHelpers;   // key -> (name, definition)
    std::string projectionName;
    std::vector<std::string> textureHelperOrder;
    std::string textureHelper(const std::string& key) {
        auto it=textureHelpers.find(key);
        if(it!=textureHelpers.end())return it->second.first;
        // textureProj divides by the last component: one name, overloaded for vec3 and vec4.
        const bool projection=key=="proj3"||key=="proj4";
        if(projection && projectionName.empty())projectionName=fresh("glsl_textureProj");
        const std::string name=projection?projectionName:fresh("glsl_"+key);
        std::string def;
        auto use=[&](const std::string& k){return textureHelper(k);};
        if(key=="cubeFace")
            // GLSL ES 3.00 table 3.21: the major axis selects the face, then s and t within it.
            def="float3 "+name+"(float3 r) {\n"
                "    float3 a = abs(r);\n"
                "    if (a.x >= a.y && a.x >= a.z) {\n"
                "        return r.x >= 0.0 ? float3(0.5 - 0.5 * r.z / a.x, 0.5 - 0.5 * r.y / a.x, 0.0) : float3(0.5 + 0.5 * r.z / a.x, 0.5 - 0.5 * r.y / a.x, 1.0);\n"
                "    }\n"
                "    if (a.y >= a.z) {\n"
                "        return r.y >= 0.0 ? float3(0.5 + 0.5 * r.x / a.y, 0.5 + 0.5 * r.z / a.y, 2.0) : float3(0.5 + 0.5 * r.x / a.y, 0.5 - 0.5 * r.z / a.y, 3.0);\n"
                "    }\n"
                "    return r.z >= 0.0 ? float3(0.5 + 0.5 * r.x / a.z, 0.5 - 0.5 * r.y / a.z, 4.0) : float3(0.5 - 0.5 * r.x / a.z, 0.5 - 0.5 * r.y / a.z, 5.0);\n"
                "}\n";
        else if(key=="gather")
            // Centre of a texel of the 2x2 footprint of linear filtering at uv.
            def="float2 "+name+"(float2 uv, float2 size, float2 corner) { return (floor(uv * size - 0.5) + corner + 0.5) / size; }\n";
        else if(key=="atlas")
            def="float2 "+name+"(float3 i, float3 size) { return float2(i.x + 0.5, i.z * size.y + i.y + 0.5) / float2(size.x, size.y * size.z); }\n";
        else if(key.compare(0,5,"wrap_")==0) {
            const std::string mode=key.substr(5,key.size()-7), T=key.back()=='2'?"float2":"float3";
            if(mode=="repeat")def=T+" "+name+"("+T+" i, "+T+" size) { return i - size * floor(i / size); }\n";
            else if(mode=="clamp")def=T+" "+name+"("+T+" i, "+T+" size) { return clamp(i, "+T+"(0.0), size - 1.0); }\n";
            else def=T+" "+name+"("+T+" i, "+T+" size) {\n    "+T+" m = i - 2.0 * size * floor(i / (2.0 * size)) - size;\n"
                "    return size - 1.0 - mix(-1.0 - m, m, step("+T+"(0.0), m));\n}\n";
        } else if(key.compare(0,8,"texel3D_")==0) {
            const std::string atlas=use("atlas"), wrap=use("wrap_"+key.substr(8)+"_3");
            def="float2 "+name+"(float3 i, float3 size) { return "+atlas+"("+wrap+"(i, size), size); }\n";
        } else if(key.compare(0,11,"texelLayer_")==0) {
            const std::string atlas=use("atlas"), wrap=use("wrap_"+key.substr(11)+"_2");
            def="float2 "+name+"(float2 i, float layer, float3 size) {\n"
                "    return "+atlas+"(float3("+wrap+"(i, size.xy), clamp(floor(layer + 0.5), 0.0, size.z - 1.0)), size);\n}\n";
        } else if(key=="bilinear")
            def="float4 "+name+"(float4 a, float4 b, float4 c, float4 d, float2 t) { return mix(mix(a, b, t.x), mix(c, d, t.x), t.y); }\n";
        else if(key=="trilinear")
            def="float4 "+name+"(float4 a, float4 b, float4 c, float4 d, float4 e, float4 f, float4 g, float4 h, float3 t) {\n"
                "    return mix(mix(mix(a, b, t.x), mix(c, d, t.x), t.y), mix(mix(e, f, t.x), mix(g, h, t.x), t.y), t.z);\n}\n";
        else if(key=="proj3")def="float2 "+name+"(float3 p) { return p.xy / p.z; }\n";
        else if(key=="proj4")def="float2 "+name+"(float4 p) { return p.xy / p.w; }\n";
        else if(key=="proj3D")def="float3 "+name+"(float4 p) { return p.xyz / p.w; }\n";
        textureHelpers[key]={name,def};
        textureHelperOrder.push_back(key);
        return name;
    }
    std::string textureDefinitions() const {
        std::string s;
        for(const auto& k:textureHelperOrder)s+=textureHelpers.at(k).second;
        return s;
    }

    // A texture built-in, written with the child shaders of its sampler.
    GLSL_SKSL_NOINLINE void textureLookup(std::string& o, const Expr& e, int lane) {
        const SamplerRef ref=resolveSampler(*e.operands[0],binding());
        if(!ref.uniform) {
            sink.error(e.loc,"internal","Internal error: the sampler of "+ast::toGlsl(e)+" was not resolved");
            return;
        }
        const Symbol* u=ref.uniform;
        if(!ref.index){o+=lookupText(e,lane,u,ref.element);return;}
        // An array of samplers indexed by a value that is not constant: a selection among the children.
        const std::string index=expr(*ref.index,lane,6);
        const int count=u->type.arrays[0];
        std::string s;
        for(int i=0;i+1<count;++i)s+=index+" == "+std::to_string(i)+" ? "+lookupText(e,lane,u,i)+" : ";
        o+="("+s+lookupText(e,lane,u,count-1)+")";
    }
    std::string lookupText(const Expr& e, int lane, const Symbol* u, int element) {
        const std::string& name=e.function->name;
        const std::string k=kind(u->type);
        const bool size=name=="textureSize", fetch=name.compare(0,10,"texelFetch")==0, gather=name.compare(0,13,"textureGather")==0;
        const bool proj=name.find("Proj")!=std::string::npos, lod=name.find("Lod")!=std::string::npos, grad=name.find("Grad")!=std::string::npos;
        const bool offset=name.find("Offset")!=std::string::npos && name!="textureGatherOffsets";
        auto arg=[&](std::size_t i){return i<e.operands.size()?expr(*e.operands[i],lane,2):std::string();};
        std::size_t next=2;
        std::string off;
        if(lod||fetch)++next;
        if(grad)next+=2;
        if(offset)off=arg(next++);
        const bool ignoresLevel=lod||grad||(!fetch && !size && !gather && next<e.operands.size());
        const std::string S=sizeName(u,element);
        if(size) {
            const std::string T=softwareFiltered(u->type)?"int3":"int2";
            const Expr& level=*e.operands[1];
            noteLookup(e,u,"textureSize returns the size of the texture from the uniform "+quoted(S)+", which the application sets");
            if(level.value && level.value->values[0].i==0)return T+"("+S+")";
            return T+"(max(floor("+S+" / exp2(float("+arg(1)+"))), 1.0))";
        }
        // In a prelude, values the lookup repeats are computed once into temporaries.
        auto once=[&](const char* type, const std::string& text) {
            if(!lookupPrelude || text.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.")==std::string::npos)return text;
            const std::string t=temp();
            line(std::string(type)+" "+t+" = "+text+";");
            return t;
        };
        const std::string P=gather||k!="sampler2D"?once(typeName(e.operands[1]->type).c_str(),arg(1)):arg(1);
        const std::string level=ignoresLevel?"; the "+std::string(grad?"gradients are":lod?"level of detail is":"bias is")+" ignored, since child shaders have no "
            "mipmaps: it samples the base level, which matches GL for a texture without mipmaps":"";
        if(k=="sampler2D") {
            const std::string child=elementName(u,element);
            if(fetch) {
                noteLookup(e,u,"texelFetch reads the texel at its centre, where any filter returns the texel itself"+level);
                return "float4("+child+".eval((float2("+P+")"+(off.empty()?"":" + float2("+off+")")+" + 0.5) / "+S+"))";
            }
            std::string uv=proj?textureHelper(e.operands[1]->type.rows==3?"proj3":"proj4")+"("+P+")":P;
            if(gather)return gatherText(e,lane,uv,S,off,[&](const std::string& c){return child+".eval("+c+")";},u);
            if(!off.empty())uv=uv+" + float2("+off+") / "+S;
            noteLookup(e,u,quoted(e)+" samples the child shader "+quoted(child)+" with "+quoted(child+".eval")+", since SkSL runtime shaders have no textures"+level);
            return "float4("+child+".eval("+uv+"))";
        }
        if(k=="samplerCube") {
            const std::string F=once("float3",textureHelper("cubeFace")+"("+P+")");
            auto faces=[&](const std::function<std::string(const std::string&)>& sample) {
                std::string s="(";
                for(int f=0;f<5;++f)s+=F+".z < "+std::to_string(f)+".5 ? "+sample(faceName(u,element,f))+" : ";
                return s+sample(faceName(u,element,5))+")";
            };
            if(gather) {
                noteLookup(e,u,"textureGather reads the four texels of the footprint in the face the direction selects, clamped at its edges");
                const char c=kComponents[component(e)];
                static const char* const corners[4]={"float2(0.0, 1.0)","float2(1.0, 1.0)","float2(1.0, 0.0)","float2(0.0, 0.0)"};
                std::string s="float4(";
                for(int i=0;i<4;++i)s+=std::string(i?", ":"")+faces([&](const std::string& f){
                    return f+".eval("+textureHelper("gather")+"("+F+".xy, "+S+", "+corners[i]+"))."+c;});
                return s+")";
            }
            noteLookup(e,u,quoted(e)+" samples the face child shader the direction selects, as GL selects the face"+level);
            return faces([&](const std::string& f){return "float4("+f+".eval("+F+".xy))";});
        }
        // sampler3D and sampler2DArray: one child with the slices or layers stacked vertically.
        const std::string child=elementName(u,element);
        const bool layers=k=="sampler2DArray";
        if(fetch) {
            noteLookup(e,u,"texelFetch reads the texel at its centre in the stacked child shader"+level);
            return "float4("+child+".eval("+textureHelper("atlas")+"(float3("+P+(off.empty()?"":layers?" + int3("+off+", 0)":" + "+off)+"), "+S+")))";
        }
        if(gather) {
            const std::string texel=textureHelper("texelLayer_"+u->wrap);
            return gatherText(e,lane,"("+P+").xy",S+".xy",off,[&](const std::string& c){return child+".eval("+c+")";},u,
                [&](const std::string& base){return texel+"("+base+", ("+P+").z, "+S+")";});
        }
        noteLookup(e,u,quoted(e)+" reads texel centres of the child shader "+quoted(child)+" and filters ("+(u->nearest?"nearest":"linear")+") and wraps ("+u->wrap+
            ") them in the shader"+level);
        if(layers) {
            const std::string texel=textureHelper("texelLayer_"+u->wrap), uv="("+P+").xy", layer="("+P+").z", o2=off.empty()?"":" + float2("+off+")";
            if(u->nearest)return "float4("+child+".eval("+texel+"(floor("+uv+" * "+S+".xy)"+o2+", "+layer+", "+S+")))";
            const std::string q=once("float2",uv+" * "+S+".xy - 0.5");
            std::string s=textureHelper("bilinear")+"(";
            for(const char* d:{"0.0, 0.0","1.0, 0.0","0.0, 1.0","1.0, 1.0"})s+="float4("+child+".eval("+texel+"(floor("+q+")"+o2+" + float2("+d+"), "+layer+", "+S+"))), ";
            return s+"fract("+q+"))";
        }
        const std::string p3=proj?textureHelper("proj3D")+"("+P+")":P, texel=textureHelper("texel3D_"+u->wrap), o3=off.empty()?"":" + float3("+off+")";
        if(u->nearest)return "float4("+child+".eval("+texel+"(floor("+p3+" * "+S+")"+o3+", "+S+")))";
        const std::string q=once("float3",p3+" * "+S+" - 0.5");
        std::string s=textureHelper("trilinear")+"(";
        for(const char* d:{"0.0, 0.0, 0.0","1.0, 0.0, 0.0","0.0, 1.0, 0.0","1.0, 1.0, 0.0","0.0, 0.0, 1.0","1.0, 0.0, 1.0","0.0, 1.0, 1.0","1.0, 1.0, 1.0"})
            s+="float4("+child+".eval("+texel+"(floor("+q+")"+o3+" + float3("+d+"), "+S+"))), ";
        return s+"fract("+q+"))";
    }
    static int component(const Expr& e) {
        const std::string& n=e.function->name;
        const std::size_t at=n=="textureGather"?2:3;
        if(at<e.operands.size() && e.operands[at]->value)return std::clamp(e.operands[at]->value->values[0].i,0,3);
        return 0;
    }
    // textureGather: component c of the four texels of the footprint of linear filtering, in the
    // order (i0, j1), (i1, j1), (i1, j0), (i0, j0).
    std::string gatherText(const Expr& e, int lane, const std::string& uv, const std::string& S, const std::string& off,
        const std::function<std::string(const std::string&)>& sample, const Symbol* u,
        const std::function<std::string(const std::string&)>& texel={}) {
        noteLookup(e,u,"textureGather reads the four texels of the footprint at their centres");
        const bool offsets=e.function->name=="textureGatherOffsets";
        const char c=kComponents[component(e)];
        static const char* const corners[4]={"float2(0.0, 1.0)","float2(1.0, 1.0)","float2(1.0, 0.0)","float2(0.0, 0.0)"};
        std::string s="float4(";
        for(int i=0;i<4;++i) {
            std::string corner=corners[i];
            if(offsets)corner+=" + float2("+expr(*e.operands[2],lane,16)+"["+std::to_string(i)+"])";
            else if(!off.empty())corner+=" + float2("+off+")";
            const std::string at=texel?texel("floor("+uv+" * "+S+" - 0.5) + "+corner):textureHelper("gather")+"("+uv+", "+S+", "+corner+")";
            s+=std::string(i?", ":"")+sample(at)+"."+c;
        }
        return s+")";
    }
    void noteLookup(const Expr& e, const Symbol* u, const std::string& message) {
        replaced(e,"texture",message,sizedSamplers.count(u)?"set the size uniform of "+quoted(u->name)+" (see its declaration)":"no change needed",
            ".eval(");
    }
    GLSL_SKSL_NOINLINE void field(std::string& o, const Expr& e, int lane) {
        const Expr& base=*e.operands[0];
        const Symbol* s=base.kind==ExprKind::Name?base.symbol:nullptr;
        if(s && s->storage==Storage::Uniform && s->type.base==Type::Struct && s->type.structDef && s->type.structDef->block && !s->type.isArray()) {
            o+=userName(e.text);   // member of a flattened uniform block
            return;
        }
        if(isBuiltin(base,"gl_DepthRange")) {
            builtinVariable(base);
            o+=e.text=="near"?"0.0":"1.0";
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
        if(unexpressible(e)||complexLookup(e))return true;
        for(std::size_t i=0;i<e.operands.size();++i)if(spilledOperand(e,i,lane)||needsPrelude(*e.operands[i],lane))return true;
        return false;
    }
    // A vector or scalar constructor reads a matrix argument element by element; one with side
    // effects is computed once, into a temporary, before the statement.
    bool spilledOperand(const Expr& e, std::size_t i, int lane) const {
        const Expr& a=*e.operands[i];
        // Texture lookups repeat their operands (or leave out the level of detail and gradients).
        if(isTextureCall(e) && i>0)return !hoisted.count(&a) && !computed(a,lane) && sideEffects(a);
        // A selection among copies of a function for an array of samplers repeats the arguments.
        if(dynamicSamplerCall(e) && i<e.function->params.size() && e.function->params[i].direction==Direction::In && !samplerType(a.type))
            return !hoisted.count(&a) && !computed(a,lane) && sideEffects(a);
        if(deepOperand(e,i))return !hoisted.count(&a) && !computed(a,lane);
        return e.kind==ExprKind::Constructor && e.type.isBasic() && !e.type.isMatrix() && a.type.isMatrix()
            && !hoisted.count(&a) && !computed(a,lane) && sideEffects(a);
    }
    // Skia stops parsing at 50 levels of nesting (kMaxParseDepth in SkSLParser.cpp): a statement,
    // '(' and an operator add a level, a call, an index or a field two. Its GPU backend parses the
    // program again as it writes it, with every operation in parentheses, so a chain 'a + b + c'
    // counts one level per operator. GLSL compilers have no such limit: an operand that would take
    // an expression past kSpillDepth levels is computed into a temporary before the statement (the
    // left part of a long chain, in its order of evaluation), which leaves room for the statements
    // around it.
    static constexpr unsigned kSpillDepth=20;
    bool deepOperand(const Expr& e, std::size_t i) const {
        const Expr& a=*e.operands[i];
        if(depthOf(a)<=kSpillDepth || samplerType(a.type) || a.type.base==Type::Void || assignedOperand(e,i) || readWrittenOperand(e,i))return false;
        // Operands evaluated only on a condition move before the statement only without side effects.
        if(i>0 && (e.kind==ExprKind::Ternary || (e.kind==ExprKind::Binary && (e.text=="&&"||e.text=="||"))) && sideEffects(a))return false;
        switch(e.kind) {
        case ExprKind::Call: case ExprKind::Constructor: case ExprKind::Binary: case ExprKind::Ternary: return true;
        case ExprKind::Unary: return e.text!="++" && e.text!="--";
        case ExprKind::Index: case ExprKind::Assign: return i==1;
        default: return false;
        }
    }
    mutable std::unordered_map<const Expr*,unsigned> depths;
    // Levels of nesting Skia parses in the SkSL of `e`, with deep operands already computed.
    unsigned depthOf(const Expr& e) const {
        const auto it=depths.find(&e);
        if(it!=depths.end())return it->second;
        unsigned d=0;
        auto operand=[&](std::size_t i){return deepOperand(e,i)?0u:depthOf(*e.operands[i]);};
        switch(e.kind) {
        case ExprKind::IntLiteral: case ExprKind::UintLiteral: case ExprKind::FloatLiteral: case ExprKind::BoolLiteral: case ExprKind::Name: break;
        default: {
            const unsigned own=e.kind==ExprKind::Call||e.kind==ExprKind::Constructor||e.kind==ExprKind::Method||
                e.kind==ExprKind::Index||e.kind==ExprKind::Field?2u:1u;
            for(std::size_t i=0;i<e.operands.size();++i)d=std::max(d,operand(i));
            d+=own;
        }
        }
        if(e.parenthesized)++d;
        depths.emplace(&e,d);
        return d;
    }

    // Emits, before the statement being written, what SkSL cannot compute inside `e`.
    void prepare(const Expr& e, int lane) {
        if(!needsPrelude(e,lane))return;
        if(complexLookup(e)) {
            // Its parts are computed once, before the statement (without a prelude, as in a for
            // loop header, the lookup repeats them).
            prepareOperands(e,lane);
            lookupPrelude=true;
            std::string s;
            textureLookup(s,e,lane);
            lookupPrelude=false;
            const std::string t=temp();
            line(typeName(e.type)+" "+t+" = "+s+";");
            compute(e,lane,t);
            return;
        }
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
        for(std::size_t i=0;i<e.operands.size();++i)if(needsPrelude(*e.operands[i],lane)||spilledOperand(e,i,lane))last=i;
        if(last==e.operands.size())return;
        for(std::size_t i=0;i<last;++i) {
            const Expr& o=*e.operands[i];
            if(spilledOperand(e,i,lane)){spill(o,lane);continue;}
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
        if(spilledOperand(e,last,lane))spill(*e.operands[last],lane);
        else prepare(*e.operands[last],lane);
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
            std::string call=functionName(value.function,lane,calleeSpec(value));
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
        if(dynamicSamplerCall(e) && e.function->returnType.base==Type::Void)dynamicCallStatement(e,lane,samplerArgs(e));
        else if(!computed(e,lane))line(expr(e,lane)+";");   // otherwise the prelude was the statement
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
        const From from(*this,s.loc,lastLine(s));
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
        // Refused loops are still written, so the SkSL returned with the error shows what SkSL rejects.
        case StmtKind::While:
            line("while ("+(s.expr?expr(*s.expr,lane):std::string())+") {");
            ++indent; contents(*s.loop,lane); --indent;
            line("}");
            break;
        case StmtKind::DoWhile:
            line("do {");
            ++indent; contents(*s.loop,lane); --indent;
            line("} while ("+expr(*s.expr,lane)+");");
            break;
        case StmtKind::Return: returnStatement(s,lane); break;
        case StmtKind::Break: line("break;"); break;
        case StmtKind::Continue: line("continue;"); break;
        case StmtKind::Discard: discardStatement(s,lane); break;
        default: break;   // empty statements
        }
    }
    static bool isBuiltin(const Expr& e, const char* name) {
        return e.kind==ExprKind::Name && e.symbol && e.symbol->storage==Storage::BuiltIn && e.symbol->name==name;
    }
    // Built-in variables SkSL lacks, with the warning for their replacement (once per variable).
    void builtinVariable(const Expr& e) {
        const Symbol* s=e.symbol;
        if(!s || !notedBuiltins.insert(s).second)return;
        const std::string& n=s->name;
        if(n=="gl_FragData")
            sink.warning(e.loc,"software.output","gl_FragData[0] is the color the SkSL main returns: GLSL ES 1.00 has gl_MaxDrawBuffers = 1, and SkSL runtime "
                "shaders return a single color","no change needed");
        else if(n=="gl_FrontFacing")
            sink.warning(e.loc,"software.front-facing","gl_FrontFacing is true: Skia draws have no back faces, so the shader runs as for a front-facing primitive",
                "no change needed when the GL draw was front-facing; otherwise replace gl_FrontFacing by false");
        else if(n=="gl_PointCoord")
            sink.warning(e.loc,"software.point-coord","gl_PointCoord is computed as if the drawn area were the point sprite: x from 0 to 1 left to right, y from 0 at the "
                "top to 1 at the bottom of the GL window, from the coordinate and "+quoted(resolution),
                "set "+quoted(resolution.substr(0,resolution.find('.')))+" to the width and height of the drawn area, and draw with the y flip that gl_FragCoord needs (see README)");
        else if(n=="gl_DepthRange")
            sink.warning(e.loc,"software.depth-range","gl_DepthRange has its default values near = 0, far = 1, diff = 1: SkSL runtime shaders have no depth range",
                "no change needed when the GL application keeps the default glDepthRangef(0, 1)");
        else if(s->value)
            sink.warning(e.loc,"software.constant",n+" is "+std::to_string(s->value->values[0].i)+", the minimum the GLSL ES specification allows: the "
                "value of the original device is not known","no change needed unless the shader relies on a larger device limit");
        else return;
        sink.hint(n=="gl_FragData"?colorName:n=="gl_PointCoord"?resolution.substr(0,resolution.find('.')):n=="gl_FrontFacing"?"true":std::string());
    }
    std::set<const Symbol*> notedBuiltins;
    // SkSL runtime shaders cannot discard. Returning transparent black leaves the destination as it
    // is under source-over blending, as discard does. Only the pixel's own lane ends: a discarded
    // neighbour of the quad keeps running for derivatives, like a GPU helper invocation. Outside
    // main, a flag records the discard and the rest runs without effect on the result.
    void discardStatement(const Stmt& s, int lane) {
        if(lane!=0)return;
        const std::string text=current==program.main?"return half4(0.0);":discardedName+" = true;";
        if(notedDiscards.insert(&s).second) {
            sink.warning(s.loc,"software.discard","discard: SkSL runtime shaders cannot discard pixels, so the shader returns transparent black (0, 0, 0, 0) "
                "instead, which leaves the destination unchanged under source-over blending (Skia's default), as discard does",
                "draw with source-over blending (SkBlendMode::kSrcOver); with other blend modes the pixel is not left unchanged");
            sink.hint(text);
        }
        line(text);
    }
    std::set<const Stmt*> notedDiscards;
    static bool discards(const Stmt& s) {
        if(s.kind==StmtKind::Discard)return true;
        for(const auto& x:s.body)if(discards(*x))return true;
        for(const Stmt* x:{s.then.get(),s.otherwise.get(),s.init.get(),s.loop.get()})if(x && discards(*x))return true;
        return false;
    }
    // Whether every path through s ends in a return (or a discard, when `discardEnds`).
    static bool terminates(const Stmt& s, bool discardEnds) {
        switch(s.kind) {
        case StmtKind::Return: return true;
        case StmtKind::Discard: return discardEnds;
        case StmtKind::Block: for(const auto& x:s.body)if(terminates(*x,discardEnds))return true; return false;
        case StmtKind::If: return s.otherwise && terminates(*s.then,discardEnds) && terminates(*s.otherwise,discardEnds);
        default: return false;
        }
    }
    std::string mainReturn() const {
        if(discardedName.empty())return "return half4("+colorName+");";
        return "return "+discardedName+" ? half4(0.0) : half4("+colorName+");";
    }
    // GLSL leaves the value of a function that reaches its end undefined, and a function that
    // discards outside main continues past the discard; SkSL needs a return, so zero is returned.
    void fallOffReturn(const sema::Function& f) {
        if(f.returnType.base==Type::Void || returnsAggregate(f) || terminates(*f.definition->body,false))return;
        const std::string text="return "+zeroValue(f.returnType)+";";
        if(!terminates(*f.definition->body,true) && notedFallOffs.insert(&f).second) {
            sink.warning(f.definition->loc,"software.missing-return","function "+quotedName(f)+" can reach its end without returning a value, which GLSL leaves "
                "undefined and SkSL rejects; it returns zero there","add a return statement at the end of "+quotedName(f));
            sink.hint(text);
        }
        line(text);
    }
    std::set<const sema::Function*> notedFallOffs;
    std::string zeroValue(const Type& t) const {
        if(t.base==Type::Struct && t.structDef) {
            std::string s=typeName(t)+"(";
            for(std::size_t i=0;i<t.structDef->fields.size();++i)s+=(i?", ":"")+zeroValue(t.structDef->fields[i].type);
            return s+")";
        }
        const char* zero=t.base==Type::Bool?"false":t.base==Type::Float?"0.0":"0";
        return t.isScalar()?zero:typeName(t)+"("+zero+")";
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
        // Skia limits how deeply it parses statements (50 levels), so else-if chains stay flat
        // where the condition needs nothing computed before it.
        const Stmt* other=s.otherwise.get();
        for(;other && other->kind==StmtKind::If && !needsPrelude(*other->expr,lane);other=other->otherwise.get()) {
            const From from(*this,other->loc,lastLine(*other));
            line("} else if ("+expr(*other->expr,lane)+") {");
            ++indent; contents(*other->then,lane); --indent;
        }
        if(other) {
            line("} else {");
            ++indent; contents(*other,lane); --indent;
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
        if(s.init && s.init->kind==StmtKind::Declaration) {
            // Several declarators (refused by the semantic pass) share the type of the first.
            const auto& list=s.init->decl->declarators;
            for(std::size_t i=0;i<list.size();++i) {
                if(!list[i].symbol)continue;
                if(i==0)h+=declarator(list[i],lane);
                else {
                    h+=", "+symbolName(list[i].symbol,lane);
                    if(list[i].init){h+=" = ";expr(h,*list[i].init,lane,2);}
                }
            }
        } else if(s.init && s.init->expr)expr(h,*s.init->expr,lane,0);
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
        if(current==program.main){line(mainReturn());return;}
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
        const From from(*this,s.loc,lastLine(s));
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
        replaced(e,"derivative",quoted(e)+" is computed in software: SkSL runtime shaders have no derivatives, so the code that leads to it "
            "is also evaluated for the other three pixels of the 2x2 quad, and the difference is taken the way GPUs compute fine derivatives",
            "no change needed; the code before the last derivative runs up to four times per pixel, so an analytic derivative is faster where one is known",
            " "+result+" = ");
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
            for(int lane=0;lane<4 && !samplerType(e.operands[i]->type);++lane) {
                if(!args.empty())args+=", ";
                expr(args,*e.operands[i],lane,2);
            }
        release(m);
        const std::string name=functionName(&f,0,calleeSpec(e));
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
        if(!samplerParams(f)){definition(f);return;}
        const auto it=specs.find(&f);
        if(it==specs.end())return;
        for(const Spec& spec:it->second) {
            currentSpec=&spec;
            hoisted.clear();
            definition(f);
        }
        currentSpec=nullptr;
    }
    void definition(const sema::Function& f) {
        const From from(*this,f.definition?f.definition->loc:f.loc,f.definition && f.definition->body?lastLine(*f.definition->body):f.loc.line);
        current=&f;
        shared.clear();
        if(quad(&f)) {
            quadFunction(f);
            return;
        }
        if(&f==program.main) {
            mainSignature();
            block(f.definition->body->body,0,0);
            line(mainReturn());
            --indent;
            line("}");
            return;
        }
        const int copies=laneFunction(&f)?4:1;
        for(int lane=0;lane<copies;++lane) {
            std::string signature=(returnsAggregate(f)?"void":typeName(f.returnType))+" "+functionName(&f,lane,currentSpec)+"(";
            bool first=true;
            for(std::size_t i=0;i<f.params.size();++i) {
                if(samplerType(f.params[i].type))continue;
                signature+=(first?"":", ")+parameter(f,i,lane);
                first=false;
            }
            if(returnsAggregate(f))signature+=(first?"out ":", out ")+declaration(f.returnType,resultName);
            line(signature+") {");
            ++indent;
            unwrittenOutputs(f,lane);
            block(f.definition->body->body,0,lane);
            fallOffReturn(f);
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
            if(p.direction!=Direction::Out || p.type.containsOpaque() || (s && a.writes.count(s)))continue;
            zeroFill(parameterName(f.definition->params[i],i,lane),p.type);
        }
    }
    // SkSL cannot assign an array from a constructor: arrays, and structs that contain them, are
    // set element by element.
    void zeroFill(const std::string& target, const Type& t) {
        if(t.isArray()) {
            for(int k=0;k<t.arrays[0];++k)zeroFill(target+"["+std::to_string(k)+"]",t.element());
        } else if(t.base==Type::Struct && t.structDef && t.containsArray()) {
            for(const auto& field:t.structDef->fields)zeroFill(target+"."+userName(field.name),field.type);
        } else line(target+" = "+(t.isScalar()?typeName(t)+"("+zeroValue(t)+")":zeroValue(t))+";");
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
                for(int lane=0;lane<4 && !samplerType(f.params[i].type);++lane)params+=(params.empty()?"":", ")+parameter(f,i,lane);
            if(f.returnType.base!=Type::Void)
                for(int lane=1;lane<4;++lane)params+=std::string(params.empty()?"":", ")+"out "+typeName(f.returnType)+" "+laneName(retName,lane);
            line(typeName(f.returnType)+" "+functionName(&f,0,currentSpec)+"("+params+") {");
            ++indent;
        }
        for(int lane=0;lane<4;++lane)unwrittenOutputs(f,lane);
        if(!isMain && f.returnType.base!=Type::Void && !terminates(*f.definition->body,false))
            for(int lane=1;lane<4;++lane)line(laneName(retName,lane)+" = "+zeroValue(f.returnType)+";");
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
        if(isMain)line(mainReturn());
        else fallOffReturn(f);
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
            std::string name=def.name.empty()?fresh("S"):def.name.compare(0,3,"gl_")==0?fresh(def.name.substr(3)):userName(def.name);
            if(used.count(name))name=fresh(name);   // structs of the same name in different scopes
            used.insert(name);
            structNames[&def]=name;
        }
    }
    void structDefinitions() {
        for(const auto& def:program.structs) {
            if(def.block)continue;
            const From from(*this,def.loc,def.fields.empty()?def.loc.line:def.fields.back().loc.line);
            line("struct "+structNames[&def]+" {");
            ++indent;
            for(const auto& f:def.fields)line(typeName(f.type)+" "+userName(f.name)+dims(f.type)+";");
            --indent;
            line("};");
        }
    }
    void generatedGlobals() {
        const int copies=lanes?4:1;
        {
            // The color global stands for the shader's output declaration.
            const Symbol* output=program.outputs.empty()?nullptr:program.outputs[0];
            const From from(*this,output?declarationStart(output):Loc{},output?output->loc.line:0);
            for(int lane=0;lane<copies;++lane)line("float4 "+laneName(colorName,lane)+";");
        }
        if(usesFragCoord||lanes)for(int lane=0;lane<copies;++lane)line("float4 "+laneName(fragCoordName,lane)+";");
        if(lanes)line("float2 "+quadName+";");
        if(!discardedName.empty())line("bool "+discardedName+";");
        if(!zeroName.empty())line("float "+zeroName+";");
    }
    void uniformName(const std::string& name, Loc at) {
        (void)at; userName(name);
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
                    const From from(*this,d->loc,v.init?std::max(v.loc.line,lastLine(*v.init)):v.loc.line);
                    if(s->storage==Storage::Uniform) {
                        uniformName(s->name,s->loc);
                        if(samplerType(s->type))samplerDeclarations(*s);
                        else line("uniform "+typeName(s->type)+" "+userName(s->name)+dims(s->type)+";"+(s->annotation.empty()?"":"  // "+s->annotation));
                    } else if(s->storage==Storage::Input) {
                        // The texture coordinate is computed at the start of main; other inputs
                        // are refused, and keep a declaration so the SkSL shows them.
                        if(s->textureCoordinate)textureCoordinate=s;
                        for(int lane=0;lane<(lanes?4:1);++lane)line(declaration(s->type,symbolName(s,lane))+";");
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
                        const From from(*this,f.loc,f.loc.line);
                        uniformName(f.name,f.loc);
                        line("uniform "+typeName(f.type)+" "+userName(f.name)+dims(f.type)+";");
                    }
                }
            }
        }
        if(textureCoordinate)textureCoordinateDeclarations(*textureCoordinate);
        else if(usesPointCoord)resolutionUniform();
    }
    // The texture coordinate input holds UV from 0 to 1 across the drawn area: the coordinate of
    // main divided by the iResolution uniform, which the shader declares or the SkSL adds.
    const Symbol* textureCoordinate=nullptr;
    std::string resolution;
    // The size of the drawn area: the shader's iResolution uniform, or one the SkSL declares.
    // Returns a note for the warning when the SkSL declares it.
    std::string resolutionUniform() {
        if(!resolution.empty())return resolutionNote;
        for(const Symbol* u:program.uniforms)
            if(u->name=="iResolution" && u->type.base==Type::Float && u->type.isVector() && !u->type.isArray())
                resolution=u->type.rows==2?u->name:u->name+".xy";
        if(resolution.empty()) {
            // The target application declares iResolution as a vec2.
            const std::string name=fresh("iResolution");
            line("uniform float2 "+name+";");
            resolution=name;
            resolutionNote=" (declared by the SkSL, since the shader has no iResolution uniform)";
        }
        return resolutionNote;
    }
    std::string resolutionNote;
    void textureCoordinateDeclarations(const Symbol& s) {
        const From from(*this,declarationStart(&s),s.loc.line);
        const std::string declared=resolutionUniform();
        const int rows=s.type.rows;
        const std::string resolutionUniform=resolution.substr(0,resolution.find('.'));
        sink.warning(s.loc,"software.input","input "+quoted(s.name)+" is the texture coordinate: SkSL runtime shaders have no inputs, so it is computed as "
            "the coordinate of the pixel divided by "+quoted(resolution)+declared+", which gives UV from 0 to 1 across the drawn area"+
            (rows>2?std::string(", with z = 0")+(rows>3?" and w = 1":""):std::string()),
            "set the uniform "+quoted(resolutionUniform)+" to the width and height of the drawn area");
        sink.hint(symbolName(&s,0)+" = ");
    }
    void textureCoordinatePrologue(const Symbol& s) {
        const From from(*this,declarationStart(&s),s.loc.line);
        for(int lane=0;lane<(lanes?4:1);++lane) {
            const std::string uv=(lane?laneName(fragCoordName,lane)+".xy":coordName)+" / "+resolution;
            const int rows=s.type.rows;
            line(symbolName(&s,lane)+" = "+(rows==2?uv:rows==3?"float3("+uv+", 0.0)":"float4("+uv+", 0.0, 1.0)")+";");
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
        if(textureCoordinate)textureCoordinatePrologue(*textureCoordinate);
        for(int lane=0;lane<(lanes?4:1);++lane)line(laneName(colorName,lane)+" = float4(0);");
        if(!discardedName.empty())line(discardedName+" = false;");
        if(!zeroName.empty())line(zeroName+" = 0.0;");
        // SkSL global arrays cannot have initializers (const ones become plain variables).
        for(const ast::Declarator* v:globalArrays)
            for(int lane=0;lane<globalCopies(*v->symbol);++lane)
                assignParts(symbolName(v->symbol,lane),v->symbol->type,*v->init,lane);
    }
};
}

std::string emitSkSL(const ast::TranslationUnit& unit, const sema::Program& program, const std::vector<Token>& tokens, DiagnosticSink& sink,
    std::vector<SourceSpan>& origins, std::vector<std::pair<std::string,std::string>>& bindings) {
    Emitter emitter(unit,program,tokens,sink);
    auto source=emitter.run(origins);
    bindings.assign(emitter.identifierMap().begin(),emitter.identifierMap().end());
    return source;
}
}
