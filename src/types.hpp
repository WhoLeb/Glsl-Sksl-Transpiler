#pragma once
#include "diagnostics.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace glsl_sksl::sema {
struct StructDef;
// A GLSL ES type. Vectors have columns=1 and rows=N; matCxR has columns=C and rows=R.
struct Type {
    enum Base { Error, Void, Bool, Int, Uint, Float, Struct, Opaque };
    Base base=Error;
    int columns=1, rows=1;
    std::string opaque;                 // sampler, image or atomic_uint keyword
    const StructDef* structDef=nullptr;
    std::vector<int> arrays;            // sizes, outermost first; 0 while unsized
    static Type of(Base base, int rows=1, int columns=1){Type t;t.base=base;t.rows=rows;t.columns=columns;return t;}
    bool isError() const {return base==Error;}
    bool isArray() const {return !arrays.empty();}
    bool isBasic() const {return !isArray() && base>=Bool && base<=Float;}       // scalar, vector or matrix
    bool isScalar() const {return isBasic() && columns==1 && rows==1;}
    bool isVector() const {return isBasic() && columns==1 && rows>1;}
    bool isMatrix() const {return isBasic() && columns>1;}
    bool isNumeric() const {return isBasic() && base!=Bool;}
    bool isIntegral() const {return isBasic() && (base==Int||base==Uint) && columns==1;}
    int components() const {return columns*rows;}
    Type element() const;               // outermost array dimension removed
    Type component() const;             // scalar type of a basic type
    bool containsOpaque() const;
    bool containsArray() const;
    bool contains(Base b) const;
};
bool operator==(const Type& a, const Type& b);
inline bool operator!=(const Type& a, const Type& b){return !(a==b);}
std::string toString(const Type& t);    // GLSL spelling: vec3, mat2x3, float[2], S
bool keywordType(const std::string& keyword, Type& out);   // void, scalars, vectors, matrices, opaque types

struct Field { std::string name; Type type; Loc loc; };
struct StructDef { std::string name; std::vector<Field> fields; Loc loc; bool block=false; };

// Constant values are flattened: struct fields and array elements in order, matrices column-major.
struct Scalar {
    Type::Base base=Type::Float;
    float f=0; std::int32_t i=0; std::uint32_t u=0; bool b=false;
    static Scalar of(float v){Scalar s;s.base=Type::Float;s.f=v;return s;}
    static Scalar of(std::int32_t v){Scalar s;s.base=Type::Int;s.i=v;return s;}
    static Scalar ofUint(std::uint32_t v){Scalar s;s.base=Type::Uint;s.u=v;return s;}
    static Scalar of(bool v){Scalar s;s.base=Type::Bool;s.b=v;return s;}
    bool operator==(const Scalar& o) const;
};
// Constructor conversion between basic types; false where GLSL leaves the result undefined.
bool convert(const Scalar& from, Type::Base to, Scalar& out);
struct Constant { Type type; std::vector<Scalar> values; };
using ConstantPtr=std::shared_ptr<const Constant>;
int slotCount(const Type& t);
}
