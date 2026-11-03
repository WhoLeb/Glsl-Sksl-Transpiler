#include "sema.hpp"
#include <map>
#include <set>
#include <sstream>
namespace glsl_sksl::sema {
namespace {
// Fragment-shader built-in functions, GLSL ES 1.00-3.20 sections 8.1-8.9. Generic names expand in
// lockstep: genType/genIType/genUType/genBType to sizes 1-4, vec/ivec/uvec/bvec to 2-4, mat to all
// matrix shapes and sqmat to square ones. "out" marks output parameters.
const char* const kSpec=R"(
100 genType radians(genType)
100 genType degrees(genType)
100 genType sin(genType)
100 genType cos(genType)
100 genType tan(genType)
100 genType asin(genType)
100 genType acos(genType)
100 genType atan(genType,genType)
100 genType atan(genType)
300 genType sinh(genType)
300 genType cosh(genType)
300 genType tanh(genType)
300 genType asinh(genType)
300 genType acosh(genType)
300 genType atanh(genType)
100 genType pow(genType,genType)
100 genType exp(genType)
100 genType log(genType)
100 genType exp2(genType)
100 genType log2(genType)
100 genType sqrt(genType)
100 genType inversesqrt(genType)
100 genType abs(genType)
300 genIType abs(genIType)
100 genType sign(genType)
300 genIType sign(genIType)
100 genType floor(genType)
300 genType trunc(genType)
300 genType round(genType)
300 genType roundEven(genType)
100 genType ceil(genType)
100 genType fract(genType)
100 genType mod(genType,float)
100 genType mod(genType,genType)
300 genType modf(genType,out genType)
100 genType min(genType,genType)
100 genType min(genType,float)
300 genIType min(genIType,genIType)
300 genIType min(genIType,int)
300 genUType min(genUType,genUType)
300 genUType min(genUType,uint)
100 genType max(genType,genType)
100 genType max(genType,float)
300 genIType max(genIType,genIType)
300 genIType max(genIType,int)
300 genUType max(genUType,genUType)
300 genUType max(genUType,uint)
100 genType clamp(genType,genType,genType)
100 genType clamp(genType,float,float)
300 genIType clamp(genIType,genIType,genIType)
300 genIType clamp(genIType,int,int)
300 genUType clamp(genUType,genUType,genUType)
300 genUType clamp(genUType,uint,uint)
100 genType mix(genType,genType,genType)
100 genType mix(genType,genType,float)
300 genType mix(genType,genType,genBType)
310 genIType mix(genIType,genIType,genBType)
310 genUType mix(genUType,genUType,genBType)
310 genBType mix(genBType,genBType,genBType)
100 genType step(genType,genType)
100 genType step(float,genType)
100 genType smoothstep(genType,genType,genType)
100 genType smoothstep(float,float,genType)
300 genBType isnan(genType)
300 genBType isinf(genType)
300 genIType floatBitsToInt(genType)
300 genUType floatBitsToUint(genType)
300 genType intBitsToFloat(genIType)
300 genType uintBitsToFloat(genUType)
320 genType fma(genType,genType,genType)
310 genType frexp(genType,out genIType)
310 genType ldexp(genType,genIType)
300 uint packSnorm2x16(vec2)
300 vec2 unpackSnorm2x16(uint)
300 uint packUnorm2x16(vec2)
300 vec2 unpackUnorm2x16(uint)
300 uint packHalf2x16(vec2)
300 vec2 unpackHalf2x16(uint)
310 uint packUnorm4x8(vec4)
310 uint packSnorm4x8(vec4)
310 vec4 unpackUnorm4x8(uint)
310 vec4 unpackSnorm4x8(uint)
100 float length(genType)
100 float distance(genType,genType)
100 float dot(genType,genType)
100 vec3 cross(vec3,vec3)
100 genType normalize(genType)
100 genType faceforward(genType,genType,genType)
100 genType reflect(genType,genType)
100 genType refract(genType,genType,float)
100 mat matrixCompMult(mat,mat)
300 float determinant(sqmat)
300 sqmat inverse(sqmat)
100 bvec lessThan(vec,vec)
100 bvec lessThan(ivec,ivec)
300 bvec lessThan(uvec,uvec)
100 bvec lessThanEqual(vec,vec)
100 bvec lessThanEqual(ivec,ivec)
300 bvec lessThanEqual(uvec,uvec)
100 bvec greaterThan(vec,vec)
100 bvec greaterThan(ivec,ivec)
300 bvec greaterThan(uvec,uvec)
100 bvec greaterThanEqual(vec,vec)
100 bvec greaterThanEqual(ivec,ivec)
300 bvec greaterThanEqual(uvec,uvec)
100 bvec equal(vec,vec)
100 bvec equal(ivec,ivec)
300 bvec equal(uvec,uvec)
100 bvec equal(bvec,bvec)
100 bvec notEqual(vec,vec)
100 bvec notEqual(ivec,ivec)
300 bvec notEqual(uvec,uvec)
100 bvec notEqual(bvec,bvec)
100 bool any(bvec)
100 bool all(bvec)
100 bvec not(bvec)
310 genUType uaddCarry(genUType,genUType,out genUType)
310 genUType usubBorrow(genUType,genUType,out genUType)
310 void umulExtended(genUType,genUType,out genUType,out genUType)
310 void imulExtended(genIType,genIType,out genIType,out genIType)
310 genIType bitfieldExtract(genIType,int,int)
310 genUType bitfieldExtract(genUType,int,int)
310 genIType bitfieldInsert(genIType,genIType,int,int)
310 genUType bitfieldInsert(genUType,genUType,int,int)
310 genIType bitfieldReverse(genIType)
310 genUType bitfieldReverse(genUType)
310 genIType bitCount(genIType)
310 genIType bitCount(genUType)
310 genIType findLSB(genIType)
310 genIType findLSB(genUType)
310 genIType findMSB(genIType)
310 genIType findMSB(genUType)
300 genType dFdx(genType)
300 genType dFdy(genType)
300 genType fwidth(genType)
100 vec4 texture2D(sampler2D,vec2)
100 vec4 texture2D(sampler2D,vec2,float)
100 vec4 texture2DProj(sampler2D,vec3)
100 vec4 texture2DProj(sampler2D,vec3,float)
100 vec4 texture2DProj(sampler2D,vec4)
100 vec4 texture2DProj(sampler2D,vec4,float)
300 vec4 texture(sampler2D,vec2)
300 vec4 texture(sampler2D,vec2,float)
300 vec4 textureProj(sampler2D,vec3)
300 vec4 textureProj(sampler2D,vec3,float)
300 vec4 textureProj(sampler2D,vec4)
300 vec4 textureProj(sampler2D,vec4,float)
100 vec4 textureCube(samplerCube,vec3)
100 vec4 textureCube(samplerCube,vec3,float)
100 vec4 texture2DLodEXT(sampler2D,vec2,float)
100 vec4 texture2DProjLodEXT(sampler2D,vec3,float)
100 vec4 texture2DProjLodEXT(sampler2D,vec4,float)
100 vec4 textureCubeLodEXT(samplerCube,vec3,float)
100 vec4 texture2DGradEXT(sampler2D,vec2,vec2,vec2)
100 vec4 texture2DProjGradEXT(sampler2D,vec3,vec2,vec2)
100 vec4 texture2DProjGradEXT(sampler2D,vec4,vec2,vec2)
100 vec4 textureCubeGradEXT(samplerCube,vec3,vec3,vec3)
300 vec4 texture(sampler2D,vec2)
300 vec4 texture(sampler2D,vec2,float)
300 vec4 textureLod(sampler2D,vec2,float)
300 vec4 textureGrad(sampler2D,vec2,vec2,vec2)
300 ivec2 textureSize(sampler2D,int)
300 vec4 textureOffset(sampler2D,vec2,ivec2)
300 vec4 textureOffset(sampler2D,vec2,ivec2,float)
300 vec4 textureLodOffset(sampler2D,vec2,float,ivec2)
300 vec4 textureGradOffset(sampler2D,vec2,vec2,vec2,ivec2)
300 vec4 texelFetch(sampler2D,ivec2,int)
300 vec4 texelFetchOffset(sampler2D,ivec2,int,ivec2)
300 vec4 texture(sampler3D,vec3)
300 vec4 texture(sampler3D,vec3,float)
300 vec4 textureLod(sampler3D,vec3,float)
300 vec4 textureGrad(sampler3D,vec3,vec3,vec3)
300 ivec3 textureSize(sampler3D,int)
300 vec4 textureOffset(sampler3D,vec3,ivec3)
300 vec4 textureOffset(sampler3D,vec3,ivec3,float)
300 vec4 textureLodOffset(sampler3D,vec3,float,ivec3)
300 vec4 textureGradOffset(sampler3D,vec3,vec3,vec3,ivec3)
300 vec4 texelFetch(sampler3D,ivec3,int)
300 vec4 texelFetchOffset(sampler3D,ivec3,int,ivec3)
300 vec4 texture(samplerCube,vec3)
300 vec4 texture(samplerCube,vec3,float)
300 vec4 textureLod(samplerCube,vec3,float)
300 vec4 textureGrad(samplerCube,vec3,vec3,vec3)
300 ivec2 textureSize(samplerCube,int)
300 vec4 texture(sampler2DArray,vec3)
300 vec4 texture(sampler2DArray,vec3,float)
300 vec4 textureLod(sampler2DArray,vec3,float)
300 vec4 textureGrad(sampler2DArray,vec3,vec2,vec2)
300 ivec3 textureSize(sampler2DArray,int)
300 vec4 textureOffset(sampler2DArray,vec3,ivec2)
300 vec4 textureOffset(sampler2DArray,vec3,ivec2,float)
300 vec4 textureLodOffset(sampler2DArray,vec3,float,ivec2)
300 vec4 textureGradOffset(sampler2DArray,vec3,vec2,vec2,ivec2)
300 vec4 texelFetch(sampler2DArray,ivec3,int)
300 vec4 texelFetchOffset(sampler2DArray,ivec3,int,ivec2)
300 vec4 textureProj(sampler2D,vec3)
300 vec4 textureProj(sampler2D,vec3,float)
300 vec4 textureProjLod(sampler2D,vec3,float)
300 vec4 textureProjGrad(sampler2D,vec3,vec2,vec2)
300 vec4 textureProjOffset(sampler2D,vec3,ivec2)
300 vec4 textureProjOffset(sampler2D,vec3,ivec2,float)
300 vec4 textureProjLodOffset(sampler2D,vec3,float,ivec2)
300 vec4 textureProjGradOffset(sampler2D,vec3,vec2,vec2,ivec2)
300 vec4 textureProj(sampler2D,vec4)
300 vec4 textureProj(sampler2D,vec4,float)
300 vec4 textureProjLod(sampler2D,vec4,float)
300 vec4 textureProjGrad(sampler2D,vec4,vec2,vec2)
300 vec4 textureProjOffset(sampler2D,vec4,ivec2)
300 vec4 textureProjOffset(sampler2D,vec4,ivec2,float)
300 vec4 textureProjLodOffset(sampler2D,vec4,float,ivec2)
300 vec4 textureProjGradOffset(sampler2D,vec4,vec2,vec2,ivec2)
300 vec4 textureProj(sampler3D,vec4)
300 vec4 textureProj(sampler3D,vec4,float)
300 vec4 textureProjLod(sampler3D,vec4,float)
300 vec4 textureProjGrad(sampler3D,vec4,vec3,vec3)
300 vec4 textureProjOffset(sampler3D,vec4,ivec3)
300 vec4 textureProjOffset(sampler3D,vec4,ivec3,float)
300 vec4 textureProjLodOffset(sampler3D,vec4,float,ivec3)
300 vec4 textureProjGradOffset(sampler3D,vec4,vec3,vec3,ivec3)
310 vec4 textureGather(sampler2D,vec2)
310 vec4 textureGather(sampler2D,vec2,int)
310 vec4 textureGatherOffset(sampler2D,vec2,ivec2)
310 vec4 textureGatherOffset(sampler2D,vec2,ivec2,int)
310 vec4 textureGather(sampler2DArray,vec3)
310 vec4 textureGather(sampler2DArray,vec3,int)
310 vec4 textureGatherOffset(sampler2DArray,vec3,ivec2)
310 vec4 textureGatherOffset(sampler2DArray,vec3,ivec2,int)
310 vec4 textureGather(samplerCube,vec3)
310 vec4 textureGather(samplerCube,vec3,int)
)";
// Built-ins that later versions removed: GLSL ES 3.00 replaced texture2D, textureCube and the
// GL_EXT_shader_texture_lod functions by texture, textureLod and textureGrad.
int lastVersion(const std::string& name) {
    const bool extension=name.size()>3 && name.compare(name.size()-3,3,"EXT")==0;
    return name.compare(0,9,"texture2D")==0||name.compare(0,11,"textureCube")==0||extension?100:320;
}

std::string reason(const std::string& name) {
    static const std::set<std::string> bits={"floatBitsToInt","floatBitsToUint","intBitsToFloat","uintBitsToFloat","packSnorm2x16",
        "unpackSnorm2x16","packUnorm2x16","unpackUnorm2x16","packHalf2x16","unpackHalf2x16","packUnorm4x8","packSnorm4x8",
        "unpackUnorm4x8","unpackSnorm4x8","uaddCarry","usubBorrow","umulExtended","imulExtended","bitfieldExtract",
        "bitfieldInsert","bitfieldReverse","bitCount","findLSB","findMSB","frexp","ldexp"};
    if(bits.count(name))return "no bit-level access in SkSL";
    if(name=="isnan"||name=="isinf")return "NaN and infinity tests are unreliable in SkSL";
    return {};
}

struct Table {
    std::deque<Function> functions;
    std::map<std::string,std::vector<const Function*>> byName;
    void add(Function f) {
        auto& list=byName[f.name];
        for(const Function* g:list) {
            if(g->params.size()!=f.params.size())continue;
            bool same=true;
            for(std::size_t i=0;i<f.params.size();++i)if(g->params[i].type!=f.params[i].type)same=false;
            if(same)return;
        }
        f.builtin=true;
        f.unsupported=reason(f.name);
        functions.push_back(std::move(f));
        list.push_back(&functions.back());
    }
};

Type generic(const std::string& word, int size, int columns) {
    Type t;
    auto vector=[&](Type::Base b){return Type::of(b,size);};
    if(word=="genType"||word=="vec")return vector(Type::Float);
    if(word=="genIType"||word=="ivec")return vector(Type::Int);
    if(word=="genUType"||word=="uvec")return vector(Type::Uint);
    if(word=="genBType"||word=="bvec")return vector(Type::Bool);
    if(word=="mat"||word=="sqmat")return Type::of(Type::Float,size,columns);
    keywordType(word,t);
    return t;
}

const Table& table() {
    static const Table built=[] {
        Table t;
        std::istringstream lines(kSpec);
        std::string line;
        while(std::getline(lines,line)) {
            if(line.empty())continue;
            for(char& c:line)if(c=='('||c==')'||c==',')c=' ';
            std::istringstream words(line);
            int version=0; std::string result, name, word;
            words>>version>>result>>name;
            std::vector<std::pair<std::string,bool>> params;
            bool out=false;
            while(words>>word) {
                if(word=="out"){out=true;continue;}
                params.push_back({word,out});out=false;
            }
            const std::string all=line;
            std::vector<std::pair<int,int>> shapes;   // (size or rows, columns)
            if(all.find("gen")!=std::string::npos)shapes={{1,1},{2,1},{3,1},{4,1}};
            else if(all.find("sqmat")!=std::string::npos)shapes={{2,2},{3,3},{4,4}};
            else if(all.find(" mat")!=std::string::npos)
                for(int c=2;c<=4;++c)for(int r=2;r<=4;++r)shapes.push_back({r,c});
            else if(all.find("vec ")!=std::string::npos||all.find("vec\n")!=std::string::npos||all.find(" bvec")!=std::string::npos)
                shapes={{2,1},{3,1},{4,1}};
            else shapes={{1,1}};
            for(const auto& shape:shapes) {
                Function f; f.name=name; f.version=version; f.lastVersion=lastVersion(name);
                f.returnType=generic(result,shape.first,shape.second);
                for(const auto& p:params)
                    f.params.push_back({generic(p.first,shape.first,shape.second),p.second?Direction::Out:Direction::In,false,{}});
                t.add(std::move(f));
            }
        }
        for(const char* sampler:{"sampler2D","sampler2DArray"})for(bool component:{false,true}) {
            Function gather; gather.name="textureGatherOffsets"; gather.version=320;
            gather.returnType=Type::of(Type::Float,4);
            Type offsets=Type::of(Type::Int,2); offsets.arrays.push_back(4);
            gather.params={{generic(sampler,1,1),Direction::In,false,{}},{Type::of(Type::Float,std::string(sampler)=="sampler2D"?2:3),Direction::In,false,{}},
                {offsets,Direction::In,false,{}}};
            if(component)gather.params.push_back({Type::of(Type::Int),Direction::In,false,{}});
            t.add(gather);
        }
        for(int c=2;c<=4;++c)for(int r=2;r<=4;++r) {
            Function outer; outer.name="outerProduct"; outer.version=300;
            outer.returnType=Type::of(Type::Float,r,c);
            outer.params={{Type::of(Type::Float,r),Direction::In,false,{}},{Type::of(Type::Float,c),Direction::In,false,{}}};
            t.add(outer);
            Function transpose; transpose.name="transpose"; transpose.version=300;
            transpose.returnType=Type::of(Type::Float,c,r);
            transpose.params={{Type::of(Type::Float,r,c),Direction::In,false,{}}};
            t.add(transpose);
        }
        return t;
    }();
    return built;
}

struct Range { int first, last; };
const std::map<std::string,Range>& opaqueBuiltins() {
    static const std::map<std::string,Range> names=[] {
        std::map<std::string,Range> m;
        auto add=[&](const char* list,int first,int last){std::istringstream in(list);std::string w;while(in>>w)m[w]={first,last};};
        add("texture2D texture2DProj textureCube",100,100);
        add("texture textureProj textureLod textureOffset texelFetch texelFetchOffset textureProjOffset textureLodOffset "
            "textureProjLod textureProjLodOffset textureGrad textureGradOffset textureProjGrad textureProjGradOffset textureSize",300,320);
        add("textureGather textureGatherOffset imageSize imageLoad imageStore atomicCounter atomicCounterIncrement "
            "atomicCounterDecrement atomicAdd atomicMin atomicMax atomicAnd atomicOr atomicXor atomicExchange atomicCompSwap "
            "memoryBarrier memoryBarrierAtomicCounter memoryBarrierBuffer memoryBarrierImage memoryBarrierShared groupMemoryBarrier",310,320);
        add("textureGatherOffsets imageAtomicAdd imageAtomicMin imageAtomicMax imageAtomicAnd imageAtomicOr imageAtomicXor "
            "imageAtomicExchange imageAtomicCompSwap interpolateAtCentroid interpolateAtSample interpolateAtOffset",320,320);
        return m;
    }();
    return names;
}
}

const std::vector<const Function*>& builtinOverloads(const std::string& name) {
    static const std::vector<const Function*> none;
    const auto& byName=table().byName;
    const auto it=byName.find(name);
    return it==byName.end()?none:it->second;
}

bool isBuiltinFunctionName(const std::string& name, int version) {
    for(const Function* f:builtinOverloads(name))if(f->version<=version && version<=f->lastVersion)return true;
    return isUnsupportedBuiltinName(name,version);
}

bool isUnsupportedBuiltinName(const std::string& name, int version) {
    const auto& names=opaqueBuiltins();
    const auto it=names.find(name);
    return it!=names.end() && version>=it->second.first && version<=it->second.last;
}

bool isDerivative(const std::string& name) {
    return name=="dFdx"||name=="dFdy"||name=="fwidth";
}

std::string signature(const Function& f) {
    std::string s=f.name+"(";
    for(std::size_t i=0;i<f.params.size();++i) {
        if(i)s+=", ";
        if(f.params[i].direction==Direction::Out)s+="out ";
        else if(f.params[i].direction==Direction::InOut)s+="inout ";
        s+=toString(f.params[i].type);
    }
    return s+")";
}
}
