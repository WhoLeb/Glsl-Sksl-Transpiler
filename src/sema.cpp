#include <charconv>
#include "sema.hpp"
#include "language.hpp"
#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#ifdef _MSC_VER
#define GLSL_SKSL_NOINLINE __declspec(noinline)
#else
#define GLSL_SKSL_NOINLINE __attribute__((noinline))
#endif
namespace glsl_sksl::sema {
namespace {
using ast::ExprKind;
using ast::StmtKind;
using ast::DeclKind;

std::string where(Loc l){return std::to_string(l.line)+":"+std::to_string(l.column);}
std::string quote(const std::string& s){return "'"+s+"'";}
ConstantPtr makeConstant(const Type& t, std::vector<Scalar> values) {
    auto c=std::make_shared<Constant>();
    c->type=t; c->values=std::move(values);
    return c;
}
std::int32_t wrap(std::uint32_t v){return static_cast<std::int32_t>(v);}

// GLSL ES built-in variables of fragment shaders (section 7). The emitter replaces gl_FragData
// (an array of gl_MaxDrawBuffers = 1 color in GLSL ES 1.00), gl_FrontFacing, gl_PointCoord and
// gl_DepthRange.
struct BuiltinVariable { const char* name; const char* type; int first, last; bool writable; const char* unsupported; };
const BuiltinVariable kBuiltinVariables[]={
    {"gl_FragColor","vec4",100,100,true,""},
    {"gl_FragData","vec4[]",100,100,true,""},
    {"gl_FragCoord","vec4",100,320,false,""},       // x and y only; see targetExpr
    {"gl_FrontFacing","bool",100,320,false,""},
    {"gl_PointCoord","vec2",100,320,false,""},
    {"gl_FragDepth","float",300,320,true,"depth output is not available in SkSL runtime shaders"},
    {"gl_HelperInvocation","bool",310,320,false,"helper invocations are not visible in SkSL runtime shaders"},
    {"gl_SampleID","int",320,320,false,"per-sample shading is not available in SkSL runtime shaders"},
    {"gl_SamplePosition","vec2",320,320,false,"per-sample shading is not available in SkSL runtime shaders"},
    {"gl_SampleMaskIn","int[]",320,320,false,"sample masks are not available in SkSL runtime shaders"},
    {"gl_SampleMask","int[]",320,320,true,"sample masks are not available in SkSL runtime shaders"},
    {"gl_PrimitiveID","int",320,320,false,"primitive IDs are not available in SkSL runtime shaders"},
    {"gl_Layer","int",320,320,false,"layered rendering is not available in SkSL runtime shaders"},
    {"gl_DepthRange","gl_DepthRangeParameters",100,320,false,""},
};
// Implementation constants depend on the device. The SkSL gets the minimum value the GLSL ES
// specification of the version allows, the value of a device with exactly the required limits.
struct ImplementationConstant { const char* name; int first; int minimum100, minimum300, minimum310; };
const ImplementationConstant kImplementationConstants[]={
    {"gl_MaxVertexAttribs",100,8,16,16},{"gl_MaxVertexUniformVectors",100,128,256,256},{"gl_MaxVaryingVectors",100,8,15,15},
    {"gl_MaxVertexTextureImageUnits",100,0,16,16},{"gl_MaxCombinedTextureImageUnits",100,8,32,48},{"gl_MaxTextureImageUnits",100,8,16,16},
    {"gl_MaxFragmentUniformVectors",100,16,224,256},{"gl_MaxDrawBuffers",100,1,4,4},{"gl_MaxVertexOutputVectors",300,0,16,16},
    {"gl_MaxFragmentInputVectors",300,0,15,15},{"gl_MinProgramTexelOffset",300,0,-8,-8},{"gl_MaxProgramTexelOffset",300,0,7,7},
    {"gl_MaxImageUnits",310,0,0,4},{"gl_MaxFragmentImageUniforms",310,0,0,0},{"gl_MaxCombinedImageUniforms",310,0,0,4},
    {"gl_MaxFragmentAtomicCounters",310,0,0,0},{"gl_MaxAtomicCounterBindings",310,0,0,1},{"gl_MaxFragmentAtomicCounterBuffers",310,0,0,0},
};

struct Entry { Symbol* variable=nullptr; StructDef* type=nullptr; std::vector<Function*> functions; };

enum class Context { Global, Local, Parameter, Return, BlockMember, Block, QualifierOnly };
struct Quals {
    std::string storage; Loc storageLoc;
    bool constQualified=false, invariant=false, precise=false, memory=false;
    std::string precision, interpolation, auxiliary;
    std::vector<const ast::LayoutId*> layout;
    int location=-1;
};

class Analyzer {
public:
    Analyzer(ast::TranslationUnit& unit, DiagnosticSink& sink, const std::string& vec2Input):unit(unit),sink(sink),program(std::make_unique<Program>()),
        version(unit.version),derivativesExtension(unit.standardDerivatives), textureLodExtension(unit.shaderTextureLod), vec2Input(vec2Input) {
        program->version=version;
        push();
    }

    std::unique_ptr<Program> run() {
        for(auto& d:unit.decls)declaration(*d,true);
        annotateUniforms();
        annotateSamplers();
        chooseTextureCoordinate();
        finishProgram();
        // Also after errors, so every problem is reported at once.
        for(auto& d:unit.decls)targetDeclaration(*d);
        unusedIterationAnnotations();
        return std::move(program);
    }

private:
    ast::TranslationUnit& unit;
    DiagnosticSink& sink;
    std::unique_ptr<Program> program;
    int version;
    bool derivativesExtension, textureLodExtension;
    std::string vec2Input;   // an input typed as a vec2 (its x and y), see analyze
    std::vector<std::unordered_map<std::string,Entry>> scopes;
    std::vector<std::map<std::string,std::string>> precisions;
    std::map<std::string,Symbol*> builtinSymbols;
    Function* current=nullptr;

    bool available(const Function& f) const {
        if(version>f.lastVersion)return false;
        if(f.name.size()>3 && f.name.compare(f.name.size()-3,3,"EXT")==0 && !textureLodExtension)return false;
        return f.version<=version || (derivativesExtension && isDerivative(f.name));
    }

    // ------------------------------------------------------------------ diagnostics
    void invalid(Loc at, const std::string& code, const std::string& message, const std::string& suggestion={}) {
        sink.error(at,"sema."+code,"Invalid "+message,suggestion);
    }
    void unsupported(Loc at, const std::string& code, const std::string& message, const std::string& suggestion={}) {
        sink.error(at,"unsupported."+code,"Unsupported "+message,suggestion);
    }

    // ------------------------------------------------------------------ scopes
    void push(){scopes.emplace_back();precisions.emplace_back();}
    void pop(){scopes.pop_back();precisions.pop_back();}
    struct Scope {
        Analyzer& a;
        explicit Scope(Analyzer& a):a(a){a.push();}
        ~Scope(){a.pop();}
    };
    Entry* find(const std::string& name) {
        for(auto s=scopes.rbegin();s!=scopes.rend();++s) {
            const auto it=s->find(name);
            if(it!=s->end() && (it->second.variable||it->second.type||!it->second.functions.empty()))return &it->second;
        }
        return nullptr;
    }
    // Declares a name in the innermost scope, rejecting a second declaration in the same scope.
    Entry* declareName(const std::string& name, Loc at, const char* kind) {
        auto& scope=scopes.back();
        const auto it=scope.find(name);
        if(it!=scope.end() && (it->second.variable||it->second.type||!it->second.functions.empty())) {
            const Entry& e=it->second;
            const Loc previous=e.variable?e.variable->loc:e.type?e.type->loc:!e.functions.empty()?e.functions.front()->loc:Loc{};
            invalid(at,"redefinition","redefinition of "+quote(name)+" as a "+kind+" (previously declared at "+where(previous)+")",
                "rename one of them");
            return nullptr;
        }
        return &scope[name];
    }
    bool hasDefaultPrecision(const std::string& keyword) const {
        for(auto p=precisions.rbegin();p!=precisions.rend();++p)if(p->count(keyword))return true;
        return keyword=="int"||keyword=="sampler2D"||keyword=="samplerCube"||keyword=="atomic_uint";
    }

    // ------------------------------------------------------------------ types
    Type resolve(const ast::TypeSpec& spec) {
        Type t;
        if(spec.structDef) {
            StructDef* s=defineStruct(*spec.structDef);
            if(!s)return {};
            t=Type::of(Type::Struct); t.structDef=s;
        } else if(!keywordType(spec.name,t)) {
            Entry* e=find(spec.name);
            if(!e || !e->type){invalid(spec.loc,"unknown-type","type "+quote(spec.name)+": it is not declared");return {};}
            t=Type::of(Type::Struct); t.structDef=e->type;
        }
        if(!spec.arrays.empty() && version<300)
            invalid(spec.loc,"array-syntax","array type "+quote(spec.name+"[]")+": array sizes after the type name require GLSL ES 3.00",
                "write the size after the variable name");
        for(const auto& a:spec.arrays)t.arrays.push_back(arraySize(a,"type "+quote(spec.name)));
        return t;
    }
    Type withArrays(Type t, const std::vector<ast::ArraySize>& arrays, const std::string& name) {
        std::vector<int> outer;
        for(const auto& a:arrays)outer.push_back(arraySize(a,quote(name)));
        t.arrays.insert(t.arrays.begin(),outer.begin(),outer.end());
        return t;
    }
    void checkArrayShape(const Type& t, Loc at, const std::string& name) {
        if(t.arrays.size()>1 && version<310)
            invalid(at,"array-of-arrays","declaration of "+quote(name)+": arrays of arrays require GLSL ES 3.10");
    }
    int arraySize(const ast::ArraySize& a, const std::string& what) {
        if(!a.size)return 0;
        const Type t=expr(*a.size);
        if(t.isError())return 1;
        if(!t.isIntegral() || !t.isScalar()) {
            invalid(a.loc,"array-size","array size of "+what+": expected an integer constant expression, found "+quote(toString(t)));
            return 1;
        }
        if(!a.size->constant) {
            invalid(a.loc,"array-size","array size of "+what+": "+quote(ast::toGlsl(*a.size))+" is not a constant expression",
                "use a literal or a const variable");
            return 1;
        }
        long long v=0;
        if(!integer(*a.size,v)) {
            unsupported(a.loc,"constant-evaluation","array size of "+what+": "+quote(ast::toGlsl(*a.size))+" cannot be evaluated at translation time",
                "use a literal or a simple integer expression");
            return 1;
        }
        if(v<=0) {
            invalid(a.loc,"array-size","array size of "+what+": the size must be greater than zero, found "+std::to_string(v));
            return 1;
        }
        return static_cast<int>(std::min<long long>(v,INT_MAX));
    }
    static bool integer(const ast::Expr& e, long long& v) {
        if(!e.value || !e.type.isScalar() || e.value->values.empty())return false;
        const Scalar& s=e.value->values[0];
        if(s.base==Type::Int){v=s.i;return true;}
        if(s.base==Type::Uint){v=s.u;return true;}
        return false;
    }

    StructDef* defineStruct(const ast::StructDecl& d) {
        program->structs.emplace_back();
        StructDef& s=program->structs.back();
        s.name=d.name; s.loc=d.loc;
        std::set<std::string> names;
        for(const auto& group:d.members) {
            const Quals q=qualifiers(group.qualifiers,Context::BlockMember);
            const Type base=resolve(group.type);
            for(const auto& f:group.fields) {
                Type t=withArrays(base,f.arrays,f.name);
                checkArrayShape(t,f.loc,f.name);
                if(t.base==Type::Void)invalid(f.loc,"void-variable","member "+quote(f.name)+" cannot have type void");
                for(int n:t.arrays)if(!n)invalid(f.loc,"unsized-array","member "+quote(f.name)+" must have an explicit array size");
                if(!names.insert(f.name).second)invalid(f.loc,"redefinition","struct member "+quote(f.name)+" is declared twice");
                checkPrecision(q,t,f.loc,"member "+quote(f.name));
                s.fields.push_back({f.name,t,f.loc});
            }
        }
        if(!d.name.empty())
            if(Entry* e=declareName(d.name,d.loc,"struct"))e->type=&s;
        return &s;
    }

    // ------------------------------------------------------------------ qualifiers
    static int rank(const std::string& w) {
        if(w=="layout")return 0;
        if(w=="invariant"||w=="precise")return 1;
        if(w=="smooth"||w=="flat")return 2;
        if(w=="centroid"||w=="sample"||w=="patch")return 3;
        if(w=="lowp"||w=="mediump"||w=="highp")return 5;
        if(w=="coherent"||w=="volatile"||w=="restrict"||w=="readonly"||w=="writeonly")return 6;
        return 4;
    }
    static bool isStorage(const std::string& w) {
        return w=="const"||w=="in"||w=="out"||w=="inout"||w=="uniform"||w=="buffer"||w=="shared"||w=="attribute"||w=="varying";
    }

    Quals qualifiers(const ast::Qualifiers& list, Context ctx) {
        Quals q;
        std::set<std::string> seen;
        int lastRank=-1; std::string lastWord;
        for(const auto& x:list.list) {
            const std::string& w=x.word;
            if(w!="layout" && !seen.insert(w).second){invalid(x.loc,"qualifier","qualifier "+quote(w)+" is repeated");continue;}
            const int r=rank(w);
            if(version<310 && r<lastRank && !(ctx==Context::Parameter && isStorage(w) && isStorage(lastWord)))
                invalid(x.loc,"qualifier-order","qualifier order: "+quote(w)+" must come before "+quote(lastWord)+" in "+versionName(version),
                    "order qualifiers as: layout, invariant, interpolation, storage, precision");
            lastRank=std::max(lastRank,r); lastWord=w;
            if(w=="layout"){for(const auto& id:x.layout)q.layout.push_back(&id);continue;}
            if(w=="lowp"||w=="mediump"||w=="highp"){q.precision=w;continue;}
            if(w=="invariant"){q.invariant=true;continue;}
            if(w=="precise"){q.precise=true;continue;}
            if(w=="smooth"||w=="flat"){if(!q.interpolation.empty())invalid(x.loc,"qualifier","only one interpolation qualifier is allowed");q.interpolation=w;continue;}
            if(w=="centroid"||w=="sample"||w=="patch"){q.auxiliary=w;continue;}
            if(rank(w)==6){q.memory=true;continue;}
            if(w=="const" && ctx==Context::Parameter){q.constQualified=true;continue;}
            if(!q.storage.empty()) {
                invalid(x.loc,"qualifier","declaration has more than one storage qualifier ("+quote(q.storage.empty()?"const":q.storage)+" and "+quote(w)+")");
                continue;
            }
            if(w=="const")q.constQualified=true;
            q.storage=w; q.storageLoc=x.loc;
        }
        if(ctx==Context::Parameter && q.constQualified && (q.storage=="out"||q.storage=="inout"))
            invalid(q.storageLoc,"qualifier","parameter qualifier "+quote("const "+q.storage)+": only input parameters can be const");
        const Loc at=list.list.empty()?Loc{}:list.list.front().loc;
        auto refuse=[&](const std::string& what, const std::string& context, const std::string& suggestion={}) {
            invalid(at,"qualifier",quote(what)+" is not allowed "+context,suggestion);
        };
        switch(ctx) {
        case Context::Local:
            if(!q.storage.empty() && q.storage!="const")refuse(q.storage,"on local variables","declare it at global scope");
            if(!q.layout.empty())refuse("layout","on local variables");
            if(q.invariant)refuse("invariant","on local variables");
            if(!q.interpolation.empty()||!q.auxiliary.empty())refuse(q.interpolation+q.auxiliary,"on local variables");
            break;
        case Context::Parameter:
            if(!q.storage.empty() && q.storage!="in" && q.storage!="out" && q.storage!="inout")refuse(q.storage,"on function parameters");
            if(!q.layout.empty()||q.invariant||q.precise||!q.interpolation.empty()||!q.auxiliary.empty())refuse(list.list.front().word,"on function parameters");
            break;
        case Context::Return:
            for(const auto& x:list.list)
                if(x.word!="lowp" && x.word!="mediump" && x.word!="highp" && x.word!="precise"){refuse(x.word,"on a function return type");break;}
            break;
        case Context::Global: globalQualifiers(q,at); break;
        default: break;
        }
        checkLayout(q,ctx);
        return q;
    }

    void globalQualifiers(const Quals& q, Loc at) {
        const std::string& s=q.storage;
        if(s=="inout")invalid(at,"qualifier",quote("inout")+" is not allowed at global scope","use 'in' or 'out'");
        else if(s=="attribute")invalid(at,"qualifier",quote("attribute")+" is only allowed in vertex shaders");
        else if(s=="shared")invalid(at,"qualifier",quote("shared")+" is only allowed in compute shaders");
        else if((s=="in"||s=="out") && version<300)
            invalid(at,"qualifier",quote(s)+" at global scope requires GLSL ES 3.00",s=="out"?"write gl_FragColor in GLSL ES 1.00":"use 'varying' in GLSL ES 1.00");
        if((!q.interpolation.empty()||q.auxiliary=="centroid"||q.auxiliary=="sample") && s!="in")
            invalid(at,"qualifier",quote(q.interpolation.empty()?q.auxiliary:q.interpolation)+" is only allowed on fragment shader inputs");
        if(q.auxiliary=="patch")invalid(at,"qualifier",quote("patch")+" is only allowed in tessellation shaders");
        if(q.invariant && !(version>=300?s=="out":s=="varying"))
            invalid(at,"qualifier",quote("invariant")+" is only allowed on "+(version>=300?"outputs":"varyings")+" here");
    }

    void checkLayout(Quals& q, Context ctx) {
        struct Rule { const char* name; bool value; int version; };
        static const Rule rules[]={
            {"location",true,300},{"binding",true,310},{"offset",true,310},{"index",true,320},
            {"shared",false,300},{"packed",false,300},{"std140",false,300},{"std430",false,310},{"row_major",false,300},{"column_major",false,300},
            {"early_fragment_tests",false,310},
            {"rgba32f",false,310},{"rgba16f",false,310},{"r32f",false,310},{"rgba8",false,310},{"rgba8_snorm",false,310},{"rgba32i",false,310},
            {"rgba16i",false,310},{"rgba8i",false,310},{"r32i",false,310},{"rgba32ui",false,310},{"rgba16ui",false,310},{"rgba8ui",false,310},{"r32ui",false,310},
            {"blend_support_multiply",false,320},{"blend_support_screen",false,320},{"blend_support_overlay",false,320},
            {"blend_support_darken",false,320},{"blend_support_lighten",false,320},{"blend_support_colordodge",false,320},
            {"blend_support_colorburn",false,320},{"blend_support_hardlight",false,320},{"blend_support_softlight",false,320},
            {"blend_support_difference",false,320},{"blend_support_exclusion",false,320},{"blend_support_hsl_hue",false,320},
            {"blend_support_hsl_saturation",false,320},{"blend_support_hsl_color",false,320},{"blend_support_hsl_luminosity",false,320},
            {"blend_support_all_equations",false,320},
        };
        // SkSL has no layout qualifiers and the SkSL drops them all, so a problem with one cannot
        // change the result: it is reported as a warning. Only 'location' has an effect (which
        // output and input are used), and a location that is not a valid number is ignored.
        auto ignored=[&](const ast::LayoutId& id, const std::string& problem, const std::string& suggestion) {
            sink.warning(id.loc,"sema.layout","Ignored invalid layout qualifier "+quote(id.name)+": "+problem+
                "; the SkSL has no layout qualifiers, so it is dropped",suggestion);
        };
        for(const ast::LayoutId* id:q.layout) {
            const Rule* rule=nullptr;
            for(const auto& r:rules)if(id->name==r.name)rule=&r;
            if(!rule) {
                std::string closest; std::size_t best=3;
                for(const auto& r:rules) {
                    const std::size_t d=editDistance(id->name,r.name);
                    if(d<best){best=d;closest=r.name;}
                }
                ignored(*id,"it is not defined in "+versionName(version),closest.empty()?"remove it":"did you mean "+quote(closest)+"?");
                continue;
            }
            if(rule->version>version){ignored(*id,"it requires "+versionName(rule->version),"remove it, or raise the #version");continue;}
            if(rule->value!=static_cast<bool>(id->value)) {
                ignored(*id,rule->value?"it needs a value, as in "+id->name+"=0":"it does not take a value","write it as GLSL ES defines it");
                continue;
            }
            if(!id->value)continue;
            const Type t=expr(*id->value);
            long long v=-1;
            if(!t.isError() && (!id->value->constant || !integer(*id->value,v) || v<0)) {
                ignored(*id,"it needs a non-negative integer constant","use a number such as 0");
                continue;
            }
            if(id->name=="location") {
                q.location=static_cast<int>(v);
                const bool allowed=(q.storage=="out" && ctx==Context::Global)||((q.storage=="in"||q.storage=="uniform") && version>=310 && ctx==Context::Global);
                if(!allowed)ignored(*id,"a location is not allowed here"+std::string(q.storage=="in" && version<310?" before GLSL ES 3.10":""),
                    q.storage=="in" && version<310?"use '#version 310 es' or later":"remove it");
            }
        }
    }
    static std::size_t editDistance(const std::string& a, const std::string& b) {
        std::vector<std::size_t> row(b.size()+1);
        for(std::size_t j=0;j<=b.size();++j)row[j]=j;
        for(std::size_t i=1;i<=a.size();++i) {
            std::size_t diagonal=row[0]; row[0]=i;
            for(std::size_t j=1;j<=b.size();++j) {
                const std::size_t above=row[j];
                row[j]=std::min({row[j]+1,row[j-1]+1,diagonal+(a[i-1]==b[j-1]?0:1)});
                diagonal=above;
            }
        }
        return row[b.size()];
    }

    void checkPrecision(const Quals& q, const Type& t, Loc at, const std::string& what) {
        if(t.base==Type::Float && q.precision.empty() && !hasDefaultPrecision("float"))
            invalid(at,"precision",what+": no precision is specified for 'float' and fragment shaders have no default",
                "add 'precision highp float;' at the top of the shader, or qualify the declaration with highp, mediump or lowp");
    }

    // ------------------------------------------------------------------ declarations
    void declaration(ast::Decl& d, bool global) {
        switch(d.kind) {
        case DeclKind::Precision: precision(d); break;
        case DeclKind::Function:
            if(!global){invalid(d.loc,"local-function","prototype of "+quote(d.function->name)+": functions must be declared at global scope");break;}
            function(d);
            break;
        case DeclKind::Variable: variables(d,global); break;
        case DeclKind::Block:
            if(!global){invalid(d.loc,"local-block","interface block "+quote(d.block->name)+": blocks must be declared at global scope");break;}
            block(d);
            break;
        case DeclKind::Qualifier: qualifierDeclaration(d,global); break;
        }
    }

    void precision(ast::Decl& d) {
        const std::string& p=d.qualifiers.list.front().word;
        Type t;
        if(!d.type.structDef && keywordType(d.type.name,t) && !t.isArray() && (t.base==Type::Opaque||(t.isScalar() && (t.base==Type::Float||t.base==Type::Int))))
            precisions.back()[d.type.name]=p;
        else invalid(d.loc,"precision","precision statement for "+quote(d.type.name)+": default precision applies only to float, int and opaque types");
    }

    Storage storageOf(const Quals& q, bool global) {
        const std::string& s=q.storage;
        if(s=="uniform")return Storage::Uniform;
        if(s=="in"||s=="varying")return Storage::Input;
        if(s=="out")return Storage::Output;
        if(s=="buffer")return Storage::Buffer;
        return global?Storage::Global:Storage::Local;
    }

    void variables(ast::Decl& d, bool global) {
        const Quals q=qualifiers(d.qualifiers,global?Context::Global:Context::Local);
        const Type base=resolve(d.type);
        if(d.declarators.empty()) {
            if(!d.type.structDef)sink.warning(d.loc,"sema.empty-declaration","declaration of "+quote(d.type.name)+" declares nothing","remove it");
            return;
        }
        const Storage storage=storageOf(q,global);
        for(auto& v:d.declarators)variable(v,base,q,storage,global);
    }

    Symbol* variable(ast::Declarator& v, const Type& base, const Quals& q, Storage storage, bool global) {
        Type t=withArrays(base,v.arrays,v.name);
        checkArrayShape(t,v.loc,v.name);
        const std::string what="declaration of "+quote(v.name);
        bool unsized=false;
        for(int n:t.arrays)if(!n)unsized=true;
        if(unsized && version<300)invalid(v.loc,"unsized-array",what+": arrays without a size require GLSL ES 3.00");
        if(t.base==Type::Void)invalid(v.loc,"void-variable",what+": variables cannot have type void");
        if(q.memory && !(t.base==Type::Opaque && t.opaque.find("image")!=std::string::npos))
            invalid(v.loc,"qualifier",what+": memory qualifiers (coherent, volatile, restrict, readonly, writeonly) only apply to images and buffer blocks");
        if(t.containsOpaque() && storage!=Storage::Uniform)
            invalid(v.loc,"opaque",what+": "+quote(toString(t))+" variables must be uniform");
        if(v.init) {
            const Type init=expr(*v.init);
            if(storage==Storage::Uniform||storage==Storage::Input||storage==Storage::Output||storage==Storage::Buffer)
                invalid(v.loc,"initializer",what+": "+quote(q.storage)+" variables cannot have an initializer",
                    storage==Storage::Uniform?"set the value from the application":"remove the initializer");
            else if(!init.isError()) {
                if(unsized && init.isArray() && init.arrays.size()==t.arrays.size()) {
                    Type sized=t;
                    for(std::size_t k=0;k<sized.arrays.size();++k)if(!sized.arrays[k])sized.arrays[k]=init.arrays[k];
                    t=sized; unsized=false;
                }
                if(t.isArray() && version<300)invalid(v.loc,"array-initializer",what+": arrays cannot be initialized in GLSL ES 1.00");
                else if(init!=t)
                    invalid(v.init->loc,"type-mismatch",what+": cannot initialize "+quote(toString(t))+" with a value of type "+quote(toString(init)),
                        "GLSL ES has no implicit conversions; use a constructor such as "+toString(t.element())+"(...)");
                else if(q.constQualified && !v.init->constant)
                    invalid(v.init->loc,"const-initializer",what+": a const variable needs a constant expression, but "+quote(ast::toGlsl(*v.init))+" is not constant",
                        "remove 'const', or compute the value from literals and other const variables");
                else if(global && !v.init->constant)
                    invalid(v.init->loc,"global-initializer",what+": global initializers must be constant expressions, but "+quote(ast::toGlsl(*v.init))+" is not constant",
                        "assign the value at the start of main() instead");
            }
        } else {
            if(q.constQualified)invalid(v.loc,"const-initializer",what+": const variables must be initialized");
            if(unsized && version>=300)invalid(v.loc,"unsized-array",what+": an array without a size needs an initializer","give the array a size");
        }
        checkPrecision(q,t,v.loc,what);
        if(storage==Storage::Output)checkOutputType(t,v.loc,v.name);
        program->symbols.emplace_back();
        Symbol& s=program->symbols.back();
        s.name=v.name; s.type=t; s.loc=v.loc; s.storage=storage;
        s.constQualified=q.constQualified; s.location=q.location;
        if(storage==Storage::Input && v.name==vec2Input && t.base==Type::Float && t.isVector() && !t.isArray())s.type=Type::of(Type::Float,2);
        if(q.constQualified && v.init && v.init->constant && v.init->type==t) {
            s.constantExpression=true; s.value=v.init->value;
        }
        if(q.constQualified){s.readOnly=true;s.readOnlyReason=quote(v.name)+" is const";}
        else if(storage==Storage::Uniform){s.readOnly=true;s.readOnlyReason=quote(v.name)+" is a uniform";}
        else if(storage==Storage::Input){s.readOnly=true;s.readOnlyReason=quote(v.name)+" is a shader input";}
        if(storage==Storage::Uniform)program->uniforms.push_back(&s);
        if(storage==Storage::Input)program->inputs.push_back(&s);
        if(storage==Storage::Output)program->outputs.push_back(&s);
        if(Entry* e=declareName(v.name,v.loc,"variable"))e->variable=&s;
        v.symbol=&s;
        return &s;
    }

    void checkOutputType(const Type& t, Loc at, const std::string& name) {
        const Type e=t.element();
        if(t.arrays.size()>1 || !e.isBasic() || e.isMatrix() || e.base==Type::Bool)
            invalid(at,"output-type","fragment output "+quote(name)+": outputs must be float, int or uint scalars or vectors, not "+quote(toString(t)));
    }

    void block(ast::Decl& d) {
        ast::InterfaceBlock& b=*d.block;
        const Quals q=qualifiers(b.qualifiers,Context::Block);
        const std::string& s=q.storage;
        const bool allowed=(s=="uniform" && version>=300)||(s=="buffer" && version>=310)||((s=="in"||s=="out") && version>=320);
        if(!allowed)invalid(b.loc,"block","interface block "+quote(b.name)+": "+(s.empty()?"a storage qualifier is required":quote(s)+" blocks are not available in "+versionName(version)));
        program->structs.emplace_back();
        StructDef& def=program->structs.back();
        def.name=b.name; def.loc=b.loc; def.block=true;
        std::set<std::string> names;
        for(const auto& group:b.members) {
            const Quals mq=qualifiers(group.qualifiers,Context::BlockMember);
            if(group.type.structDef)invalid(group.type.loc,"block","member of block "+quote(b.name)+": struct definitions are not allowed in blocks");
            const Type base=resolve(group.type);
            for(const auto& f:group.fields) {
                Type t=withArrays(base,f.arrays,f.name);
                if(t.containsOpaque())invalid(f.loc,"block","member "+quote(f.name)+" of block "+quote(b.name)+": opaque types are not allowed in blocks");
                if(!names.insert(f.name).second)invalid(f.loc,"redefinition","block member "+quote(f.name)+" is declared twice");
                checkPrecision(mq,t,f.loc,"member "+quote(f.name));
                def.fields.push_back({f.name,t,f.loc});
            }
        }
        const Storage storage=storageOf(q,true);
        auto add=[&](const std::string& name, const Type& t, Loc at) {
            program->symbols.emplace_back();
            Symbol& sym=program->symbols.back();
            sym.name=name; sym.type=t; sym.loc=at; sym.storage=storage;
            if(storage==Storage::Uniform){sym.readOnly=true;sym.readOnlyReason=quote(name)+" is a uniform";}
            if(storage==Storage::Input){sym.readOnly=true;sym.readOnlyReason=quote(name)+" is a shader input";}
            if(storage==Storage::Uniform)program->uniforms.push_back(&sym);
            if(Entry* e=declareName(name,at,"variable"))e->variable=&sym;
        };
        if(b.instance.empty()) {
            for(const auto& f:def.fields)add(f.name,f.type,f.loc);
        } else {
            Type t=Type::of(Type::Struct); t.structDef=&def;
            add(b.instance,withArrays(t,b.instanceArrays,b.instance),b.instanceLoc);
        }
    }

    void qualifierDeclaration(ast::Decl& d, bool global) {
        const Quals q=qualifiers(d.qualifiers,Context::QualifierOnly);
        if(!global){invalid(d.loc,"qualifier","qualifier-only declarations must be at global scope");return;}
        for(const auto& n:d.names) {
            Entry* e=find(n.first);
            const Symbol* s=e && e->variable?e->variable:builtinSymbol(n.first);
            if(!s){invalid(n.second,"undeclared",quote(n.first)+" is not declared");continue;}
            if(q.invariant) {
                const bool output=s->storage==Storage::Output||(s->storage==Storage::BuiltIn && !s->readOnly);
                const bool varying=version<300 && s->storage==Storage::Input;
                if(!output && !varying)invalid(n.second,"qualifier",quote("invariant")+" can only be applied to shader outputs, not "+quote(n.first));
            }
        }
    }

    void function(ast::Decl& d) {
        ast::Function& f=*d.function;
        const Quals rq=qualifiers(f.returnQualifiers,Context::Return);
        const Type ret=resolve(f.returnType);
        if(f.returnType.structDef)invalid(f.loc,"struct-in-signature","return type of "+quote(f.name)+": struct definitions are not allowed in function return types");
        if(ret.isArray() && version<300)invalid(f.loc,"array-return","function "+quote(f.name)+": functions cannot return arrays in GLSL ES 1.00");
        for(int n:ret.arrays)if(!n)invalid(f.loc,"unsized-array","function "+quote(f.name)+": the returned array needs a size");
        if(ret.containsOpaque())invalid(f.loc,"opaque","function "+quote(f.name)+": functions cannot return opaque types");
        if(ret.base!=Type::Void)checkPrecision(rq,ret,f.loc,"return type of "+quote(f.name));
        std::vector<Param> params;
        for(auto& p:f.params) {
            const Quals pq=qualifiers(p.qualifiers,Context::Parameter);
            if(p.type.structDef)invalid(p.loc,"struct-in-signature","parameter "+quote(p.name)+": struct definitions are not allowed in parameter lists");
            Type t=withArrays(resolve(p.type),p.arrays,p.name);
            checkArrayShape(t,p.loc,p.name);
            for(int n:t.arrays)if(!n)invalid(p.loc,"unsized-array","parameter "+quote(p.name)+": array parameters need a size");
            if(t.base==Type::Void)invalid(p.loc,"void-variable","parameter "+quote(p.name)+" of "+quote(f.name)+" cannot have type void",
                "write f(void) or f() for a function without parameters");
            checkPrecision(pq,t,p.loc,"parameter "+quote(p.name));
            const Direction dir=pq.storage=="out"?Direction::Out:pq.storage=="inout"?Direction::InOut:Direction::In;
            if(t.containsOpaque() && dir!=Direction::In)invalid(p.loc,"opaque","parameter "+quote(p.name)+": opaque types can only be input parameters");
            params.push_back({t,dir,pq.constQualified,p.name});
        }
        if(f.name=="main") {
            if(ret.base!=Type::Void || !params.empty())
                invalid(f.loc,"main","declaration of main: it must be declared as void main()","write void main() and return the color by writing the output");
        }
        if(isBuiltinFunctionName(f.name,version)) {
            bool same=version>=300;
            for(const Function* b:builtinOverloads(f.name))
                if(b->version<=version && b->params.size()==params.size()) {
                    bool match=true;
                    for(std::size_t i=0;i<params.size();++i)if(b->params[i].type!=params[i].type)match=false;
                    if(match)same=true;
                }
            if(same)invalid(f.loc,"builtin-redefinition","declaration of "+quote(f.name)+": built-in functions cannot be "+
                (version>=300?"redefined or overloaded in "+versionName(version):std::string("redefined")),"rename the function");
        }
        Entry& e=scopes.front()[f.name];
        if(e.variable||e.type) {
            invalid(f.loc,"redefinition","declaration of function "+quote(f.name)+": the name is already used by a "+(e.variable?"variable":"struct"));
            return;
        }
        Function* fn=nullptr;
        for(Function* g:e.functions) {
            bool match=g->params.size()==params.size();
            for(std::size_t i=0;match && i<params.size();++i)if(g->params[i].type!=params[i].type)match=false;
            if(!match)continue;
            fn=g;
            if(g->returnType!=ret)
                invalid(f.loc,"overload","declaration of "+quote(f.name)+": it differs from the declaration at "+where(g->loc)+" only in its return type");
            for(std::size_t i=0;i<params.size();++i)
                if(g->params[i].direction!=params[i].direction || g->params[i].constQualified!=params[i].constQualified)
                    invalid(f.params[i].loc,"overload","parameter "+std::to_string(i+1)+" of "+quote(f.name)+": its qualifiers differ from the declaration at "+where(g->loc));
            if(f.body && g->definition)invalid(f.loc,"redefinition","redefinition of function "+quote(signature(*g))+" (previously defined at "+where(g->definition->loc)+")");
        }
        if(f.name=="main" && !fn && !e.functions.empty())invalid(f.loc,"main","declaration of main: main cannot be overloaded");
        if(!fn) {
            program->functions.emplace_back();
            fn=&program->functions.back();
            fn->name=f.name; fn->returnType=ret; fn->params=params; fn->loc=f.loc; fn->declaration=&f;
            e.functions.push_back(fn);
        }
        f.symbol=fn;
        if(!f.body)return;
        fn->definition=&f;
        for(std::size_t i=0;i<params.size();++i)fn->params[i].name=params[i].name;
        if(f.name=="main" && ret.base==Type::Void && params.empty())program->main=fn;
        Scope scope(*this);
        current=fn;
        for(std::size_t i=0;i<f.params.size();++i) {
            ast::Param& p=f.params[i];
            if(p.name.empty())continue;
            program->symbols.emplace_back();
            Symbol& s=program->symbols.back();
            s.name=p.name; s.type=params[i].type; s.loc=p.loc; s.storage=Storage::Parameter;
            s.direction=params[i].direction; s.constQualified=params[i].constQualified;
            if(s.constQualified){s.readOnly=true;s.readOnlyReason=quote(p.name)+" is a const parameter";}
            if(Entry* entry=declareName(p.name,p.loc,"parameter"))entry->variable=&s;
            p.symbol=&s;
        }
        for(auto& stmt:f.body->body)statement(*stmt);
        current=nullptr;
    }

    // ------------------------------------------------------------------ statements
    void scoped(ast::Stmt& s) {
        Scope scope(*this);
        statement(s);
    }
    void condition(ast::Expr& e, const char* statementName) {
        const Type t=expr(e);
        if(!t.isError() && !(t.isScalar() && t.base==Type::Bool))
            invalid(e.loc,"condition","condition of "+quote(statementName)+": expected a bool, found "+quote(toString(t)),
                t.isVector() && t.base==Type::Bool?"reduce the vector with any() or all()":"compare the value, e.g. x != 0.0");
    }
    void conditionDeclaration(ast::Stmt& s, const char* statementName) {
        if(!s.decl){if(s.expr)condition(*s.expr,statementName);return;}
        variables(*s.decl,false);
        const auto& v=s.decl->declarators.front();
        if(v.symbol && !v.symbol->type.isError() && !(v.symbol->type.isScalar() && v.symbol->type.base==Type::Bool))
            invalid(v.loc,"condition","condition of "+quote(statementName)+": the declared variable must be a bool");
    }

    void statement(ast::Stmt& s) {
        switch(s.kind) {
        case StmtKind::Block:
            if(s.scoped){Scope scope(*this);for(auto& x:s.body)statement(*x);}
            else for(auto& x:s.body)statement(*x);
            break;
        case StmtKind::Declaration: declaration(*s.decl,false); break;
        case StmtKind::Expression: expr(*s.expr); break;
        case StmtKind::If:
            condition(*s.expr,"if");
            scoped(*s.then);
            if(s.otherwise)scoped(*s.otherwise);
            break;
        case StmtKind::Switch: switchStatement(s); break;
        case StmtKind::Case: case StmtKind::Default: break;
        case StmtKind::For: {
            Scope scope(*this);
            if(s.init)statement(*s.init);
            conditionDeclaration(s,"for");
            if(s.step)expr(*s.step);
            loopBody(*s.loop);
            break;
        }
        case StmtKind::While: {
            Scope scope(*this);
            conditionDeclaration(s,"while");
            loopBody(*s.loop);
            break;
        }
        case StmtKind::DoWhile:
            scoped(*s.loop);
            condition(*s.expr,"do-while");
            break;
        case StmtKind::Return: returnStatement(s); break;
        default: break;
        }
    }
    void loopBody(ast::Stmt& body) {
        if(body.kind==StmtKind::Block && !body.scoped)for(auto& x:body.body)statement(*x);
        else statement(body);
    }

    void switchStatement(ast::Stmt& s) {
        const Type selector=expr(*s.expr);
        const bool selectorOk=!selector.isError() && selector.isScalar() && selector.isIntegral();
        if(!selector.isError() && !selectorOk)
            invalid(s.expr->loc,"switch","switch selector: expected an int or uint, found "+quote(toString(selector)));
        Scope scope(*this);
        std::map<long long,Loc> labels;
        const ast::Stmt* defaultLabel=nullptr;
        bool labelled=false;
        for(auto& x:s.body) {
            if(x->kind==StmtKind::Case) {
                labelled=true;
                const Type t=expr(*x->expr);
                if(t.isError())continue;
                long long v=0;
                if(!x->expr->constant)invalid(x->expr->loc,"case","case label "+quote(ast::toGlsl(*x->expr))+" is not a constant expression");
                else if(selectorOk && t!=selector)
                    invalid(x->expr->loc,"case","case label "+quote(ast::toGlsl(*x->expr))+": expected "+quote(toString(selector))+" like the selector, found "+quote(toString(t)));
                else if(integer(*x->expr,v)) {
                    const auto previous=labels.find(v);
                    if(previous!=labels.end())invalid(x->expr->loc,"case","duplicate case label "+std::to_string(v)+" (also at "+where(previous->second)+")");
                    else labels[v]=x->loc;
                }
            } else if(x->kind==StmtKind::Default) {
                labelled=true;
                if(defaultLabel)invalid(x->loc,"case","second 'default' label in the switch (the first is at "+where(defaultLabel->loc)+")");
                defaultLabel=x.get();
            } else {
                if(!labelled)invalid(x->loc,"switch","statement before the first case label of a switch");
                labelled=true;
                statement(*x);
            }
        }
        if(!s.body.empty() && (s.body.back()->kind==StmtKind::Case||s.body.back()->kind==StmtKind::Default))
            invalid(s.body.back()->loc,"switch","the last label of a switch must be followed by a statement","add 'break;' after the label");
    }

    void returnStatement(ast::Stmt& s) {
        if(!current)return;
        const Type& ret=current->returnType;
        if(s.expr) {
            const Type t=expr(*s.expr);
            if(ret.base==Type::Void)invalid(s.loc,"return","return in "+quote(current->name)+": a void function cannot return a value");
            else if(!t.isError() && !ret.isError() && t!=ret)
                invalid(s.expr->loc,"return","return in "+quote(current->name)+": expected "+quote(toString(ret))+", found "+quote(toString(t)));
        } else if(ret.base!=Type::Void && !ret.isError())
            invalid(s.loc,"return","return in "+quote(current->name)+": the function must return a "+quote(toString(ret))+" value");
    }

    // ------------------------------------------------------------------ expressions
    Symbol* builtinSymbol(const std::string& name) {
        const auto cached=builtinSymbols.find(name);
        if(cached!=builtinSymbols.end())return cached->second;
        Symbol s;
        bool found=false;
        for(const auto& b:kBuiltinVariables) {
            if(name!=b.name || version<b.first || version>b.last)continue;
            found=true;
            std::string type=b.type;
            const bool unsizedArray=type.size()>2 && type.compare(type.size()-2,2,"[]")==0;
            if(unsizedArray)type.resize(type.size()-2);
            if(type=="gl_DepthRangeParameters")s.type=depthRangeType();
            else if(!type.empty())keywordType(type,s.type);
            if(unsizedArray)s.type.arrays.push_back(name=="gl_FragData"?1:0);
            s.readOnly=!b.writable;
            s.unsupported=b.unsupported;
        }
        for(const auto& c:kImplementationConstants)
            if(name==c.name && version>=c.first) {
                found=true;
                s.type=Type::of(Type::Int); s.readOnly=true; s.constQualified=true; s.constantExpression=true;
                s.value=makeConstant(s.type,{Scalar::of(static_cast<std::int32_t>(version>=310?c.minimum310:version>=300?c.minimum300:c.minimum100))});
            }
        if(!found){builtinSymbols[name]=nullptr;return nullptr;}
        s.name=name; s.storage=Storage::BuiltIn;
        if(s.readOnly && s.readOnlyReason.empty())s.readOnlyReason=quote(name)+" is read-only";
        program->symbols.push_back(std::move(s));
        builtinSymbols[name]=&program->symbols.back();
        return &program->symbols.back();
    }

    // struct gl_DepthRangeParameters { highp float near, far, diff; }
    Type depthRangeType() {
        if(!depthRange) {
            program->structs.push_back({"gl_DepthRangeParameters",{},{}});
            depthRange=&program->structs.back();
            for(const char* f:{"near","far","diff"})depthRange->fields.push_back({f,Type::of(Type::Float),{}});
        }
        Type t=Type::of(Type::Struct);
        t.structDef=depthRange;
        return t;
    }
    StructDef* depthRange=nullptr;

    std::string missingBuiltinHint(const std::string& name) const {
        if(name=="gl_FragColor"||name=="gl_FragData")
            return version>=300?"GLSL ES 3.00 removed "+name+"; declare 'out vec4 fragColor;' and write to it":"";
        for(const auto& b:kBuiltinVariables)if(name==b.name)return quote(name)+" is not available in "+versionName(version);
        return "declare it before use";
    }

    Type nameExpr(ast::Expr& e) {
        Entry* entry=find(e.text);
        Symbol* s=nullptr;
        if(entry) {
            if(!entry->variable) {
                invalid(e.loc,"not-a-variable",quote(e.text)+" is a "+(entry->type?"type":"function")+", not a variable");
                return {};
            }
            s=entry->variable;
        } else s=builtinSymbol(e.text);
        if(!s){invalid(e.loc,"undeclared","use of undeclared identifier "+quote(e.text),missingBuiltinHint(e.text));return {};}
        e.symbol=s;
        e.constant=s->constantExpression;
        e.value=s->value;
        return s->type;
    }

    // Root variable of an l-value expression, or null.
    static const Symbol* root(const ast::Expr& e) {
        switch(e.kind) {
        case ExprKind::Name: return e.symbol;
        case ExprKind::Index: case ExprKind::Field: return root(*e.operands[0]);
        default: return nullptr;
        }
    }
    bool lvalue(const ast::Expr& e, std::string& why) const {
        switch(e.kind) {
        case ExprKind::Name:
            if(!e.symbol)return false;
            if(e.symbol->readOnly){why=e.symbol->readOnlyReason;return false;}
            return true;
        case ExprKind::Index: return lvalue(*e.operands[0],why);
        case ExprKind::Field:
            if(!e.swizzle.empty()) {
                for(std::size_t i=0;i<e.swizzle.size();++i)
                    for(std::size_t j=i+1;j<e.swizzle.size();++j)
                        if(e.swizzle[i]==e.swizzle[j]){why="the swizzle ."+e.text+" repeats a component";return false;}
            }
            return lvalue(*e.operands[0],why);
        default:
            why="the expression is not a variable, array element, struct field or swizzle";
            return false;
        }
    }
    bool requireLvalue(const ast::Expr& e, const std::string& context) {
        if(e.type.isError())return false;
        std::string why;
        if(lvalue(e,why)){markWritten(e);return true;}
        invalid(e.loc,"not-lvalue",context+": "+quote(ast::toGlsl(e))+" cannot be written ("+why+")");
        return false;
    }
    void markWritten(const ast::Expr& e) {
        const Symbol* s=root(e);
        if(s && s->storage==Storage::BuiltIn) {
            if(s->name=="gl_FragColor")program->writesFragColor=true;
            if(s->name=="gl_FragData")program->writesFragData=true;
        }
    }

    // Operands are typed first in this lean function; check() then types the node itself without
    // recursing, so deep expressions do not stack the frames of the message-building handlers.
    Type expr(ast::Expr& e) {
        for(auto& o:e.operands)expr(*o);
        e.type=check(e);
        return e.type;
    }

    GLSL_SKSL_NOINLINE Type check(ast::Expr& e) {
        switch(e.kind) {
        case ExprKind::IntLiteral:
            e.constant=true; e.value=makeConstant(Type::of(Type::Int),{Scalar::of(wrap(static_cast<std::uint32_t>(e.intValue)))});
            return Type::of(Type::Int);
        case ExprKind::UintLiteral:
            e.constant=true; e.value=makeConstant(Type::of(Type::Uint),{Scalar::ofUint(static_cast<std::uint32_t>(e.intValue))});
            return Type::of(Type::Uint);
        case ExprKind::FloatLiteral:
            e.constant=true; e.value=makeConstant(Type::of(Type::Float),{Scalar::of(static_cast<float>(e.floatValue))});
            return Type::of(Type::Float);
        case ExprKind::BoolLiteral:
            e.constant=true; e.value=makeConstant(Type::of(Type::Bool),{Scalar::of(e.intValue!=0)});
            return Type::of(Type::Bool);
        case ExprKind::Name: return nameExpr(e);
        case ExprKind::Unary: case ExprKind::Postfix: return unary(e);
        case ExprKind::Binary: return binary(e);
        case ExprKind::Assign: return assign(e);
        case ExprKind::Ternary: return ternary(e);
        case ExprKind::Call: return call(e);
        case ExprKind::Constructor: return constructor(e);
        case ExprKind::Index: return index(e);
        case ExprKind::Field: return field(e);
        case ExprKind::Method: return method(e);
        case ExprKind::Comma: return e.operands[1]->type;
        }
        return {};
    }

    Type unary(ast::Expr& e) {
        ast::Expr& operand=*e.operands[0];
        const Type t=operand.type;
        if(t.isError())return {};
        const std::string& op=e.text;
        if(op=="++"||op=="--") {
            if(!t.isNumeric()){invalid(e.loc,"operator","operator "+quote(op)+" needs an int, uint or float operand, found "+quote(toString(t)));return {};}
            requireLvalue(operand,"operand of "+quote(op));
            return t;
        }
        if(op=="!") {
            if(!(t.isScalar() && t.base==Type::Bool)){invalid(e.loc,"operator","operator '!' needs a bool, found "+quote(toString(t)),t.isVector()?"use not() for bool vectors":"");return {};}
        } else if(op=="~") {
            if(!(t.isIntegral())){invalid(e.loc,"operator","operator '~' needs an int or uint operand, found "+quote(toString(t)));return {};}
        } else if(!t.isNumeric()){invalid(e.loc,"operator","operator "+quote(op)+" needs a numeric operand, found "+quote(toString(t)));return {};}
        e.constant=operand.constant;
        if(operand.value) {
            std::vector<Scalar> r;
            for(Scalar s:operand.value->values) {
                if(op=="-"){if(s.base==Type::Float)s.f=-s.f;else if(s.base==Type::Int)s.i=wrap(0u-static_cast<std::uint32_t>(s.i));else s.u=0u-s.u;}
                else if(op=="~"){if(s.base==Type::Int)s.i=~s.i;else s.u=~s.u;}
                else if(op=="!")s.b=!s.b;
                r.push_back(s);
            }
            e.value=makeConstant(t,std::move(r));
        }
        return t;
    }

    // Result type of a binary operator, or an Error type with `why` set.
    Type binaryType(const std::string& op, const Type& l, const Type& r, std::string& why) const {
        auto mismatch=[&]{why="the operand types "+quote(toString(l))+" and "+quote(toString(r))+" do not match";return Type{};};
        if(op=="&&"||op=="||"||op=="^^") {
            if(l.isScalar() && r.isScalar() && l.base==Type::Bool && r.base==Type::Bool)return Type::of(Type::Bool);
            why="both operands must be bool"; return {};
        }
        if(op=="=="||op=="!=") {
            if(l!=r)return mismatch();
            if(l.containsOpaque()){why="opaque types cannot be compared";return {};}
            if(l.base==Type::Void){why="void values cannot be compared";return {};}
            if(version<300 && l.containsArray()){why="arrays cannot be compared in GLSL ES 1.00";return {};}
            return Type::of(Type::Bool);
        }
        if(op=="<"||op==">"||op=="<="||op==">=") {
            if(l.isScalar() && l.isNumeric() && l==r)return Type::of(Type::Bool);
            why=l.isScalar() && r.isScalar()?"both operands must be int, uint or float of the same type":"relational operators only compare scalars; use lessThan() and similar for vectors";
            return {};
        }
        if(op=="<<"||op==">>") {
            if(!l.isIntegral() || !r.isIntegral()){why="shift operands must be int or uint";return {};}
            if(!r.isScalar() && r.rows!=l.rows){why="the shift count must be a scalar or a vector of the same size";return {};}
            return l;
        }
        const bool bitwise=op=="&"||op=="|"||op=="^";
        if(op=="%"||bitwise) {
            if(!l.isIntegral() || !r.isIntegral()){why=quote(op)+" needs int or uint operands";return {};}
        } else if(!l.isNumeric() || !r.isNumeric()){why=quote(op)+" needs int, uint or float operands, found "+quote(toString(l))+" and "+quote(toString(r));return {};}
        if(l.base!=r.base)return mismatch();
        if(l.isScalar())return r;
        if(r.isScalar())return l;
        if(op=="*") {
            if(l.isMatrix() && r.isMatrix()) {
                if(l.columns!=r.rows){why="matrix sizes do not match for multiplication";return {};}
                return Type::of(Type::Float,l.rows,r.columns);
            }
            if(l.isMatrix() && r.isVector()) {
                if(l.columns!=r.rows){why="the vector size must equal the number of matrix columns";return {};}
                return Type::of(Type::Float,l.rows);
            }
            if(l.isVector() && r.isMatrix()) {
                if(l.rows!=r.rows){why="the vector size must equal the number of matrix rows";return {};}
                return Type::of(Type::Float,r.columns);
            }
        }
        if(l==r)return l;
        return mismatch();
    }

    static bool componentOp(const std::string& op, const Scalar& a, const Scalar& b, Scalar& out) {
        out=a;
        if(op=="<<"||op==">>") {
            const long long count=b.base==Type::Int?b.i:static_cast<long long>(b.u);
            if(count<0||count>31)return false;
            if(a.base==Type::Int)out.i=op=="<<"?wrap(static_cast<std::uint32_t>(a.i)<<count):(a.i>>count);
            else out.u=op=="<<"?(a.u<<count):(a.u>>count);
            return true;
        }
        switch(a.base) {
        case Type::Float:
            if(op=="+")out.f=a.f+b.f; else if(op=="-")out.f=a.f-b.f; else if(op=="*")out.f=a.f*b.f;
            else if(op=="/")out.f=a.f/b.f; else return false;
            return true;
        case Type::Int: {
            const std::uint32_t x=static_cast<std::uint32_t>(a.i), y=static_cast<std::uint32_t>(b.i);
            if(op=="+")out.i=wrap(x+y); else if(op=="-")out.i=wrap(x-y); else if(op=="*")out.i=wrap(x*y);
            else if(op=="/"){if(!b.i||(a.i==INT32_MIN && b.i==-1))return false;out.i=a.i/b.i;}
            else if(op=="%"){if(b.i<=0||a.i<0)return false;out.i=a.i%b.i;}
            else if(op=="&")out.i=a.i&b.i; else if(op=="|")out.i=a.i|b.i; else if(op=="^")out.i=a.i^b.i;
            else return false;
            return true;
        }
        case Type::Uint:
            if(op=="+")out.u=a.u+b.u; else if(op=="-")out.u=a.u-b.u; else if(op=="*")out.u=a.u*b.u;
            else if(op=="/"){if(!b.u)return false;out.u=a.u/b.u;}
            else if(op=="%"){if(!b.u)return false;out.u=a.u%b.u;}
            else if(op=="&")out.u=a.u&b.u; else if(op=="|")out.u=a.u|b.u; else if(op=="^")out.u=a.u^b.u;
            else return false;
            return true;
        default: return false;
        }
    }
    static bool less(const Scalar& a, const Scalar& b) {
        return a.base==Type::Float?a.f<b.f:a.base==Type::Int?a.i<b.i:a.u<b.u;
    }

    ConstantPtr foldBinary(const std::string& op, const Type& result, const ast::Expr& l, const ast::Expr& r) const {
        if(!l.value || !r.value)return nullptr;
        const auto& a=l.value->values; const auto& b=r.value->values;
        if(op=="=="||op=="!=") {
            bool equal=a.size()==b.size();
            for(std::size_t i=0;equal && i<a.size();++i)equal=a[i]==b[i];
            return makeConstant(result,{Scalar::of(op=="=="?equal:!equal)});
        }
        if(op=="&&")return makeConstant(result,{Scalar::of(a[0].b && b[0].b)});
        if(op=="||")return makeConstant(result,{Scalar::of(a[0].b || b[0].b)});
        if(op=="^^")return makeConstant(result,{Scalar::of(a[0].b != b[0].b)});
        if(op=="<")return makeConstant(result,{Scalar::of(less(a[0],b[0]))});
        if(op==">")return makeConstant(result,{Scalar::of(less(b[0],a[0]))});
        if(op=="<=")return makeConstant(result,{Scalar::of(!less(b[0],a[0]))});
        if(op==">=")return makeConstant(result,{Scalar::of(!less(a[0],b[0]))});
        const Type& lt=l.type; const Type& rt=r.type;
        std::vector<Scalar> out;
        if(op=="*" && ((lt.isMatrix() && !rt.isScalar())||(rt.isMatrix() && !lt.isScalar()))) {
            // Linear-algebra product, column-major: M[c*rows+r].
            const int lr=lt.rows, lc=lt.isMatrix()?lt.columns:1, rc=rt.isMatrix()?rt.columns:1;
            const int inner=lt.isVector()?lt.rows:lc;
            const int rows=lt.isVector()?1:lr;
            for(int c=0;c<(rt.isVector()?1:rc);++c)
                for(int row=0;row<rows;++row) {
                    float sum=0;
                    for(int k=0;k<inner;++k) {
                        const float x=lt.isVector()?a[k].f:a[k*lr+row].f;
                        const float y=rt.isVector()?b[k].f:b[c*rt.rows+k].f;
                        sum+=x*y;
                    }
                    out.push_back(Scalar::of(sum));
                }
            return makeConstant(result,std::move(out));
        }
        const std::size_t n=std::max(a.size(),b.size());
        for(std::size_t i=0;i<n;++i) {
            Scalar s;
            if(!componentOp(op,a.size()==1?a[0]:a[i],b.size()==1?b[0]:b[i],s))return nullptr;
            out.push_back(s);
        }
        return makeConstant(result,std::move(out));
    }

    Type binary(ast::Expr& e) {
        ast::Expr& l=*e.operands[0]; ast::Expr& r=*e.operands[1];
        const Type lt=l.type, rt=r.type;
        if(lt.isError()||rt.isError())return {};
        std::string why;
        const Type t=binaryType(e.text,lt,rt,why);
        if(t.isError()) {
            invalid(e.loc,"operator","operands of "+quote(e.text)+": "+why,
                lt.base!=rt.base && lt.isNumeric() && rt.isNumeric()?"GLSL ES has no implicit conversions; convert one operand, e.g. float(i)":"");
            return {};
        }
        e.constant=l.constant && r.constant;
        e.value=foldBinary(e.text,t,l,r);
        return t;
    }

    Type assign(ast::Expr& e) {
        ast::Expr& target=*e.operands[0]; ast::Expr& value=*e.operands[1];
        const Type lt=target.type, rt=value.type;
        if(lt.isError()||rt.isError())return {};
        if(e.text=="=") {
            if(lt!=rt) {
                invalid(e.loc,"type-mismatch","assignment to "+quote(ast::toGlsl(target))+": cannot assign "+quote(toString(rt))+" to "+quote(toString(lt)),
                    "GLSL ES has no implicit conversions; use a constructor such as "+toString(lt.element())+"(...)");
                return {};
            }
            if(lt.containsOpaque()){invalid(e.loc,"opaque","assignment to "+quote(ast::toGlsl(target))+": opaque values cannot be assigned");return {};}
            if(version<300 && lt.containsArray()){invalid(e.loc,"array-assignment","assignment to "+quote(ast::toGlsl(target))+": arrays cannot be assigned in GLSL ES 1.00");return {};}
        } else {
            std::string why;
            const std::string op=e.text.substr(0,e.text.size()-1);
            const Type t=binaryType(op,lt,rt,why);
            if(t.isError()){invalid(e.loc,"operator","operands of "+quote(e.text)+": "+why);return {};}
            if(t!=lt){invalid(e.loc,"operator","operands of "+quote(e.text)+": the result "+quote(toString(t))+" cannot be stored in "+quote(toString(lt)));return {};}
        }
        requireLvalue(target,"assignment");
        return lt;
    }

    Type ternary(ast::Expr& e) {
        ast::Expr& c=*e.operands[0]; ast::Expr& a=*e.operands[1]; ast::Expr& b=*e.operands[2];
        const Type ct=c.type, at=a.type, bt=b.type;
        if(ct.isError()||at.isError()||bt.isError())return {};
        if(!(ct.isScalar() && ct.base==Type::Bool)) {
            invalid(c.loc,"condition","condition of '?:': expected a bool, found "+quote(toString(ct)),ct.isVector()?"use mix() to select per component":"");
            return {};
        }
        if(at!=bt) {
            invalid(e.loc,"type-mismatch","operands of '?:': the branches have different types "+quote(toString(at))+" and "+quote(toString(bt)),
                "convert one branch so both have the same type");
            return {};
        }
        if(version<300 && at.containsArray()){invalid(e.loc,"array-ternary","operands of '?:': arrays are not allowed in GLSL ES 1.00");return {};}
        e.constant=c.constant && a.constant && b.constant;
        if(c.value)e.value=c.value->values[0].b?a.value:b.value;
        return at;
    }

    std::vector<Type> arguments(ast::Expr& e, std::size_t from, bool& ok) {
        std::vector<Type> types;
        ok=true;
        for(std::size_t i=from;i<e.operands.size();++i) {
            types.push_back(e.operands[i]->type);
            if(types.back().isError())ok=false;
        }
        return types;
    }
    static std::string argumentList(const std::vector<Type>& types) {
        std::string s;
        for(std::size_t i=0;i<types.size();++i)s+=(i?", ":"")+toString(types[i]);
        return s;
    }

    Type call(ast::Expr& e) {
        bool ok=true;
        const std::vector<Type> types=arguments(e,0,ok);
        if(e.voidArgument && !types.empty())invalid(e.loc,"call","call of "+quote(e.text)+": 'void' cannot be combined with arguments");
        if(!ok)return {};
        Entry* entry=find(e.text);
        if(entry && entry->type){invalid(e.loc,"not-a-function",quote(e.text)+" is a type");return {};}
        if(entry && entry->variable) {
            invalid(e.loc,"not-a-function",quote(e.text)+" is a variable, not a function",
                "a variable declaration with this name hides the function; rename the variable");
            return {};
        }
        std::vector<const Function*> candidates;
        if(entry)for(const Function* f:entry->functions)candidates.push_back(f);
        if(candidates.empty()||version<300)
            for(const Function* f:builtinOverloads(e.text))if(available(*f))candidates.push_back(f);
        if(candidates.empty()) {
            if(isUnsupportedBuiltinName(e.text,version)) {
                unsupported(e.loc,"builtin","built-in function "+quote(e.text)+": SkSL runtime shaders have no images, atomic counters, memory "
                    "barriers or interpolation functions","compute the value another way, or edit the SkSL by hand");
                return {};
            }
            std::string hint="declare the function before calling it";
            for(const Function* f:builtinOverloads(e.text))
                if(f->version>version){hint=quote(e.text)+" requires "+versionName(f->version);break;}
            if(version==100 && isDerivative(e.text))hint=quote(e.text)+" requires '#extension GL_OES_standard_derivatives : enable' in GLSL ES 1.00";
            if(e.text=="texture2D"||e.text=="textureCube")hint=quote(e.text)+" was replaced by 'texture' in GLSL ES 3.00";
            if(version==100 && e.text.size()>3 && e.text.compare(e.text.size()-3,3,"EXT")==0)
                hint=quote(e.text)+" requires '#extension GL_EXT_shader_texture_lod : enable'";
            if(version==100 && (e.text=="texture2DLod"||e.text=="texture2DProjLod"||e.text=="textureCubeLod"))
                hint=quote(e.text)+" is only available in vertex shaders; use "+quote(e.text+"EXT")+" with '#extension GL_EXT_shader_texture_lod : enable'";
            invalid(e.loc,"undeclared","call of undeclared function "+quote(e.text),hint);
            return {};
        }
        const Function* match=nullptr;
        for(const Function* f:candidates) {
            if(f->params.size()!=types.size())continue;
            bool same=true;
            for(std::size_t i=0;i<types.size();++i)if(f->params[i].type!=types[i])same=false;
            if(same){match=f;break;}
        }
        if(!match && isUnsupportedBuiltinName(e.text,version)) {
            // Valid GLSL: depth-comparison, integer and multisample samplers have no child-shader equivalent.
            unsupported(e.loc,"builtin","call of "+quote(e.text+"("+argumentList(types)+")")+": only sampler2D, sampler3D, samplerCube and "
                "sampler2DArray have an equivalent in SkSL runtime shaders (child shaders)","sample a child shader in the SkSL by hand");
            return {};
        }
        if(!match) {
            std::string available;
            for(std::size_t i=0;i<candidates.size() && i<6;++i)available+=(i?", ":"")+signature(*candidates[i]);
            if(candidates.size()>6)available+=", ...";
            invalid(e.loc,"no-overload","call of "+quote(e.text+"("+argumentList(types)+")")+": no declaration of "+quote(e.text)+" takes these argument types",
                "GLSL ES has no implicit conversions; available: "+available);
            return {};
        }
        if(e.text=="main" && !match->builtin)invalid(e.loc,"main","main cannot be called");
        for(std::size_t i=0;i<types.size();++i)
            if(match->params[i].direction!=Direction::In)
                requireLvalue(*e.operands[i],"argument "+std::to_string(i+1)+" of "+quote(e.text)+" (an "+
                    (match->params[i].direction==Direction::Out?"out":"inout")+" parameter)");
        e.function=match;
        if(!match->builtin) {
            Function* f=const_cast<Function*>(match);
            f->called=true;
            if(current)current->callees.push_back(f);
            return match->returnType;
        }
        bool constant=true;
        for(const auto& a:e.operands)constant=constant && a->constant;
        for(const auto& p:match->params)if(p.direction!=Direction::In)constant=false;
        e.constant=constant && match->returnType.base!=Type::Void;
        if(e.constant)e.value=foldBuiltin(*match,e);
        return match->returnType;
    }

    ConstantPtr foldBuiltin(const Function& f, const ast::Expr& e) const {
        static const std::set<std::string> foldable={"abs","sign","min","max","clamp","floor","ceil","trunc"};
        for(const auto& a:e.operands)if(!a->value)return nullptr;
        if(!foldable.count(f.name))return foldFloatBuiltin(f,e);
        const Type& t=f.returnType;
        auto arg=[&](std::size_t i,int k)->const Scalar&{const auto& v=e.operands[i]->value->values;return v.size()==1?v[0]:v[static_cast<std::size_t>(k)];};
        std::vector<Scalar> out;
        for(int k=0;k<t.components();++k) {
            Scalar x=arg(0,k), r=x;
            if(f.name=="abs") {
                if(x.base==Type::Float)r.f=std::fabs(x.f);
                else{if(x.i==INT32_MIN)return nullptr;r.i=x.i<0?-x.i:x.i;}
            } else if(f.name=="sign") {
                if(x.base==Type::Float)r.f=x.f>0?1.0f:x.f<0?-1.0f:0.0f;
                else r.i=x.i>0?1:x.i<0?-1:0;
            } else if(f.name=="floor")r.f=std::floor(x.f);
            else if(f.name=="ceil")r.f=std::ceil(x.f);
            else if(f.name=="trunc")r.f=std::trunc(x.f);
            else if(f.name=="min")r=less(arg(1,k),x)?arg(1,k):x;
            else if(f.name=="max")r=less(x,arg(1,k))?arg(1,k):x;
            else {
                const Scalar& lo=arg(1,k); const Scalar& hi=arg(2,k);
                r=less(x,lo)?lo:x;
                r=less(hi,r)?hi:r;
            }
            out.push_back(r);
        }
        return makeConstant(t,std::move(out));
    }
    // Float built-ins with constant arguments, computed in double and rounded to float, as GLSL
    // compilers fold them. SkSL needs the value where the GLSL is a constant expression (a const
    // initializer) and the transpiler would otherwise call a generated function. Results that are
    // not finite, which GLSL leaves undefined, are not folded.
    ConstantPtr foldFloatBuiltin(const Function& f, const ast::Expr& e) const {
        const std::string& n=f.name;
        const Type& t=f.returnType;
        if(t.base!=Type::Float || t.isArray())return nullptr;
        std::vector<std::vector<double>> args;
        for(const auto& a:e.operands) {
            std::vector<double> v;
            for(const Scalar& s:a->value->values) {
                if(s.base==Type::Bool && n=="mix")v.push_back(s.b?1.0:0.0);
                else if(s.base==Type::Float)v.push_back(s.f);
                else return nullptr;
            }
            args.push_back(std::move(v));
        }
        auto at=[&](std::size_t i,int k){return args[i].size()==1?args[i][0]:args[i][static_cast<std::size_t>(k)];};
        auto dot=[&](std::size_t i,std::size_t j){double s=0;for(std::size_t k=0;k<args[i].size();++k)s+=args[i][k]*args[j][k];return s;};
        static const double pi=3.14159265358979323846;
        static const std::map<std::string,double(*)(double)> unary={
            {"radians",[](double x){return x*pi/180.0;}}, {"degrees",[](double x){return x*180.0/pi;}},
            {"sin",[](double x){return std::sin(x);}}, {"cos",[](double x){return std::cos(x);}}, {"tan",[](double x){return std::tan(x);}},
            {"asin",[](double x){return std::asin(x);}}, {"acos",[](double x){return std::acos(x);}},
            {"sinh",[](double x){return std::sinh(x);}}, {"cosh",[](double x){return std::cosh(x);}}, {"tanh",[](double x){return std::tanh(x);}},
            {"asinh",[](double x){return std::asinh(x);}}, {"acosh",[](double x){return std::acosh(x);}}, {"atanh",[](double x){return std::atanh(x);}},
            {"exp",[](double x){return std::exp(x);}}, {"log",[](double x){return std::log(x);}},
            {"exp2",[](double x){return std::exp2(x);}}, {"log2",[](double x){return std::log2(x);}},
            {"sqrt",[](double x){return std::sqrt(x);}}, {"inversesqrt",[](double x){return 1.0/std::sqrt(x);}},
            {"fract",[](double x){return x-std::floor(x);}},
            // As the generated helpers: a .5 fraction rounds up, and to even for roundEven.
            {"round",[](double x){const double f=std::floor(x);return x-f>=0.5?f+1.0:f;}},
            {"roundEven",[](double x){const double f=std::floor(x),d=x-f;return d>0.5?f+1.0:d<0.5?f:std::fmod(f,2.0)==0.0?f:f+1.0;}},
        };
        std::vector<double> out;
        const int count=t.components();
        const auto u=unary.find(n);
        if(u!=unary.end() && args.size()==1)for(int k=0;k<count;++k)out.push_back(u->second(at(0,k)));
        else if(n=="atan" && args.size()==1)for(int k=0;k<count;++k)out.push_back(std::atan(at(0,k)));
        else if(n=="atan")for(int k=0;k<count;++k)out.push_back(std::atan2(at(0,k),at(1,k)));
        else if(n=="pow")for(int k=0;k<count;++k)out.push_back(std::pow(at(0,k),at(1,k)));
        else if(n=="mod")for(int k=0;k<count;++k)out.push_back(at(0,k)-at(1,k)*std::floor(at(0,k)/at(1,k)));
        else if(n=="step")for(int k=0;k<count;++k)out.push_back(at(1,k)<at(0,k)?0.0:1.0);
        else if(n=="fma")for(int k=0;k<count;++k)out.push_back(at(0,k)*at(1,k)+at(2,k));
        else if(n=="mix" && e.operands[2]->type.base==Type::Bool)for(int k=0;k<count;++k)out.push_back(at(2,k)!=0.0?at(1,k):at(0,k));
        else if(n=="mix")for(int k=0;k<count;++k)out.push_back(at(0,k)*(1.0-at(2,k))+at(1,k)*at(2,k));
        else if(n=="smoothstep")for(int k=0;k<count;++k) {
            const double x=std::min(1.0,std::max(0.0,(at(2,k)-at(0,k))/(at(1,k)-at(0,k))));
            out.push_back(x*x*(3.0-2.0*x));
        }
        else if(n=="length")out.push_back(std::sqrt(dot(0,0)));
        else if(n=="distance") {
            double s=0;
            for(std::size_t k=0;k<args[0].size();++k)s+=(args[0][k]-args[1][k])*(args[0][k]-args[1][k]);
            out.push_back(std::sqrt(s));
        }
        else if(n=="dot")out.push_back(dot(0,1));
        else if(n=="normalize")for(int k=0;k<count;++k)out.push_back(at(0,k)/std::sqrt(dot(0,0)));
        else if(n=="cross") {
            const auto& a=args[0]; const auto& b=args[1];
            out={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
        }
        else if(n=="faceforward")for(int k=0;k<count;++k)out.push_back(dot(2,1)<0.0?at(0,k):-at(0,k));
        else if(n=="reflect")for(int k=0;k<count;++k)out.push_back(at(0,k)-2.0*dot(1,0)*at(1,k));
        else if(n=="matrixCompMult")for(int k=0;k<count;++k)out.push_back(at(0,k)*at(1,k));
        else if(n=="outerProduct") {
            // Column c of the result is the first vector times component c of the second.
            for(int c=0;c<t.columns;++c)for(int r=0;r<t.rows;++r)out.push_back(at(0,r)*at(1,c));
        }
        else if(n=="transpose") {
            // The argument has t.rows columns of t.columns rows.
            for(int c=0;c<t.columns;++c)for(int r=0;r<t.rows;++r)out.push_back(args[0][static_cast<std::size_t>(r*t.columns+c)]);
        }
        else if(n=="determinant")out.push_back(determinant(args[0],e.operands[0]->type.columns));
        else return nullptr;
        std::vector<Scalar> values;
        for(double v:out) {
            if(!std::isfinite(v) || std::fabs(v)>std::numeric_limits<float>::max())return nullptr;
            values.push_back(Scalar::of(static_cast<float>(v)));
        }
        return makeConstant(t,std::move(values));
    }
    // Determinant of an n x n column-major matrix, by Laplace expansion along the first column.
    static double determinant(const std::vector<double>& m, int n) {
        if(n==1)return m[0];
        double sum=0;
        for(int r=0;r<n;++r) {
            std::vector<double> minor;
            for(int c=1;c<n;++c)for(int k=0;k<n;++k)if(k!=r)minor.push_back(m[static_cast<std::size_t>(c*n+k)]);
            sum+=(r%2?-1.0:1.0)*m[static_cast<std::size_t>(r)]*determinant(minor,n-1);
        }
        return sum;
    }

    Type constructor(ast::Expr& e) {
        Type t=resolve(*e.constructed);
        bool ok=true;
        const std::vector<Type> types=arguments(e,0,ok);
        if(t.isError()||!ok)return {};
        const std::string name=quote(toString(t));
        if(e.voidArgument){invalid(e.loc,"constructor","constructor "+name+": 'void' is not an argument");return {};}
        if(t.isArray()) {
            if(version<300){invalid(e.loc,"constructor","array constructor "+name+": array constructors require GLSL ES 3.00");return {};}
            const Type element=t.element();
            if(!t.arrays[0])t.arrays[0]=static_cast<int>(types.size());
            if(static_cast<int>(types.size())!=t.arrays[0]){
                invalid(e.loc,"constructor","array constructor "+quote(toString(t))+": expected "+std::to_string(t.arrays[0])+" arguments, found "+std::to_string(types.size()));
                return {};
            }
            for(std::size_t i=0;i<types.size();++i)
                if(types[i]!=element){
                    invalid(e.operands[i]->loc,"constructor","argument "+std::to_string(i+1)+" of "+quote(toString(t))+": expected "+quote(toString(element))+", found "+quote(toString(types[i])));
                    return {};
                }
            for(int n:element.arrays)if(!n){invalid(e.loc,"constructor","array constructor "+quote(toString(t))+": inner array sizes must be given");return {};}
        } else if(t.base==Type::Struct) {
            const auto& fields=t.structDef->fields;
            if(types.size()!=fields.size()) {
                invalid(e.loc,"constructor","constructor of struct "+name+": expected "+std::to_string(fields.size())+" arguments, found "+std::to_string(types.size()));
                return {};
            }
            for(std::size_t i=0;i<types.size();++i)
                if(types[i]!=fields[i].type) {
                    invalid(e.operands[i]->loc,"constructor","argument "+std::to_string(i+1)+" of "+name+" (member "+quote(fields[i].name)+"): expected "+
                        quote(toString(fields[i].type))+", found "+quote(toString(types[i])),"struct constructors do not convert or broadcast values");
                    return {};
                }
        } else if(t.isBasic()) {
            if(types.empty()){invalid(e.loc,"constructor","constructor "+name+": it needs at least one argument");return {};}
            for(std::size_t i=0;i<types.size();++i)
                if(!types[i].isBasic()) {
                    invalid(e.operands[i]->loc,"constructor","argument "+std::to_string(i+1)+" of "+name+": "+quote(toString(types[i]))+" cannot be converted");
                    return {};
                }
            const bool single=types.size()==1;
            const bool broadcast=single && types[0].isScalar() && !t.isScalar();
            const bool matrixFromMatrix=single && t.isMatrix() && types[0].isMatrix();
            if(t.isMatrix() && !single)
                for(std::size_t i=0;i<types.size();++i)
                    if(types[i].isMatrix()){invalid(e.operands[i]->loc,"constructor","constructor "+name+": a matrix argument must be the only argument");return {};}
            if(!broadcast && !matrixFromMatrix) {
                int provided=0;
                for(std::size_t i=0;i<types.size();++i) {
                    if(provided>=t.components()) {
                        invalid(e.operands[i]->loc,"constructor","constructor "+name+": too many arguments; argument "+std::to_string(i+1)+" is not used",
                            "remove the extra arguments");
                        return {};
                    }
                    provided+=types[i].components();
                }
                if(provided<t.components()) {
                    invalid(e.loc,"constructor","constructor "+name+": not enough components ("+std::to_string(provided)+" given, "+std::to_string(t.components())+" needed)");
                    return {};
                }
            }
        } else {
            invalid(e.loc,"constructor","constructor "+name+": values of this type cannot be constructed");
            return {};
        }
        bool constant=true;
        for(const auto& a:e.operands)constant=constant && a->constant;
        e.constant=constant;
        if(constant)e.value=foldConstructor(t,e);
        return t;
    }

    ConstantPtr foldConstructor(const Type& t, const ast::Expr& e) const {
        std::vector<Scalar> all;
        for(const auto& a:e.operands) {
            if(!a->value)return nullptr;
            all.insert(all.end(),a->value->values.begin(),a->value->values.end());
        }
        if(!t.isBasic())return makeConstant(t,std::move(all));
        for(auto& s:all){Scalar c;if(!convert(s,t.base,c))return nullptr;s=c;}
        std::vector<Scalar> out;
        const Type& first=e.operands[0]->type;
        if(e.operands.size()==1 && first.isScalar() && t.isMatrix()) {
            Scalar zero; convert(Scalar::of(0.0f),t.base,zero);
            for(int c=0;c<t.columns;++c)for(int r=0;r<t.rows;++r)out.push_back(c==r?all[0]:zero);
        } else if(e.operands.size()==1 && first.isScalar())out.assign(static_cast<std::size_t>(t.components()),all[0]);
        else if(e.operands.size()==1 && first.isMatrix() && t.isMatrix()) {
            for(int c=0;c<t.columns;++c)for(int r=0;r<t.rows;++r)
                out.push_back(c<first.columns && r<first.rows?all[static_cast<std::size_t>(c*first.rows+r)]:Scalar::of(c==r?1.0f:0.0f));
        } else out.assign(all.begin(),all.begin()+t.components());
        return makeConstant(t,std::move(out));
    }

    Type index(ast::Expr& e) {
        ast::Expr& base=*e.operands[0]; ast::Expr& i=*e.operands[1];
        const Type bt=base.type, it=i.type;
        if(bt.isError()||it.isError())return {};
        if(!it.isScalar() || !(it.base==Type::Int||it.base==Type::Uint)) {
            invalid(i.loc,"index","index of "+quote(ast::toGlsl(base))+": expected an int, found "+quote(toString(it)));
            return {};
        }
        Type result; int size=0, stride=1;
        if(bt.isArray()){result=bt.element();size=bt.arrays[0];stride=slotCount(result);}
        else if(bt.isVector()){result=bt.component();size=bt.rows;}
        else if(bt.isMatrix()){result=Type::of(Type::Float,bt.rows);size=bt.columns;stride=bt.rows;}
        else{invalid(e.loc,"index",quote(ast::toGlsl(base))+" of type "+quote(toString(bt))+" cannot be indexed");return {};}
        long long v=0;
        if(integer(i,v)) {
            if(v<0){invalid(i.loc,"index","index "+std::to_string(v)+" of "+quote(ast::toGlsl(base))+" is negative");return {};}
            if(size && v>=size){invalid(i.loc,"index","index "+std::to_string(v)+" is out of range for "+quote(toString(bt))+" (valid: 0 to "+std::to_string(size-1)+")");return {};}
            if(base.value) {
                const auto& values=base.value->values;
                const auto from=static_cast<std::size_t>(v*stride);
                e.value=makeConstant(result,std::vector<Scalar>(values.begin()+static_cast<std::ptrdiff_t>(from),values.begin()+static_cast<std::ptrdiff_t>(from+static_cast<std::size_t>(stride))));
            }
        }
        e.constant=base.constant && i.constant;
        return result;
    }

    Type field(ast::Expr& e) {
        ast::Expr& base=*e.operands[0];
        const Type bt=base.type;
        if(bt.isError())return {};
        if(bt.base==Type::Struct && !bt.isArray()) {
            const auto& fields=bt.structDef->fields;
            int offset=0;
            for(std::size_t k=0;k<fields.size();++k) {
                if(fields[k].name==e.text) {
                    e.field=static_cast<int>(k);
                    e.constant=base.constant;
                    if(base.value) {
                        const auto& values=base.value->values;
                        const int n=slotCount(fields[k].type);
                        e.value=makeConstant(fields[k].type,std::vector<Scalar>(values.begin()+offset,values.begin()+offset+n));
                    }
                    return fields[k].type;
                }
                offset+=slotCount(fields[k].type);
            }
            invalid(e.loc,"field",quote(toString(bt))+" has no member named "+quote(e.text));
            return {};
        }
        if(!bt.isVector()) {
            invalid(e.loc,"field",quote(ast::toGlsl(base))+" of type "+quote(toString(bt))+" has no field "+quote(e.text),
                bt.isScalar()?"GLSL ES does not allow swizzles on scalars; construct a vector first":"");
            return {};
        }
        static const char* const sets[]={"xyzw","rgba","stpq"};
        int set=-1;
        for(char c:e.text) {
            int found=-1, position=-1;
            for(int s=0;s<3;++s){const char* p=std::char_traits<char>::find(sets[s],4,c);if(p){found=s;position=static_cast<int>(p-sets[s]);}}
            if(found<0){invalid(e.loc,"swizzle","swizzle ."+e.text+": "+quote(std::string(1,c))+" is not a vector component","use x, y, z, w or r, g, b, a or s, t, p, q");return {};}
            if(set>=0 && found!=set){invalid(e.loc,"swizzle","swizzle ."+e.text+" mixes component sets","use letters from one set: xyzw, rgba or stpq");return {};}
            set=found;
            if(position>=bt.rows){invalid(e.loc,"swizzle","swizzle ."+e.text+": "+quote(std::string(1,c))+" is out of range for "+quote(toString(bt)));return {};}
            e.swizzle.push_back(position);
        }
        if(e.swizzle.size()>4){invalid(e.loc,"swizzle","swizzle ."+e.text+" selects more than 4 components");e.swizzle.clear();return {};}
        const Type result=e.swizzle.size()==1?bt.component():Type::of(bt.base,static_cast<int>(e.swizzle.size()));
        e.constant=base.constant;
        if(base.value) {
            std::vector<Scalar> values;
            for(int k:e.swizzle)values.push_back(base.value->values[static_cast<std::size_t>(k)]);
            e.value=makeConstant(result,std::move(values));
        }
        return result;
    }

    Type method(ast::Expr& e) {
        ast::Expr& base=*e.operands[0];
        const Type bt=base.type;
        bool ok=true;
        arguments(e,1,ok);
        if(bt.isError())return {};
        if(e.text!="length"){invalid(e.loc,"method","method "+quote(e.text)+" does not exist; only length() is available on arrays");return {};}
        if(e.operands.size()>1||e.voidArgument){invalid(e.loc,"method","length() takes no arguments");return {};}
        if(!bt.isArray() && !bt.isVector() && !bt.isMatrix()){invalid(e.loc,"method","length() applies to arrays, vectors and matrices, not to "+quote(toString(bt)));return {};}
        if(version<300){invalid(e.loc,"method","length() requires GLSL ES 3.00");return {};}
        e.constant=true;
        const int length=bt.isArray()?bt.arrays[0]:bt.isMatrix()?bt.columns:bt.rows;
        e.value=makeConstant(Type::of(Type::Int),{Scalar::of(static_cast<std::int32_t>(length))});
        return Type::of(Type::Int);
    }

    // ------------------------------------------------------------------ whole-program rules
    void finishProgram() {
        if(!program->main)invalid(Loc{1,1},"missing-main","shader: it does not define void main()","add void main() { ... }");
        for(const auto& f:program->functions)
            if(f.called && !f.definition)
                invalid(f.loc,"undefined-function","function "+quote(signature(f))+" is called but never defined","add a definition with a body");
        // Static recursion is not allowed in GLSL ES.
        std::map<const Function*,int> state;
        std::vector<const Function*> path;
        std::set<const Function*> reported;
        std::function<void(const Function*)> visit=[&](const Function* f) {
            state[f]=1; path.push_back(f);
            for(const Function* g:f->callees) {
                if(state[g]==1) {
                    if(reported.insert(g).second) {
                        std::string cycle;
                        for(auto it=std::find(path.begin(),path.end(),g);it!=path.end();++it)cycle+=(*it)->name+" -> ";
                        invalid(g->loc,"recursion","recursion: "+cycle+g->name,"GLSL ES does not allow recursion; rewrite it as a loop");
                    }
                } else if(!state[g])visit(g);
            }
            path.pop_back(); state[f]=2;
        };
        for(const auto& f:program->functions)if(!state[&f])visit(&f);
        if(version>=300) {
            const auto& outputs=program->outputs;
            std::map<int,const Symbol*> locations;
            for(const Symbol* o:outputs) {
                if(outputs.size()>1 && o->location<0)
                    invalid(o->loc,"output-location","fragment output "+quote(o->name)+": with several outputs, every output needs layout(location=N)");
                if(o->location>=0) {
                    const auto [it,inserted]=locations.insert({o->location,o});
                    if(!inserted)invalid(o->loc,"output-location","fragment outputs "+quote(it->second->name)+" and "+quote(o->name)+" use the same location "+std::to_string(o->location));
                }
            }
        } else if(program->writesFragColor && program->writesFragData)
            invalid(Loc{1,1},"fragment-output","shader: it writes both gl_FragColor and gl_FragData");
    }

    // ------------------------------------------------------------------ uniform annotations
    // '// @minmax{a, b} @init{v}' on the line of a uniform declaration, or alone on the line before
    // it, states the values the application gives the uniform (see loopIssues). Problems with an
    // annotation are warnings: the annotation is then ignored.
    struct Annotation { bool hasRange=false, hasInit=false; double minimum=0, maximum=0, initial=0; std::string text, error; };
    static Annotation parseAnnotation(const std::string& comment) {
        Annotation a;
        const std::size_t first=std::min(comment.find("@minmax"),comment.find("@init"));
        std::string text=comment.substr(first);
        for(char& c:text)if(c=='\n'||c=='\r'||c=='\t')c=' ';
        while(!text.empty() && text.back()==' ')text.pop_back();
        a.text=text;
        std::size_t p=0;
        auto skip=[&]{while(p<text.size() && text[p]==' ')++p;};
        auto expect=[&](char c){skip();if(p<text.size() && text[p]==c){++p;return true;}return false;};
        auto number=[&](double& out) {
            skip();
            if(p<text.size() && text[p]=='+')++p;
            const char* b=text.data()+p; const char* e=text.data()+text.size();
            const auto r=std::from_chars(b,e,out);
            if(r.ec!=std::errc() || !std::isfinite(out))return false;
            p+=static_cast<std::size_t>(r.ptr-b);
            if(p<text.size() && (text[p]=='f'||text[p]=='F'))++p;
            return true;
        };
        while((p=text.find('@',p))!=std::string::npos) {
            ++p;
            if(text.compare(p,6,"minmax")==0) {
                p+=6;
                if(a.hasRange){a.error="it gives @minmax twice";return a;}
                if(!expect('{')||!number(a.minimum)||!expect(',')||!number(a.maximum)||!expect('}')){a.error="@minmax must be written @minmax{MIN, MAX} with two numbers";return a;}
                a.hasRange=true;
            } else if(text.compare(p,4,"init")==0) {
                p+=4;
                if(a.hasInit){a.error="it gives @init twice";return a;}
                if(!expect('{')||!number(a.initial)||!expect('}')){a.error="@init must be written @init{VALUE} with a number";return a;}
                a.hasInit=true;
            }
        }
        if(a.hasRange && a.minimum>a.maximum)a.error="the @minmax minimum is greater than the maximum";
        else if(a.hasRange && a.hasInit && (a.initial<a.minimum||a.initial>a.maximum))a.error="the @init value is outside the @minmax range";
        return a;
    }
    static bool fitsInt(double v){return v==std::floor(v) && v>=INT_MIN && v<=INT_MAX;}

    // '// @filter{nearest|linear} @wrap{repeat|clamp|mirror}' on the line of a sampler uniform, or
    // alone on the line before it: the GL sampler state of a texture the SkSL filters itself.
    void annotateSamplers() {
        for(const Comment& c:unit.comments) {
            if(c.text.find("@filter")==std::string::npos && c.text.find("@wrap")==std::string::npos)continue;
            std::string error, filter, wrap;
            for(const char* key:{"@filter","@wrap"}) {
                const std::size_t at=c.text.find(key);
                if(at==std::string::npos)continue;
                const std::size_t open=c.text.find_first_not_of(' ',at+std::strlen(key));
                const std::size_t close=open==std::string::npos?open:c.text.find('}',open);
                std::string value=open!=std::string::npos && c.text[open]=='{' && close!=std::string::npos?c.text.substr(open+1,close-open-1):std::string("?");
                value.erase(0,value.find_first_not_of(' '));
                value.erase(value.find_last_not_of(' ')+1);
                (key[1]=='f'?filter:wrap)=value;
            }
            if(!filter.empty() && filter!="nearest" && filter!="linear")error="@filter must be written @filter{nearest} or @filter{linear}";
            if(!wrap.empty() && wrap!="repeat" && wrap!="clamp" && wrap!="mirror")error="@wrap must be written @wrap{repeat}, @wrap{clamp} or @wrap{mirror}";
            Symbol* target=nullptr;
            for(Symbol& s:program->symbols)
                if(s.storage==Storage::Uniform && s.type.base==Type::Opaque && ((c.loc.line<=s.loc.line && s.loc.line<=c.endLine) || (c.ownLine && c.endLine+1==s.loc.line)))
                    target=&s;
            if(!target && error.empty())error="it does not belong to a sampler uniform declaration";
            if(!error.empty()) {
                sink.warning(c.loc,"annotation.invalid","sampler annotation is ignored: "+error,
                    "write '// @filter{linear} @wrap{repeat}' after the sampler uniform, or alone on the line before it");
                continue;
            }
            if(!filter.empty())target->nearest=filter=="nearest";
            if(!wrap.empty())target->wrap=wrap;
        }
    }

    void annotateUniforms() {
        for(const Comment& c:unit.comments) {
            if(c.text.find("@minmax")==std::string::npos && c.text.find("@init")==std::string::npos)continue;
            std::vector<Symbol*> targets;
            for(Symbol& s:program->symbols)
                if(s.storage==Storage::Uniform && ((c.loc.line<=s.loc.line && s.loc.line<=c.endLine) || (c.ownLine && c.endLine+1==s.loc.line)))
                    targets.push_back(&s);
            const Annotation a=parseAnnotation(c.text);
            if(targets.empty()) {
                sink.warning(c.loc,"annotation.unused","annotation '"+a.text+"' is ignored: it does not belong to a uniform declaration",
                    "write it after the uniform on the same line, or alone on the line before it");
                continue;
            }
            for(Symbol* s:targets) {
                std::string error=a.error;
                const bool isInt=s->type.base==Type::Int;
                if(error.empty() && !(s->type.isScalar() && (isInt||s->type.base==Type::Float)))
                    error="only int and float uniforms can be annotated, and "+quote(s->name)+" is "+quote(toString(s->type));
                else if(error.empty() && isInt && ((a.hasRange && (!fitsInt(a.minimum)||!fitsInt(a.maximum))) || (a.hasInit && !fitsInt(a.initial))))
                    error=quote(s->name)+" is an int, so the annotation needs whole numbers";
                else if(error.empty() && !s->annotation.empty())
                    error=quote(s->name)+" already has an annotation";
                if(!error.empty()) {
                    sink.warning(c.loc,"annotation.invalid","annotation '"+a.text+"' of uniform "+quote(s->name)+" is ignored: "+error,
                        "write '// @minmax{MIN, MAX} @init{VALUE}' with MIN <= VALUE <= MAX");
                    continue;
                }
                s->annotation=a.text;
                s->hasRange=a.hasRange; s->minimum=a.minimum; s->maximum=a.maximum;
                s->hasInit=a.hasInit; s->initial=a.initial;
            }
        }
    }

    // Shaders of the target application receive UV coordinates (0 to 1 across the drawn area) in
    // their first input, such as 'layout(location = 0) in highp vec4 uv_coords;'. SkSL runtime
    // shaders have no inputs, so that one is computed from the coordinate parameter of main; other
    // inputs stay unsupported.
    void chooseTextureCoordinate() {
        Symbol* chosen=nullptr;
        int inputs=0;
        for(Symbol& s:program->symbols) {
            if(s.storage!=Storage::Input)continue;
            ++inputs;
            if(s.location==0 || (!chosen && s.location<0))chosen=&s;
        }
        if(chosen && (chosen->location==0 || inputs==1) && chosen->type.base==Type::Float && chosen->type.isVector())
            chosen->textureCoordinate=true;
    }

    // ------------------------------------------------------------------ SkSL capability checks
    std::vector<const Symbol*> loopIndices;
    std::vector<char> breakTargets;      // 'L' loop, 'S' switch
    bool reportedUint=false, reportedMatrix=false;
    // Samplers the emitter replaces by child shaders: the float sampler kinds without depth
    // comparison or multisampling, alone or in an array.
    static bool childSampler(const Type& t) {
        static const std::set<std::string> kinds={"sampler2D","sampler3D","samplerCube","sampler2DArray"};
        return t.base==Type::Opaque && kinds.count(t.opaque) && t.arrays.size()<=1;
    }
    static std::string unsupportedSampler(const std::string& what, const Type& t) {
        return what+": SkSL runtime shaders have no images or atomic counters, and "+quote(toString(t))+" has no child-shader equivalent: "
            "only sampler2D, sampler3D, samplerCube and sampler2DArray (without depth comparison, integer texels or multisampling) do";
    }
    static bool nonSquare(const Type& t) {
        if(t.base==Type::Float && t.columns>1 && t.columns!=t.rows)return true;
        if(t.base==Type::Struct && t.structDef)for(const auto& f:t.structDef->fields)if(nonSquare(f.type))return true;
        return false;
    }

    void targetDeclaration(ast::Decl& d) {
        switch(d.kind) {
        case DeclKind::Variable:
            for(auto& v:d.declarators) {
                if(!v.symbol)continue;
                targetSymbol(*v.symbol);
                if(v.init)targetRoot(*v.init);
            }
            break;
        case DeclKind::Function:
            if(d.function->body) {
                for(const auto& p:d.function->params) {
                    if(!p.symbol)continue;
                    if(p.symbol->type.containsOpaque()) {
                        // The emitter writes a copy of the function for each sampler it receives.
                        if(!childSampler(p.symbol->type))
                            unsupported(p.loc,"sampler",unsupportedSampler("parameter "+quote(p.name),p.symbol->type),
                                "use a sampler2D, sampler3D, samplerCube or sampler2DArray");
                    } else targetType(p.symbol->type,p.loc,"parameter "+quote(p.name));
                }
                const Type returned=d.function->symbol?d.function->symbol->returnType:Type{};
                targetType(returned,d.function->loc,"return type of "+quote(d.function->name));
                targetList(d.function->body->body);
            }
            break;
        case DeclKind::Block:
            if(d.block->qualifiers.has("buffer"))
                unsupported(d.loc,"storage-buffer","buffer block "+quote(d.block->name)+": storage buffers are not available in SkSL runtime shaders");
            else if(d.block->qualifiers.has("in")||d.block->qualifiers.has("out"))
                unsupported(d.loc,"io-block","interface block "+quote(d.block->name)+": shader input and output blocks are not available in SkSL runtime shaders");
            else if(!d.block->instanceArrays.empty())
                unsupported(d.block->instanceLoc,"block-array","uniform block array "+quote(d.block->instance)+": SkSL runtime shaders have no uniform blocks, and arrays of them cannot be flattened",
                    "declare the members as separate uniform arrays");
            break;
        default: break;
        }
    }

    bool targetType(const Type& t, Loc at, const std::string& what) {
        if(t.contains(Type::Uint)) {
            unsupported(at,"uint",what+": unsigned integers ('uint') are not available in SkSL runtime shaders","use int where the values fit in 31 bits");
            return false;
        }
        if(nonSquare(t)) {
            unsupported(at,"matrix-shape",what+": SkSL runtime shaders only have square matrices, not "+quote(toString(t)),
                "use a square matrix with unused rows or columns set to zero, or separate vectors");
            return false;
        }
        if(nestedArrays(t)) {
            unsupported(at,"array-of-arrays",what+": SkSL has no arrays of arrays","flatten the array, e.g. a[i][j] becomes a[i * COLUMNS + j]");
            return false;
        }
        return true;
    }
    static bool nestedArrays(const Type& t) {
        if(t.arrays.size()>1)return true;
        if(t.base==Type::Struct && t.structDef)for(const auto& f:t.structDef->fields)if(nestedArrays(f.type))return true;
        return false;
    }

    void targetSymbol(const Symbol& s) {
        const std::string what="declaration of "+quote(s.name);
        if(!targetType(s.type,s.loc,what))return;
        switch(s.storage) {
        case Storage::Input:
            if(!s.textureCoordinate)
                unsupported(s.loc,"input",what+": shader inputs (varyings) are not available in SkSL runtime shaders, and only the input at "
                    "location 0 is treated as the texture coordinate",
                    "pass the value as a uniform, or compute it from the texture coordinate");
            break;
        case Storage::Uniform:
            if(childSampler(s.type))break;   // becomes child shaders
            if(s.type.containsOpaque())
                unsupported(s.loc,"sampler",unsupportedSampler(what,s.type),
                    "use a sampler2D, sampler3D, samplerCube or sampler2DArray, or declare 'uniform shader "+s.name+";' and sample it with "+s.name+".eval(coord) in the SkSL by hand");
            else if(s.type.contains(Type::Bool))
                unsupported(s.loc,"bool-uniform",what+": SkSL runtime shaders cannot have bool uniforms","declare it as an int and test it with != 0");
            else if(s.type.base==Type::Struct && s.type.structDef && !s.type.structDef->block)
                unsupported(s.loc,"struct-uniform",what+": SkSL runtime shaders cannot have struct uniforms, and uniforms keep their names so the application can set them",
                    "declare each member as a separate uniform");
            break;
        case Storage::Output:
            if(s.location>0)
                unsupported(s.loc,"output-location",what+": the output is bound to location "+std::to_string(s.location)+
                    ", but SkSL runtime shaders produce only the color that location 0 receives","use layout(location = 0) or remove the layout qualifier");
            else if(program->outputs.size()>1)
                unsupported(s.loc,"multiple-outputs",what+": SkSL runtime shaders produce a single color, but the shader declares "+std::to_string(program->outputs.size())+" outputs");
            else if(s.type!=Type::of(Type::Float,4))
                unsupported(s.loc,"output-type",what+": SkSL runtime shaders return a vec4 color, but the output is "+quote(toString(s.type)),
                    "declare 'out vec4' and write every component");
            break;
        case Storage::Buffer:
            unsupported(s.loc,"storage-buffer",what+": storage buffers are not available in SkSL runtime shaders");
            break;
        default: break;
        }
    }

    void targetRoot(ast::Expr& e) {
        reportedUint=false; reportedMatrix=false;
        targetExpr(e);
    }

    // Recursion stays in these lean functions; the per-node checks, which build message strings,
    // live in out-of-line functions so deep nesting does not multiply their stack frames.
    void targetStatement(ast::Stmt& s) {
        statementIssues(s);
        switch(s.kind) {
        case StmtKind::Block: targetList(s.body); break;
        case StmtKind::Declaration: targetDeclaration(*s.decl); break;
        case StmtKind::Expression: targetRoot(*s.expr); break;
        case StmtKind::If:
            targetRoot(*s.expr);
            targetStatement(*s.then);
            if(s.otherwise)targetStatement(*s.otherwise);
            break;
        case StmtKind::Switch:
            targetRoot(*s.expr);
            breakTargets.push_back('S');
            for(auto& x:s.body)if(x->kind!=StmtKind::Case && x->kind!=StmtKind::Default)targetStatement(*x);
            breakTargets.pop_back();
            break;
        case StmtKind::For: {
            std::string problem, suggestion;
            const Symbol* index=loopIssues(s,problem,suggestion);
            if(!problem.empty()) {
                if(rewriteLoop(s,problem,suggestion)){targetStatement(s);break;}   // a block holding the initializer and a for loop
                index=nullptr;
            }
            if(s.init && s.init->kind==StmtKind::Declaration)targetDeclaration(*s.init->decl);
            else if(s.init && s.init->expr)targetRoot(*s.init->expr);
            if(s.expr)targetRoot(*s.expr);
            if(s.step)targetRoot(*s.step);
            if(index)loopIndices.push_back(index);
            breakTargets.push_back('L');
            targetStatement(*s.loop);
            breakTargets.pop_back();
            if(index)loopIndices.pop_back();
            break;
        }
        case StmtKind::While: case StmtKind::DoWhile:
            if(rewriteLoop(s,{},{})){targetStatement(s);break;}   // now a for loop
            breakTargets.push_back('L');
            targetStatement(*s.loop);
            breakTargets.pop_back();
            break;
        case StmtKind::Return: if(s.expr)targetRoot(*s.expr); break;
        default: break;
        }
    }

    GLSL_SKSL_NOINLINE void statementIssues(const ast::Stmt& s) {
        switch(s.kind) {
        case StmtKind::Switch: {
            bool sawDefault=false;
            for(const auto& x:s.body) {
                if(x->kind==StmtKind::Default)sawDefault=true;
                else if(x->kind==StmtKind::Case && sawDefault) {
                    unsupported(x->loc,"switch-default","switch: SkSL requires 'default' to be the last label",
                        "move the default label and its statements to the end of the switch, keeping any fall-through behaviour");
                    sawDefault=false;
                }
            }
            break;
        }
        case StmtKind::Continue:
            if(!breakTargets.empty() && breakTargets.back()=='S')
                unsupported(s.loc,"continue-in-switch","continue: SkSL does not allow 'continue' inside a switch statement",
                    "set a flag, break out of the switch, and continue after it");
            break;
        default: break;
        }
    }

    // Loops in SkSL runtime shaders follow GLSL ES 1.00 Appendix A: one int or float index with a
    // constant start, compared against a constant, stepped by a constant, not modified in the body.
    // A start or bound that is not constant is accepted when its range is known (see range): the
    // loop then runs over a constant range that covers it, and statements at the start of the body
    // skip the iterations before the real start and leave at the real bound, so exactly the GLSL
    // iterations run:
    //   for (int i = a; i < n; i++) BODY
    //   for (int i = A; i < N; i++) { if (i < a) continue; if (!(i < n)) break; BODY }
    // Relying on the @minmax annotation of a uniform is reported as a warning.
    // Returns the loop index, if there is one.
    GLSL_SKSL_NOINLINE const Symbol* loopIssues(ast::Stmt& s, std::string& problem, std::string& suggestion) {
        const Symbol* index=nullptr;
        ast::Declarator* declarator=nullptr;
        if(!s.init || s.init->kind!=StmtKind::Declaration || s.init->decl->kind!=DeclKind::Variable || s.init->decl->declarators.size()!=1)
            problem="the initializer must declare exactly one loop index, as in 'for (int i = 0; ...'";
        else {
            declarator=&s.init->decl->declarators.front();
            index=declarator->symbol;
            if(!index || !(index->type.isScalar() && (index->type.base==Type::Int||index->type.base==Type::Float)))
                problem="the loop index must be an int or float";
            else if(!declarator->init)problem="the loop index "+quote(declarator->name)+" must be initialized";
        }
        const std::string name=index?index->name:"i";
        if(problem.empty() && !s.decl)splitCondition(s,index);
        ast::Expr* c=s.expr.get();
        if(problem.empty()) {
            static const std::set<std::string> relational={"<",">","<=",">=","==","!="};
            if(s.decl)problem="the condition cannot declare a variable";
            else if(!c)problem="the loop needs a condition";
            else if(c->kind!=ExprKind::Binary || !relational.count(c->text) || c->operands[0]->kind!=ExprKind::Name || c->operands[0]->symbol!=index)
                problem="the condition must compare the loop index with a constant, as in '"+name+" < 10'";
        }
        double step=0;
        if(problem.empty()) {
            const ast::Expr* x=s.step.get();
            auto isIndex=[&](const ast::Expr& e){return e.kind==ExprKind::Name && e.symbol==index;};
            if(x && (x->kind==ExprKind::Unary||x->kind==ExprKind::Postfix) && (x->text=="++"||x->text=="--") && isIndex(*x->operands[0]))
                step=x->text=="++"?1:-1;
            else if(x && x->kind==ExprKind::Assign && (x->text=="+="||x->text=="-=") && isIndex(*x->operands[0]) && x->operands[1]->constant) {
                const auto v=scalarValue(*x->operands[1]);
                step=v?(x->text=="+="?*v:-*v):std::numeric_limits<double>::quiet_NaN();
            } else problem="the step must be "+name+"++, "+name+"--, or "+name+" += / -= a constant expression";
        }
        if(problem.empty() && writes(*s.loop,index))problem="the loop index "+quote(name)+" is modified inside the loop body";
        if(problem.empty()) {
            if(!declarator->init->constant||!c->operands[1]->constant)problem=boundLoop(s,*declarator,*c,step,suggestion);
            else if(const auto first=scalarValue(*declarator->init))
                if(const auto last=scalarValue(*c->operands[1])) {
                    indexRanges[index]=Range{std::min(*first,*last),std::max(*first,*last),{}};
                    // A constant range can still be longer than SkSL allows (see rewriteLoop).
                    const std::string& op=c->text;
                    const bool towards=(op=="<"||op=="<=")?step>0:(op==">"||op==">=")?step<0:false;
                    const double span=(*last-*first)/step;
                    const double iterations=std::max(0.0,op=="<="||op==">="?std::floor(span)+1:std::ceil(span));
                    if(towards && iterations>kMaxLoopIterations)
                        problem="the loop runs "+number(iterations)+" iterations, more than the "+number(kMaxLoopIterations)+" SkSL allows in a loop";
                }
        }
        return index;
    }

    // A condition 'i < 16 && t < 2.0' bounds the loop through its comparison of the index: the
    // loop keeps that comparison, and the other parts leave the loop from the start of the body,
    // in their order, after the comparison as in GLSL:
    //   for (int i = 0; i < 16 && c; i++) B     for (int i = 0; i < 16; i++) { if (!(c)) break; B }
    // A comparison that is not the first part moves ahead of the parts before it, which must then
    // have no side effects.
    void splitCondition(ast::Stmt& s, const Symbol* index) {
        if(!s.expr || s.expr->kind!=ExprKind::Binary || s.expr->text!="&&")return;
        std::vector<ast::ExprPtr> parts;
        conjuncts(std::move(s.expr),parts);
        static const std::set<std::string> relational={"<",">","<=",">="};
        std::size_t k=parts.size();
        for(std::size_t i=0;i<parts.size() && k==parts.size();++i) {
            const ast::Expr& x=*parts[i];
            if(x.kind==ExprKind::Binary && relational.count(x.text) && x.operands[0]->kind==ExprKind::Name && x.operands[0]->symbol==index &&
                !writes(*x.operands[1],index))k=i;
            else if(effects(x))break;   // a later comparison would move ahead of this part
        }
        if(k==parts.size()) {   // nothing to split: put the condition back together
            s.expr=std::move(parts[0]);
            for(std::size_t i=1;i<parts.size();++i)s.expr=binary("&&",std::move(s.expr),std::move(parts[i]));
            return;
        }
        ast::ExprPtr rest;
        for(std::size_t i=0;i<parts.size();++i) {
            if(i==k)continue;
            rest=rest?binary("&&",std::move(rest),std::move(parts[i])):std::move(parts[i]);
        }
        s.expr=std::move(parts[k]);
        if(s.loop->kind!=StmtKind::Block) {
            auto body=std::make_unique<ast::Stmt>(StmtKind::Block,s.loop->loc);
            body->scoped=false;
            body->body.push_back(std::move(s.loop));
            s.loop=std::move(body);
        }
        sink.warning(s.loc,"software.loop","for loop: SkSL runtime shaders only allow a loop condition that compares the loop index, so the loop keeps "+
            quote(ast::toGlsl(*s.expr))+" and leaves when "+quote(ast::toGlsl(*rest))+" fails, tested at the start of the body; exactly the GLSL iterations run",
            "no change needed");
        sink.hint("for (");
        s.loop->body.insert(s.loop->body.begin(),jumpIf(notOf(std::move(rest)),StmtKind::Break,s.loc));
    }
    static void conjuncts(ast::ExprPtr e, std::vector<ast::ExprPtr>& out) {
        if(e->kind==ExprKind::Binary && e->text=="&&") {
            conjuncts(std::move(e->operands[0]),out);
            conjuncts(std::move(e->operands[1]),out);
        } else out.push_back(std::move(e));
    }
    // Whether evaluating e can change anything: assignments, ++ and --, and calls of user functions
    // or of built-ins with out parameters.
    static bool effects(const ast::Expr& e) {
        if(e.kind==ExprKind::Assign||((e.kind==ExprKind::Unary||e.kind==ExprKind::Postfix) && (e.text=="++"||e.text=="--")))return true;
        if(e.kind==ExprKind::Call && e.function) {
            if(!e.function->builtin)return true;
            for(const auto& p:e.function->params)if(p.direction!=Direction::In)return true;
        }
        for(const auto& o:e.operands)if(effects(*o))return true;
        return false;
    }

    // Statement lists, visited with the position of each statement (see counterStart).
    std::vector<std::pair<std::vector<ast::StmtPtr>*,std::size_t>> lists;
    void targetList(std::vector<ast::StmtPtr>& list) {
        for(std::size_t i=0;i<list.size();++i) {
            lists.emplace_back(&list,i);
            targetStatement(*list[i]);
            lists.pop_back();
        }
    }

    // ------------------------------------------------------------------ loops of any form
    // SkSL runtime shaders have no while or do-while loops, and only for loops of a constant range
    // (GLSL ES 1.00 Appendix A). Other loops become a for loop over a constant number of iterations
    // that keeps the original condition, so it leaves exactly when the GLSL loop does:
    //   while (c) B           for (int n = 0; n < CAP; n++) { if (!(c)) break; B }
    //   do B while (c);       for (int n = 0; n < CAP; n++) { if (n > 0 && !(c)) break; B }
    //   for (I; c; S) B       { I; for (int n = 0; n < CAP; n++) { if (n > 0) { S; } if (!(c)) break; B } }
    // continue then still reaches the condition (and the step). CAP is one more than the most
    // iterations the loop can run, so the condition is tested once more: proved from a counter
    // (see provedIterations), or stated by an annotation '// @maxiter{N}' on the line of the loop
    // or alone on the line before it, which is reported as an assumption; the loop then also
    // leaves after the condition when n == N. Without either the loop is refused.
    struct LoopBound { double iterations=0; std::vector<const Symbol*> assumed; std::string why; };
    GLSL_SKSL_NOINLINE bool rewriteLoop(ast::Stmt& s, const std::string& forProblem, std::string suggestion) {
        const char* kind=s.kind==StmtKind::While?"while loop":s.kind==StmtKind::DoWhile?"do-while loop":"for loop";
        const std::string lead=s.kind==StmtKind::For?std::string("for loop: SkSL runtime shaders only allow loops with a constant range; ")+forProblem:
            std::string(kind)+": SkSL runtime shaders have no "+(s.kind==StmtKind::While?"while":"do-while")+" loops";
        LoopBound bound;
        const std::optional<double> proved=provedIterations(s,bound);
        const std::optional<double> stated=iterationAnnotation(s);
        if(!proved && !stated) {
            if(suggestion.empty()||bound.why.empty())
                suggestion="state the most iterations the loop runs with an annotation comment on its line, as in '"+
                    std::string(s.kind==StmtKind::DoWhile?"do { // @maxiter{64}":s.kind==StmtKind::While?"while (...) { // @maxiter{64}":"for (...) { // @maxiter{64}")+
                    "', or rewrite it as 'for (int i = START; i < END; ++i)' with constant START and END";
            unsupported(s.loc,"loop",lead+", and the number of its iterations is not known: "+(bound.why.empty()?"it has no @maxiter annotation":bound.why+", and it has no @maxiter annotation"),suggestion);
            return false;
        }
        const double iterations=proved?*proved:*stated;
        const double cap=iterations+1;   // the last iteration only finds the condition false
        // More iterations than one SkSL loop allows run as two nested loops (see nestLoop).
        const bool nested=cap>kMaxLoopIterations;
        const double innerCount=std::ceil(std::sqrt(cap)), outerCount=std::ceil(cap/innerCount);
        if(nested && std::max(innerCount,outerCount)>kMaxLoopIterations) {
            unsupported(s.loc,"loop",lead+", and it can run "+number(iterations)+" iterations, but SkSL allows at most "+number(kMaxLoopIterations)+
                " in a loop, and two nested loops cover at most "+number(kMaxLoopIterations*kMaxLoopIterations),
                proved?"narrow the range of values, for example with a smaller @minmax range":"lower the @maxiter annotation");
            return false;
        }
        // The iteration counter, named by the emitter.
        Symbol& n=generatedLocal("iteration",Type::of(Type::Int),s.loc);
        auto counter=[&]{return nameOf(&n,s.loc);};
        auto loop=countingLoop(n,cap,s.loc);
        auto body=std::make_unique<ast::Stmt>(StmtKind::Block,s.loc);
        body->scoped=false;
        auto started=[&]{return binary(">",counter(),literal(n.type,0,s.loc));};
        if(s.kind==StmtKind::For && s.step) {
            auto step=std::make_unique<ast::Stmt>(StmtKind::If,s.loc);
            step->expr=started();
            step->then=std::make_unique<ast::Stmt>(StmtKind::Expression,s.step->loc);
            step->then->expr=std::move(s.step);
            body->body.push_back(std::move(step));
        }
        if(s.decl) {   // while (bool b = ...): declared at the start of each iteration
            ast::Declarator& d=s.decl->declarators.front();
            const Symbol* tested=d.symbol;
            auto declaration=std::make_unique<ast::Stmt>(StmtKind::Declaration,s.decl->loc);
            declaration->decl=std::move(s.decl);
            body->body.push_back(std::move(declaration));
            if(tested)body->body.push_back(jumpIf(notOf(nameOf(tested,s.loc)),StmtKind::Break,s.loc));
        } else if(s.expr) {
            auto leave=notOf(std::move(s.expr));
            if(s.kind==StmtKind::DoWhile)leave=binary("&&",started(),std::move(leave));
            body->body.push_back(jumpIf(std::move(leave),StmtKind::Break,s.loc));
        }
        // An annotated count is an assumption: past it the loop stops, after testing the condition.
        if(!proved)body->body.push_back(jumpIf(binary("==",counter(),literal(n.type,iterations,s.loc)),StmtKind::Break,s.loc));
        body->body.push_back(std::move(s.loop));
        ast::StmtPtr result;
        if(nested)result=nestLoop(std::move(loop),std::move(body),innerCount,outerCount,s.loc);
        else{loop->loop=std::move(body);result=std::move(loop);}
        // Report before the statement changes kind.
        const std::string over="over at most "+number(iterations)+" iterations, leaving when the condition fails";
        const std::string becomes=nested?"so it becomes two nested for loops (SkSL allows at most "+number(kMaxLoopIterations)+" iterations in a loop) "+over:
            "so it becomes a for loop "+over;
        if(proved && bound.assumed.empty())
            sink.warning(s.loc,"software.loop",lead+", "+becomes+"; exactly the GLSL iterations run","no change needed");
        else if(proved) {
            std::string names;
            for(std::size_t k=0;k<bound.assumed.size();++k) {
                if(k)names+=k+1==bound.assumed.size()?" and ":", ";
                names+=quote(bound.assumed[k]->name)+" in ["+number(bound.assumed[k]->minimum)+", "+number(bound.assumed[k]->maximum)+"]";
            }
            sink.warning(s.loc,"software.loop",lead+", "+becomes+"; the count relies on "+names+", as the @minmax annotation states, and with values outside "
                "the annotated range the loop can stop before the GLSL loop does","keep the uniform within its annotated range, or widen the annotation");
        } else
            sink.warning(s.loc,"software.loop",lead+", "+becomes+", as its @maxiter annotation states; if the GLSL loop ran more iterations, the SkSL loop "
                "stops early","keep the @maxiter annotation at or above the most iterations the loop runs");
        sink.hint(nested?"bool ":"for (");
        if((s.kind==StmtKind::For && s.init) || result->kind==StmtKind::Block) {
            // The initializer runs once, before the loop, in a scope of its own.
            s.kind=StmtKind::Block;
            s.scoped=true;
            if(s.init)s.body.push_back(std::move(s.init));
            s.body.push_back(std::move(result));
            s.expr.reset(); s.step.reset(); s.loop.reset();
        } else {
            s.kind=StmtKind::For;
            s.init=std::move(result->init); s.expr=std::move(result->expr); s.step=std::move(result->step); s.loop=std::move(result->loop);
        }
        return true;
    }
    // A loop over more iterations than SkSL allows in one loop runs as an outer loop over an inner
    // one; together they count the iterations, and the breaks of the body leave both through a flag:
    //   for (int n = 0; n < CAP; n++) B
    //   { bool left = false;
    //     for (int o = 0; o < A; o++) { for (int i = 0; i < C; i++) { int n = o * C + i; if (!(n < CAP)) break; B } if (left) break; } }
    // where every break of the loop (the one before B included) first sets left. continue reaches
    // the next value of n as before.
    ast::StmtPtr nestLoop(ast::StmtPtr loop, ast::StmtPtr body, double innerCount, double outerCount, Loc at) {
        Symbol& left=generatedLocal("left",Type::of(Type::Bool),at);
        Symbol& o=generatedLocal("outer",Type::of(Type::Int),at);
        Symbol& i=generatedLocal("inner",Type::of(Type::Int),at);
        // The counter is now computed from the two loop indices.
        ast::Declarator& counter=loop->init->decl->declarators.front();
        counter.init=arithmetic("+",arithmetic("*",nameOf(&o,at),literal(o.type,innerCount,at)),nameOf(&i,at));
        body->body.insert(body->body.begin(),jumpIf(notOf(std::move(loop->expr)),StmtKind::Break,at));
        body->body.insert(body->body.begin(),std::move(loop->init));
        redirectBreaks(*body,left);
        auto inner=countingLoop(i,innerCount,at);
        inner->loop=std::move(body);
        auto outerBody=std::make_unique<ast::Stmt>(StmtKind::Block,at);
        outerBody->scoped=false;
        outerBody->body.push_back(std::move(inner));
        outerBody->body.push_back(jumpIf(nameOf(&left,at),StmtKind::Break,at));
        auto outer=countingLoop(o,outerCount,at);
        outer->loop=std::move(outerBody);
        auto block=std::make_unique<ast::Stmt>(StmtKind::Block,at);
        block->body.push_back(declaration(left,boolLiteral(false,at),at));
        block->body.push_back(std::move(outer));
        return block;
    }
    // Breaks that leave the loop whose body this is (not those of nested loops and switches)
    // become '{ left = true; break; }'.
    static void redirectBreaks(ast::Stmt& s, const Symbol& left) {
        switch(s.kind) {
        case StmtKind::Break: {
            auto flag=std::make_unique<ast::Stmt>(StmtKind::Expression,s.loc);
            flag->expr=std::make_unique<ast::Expr>(ExprKind::Assign,s.loc);
            flag->expr->text="="; flag->expr->type=left.type; flag->expr->height=2;
            flag->expr->operands.push_back(nameOf(&left,s.loc));
            flag->expr->operands.push_back(boolLiteral(true,s.loc));
            s.kind=StmtKind::Block;
            s.body.push_back(std::move(flag));
            s.body.push_back(std::make_unique<ast::Stmt>(StmtKind::Break,s.loc));
            return;
        }
        case StmtKind::Block: for(auto& x:s.body)redirectBreaks(*x,left); return;
        case StmtKind::If:
            redirectBreaks(*s.then,left);
            if(s.otherwise)redirectBreaks(*s.otherwise,left);
            return;
        default: return;   // loops and switches keep their own breaks
        }
    }
    Symbol& generatedLocal(const std::string& name, const Type& type, Loc at) {
        program->symbols.emplace_back();
        Symbol& v=program->symbols.back();
        v.name=name; v.type=type; v.loc=at; v.storage=Storage::Local; v.generated=true;
        return v;
    }
    static ast::StmtPtr declaration(const Symbol& v, ast::ExprPtr init, Loc at) {
        auto decl=std::make_unique<ast::Decl>(DeclKind::Variable,at);
        decl->type.name=toString(v.type);
        decl->declarators.push_back({});
        decl->declarators.back().name=v.name; decl->declarators.back().loc=at;
        decl->declarators.back().init=std::move(init); decl->declarators.back().symbol=&v;
        auto s=std::make_unique<ast::Stmt>(StmtKind::Declaration,at);
        s->decl=std::move(decl);
        return s;
    }
    // for (int v = 0; v < count; v++), without its body.
    static ast::StmtPtr countingLoop(const Symbol& v, double count, Loc at) {
        auto loop=std::make_unique<ast::Stmt>(StmtKind::For,at);
        loop->init=declaration(v,literal(v.type,0,at),at);
        loop->expr=binary("<",nameOf(&v,at),literal(v.type,count,at));
        loop->step=std::make_unique<ast::Expr>(ExprKind::Postfix,at);
        loop->step->text="++"; loop->step->type=v.type; loop->step->operands.push_back(nameOf(&v,at)); loop->step->height=2;
        return loop;
    }
    static ast::ExprPtr arithmetic(const std::string& op, ast::ExprPtr a, ast::ExprPtr b) {
        auto e=binary(op,std::move(a),std::move(b));
        e->type=e->operands[0]->type;
        return e;
    }
    static ast::ExprPtr boolLiteral(bool v, Loc at) {
        auto e=std::make_unique<ast::Expr>(ExprKind::BoolLiteral,at);
        e->text=v?"true":"false"; e->intValue=v?1:0; e->type=Type::of(Type::Bool); e->constant=true;
        e->value=makeConstant(e->type,{Scalar::of(v)});
        return e;
    }
    static ast::ExprPtr notOf(ast::ExprPtr e) {
        auto n=std::make_unique<ast::Expr>(ExprKind::Unary,e->loc);
        n->text="!"; n->type=Type::of(Type::Bool); n->height=e->height+1;
        n->operands.push_back(std::move(e));
        return n;
    }

    // '// @maxiter{N}' on the line of a loop or alone on the line before it.
    std::set<const Comment*> usedIterationAnnotations;
    std::optional<double> iterationAnnotation(const ast::Stmt& s) {
        for(const Comment& c:unit.comments) {
            const std::size_t at=c.text.find("@maxiter");
            if(at==std::string::npos)continue;
            if(!((c.loc.line<=s.loc.line && s.loc.line<=c.endLine) || (c.ownLine && c.endLine+1==s.loc.line)))continue;
            usedIterationAnnotations.insert(&c);
            const char* b=c.text.c_str()+at+8;
            while(*b==' ')++b;
            double v=0;
            const char* e=b;
            if(*b=='{') {
                ++b; while(*b==' ')++b;
                const auto r=std::from_chars(b,c.text.c_str()+c.text.size(),v);
                e=r.ptr; while(*e==' ')++e;
                if(r.ec==std::errc() && *e=='}' && v>=0 && v==std::floor(v))return v;
            }
            sink.warning(c.loc,"annotation.invalid","annotation '@maxiter' is ignored: it must be written @maxiter{N} with a whole number N >= 0",
                "write '// @maxiter{64}' with the most iterations the loop runs");
            return std::nullopt;
        }
        return std::nullopt;
    }
    void unusedIterationAnnotations() {
        for(const Comment& c:unit.comments)
            if(c.text.find("@maxiter")!=std::string::npos && !usedIterationAnnotations.count(&c))
                sink.warning(c.loc,"annotation.unused","annotation '@maxiter' is ignored: it does not belong to a loop that needs it",
                    "write it on the line of a while, do-while or for loop, or alone on the line before it");
    }

    // The most iterations a loop can run, when a counter proves it: the condition compares a local
    // int or float with a value of known range (see range), and the counter starts at a value of
    // known range and moves towards it by a constant once in every iteration (the for step, an
    // increment in the condition, or a statement of the body that comes before any continue).
    // `bound.why` explains a failure.
    // With a condition 'a && b', either part bounds the loop when the other does not change its counter.
    GLSL_SKSL_NOINLINE std::optional<double> provedIterations(const ast::Stmt& s, LoopBound& bound) {
        std::vector<const ast::Expr*> parts;
        if(s.expr && !s.decl)conjunctsOf(*s.expr,parts);
        if(parts.size()<2)return provedBy(s,s.expr.get(),{},bound);
        std::optional<double> best;
        LoopBound found;
        for(std::size_t k=0;k<parts.size();++k) {
            std::vector<const ast::Expr*> others;
            for(std::size_t i=0;i<parts.size();++i)if(i!=k)others.push_back(parts[i]);
            LoopBound b;
            const auto n=provedBy(s,parts[k],others,b);
            if(n && (!best || *n<*best)){best=n;found=b;}
            else if(!n && found.why.empty())found.why=b.why;
        }
        bound=found;
        return best;
    }
    static void conjunctsOf(const ast::Expr& e, std::vector<const ast::Expr*>& out) {
        if(e.kind==ExprKind::Binary && e.text=="&&"){conjunctsOf(*e.operands[0],out);conjunctsOf(*e.operands[1],out);}
        else out.push_back(&e);
    }
    GLSL_SKSL_NOINLINE std::optional<double> provedBy(const ast::Stmt& s, const ast::Expr* c, const std::vector<const ast::Expr*>& others, LoopBound& bound) {
        static const std::set<std::string> relational={"<",">","<=",">="};
        if(c && !s.decl && c->constant && c->value && c->value->values.size()==1 && c->value->values[0].base==Type::Bool && !c->value->values[0].b)
            return s.kind==StmtKind::DoWhile?1.0:0.0;   // while (false), do ... while (false)
        if(s.decl || !c || c->kind!=ExprKind::Binary || !relational.count(c->text)) {
            bound.why=s.decl?"its condition declares a variable":!c?"it has no condition":"its condition does not compare a counter with a bound using <, >, <= or >=";
            return std::nullopt;
        }
        // The counter: a local, possibly incremented in the condition itself.
        auto counterOf=[](const ast::Expr& e, double& step) -> const Symbol* {
            const ast::Expr* x=&e;
            step=0;
            if((x->kind==ExprKind::Postfix||x->kind==ExprKind::Unary) && (x->text=="++"||x->text=="--")){step=x->text=="++"?1:-1;x=x->operands[0].get();}
            if(x->kind!=ExprKind::Name || !x->symbol || x->symbol->storage!=Storage::Local)return nullptr;
            const Type& t=x->symbol->type;
            return t.isScalar() && (t.base==Type::Int||t.base==Type::Float)?x->symbol:nullptr;
        };
        std::string op=c->text;
        double step=0;
        const Symbol* v=counterOf(*c->operands[0],step);
        const ast::Expr* limit=c->operands[1].get();
        if(!v) {
            v=counterOf(*c->operands[1],step);
            limit=c->operands[0].get();
            op=op=="<"?">":op==">"?"<":op=="<="?">=":"<=";
        }
        if(!v){bound.why="its condition does not compare a local variable with a bound";return std::nullopt;}
        const std::string name=quote(v->name);
        if(writes(*limit,v)){bound.why="its condition changes "+name+" twice";return std::nullopt;}
        for(const ast::Expr* o:others)if(writes(*o,v)){bound.why="another part of its condition changes "+name;return std::nullopt;}
        const ast::Stmt& body=*s.loop;
        if(step!=0) {
            if(writes(body,v)||(s.step && writes(*s.step,v))){bound.why=name+" also changes outside the condition";return std::nullopt;}
        } else if(s.kind==StmtKind::For && s.step) {
            if(!stepOf(*s.step,v,step)||writes(body,v)){bound.why=name+" does not change by a constant step once per iteration";return std::nullopt;}
        } else {
            // One statement of the body, before any continue, steps the counter; nothing else writes it.
            const std::vector<ast::StmtPtr>* list=body.kind==StmtKind::Block?&body.body:nullptr;
            bool found=false, skipped=false;
            const std::size_t count=list?list->size():1;
            for(std::size_t i=0;i<count;++i) {
                const ast::Stmt& x=list?*(*list)[i]:body;
                if(!writes(x,v)){skipped=skipped||continues(x);continue;}
                if(found || skipped || x.kind!=StmtKind::Expression || !stepOf(*x.expr,v,step)){found=false;break;}
                found=true;
            }
            if(!found){bound.why=name+" does not change by a constant step in one statement that every iteration runs";return std::nullopt;}
        }
        if(!(step==step) || step==0){bound.why=name+" does not change by a known constant";return std::nullopt;}
        const bool up=step>0;
        if(up!=(op=="<"||op=="<=")){bound.why=name+" moves away from the bound";return std::nullopt;}
        // Where the counter starts.
        const ast::Expr* start=counterStart(s,v);
        if(!start){bound.why="the value "+name+" starts with is not known: it must be declared with an initializer right before the loop, unchanged until it";return std::nullopt;}
        const auto first=range(*start,s), last=range(*limit,s);
        if(!first || !std::isfinite(up?first->lo:first->hi)){bound.why="the start value "+quote(ast::toGlsl(*start))+" of "+name+" has no known range";return std::nullopt;}
        if(!last || !std::isfinite(up?last->hi:last->lo)) {
            std::vector<const Symbol*> missing;
            unannotatedUniforms(*limit,missing);
            bound.why="the bound "+quote(ast::toGlsl(*limit))+" has no known range"+(missing.empty()?std::string():" (uniform "+quote(missing.front()->name)+" has no @minmax annotation)");
            return std::nullopt;
        }
        const double span=((up?last->hi:last->lo)-(up?first->lo:first->hi))/step;
        double iterations=std::max(0.0,(op=="<="||op==">=")?std::floor(span)+1:std::ceil(span));
        if(v->type.base==Type::Float)iterations+=1;      // rounding of the accumulated steps
        if(s.kind==StmtKind::DoWhile)iterations+=1;      // the body runs before the first test
        bound.assumed=first->assumed;
        bound.assumed.insert(bound.assumed.end(),last->assumed.begin(),last->assumed.end());
        std::sort(bound.assumed.begin(),bound.assumed.end());
        bound.assumed.erase(std::unique(bound.assumed.begin(),bound.assumed.end()),bound.assumed.end());
        return iterations;
    }
    // v++, ++v, v--, --v, v += c, v -= c with a constant c.
    static bool stepOf(const ast::Expr& e, const Symbol* v, double& step) {
        auto isV=[&](const ast::Expr& x){return x.kind==ExprKind::Name && x.symbol==v;};
        if((e.kind==ExprKind::Unary||e.kind==ExprKind::Postfix) && (e.text=="++"||e.text=="--") && isV(*e.operands[0])){step=e.text=="++"?1:-1;return true;}
        if(e.kind==ExprKind::Assign && (e.text=="+="||e.text=="-=") && isV(*e.operands[0]) && e.operands[1]->constant) {
            const auto c=scalarValue(*e.operands[1]);
            if(!c)return false;
            step=e.text=="+="?*c:-*c;
            return true;
        }
        return false;
    }
    // Whether s contains a continue of the enclosing loop.
    static bool continues(const ast::Stmt& s) {
        if(s.kind==StmtKind::Continue)return true;
        if(s.kind==StmtKind::For||s.kind==StmtKind::While||s.kind==StmtKind::DoWhile)return false;
        for(const auto& x:s.body)if(continues(*x))return true;
        for(const ast::Stmt* x:{s.then.get(),s.otherwise.get()})if(x && continues(*x))return true;
        return false;
    }
    // The value a loop counter has when the loop starts: the initializer of a for loop, or the
    // initializer of its declaration in the statements right before the loop, which leave it unchanged.
    const ast::Expr* counterStart(const ast::Stmt& s, const Symbol* v) const {
        if(s.kind==StmtKind::For && s.init) {
            if(s.init->kind==StmtKind::Declaration)
                for(const auto& d:s.init->decl->declarators)if(d.symbol==v)return d.init.get();
            const ast::Expr* e=s.init->expr.get();
            if(e && e->kind==ExprKind::Assign && e->text=="=" && e->operands[0]->kind==ExprKind::Name && e->operands[0]->symbol==v)return e->operands[1].get();
            if(writes(*s.init,v))return nullptr;
        }
        if(lists.empty() || (*lists.back().first)[lists.back().second].get()!=&s)return nullptr;
        const auto& list=*lists.back().first;
        for(std::size_t i=lists.back().second;i-->0;) {
            const ast::Stmt& x=*list[i];
            if(x.kind==StmtKind::Declaration)
                for(const auto& d:x.decl->declarators)if(d.symbol==v)return d.init.get();
            if(writes(x,v))return nullptr;
        }
        return nullptr;
    }

    // SkSL rejects loops that can run 100000 times or more ("loop must guarantee termination in
    // fewer iterations"; measured with skslc).
    static constexpr double kMaxLoopIterations=99999;

    // Replaces a start or bound that is not constant by a constant that covers its range, moving
    // the original expressions into guards at the start of the body. Returns the problem when the
    // loop cannot be rewritten exactly.
    GLSL_SKSL_NOINLINE std::string boundLoop(ast::Stmt& s, ast::Declarator& declarator, ast::Expr& condition, double step, std::string& suggestion) {
        const Symbol* index=declarator.symbol;
        const std::string& name=index->name;
        const std::string& op=condition.text;
        ast::Expr& start=*declarator.init;
        ast::Expr& bound=*condition.operands[1];
        const bool isInt=index->type.base==Type::Int;
        const std::string startClause="the loop index "+quote(name)+" starts at "+quote(ast::toGlsl(start))+", which is not a constant expression";
        const std::string boundClause="the bound "+quote(ast::toGlsl(bound))+" is not a constant expression";
        const std::string what=!start.constant?startClause:boundClause;
        if(!(step==step) || step==0)return what+", and the step is not a known non-zero constant";
        const bool up=step>0;
        if(op=="=="||op=="!=")return what+", and a condition using "+quote(op)+" does not limit the number of iterations";
        if(up!=(op=="<"||op=="<="))return what+", and the loop index moves away from the bound, so the loop runs zero times or never ends";
        std::vector<const Symbol*> assumed;
        double first=0, last=0;
        if(start.constant) {
            const auto v=scalarValue(start);
            if(!v)return boundClause;
            first=*v;
        } else {
            if(!isInt)return startClause+"; a float loop index needs a constant start";
            if(step!=1 && step!=-1)return startClause+"; a start that is not constant can only be replaced by a constant range when the index steps by 1";
            const auto r=range(start,s);
            const double limit=r?(up?r->lo:r->hi):0;
            if(!r || !std::isfinite(limit))return unknownRange(start,startClause,suggestion);
            first=limit;
            assumed.insert(assumed.end(),r->assumed.begin(),r->assumed.end());
        }
        if(bound.constant) {
            const auto v=scalarValue(bound);
            if(!v)return startClause;
            last=*v;
        } else {
            const auto r=range(bound,s);
            const double limit=r?(up?r->hi:r->lo):0;
            if(!r || !std::isfinite(limit))return unknownRange(bound,boundClause,suggestion);
            last=up?std::ceil(limit):std::floor(limit);
            assumed.insert(assumed.end(),r->assumed.begin(),r->assumed.end());
        }
        const double span=(last-first)/step;
        const bool inclusive=op=="<="||op==">=";
        const double iterations=std::max(0.0,inclusive?std::floor(span)+1:std::ceil(span));
        if(iterations>kMaxLoopIterations) {
            suggestion="narrow the range of values, for example with a smaller @minmax range";
            return what+", and covering its range takes "+number(iterations)+" iterations, but SkSL allows at most "+number(kMaxLoopIterations);
        }
        // Guards first, while the original expressions are still in place.
        std::vector<ast::StmtPtr> guards;
        if(!start.constant) {
            auto skip=binary(up?"<":">",nameOf(index,s.loc),std::move(declarator.init));
            guards.push_back(jumpIf(std::move(skip),StmtKind::Continue,s.loc));
            declarator.init=literal(index->type,first,s.loc);
        }
        if(!bound.constant) {
            auto inside=binary(op,nameOf(index,s.loc),std::move(condition.operands[1]));
            auto outside=std::make_unique<ast::Expr>(ExprKind::Unary,s.loc);
            outside->text="!"; outside->type=Type::of(Type::Bool); outside->height=inside->height+1;
            outside->operands.push_back(std::move(inside));
            guards.push_back(jumpIf(std::move(outside),StmtKind::Break,s.loc));
            condition.operands[1]=literal(index->type,last,s.loc);
        }
        if(s.loop->kind!=StmtKind::Block) {
            auto body=std::make_unique<ast::Stmt>(StmtKind::Block,s.loop->loc);
            body->scoped=false;
            body->body.push_back(std::move(s.loop));
            s.loop=std::move(body);
        }
        s.loop->body.insert(s.loop->body.begin(),std::make_move_iterator(guards.begin()),std::make_move_iterator(guards.end()));
        std::sort(assumed.begin(),assumed.end());
        assumed.erase(std::unique(assumed.begin(),assumed.end()),assumed.end());
        indexRanges[index]=Range{std::min(first,last),std::max(first,last),assumed};
        const std::string range="over the constant range '"+name+" = "+number(first)+"; "+name+" "+op+" "+number(last)+"' (at most "+number(iterations)+" iterations)";
        const std::string replaced=std::string("for loop: SkSL runtime shaders only run loops with a constant range, so it runs ")+range+
            " and checks the original "+(start.constant?"bound":bound.constant?"start":"start and bound")+" at the start of the body";
        if(assumed.empty())
            sink.warning(s.loc,"software.loop",replaced+"; exactly the GLSL iterations run","no change needed");
        else {
            std::string names;
            for(std::size_t k=0;k<assumed.size();++k) {
                if(k)names+=k+1==assumed.size()?" and ":", ";
                names+=quote(assumed[k]->name)+" in ["+number(assumed[k]->minimum)+", "+number(assumed[k]->maximum)+"]";
            }
            sink.warning(s.loc,"software.loop",replaced+"; the range relies on "+names+", as the @minmax annotation states, and with values outside "
                "the annotated range the loop runs fewer iterations than in GLSL","keep the uniform within its annotated range, or widen the annotation");
        }
        sink.hint("for (");
        return {};
    }

    GLSL_SKSL_NOINLINE std::string unknownRange(const ast::Expr& e, const std::string& what, std::string& suggestion) const {
        std::vector<const Symbol*> missing;
        unannotatedUniforms(e,missing);
        if(!missing.empty()) {
            const Symbol* u=missing.front();
            const bool isInt=u->type.base==Type::Int;
            suggestion="give the uniform its range of values with an annotation comment: 'uniform "+toString(u->type)+" "+u->name+
                (isInt?"; // @minmax{0, 32} @init{0}'":"; // @minmax{0.0, 1.0} @init{0.0}'");
            std::string names;
            for(std::size_t k=0;k<missing.size();++k)names+=(k?", ":"")+quote(missing[k]->name);
            return what+", and "+(missing.size()==1?"uniform "+names+" has":"uniforms "+names+" have")+" no @minmax annotation that gives its range";
        }
        suggestion="clamp it to a constant range, as in 'clamp("+ast::toGlsl(e)+(e.type.base==Type::Float?", 0.0, 64.0)'":", 0, 64)'")+", or loop to a constant maximum and exit early with break";
        return what+", and its range cannot be determined";
    }
    void unannotatedUniforms(const ast::Expr& e, std::vector<const Symbol*>& out) const {
        if(e.kind==ExprKind::Name && e.symbol && e.symbol->storage==Storage::Uniform && !e.symbol->hasRange &&
            std::find(out.begin(),out.end(),e.symbol)==out.end())out.push_back(e.symbol);
        for(const auto& o:e.operands)unannotatedUniforms(*o,out);
    }

    // ------------------------------------------------------------------ value ranges
    // Conservative range of a scalar int or float expression that has no side effects and keeps
    // its value while a loop runs: constants, uniforms (unbounded unless annotated), indices of
    // enclosing loops, locals and parameters the loop does not write, combined with + - * /,
    // min, max, clamp, abs and int()/float(). Unknown parts are infinite.
    struct Range { double lo=0, hi=0; std::vector<const Symbol*> assumed; };
    std::map<const Symbol*,Range> indexRanges;   // loops accepted so far

    static std::optional<double> scalarValue(const ast::Expr& e) {
        if(!e.value || e.value->values.size()!=1)return std::nullopt;
        const Scalar& v=e.value->values[0];
        if(v.base==Type::Int)return static_cast<double>(v.i);
        if(v.base==Type::Float)return static_cast<double>(v.f);
        return std::nullopt;
    }
    // `wraps`: the operation can overflow an int (arithmetic, conversion from float).
    static Range fit(Range r, bool isInt, bool wraps=false) {
        constexpr double inf=std::numeric_limits<double>::infinity();
        if(r.lo!=r.lo||r.hi!=r.hi){r.lo=-inf;r.hi=inf;}
        if(isInt) {
            // Integer overflow wraps, so the value can then be anything; an unbounded operand
            // (an infinite end) can overflow too.
            const bool overflow=(std::isfinite(r.lo) && r.lo<INT_MIN)||(std::isfinite(r.hi) && r.hi>INT_MAX)||
                (wraps && (!std::isfinite(r.lo)||!std::isfinite(r.hi)));
            if(overflow){r.lo=-inf;r.hi=inf;}
        } else {
            // Room for float rounding, which the shader does in single precision.
            r.lo-=std::fabs(r.lo)*1e-6+1e-30;
            r.hi+=std::fabs(r.hi)*1e-6+1e-30;
        }
        return r;
    }
    GLSL_SKSL_NOINLINE std::optional<Range> range(const ast::Expr& e, const ast::Stmt& loop) const {
        constexpr double inf=std::numeric_limits<double>::infinity();
        const Type& t=e.type;
        if(!t.isScalar() || (t.base!=Type::Int && t.base!=Type::Float))return std::nullopt;
        const bool isInt=t.base==Type::Int;
        if(e.constant) {
            if(const auto v=scalarValue(e))return Range{*v,*v,{}};
            return std::nullopt;
        }
        auto join=[](Range& r, const Range& a){r.assumed.insert(r.assumed.end(),a.assumed.begin(),a.assumed.end());};
        switch(e.kind) {
        case ExprKind::Name: {
            const Symbol* s=e.symbol;
            if(!s)return std::nullopt;
            if(s->storage==Storage::Uniform)
                return s->hasRange?fit(Range{s->minimum,s->maximum,{s}},isInt):Range{-inf,inf,{}};
            const auto it=indexRanges.find(s);
            if(it!=indexRanges.end())return it->second;
            if((s->storage==Storage::Local||s->storage==Storage::Parameter) && !writes(*loop.loop,s) && !(loop.step && writes(*loop.step,s)))
                return Range{-inf,inf,{}};
            return std::nullopt;
        }
        case ExprKind::Unary: {
            if(e.text!="-" && e.text!="+")return std::nullopt;
            auto a=range(*e.operands[0],loop);
            if(!a)return std::nullopt;
            if(e.text=="-")*a=Range{-a->hi,-a->lo,a->assumed};
            return fit(*a,isInt,e.text=="-");
        }
        case ExprKind::Binary: {
            const std::string& op=e.text;
            if(op!="+" && op!="-" && op!="*" && op!="/")return std::nullopt;
            const auto a=range(*e.operands[0],loop), b=range(*e.operands[1],loop);
            if(!a||!b)return std::nullopt;
            Range r;
            join(r,*a); join(r,*b);
            if(op=="+"){r.lo=a->lo+b->lo;r.hi=a->hi+b->hi;}
            else if(op=="-"){r.lo=a->lo-b->hi;r.hi=a->hi-b->lo;}
            else if(op=="/" && b->lo<=0 && b->hi>=0){r.lo=-inf;r.hi=inf;}
            else {
                // Products and quotients are monotonic in each operand on these intervals (the
                // divisor keeps its sign), so the extremes are at the corners; truncation keeps that.
                const double xs[2]={a->lo,a->hi}, ys[2]={b->lo,b->hi};
                r.lo=inf; r.hi=-inf;
                for(double x:xs)for(double y:ys) {
                    double v=op=="*"?x*y:x/y;
                    if(isInt && op=="/")v=std::trunc(v);
                    r.lo=std::min(r.lo,v); r.hi=std::max(r.hi,v);
                }
            }
            return fit(r,isInt,true);
        }
        case ExprKind::Call: {
            if(!e.function || !e.function->builtin)return std::nullopt;
            std::vector<Range> args;
            for(const auto& o:e.operands) {
                auto a=range(*o,loop);
                if(!a)return std::nullopt;
                args.push_back(std::move(*a));
            }
            Range r;
            for(const auto& a:args)join(r,a);
            const std::string& f=e.text;
            if((f=="min"||f=="max") && args.size()==2) {
                const bool lower=f=="min";
                r.lo=lower?std::min(args[0].lo,args[1].lo):std::max(args[0].lo,args[1].lo);
                r.hi=lower?std::min(args[0].hi,args[1].hi):std::max(args[0].hi,args[1].hi);
            } else if(f=="clamp" && args.size()==3) {
                // min(max(x, minVal), maxVal)
                r.lo=std::min(std::max(args[0].lo,args[1].lo),args[2].lo);
                r.hi=std::min(std::max(args[0].hi,args[1].hi),args[2].hi);
            } else if(f=="abs" && args.size()==1) {
                const Range& a=args[0];
                if(isInt && a.lo<=INT_MIN){r.lo=-inf;r.hi=inf;}     // abs(INT_MIN) stays negative
                else if(a.lo>=0){r.lo=a.lo;r.hi=a.hi;}
                else if(a.hi<=0){r.lo=-a.hi;r.hi=-a.lo;}
                else {r.lo=0;r.hi=std::max(-a.lo,a.hi);}
            } else return std::nullopt;
            return fit(r,isInt);
        }
        case ExprKind::Constructor: {
            if(e.operands.size()!=1)return std::nullopt;
            auto a=range(*e.operands[0],loop);
            if(!a)return std::nullopt;
            const bool fromFloat=e.operands[0]->type.base==Type::Float;
            if(isInt){a->lo=std::trunc(a->lo);a->hi=std::trunc(a->hi);}
            return fit(*a,isInt,isInt && fromFloat);
        }
        default: return std::nullopt;
        }
    }

    // Syntax tree nodes for loop guards, with the annotations the semantic pass would give them.
    static ast::ExprPtr literal(const Type& type, double v, Loc at) {
        const bool isInt=type.base==Type::Int;
        auto e=std::make_unique<ast::Expr>(isInt?ExprKind::IntLiteral:ExprKind::FloatLiteral,at);
        e->type=Type::of(type.base); e->constant=true;
        if(isInt) {
            const auto i=static_cast<std::int32_t>(v);
            e->intValue=static_cast<std::uint32_t>(i); e->text=std::to_string(i);
            e->value=makeConstant(e->type,{Scalar::of(i)});
        } else {
            char text[64];
            const auto formatted=std::to_chars(text,text+sizeof text,v,std::chars_format::fixed,1);
            *formatted.ptr=0;
            e->text=text; e->floatValue=v;
            e->value=makeConstant(e->type,{Scalar::of(static_cast<float>(v))});
        }
        return e;
    }
    static ast::ExprPtr nameOf(const Symbol* s, Loc at) {
        auto e=std::make_unique<ast::Expr>(ExprKind::Name,at);
        e->text=s->name; e->symbol=s; e->type=s->type;
        return e;
    }
    static ast::ExprPtr binary(const std::string& op, ast::ExprPtr a, ast::ExprPtr b) {
        auto e=std::make_unique<ast::Expr>(ExprKind::Binary,a->loc);
        e->text=op; e->type=Type::of(Type::Bool);
        e->height=std::max(a->height,b->height)+1;
        e->operands.push_back(std::move(a)); e->operands.push_back(std::move(b));
        return e;
    }
    static ast::StmtPtr jumpIf(ast::ExprPtr condition, StmtKind jump, Loc at) {
        auto s=std::make_unique<ast::Stmt>(StmtKind::If,at);
        s->expr=std::move(condition);
        s->then=std::make_unique<ast::Stmt>(jump,at);
        return s;
    }
    static std::string number(double v) {
        char text[64];
        const auto formatted=std::to_chars(text,text+sizeof text,v,std::chars_format::general,6);
        *formatted.ptr=0;
        return text;
    }

    bool writes(const ast::Stmt& s, const Symbol* target) const {
        bool found=false;
        auto expression=[&](const ast::Expr& e){if(writes(e,target))found=true;};
        if(s.expr)expression(*s.expr);
        if(s.step)expression(*s.step);
        if(s.decl)for(const auto& v:s.decl->declarators)if(v.init)expression(*v.init);
        for(const auto& x:s.body)found=found||writes(*x,target);
        for(const ast::Stmt* x:{s.then.get(),s.otherwise.get(),s.init.get(),s.loop.get()})if(x)found=found||writes(*x,target);
        return found;
    }
    bool writes(const ast::Expr& e, const Symbol* target) const {
        if((e.kind==ExprKind::Assign||((e.kind==ExprKind::Unary||e.kind==ExprKind::Postfix) && (e.text=="++"||e.text=="--"))) && root(*e.operands[0])==target)return true;
        if(e.kind==ExprKind::Call && e.function)
            for(std::size_t i=0;i<e.operands.size() && i<e.function->params.size();++i)
                if(e.function->params[i].direction!=Direction::In && root(*e.operands[i])==target)return true;
        for(const auto& o:e.operands)if(writes(*o,target))return true;
        return false;
    }

    // GLSL ES 1.00 Appendix A constant-index-expressions: constants, loop indices and
    // expressions combining them without user function calls.
    bool constantIndex(const ast::Expr& e) const {
        if(e.constant)return true;
        switch(e.kind) {
        case ExprKind::Name: return std::find(loopIndices.begin(),loopIndices.end(),e.symbol)!=loopIndices.end();
        case ExprKind::Unary: case ExprKind::Binary: case ExprKind::Ternary: case ExprKind::Constructor: case ExprKind::Field:
            for(const auto& o:e.operands)if(!constantIndex(*o))return false;
            return e.kind!=ExprKind::Unary || (e.text!="++" && e.text!="--");
        case ExprKind::Call:
            if(!e.function || !e.function->builtin)return false;
            for(const auto& o:e.operands)if(!constantIndex(*o))return false;
            return true;
        default: return false;
        }
    }

    static bool isFragCoord(const ast::Expr& e) {
        return e.kind==ExprKind::Name && e.symbol && e.symbol->storage==Storage::BuiltIn && e.symbol->name=="gl_FragCoord";
    }
    // gl_FragCoord.xy is the coordinate parameter of the SkSL main; depth and 1/w have no equivalent.
    GLSL_SKSL_NOINLINE bool fragCoordAccess(const ast::Expr& e) {
        if(e.kind!=ExprKind::Field || !isFragCoord(*e.operands[0]))return false;
        for(int c:e.swizzle)
            if(c>1) {
                unsupported(e.loc,"frag-coord","use of "+quote(ast::toGlsl(e))+": only the x and y components of gl_FragCoord are available, "
                    "because SkSL runtime shaders have no fragment depth or 1/w","use gl_FragCoord.xy");
                break;
            }
        return true;
    }

    void targetExpr(ast::Expr& e) {
        if(fragCoordAccess(e))return;
        for(auto& o:e.operands)targetExpr(*o);
        if(e.constructed)for(auto& a:e.constructed->arrays)if(a.size)targetExpr(*a.size);
        expressionIssues(e);
    }
    GLSL_SKSL_NOINLINE void expressionIssues(const ast::Expr& e) {
        if(!reportedMatrix && nonSquare(e.type)) {
            reportedMatrix=true;
            unsupported(e.loc,"matrix-shape","expression "+quote(ast::toGlsl(e))+": SkSL runtime shaders only have square matrices, not "+quote(toString(e.type)));
        }
        if(!reportedUint && (e.type.contains(Type::Uint)||e.kind==ExprKind::UintLiteral)) {
            reportedUint=true;
            unsupported(e.loc,"uint","expression "+quote(ast::toGlsl(e))+": unsigned integers ('uint') are not available in SkSL runtime shaders",
                "use int where the values fit in 31 bits");
        }
        if(e.kind==ExprKind::Name && e.symbol && !e.symbol->unsupported.empty())
            unsupported(e.loc,"builtin","built-in "+quote(e.text)+": "+e.symbol->unsupported);
        if(isFragCoord(e))
            unsupported(e.loc,"frag-coord","use of 'gl_FragCoord' as a whole: only its x and y components are available, "
                "because SkSL runtime shaders have no fragment depth or 1/w","use gl_FragCoord.xy");
        if(e.kind==ExprKind::Call && e.function && !e.function->unsupported.empty())
            unsupported(e.loc,"builtin","built-in function "+quote(e.text)+": "+e.function->unsupported);
        // Arrays of samplers are indexed by a selection among their child shaders (see the emitter).
        if(e.kind==ExprKind::Index && !constantIndex(*e.operands[1]) && !e.operands[0]->type.containsOpaque())
            unsupported(e.loc,"dynamic-index","index "+quote(ast::toGlsl(e))+": SkSL runtime shaders only allow constant or loop-index expressions as indices, and "+
                quote(ast::toGlsl(*e.operands[1]))+" is neither",
                "select the element with a loop over the constant range, e.g. for (int k = 0; k < N; ++k) if (k == "+ast::toGlsl(*e.operands[1])+") value = "+
                ast::toGlsl(*e.operands[0])+"[k];");
    }
};
}

std::unique_ptr<Program> analyze(ast::TranslationUnit& unit, DiagnosticSink& sink, const std::string& vec2Input) {
    return Analyzer(unit,sink,vec2Input).run();
}
}
