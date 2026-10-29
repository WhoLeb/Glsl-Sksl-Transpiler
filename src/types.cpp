#include "types.hpp"
#include <cmath>
namespace glsl_sksl::sema {
Type Type::element() const {
    Type t=*this;
    if(!t.arrays.empty())t.arrays.erase(t.arrays.begin());
    return t;
}
Type Type::component() const {return of(base);}
bool Type::containsOpaque() const {
    if(base==Opaque)return true;
    if(base==Struct && structDef)for(const auto& f:structDef->fields)if(f.type.containsOpaque())return true;
    return false;
}
bool Type::containsArray() const {
    if(isArray())return true;
    if(base==Struct && structDef)for(const auto& f:structDef->fields)if(f.type.containsArray())return true;
    return false;
}
bool Type::contains(Base b) const {
    if(base==b)return true;
    if(base==Struct && structDef)for(const auto& f:structDef->fields)if(f.type.contains(b))return true;
    return false;
}
bool operator==(const Type& a, const Type& b) {
    return a.base==b.base && a.columns==b.columns && a.rows==b.rows && a.opaque==b.opaque && a.structDef==b.structDef && a.arrays==b.arrays;
}

std::string toString(const Type& t) {
    std::string s;
    switch(t.base) {
    case Type::Error: s="<error>"; break;
    case Type::Void: s="void"; break;
    case Type::Struct: s=t.structDef?t.structDef->name:"struct"; if(s.empty())s="<anonymous struct>"; break;
    case Type::Opaque: s=t.opaque; break;
    default: {
        const char* prefix=t.base==Type::Bool?"b":t.base==Type::Int?"i":t.base==Type::Uint?"u":"";
        const char* scalar=t.base==Type::Bool?"bool":t.base==Type::Int?"int":t.base==Type::Uint?"uint":"float";
        if(t.columns>1)s=t.columns==t.rows?"mat"+std::to_string(t.columns):"mat"+std::to_string(t.columns)+"x"+std::to_string(t.rows);
        else if(t.rows>1)s=std::string(prefix)+"vec"+std::to_string(t.rows);
        else s=scalar;
    }
    }
    for(int n:t.arrays)s+="["+(n?std::to_string(n):std::string())+"]";
    return s;
}

bool keywordType(const std::string& w, Type& out) {
    static const std::pair<const char*,Type::Base> scalars[]={{"void",Type::Void},{"bool",Type::Bool},{"int",Type::Int},{"uint",Type::Uint},{"float",Type::Float}};
    for(const auto& s:scalars)if(w==s.first){out=Type::of(s.second);return true;}
    auto digit=[](char c){return c>='2' && c<='4';};
    const std::size_t v=w.find("vec");
    if(v!=std::string::npos && w.size()==v+4 && digit(w.back())) {
        const std::string prefix=w.substr(0,v);
        const Type::Base base=prefix.empty()?Type::Float:prefix=="i"?Type::Int:prefix=="u"?Type::Uint:prefix=="b"?Type::Bool:Type::Error;
        if(base==Type::Error)return false;
        out=Type::of(base,w.back()-'0');
        return true;
    }
    if(w.rfind("mat",0)==0) {
        if(w.size()==4 && digit(w[3])){out=Type::of(Type::Float,w[3]-'0',w[3]-'0');return true;}
        if(w.size()==6 && digit(w[3]) && w[4]=='x' && digit(w[5])){out=Type::of(Type::Float,w[5]-'0',w[3]-'0');return true;}
        return false;
    }
    if(w.find("sampler")!=std::string::npos||w.find("image")!=std::string::npos||w=="atomic_uint") {
        out=Type::of(Type::Opaque); out.opaque=w;
        return true;
    }
    return false;
}

bool Scalar::operator==(const Scalar& o) const {
    if(base!=o.base)return false;
    switch(base) {
    case Type::Float: return f==o.f;
    case Type::Int: return i==o.i;
    case Type::Uint: return u==o.u;
    default: return b==o.b;
    }
}

bool convert(const Scalar& s, Type::Base to, Scalar& out) {
    if(s.base==to){out=s;return true;}
    switch(to) {
    case Type::Float:
        out=Scalar::of(s.base==Type::Int?static_cast<float>(s.i):s.base==Type::Uint?static_cast<float>(s.u):s.b?1.0f:0.0f);
        return true;
    case Type::Int:
        if(s.base==Type::Float) {
            if(!(s.f>-2147483649.0f && s.f<2147483648.0f))return false;   // undefined outside the int range
            out=Scalar::of(static_cast<std::int32_t>(s.f));
            return true;
        }
        out=Scalar::of(s.base==Type::Uint?static_cast<std::int32_t>(s.u):static_cast<std::int32_t>(s.b));
        return true;
    case Type::Uint:
        if(s.base==Type::Float) {
            if(!(s.f>-1.0f && s.f<4294967296.0f))return false;
            out=Scalar::ofUint(static_cast<std::uint32_t>(s.f));
            return true;
        }
        out=Scalar::ofUint(s.base==Type::Int?static_cast<std::uint32_t>(s.i):static_cast<std::uint32_t>(s.b));
        return true;
    case Type::Bool:
        out=Scalar::of(s.base==Type::Float?s.f!=0.0f:s.base==Type::Int?s.i!=0:s.u!=0);
        return true;
    default: return false;
    }
}

int slotCount(const Type& t) {
    int n=1;
    for(int a:t.arrays)n*=a;
    if(t.base==Type::Struct) {
        int fields=0;
        if(t.structDef)for(const auto& f:t.structDef->fields)fields+=slotCount(f.type);
        return n*fields;
    }
    if(t.base==Type::Opaque||t.base==Type::Void||t.base==Type::Error)return 0;
    return n*t.components();
}
}
