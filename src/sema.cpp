#include "sema.hpp"
#include "language.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <functional>
#include <map>
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

// GLSL ES built-in variables of fragment shaders (section 7). Values of the implementation
// constants are device-dependent, so they cannot be reproduced and are refused.
struct BuiltinVariable { const char* name; const char* type; int first, last; bool writable; const char* unsupported; };
const BuiltinVariable kBuiltinVariables[]={
    {"gl_FragColor","vec4",100,100,true,""},
    {"gl_FragData","vec4[]",100,100,true,"multiple render targets are not available in SkSL runtime shaders; write gl_FragColor instead"},
    {"gl_FragCoord","vec4",100,320,false,""},       // x and y only; see targetExpr
    {"gl_FrontFacing","bool",100,320,false,"primitive facing is not available in SkSL runtime shaders"},
    {"gl_PointCoord","vec2",100,320,false,"point sprite coordinates are not available in SkSL runtime shaders"},
    {"gl_FragDepth","float",300,320,true,"depth output is not available in SkSL runtime shaders"},
    {"gl_HelperInvocation","bool",310,320,false,"helper invocations are not visible in SkSL runtime shaders"},
    {"gl_SampleID","int",320,320,false,"per-sample shading is not available in SkSL runtime shaders"},
    {"gl_SamplePosition","vec2",320,320,false,"per-sample shading is not available in SkSL runtime shaders"},
    {"gl_SampleMaskIn","int[]",320,320,false,"sample masks are not available in SkSL runtime shaders"},
    {"gl_SampleMask","int[]",320,320,true,"sample masks are not available in SkSL runtime shaders"},
    {"gl_PrimitiveID","int",320,320,false,"primitive IDs are not available in SkSL runtime shaders"},
    {"gl_Layer","int",320,320,false,"layered rendering is not available in SkSL runtime shaders"},
    {"gl_DepthRange","",100,320,false,"the depth range is not available in SkSL runtime shaders"},
};
const char* const kImplementationConstants[][2]={
    {"gl_MaxVertexAttribs","100"},{"gl_MaxVertexUniformVectors","100"},{"gl_MaxVaryingVectors","100"},
    {"gl_MaxVertexTextureImageUnits","100"},{"gl_MaxCombinedTextureImageUnits","100"},{"gl_MaxTextureImageUnits","100"},
    {"gl_MaxFragmentUniformVectors","100"},{"gl_MaxDrawBuffers","100"},{"gl_MaxVertexOutputVectors","300"},
    {"gl_MaxFragmentInputVectors","300"},{"gl_MinProgramTexelOffset","300"},{"gl_MaxProgramTexelOffset","300"},
    {"gl_MaxImageUnits","310"},{"gl_MaxFragmentImageUniforms","310"},{"gl_MaxCombinedImageUniforms","310"},
    {"gl_MaxFragmentAtomicCounters","310"},{"gl_MaxAtomicCounterBindings","310"},{"gl_MaxFragmentAtomicCounterBuffers","310"},
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
    Analyzer(ast::TranslationUnit& unit, DiagnosticSink& sink):unit(unit),sink(sink),program(std::make_unique<Program>()),version(unit.version),
        derivativesExtension(unit.standardDerivatives) {
        program->version=version;
        push();
    }

    std::unique_ptr<Program> run() {
        for(auto& d:unit.decls)declaration(*d,true);
        finishProgram();
        if(sink.errors==0)for(auto& d:unit.decls)targetDeclaration(*d);
        return std::move(program);
    }

private:
    ast::TranslationUnit& unit;
    DiagnosticSink& sink;
    std::unique_ptr<Program> program;
    int version;
    bool derivativesExtension;
    std::vector<std::unordered_map<std::string,Entry>> scopes;
    std::vector<std::map<std::string,std::string>> precisions;
    std::map<std::string,Symbol*> builtinSymbols;
    Function* current=nullptr;

    bool available(const Function& f) const {
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
        for(const ast::LayoutId* id:q.layout) {
            const Rule* rule=nullptr;
            for(const auto& r:rules)if(id->name==r.name)rule=&r;
            if(!rule){invalid(id->loc,"layout","layout qualifier "+quote(id->name)+" is not defined in "+versionName(version));continue;}
            if(rule->version>version){invalid(id->loc,"layout","layout qualifier "+quote(id->name)+" requires "+versionName(rule->version));continue;}
            if(rule->value!=static_cast<bool>(id->value)) {
                invalid(id->loc,"layout","layout qualifier "+quote(id->name)+(rule->value?" needs a value, as in "+id->name+"=0":" does not take a value"));
                continue;
            }
            if(!id->value)continue;
            const Type t=expr(*id->value);
            long long v=-1;
            if(!t.isError() && (!id->value->constant || !integer(*id->value,v) || v<0))
                invalid(id->loc,"layout","layout qualifier "+quote(id->name)+" needs a non-negative integer constant");
            if(id->name=="location") {
                q.location=static_cast<int>(v);
                const bool allowed=(q.storage=="out" && ctx==Context::Global)||((q.storage=="in"||q.storage=="uniform") && version>=310 && ctx==Context::Global);
                if(!allowed)invalid(id->loc,"layout","layout qualifier 'location' is not allowed here");
            }
        }
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

    static bool returns(const ast::Stmt& s) {
        switch(s.kind) {
        case StmtKind::Return: case StmtKind::Discard: return true;
        case StmtKind::Block: for(const auto& x:s.body)if(returns(*x))return true; return false;
        case StmtKind::If: return s.otherwise && returns(*s.then) && returns(*s.otherwise);
        default: return false;
        }
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
            if(!type.empty())keywordType(type,s.type);
            if(unsizedArray)s.type.arrays.push_back(0);
            s.readOnly=!b.writable;
            s.unsupported=b.unsupported;
        }
        for(const auto& c:kImplementationConstants)
            if(name==c[0] && version>=std::stoi(c[1])) {
                found=true;
                s.type=Type::of(Type::Int); s.readOnly=true; s.constQualified=true; s.constantExpression=true;
                s.unsupported="its value depends on the device, so the result cannot be reproduced";
            }
        if(!found){builtinSymbols[name]=nullptr;return nullptr;}
        s.name=name; s.storage=Storage::BuiltIn;
        if(s.readOnly && s.readOnlyReason.empty())s.readOnlyReason=quote(name)+" is read-only";
        program->symbols.push_back(std::move(s));
        builtinSymbols[name]=&program->symbols.back();
        return &program->symbols.back();
    }

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
                unsupported(e.loc,"builtin","built-in function "+quote(e.text)+": textures, images and atomic counters are not available in SkSL runtime shaders",
                    "sample a child shader instead ('uniform shader' and .eval())");
                return {};
            }
            std::string hint="declare the function before calling it";
            for(const Function* f:builtinOverloads(e.text))
                if(f->version>version){hint=quote(e.text)+" requires "+versionName(f->version);break;}
            if(version==100 && isDerivative(e.text))hint=quote(e.text)+" requires '#extension GL_OES_standard_derivatives : enable' in GLSL ES 1.00";
            if(e.text=="texture2D"||e.text=="textureCube")hint=quote(e.text)+" was replaced by 'texture' in GLSL ES 3.00";
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
        if(!foldable.count(f.name))return nullptr;
        for(const auto& a:e.operands)if(!a->value)return nullptr;
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

    // ------------------------------------------------------------------ SkSL capability checks
    std::vector<const Symbol*> loopIndices;
    std::vector<char> breakTargets;      // 'L' loop, 'S' switch
    bool reportedUint=false, reportedMatrix=false;
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
                for(const auto& p:d.function->params)if(p.symbol)targetType(p.symbol->type,p.loc,"parameter "+quote(p.name));
                const Type returned=d.function->symbol?d.function->symbol->returnType:Type{};
                targetType(returned,d.function->loc,"return type of "+quote(d.function->name));
                for(auto& s:d.function->body->body)targetStatement(*s);
                const Function* fn=d.function->symbol;
                if(fn && fn->returnType.base!=Type::Void && !returns(*d.function->body))
                    unsupported(d.function->loc,"missing-return","function "+quote(fn->name)+": it can reach its end without returning a value, which SkSL rejects and GLSL leaves undefined",
                        "add a return statement at the end of "+quote(fn->name));
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
            unsupported(s.loc,"input",what+": shader inputs (varyings) are not available in SkSL runtime shaders",
                "pass the value as a uniform, or compute it from the coordinate parameter of main");
            break;
        case Storage::Uniform:
            if(s.type.containsOpaque())
                unsupported(s.loc,"sampler",what+": samplers, images and atomic counters are not available in SkSL runtime shaders",
                    "use a child shader: 'uniform shader "+s.name+";' sampled with "+s.name+".eval(coord)");
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
        case StmtKind::Block: for(auto& x:s.body)targetStatement(*x); break;
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
            const Symbol* index=loopIssues(s);
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
        case StmtKind::While: case StmtKind::DoWhile:
            unsupported(s.loc,"loop",std::string(s.kind==StmtKind::While?"while":"do-while")+" loop: SkSL runtime shaders only allow for loops with a constant range",
                std::string("rewrite it as a for loop with a constant maximum iteration count that breaks when the condition fails: ")+
                "for (int i = 0; i < MAX; ++i) { if (!("+(s.expr?ast::toGlsl(*s.expr):std::string("condition"))+")) break; ... }");
            break;
        case StmtKind::Continue:
            if(!breakTargets.empty() && breakTargets.back()=='S')
                unsupported(s.loc,"continue-in-switch","continue: SkSL does not allow 'continue' inside a switch statement",
                    "set a flag, break out of the switch, and continue after it");
            break;
        case StmtKind::Discard:
            unsupported(s.loc,"discard","discard: SkSL runtime shaders cannot discard fragments",
                "return a fully transparent color instead; this matches discard only with source-over blending");
            break;
        default: break;
        }
    }

    // Loops in SkSL runtime shaders follow GLSL ES 1.00 Appendix A: one int or float index with a
    // constant start, compared against a constant, stepped by a constant, not modified in the body.
    // Returns the loop index, if there is one.
    GLSL_SKSL_NOINLINE const Symbol* loopIssues(const ast::Stmt& s) {
        std::string problem, suggestion;
        const Symbol* index=nullptr;
        if(!s.init || s.init->kind!=StmtKind::Declaration || s.init->decl->kind!=DeclKind::Variable || s.init->decl->declarators.size()!=1)
            problem="the initializer must declare exactly one loop index, as in 'for (int i = 0; ...'";
        else {
            const auto& v=s.init->decl->declarators.front();
            index=v.symbol;
            if(!index || !(index->type.isScalar() && (index->type.base==Type::Int||index->type.base==Type::Float)))
                problem="the loop index must be an int or float";
            else if(!v.init)problem="the loop index "+quote(v.name)+" must be initialized";
            else if(!v.init->constant)
                problem="the loop index "+quote(v.name)+" starts at "+quote(ast::toGlsl(*v.init))+", which is not a constant expression";
        }
        const std::string name=index?index->name:"i";
        if(problem.empty()) {
            const ast::Expr* c=s.expr.get();
            static const std::set<std::string> relational={"<",">","<=",">=","==","!="};
            if(s.decl)problem="the condition cannot declare a variable";
            else if(!c)problem="the loop needs a condition";
            else if(c->kind!=ExprKind::Binary || !relational.count(c->text) || c->operands[0]->kind!=ExprKind::Name || c->operands[0]->symbol!=index)
                problem="the condition must compare the loop index with a constant, as in '"+name+" < 10'";
            else if(!c->operands[1]->constant) {
                const std::string bound=ast::toGlsl(*c->operands[1]);
                problem="the bound "+quote(bound)+" is not a constant expression";
                suggestion="loop to a constant maximum and exit early: for ("+toString(index->type)+" "+name+" = "+ast::toGlsl(*s.init->decl->declarators.front().init)+
                    "; "+name+" "+c->text+" MAX; ...) { if (!("+name+" "+c->text+" "+bound+")) break; ... } where MAX is a constant limit on "+quote(bound);
            }
        }
        if(problem.empty()) {
            const ast::Expr* step=s.step.get();
            auto isIndex=[&](const ast::Expr& x){return x.kind==ExprKind::Name && x.symbol==index;};
            const bool ok=step && (((step->kind==ExprKind::Unary||step->kind==ExprKind::Postfix) && (step->text=="++"||step->text=="--") && isIndex(*step->operands[0]))
                ||(step->kind==ExprKind::Assign && (step->text=="+="||step->text=="-=") && isIndex(*step->operands[0]) && step->operands[1]->constant));
            if(!ok)problem="the step must be "+name+"++, "+name+"--, or "+name+" += / -= a constant expression";
        }
        if(problem.empty() && writes(*s.loop,index))problem="the loop index "+quote(name)+" is modified inside the loop body";
        if(!problem.empty()) {
            if(suggestion.empty())suggestion="rewrite the loop as 'for (int i = START; i < END; ++i)' with constant START and END, using break for early exits";
            unsupported(s.loc,"loop","for loop: SkSL runtime shaders only allow loops with a constant range; "+problem,suggestion);
        }
        return index;
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
        static const std::set<std::string> bitwise={"&","|","^","~","<<",">>","&=","|=","^=","<<=",">>="};
        if((e.kind==ExprKind::Binary||e.kind==ExprKind::Assign||e.kind==ExprKind::Unary) && !e.constant) {
            if(e.text=="%"||e.text=="%=")
                unsupported(e.loc,"operator","operator "+quote(e.text)+": SkSL runtime shaders have no integer remainder",
                    "for non-negative operands below 2^24, int(mod(float(a), float(b))) gives the same result");
            else if(bitwise.count(e.text))
                unsupported(e.loc,"operator","operator "+quote(e.text)+": SkSL runtime shaders have no bitwise operators",
                    e.text[0]=='<'||e.text[0]=='>'?"a shift by a constant n is a multiplication or division by 2^n for non-negative values":"rewrite the computation without bit operations");
        }
        if(e.kind==ExprKind::Index && !constantIndex(*e.operands[1]))
            unsupported(e.loc,"dynamic-index","index "+quote(ast::toGlsl(e))+": SkSL runtime shaders only allow constant or loop-index expressions as indices, and "+
                quote(ast::toGlsl(*e.operands[1]))+" is neither",
                "select the element with a loop over the constant range, e.g. for (int k = 0; k < N; ++k) if (k == "+ast::toGlsl(*e.operands[1])+") value = "+
                ast::toGlsl(*e.operands[0])+"[k];");
    }
};
}

std::unique_ptr<Program> analyze(ast::TranslationUnit& unit, DiagnosticSink& sink) {
    return Analyzer(unit,sink).run();
}
}
