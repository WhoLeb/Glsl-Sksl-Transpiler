#pragma once
#include "diagnostics.hpp"
#include "types.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
// Syntax tree for GLSL ES 1.00-3.20. It records what was written, with positions; the semantic
// pass fills in the annotations marked "sema".
namespace glsl_sksl::sema { struct Symbol; struct Function; }
namespace glsl_sksl::ast {
struct Expr; struct Stmt; struct Decl; struct StructDecl;
using ExprPtr=std::unique_ptr<Expr>;
using StmtPtr=std::unique_ptr<Stmt>;
using DeclPtr=std::unique_ptr<Decl>;

struct LayoutId { std::string name; ExprPtr value; Loc loc; };   // value is null for layout(std140)
struct Qualifier {
    std::string word;               // const, in, out, uniform, highp, invariant, layout, ...
    std::vector<LayoutId> layout;   // layout only
    Loc loc;
};
struct Qualifiers {
    std::vector<Qualifier> list;    // in source order
    bool has(const std::string& word) const;
    bool empty() const {return list.empty();}
};
struct ArraySize { ExprPtr size; Loc loc; };                  // size is null for []
struct TypeSpec {
    std::string name;               // builtin type keyword or struct name; empty for an anonymous struct
    std::shared_ptr<StructDecl> structDef;                    // set for struct specifiers written in place
    std::vector<ArraySize> arrays;  // float[2][3]
    Loc loc;
};
struct Declarator {
    std::string name; Loc loc;
    std::vector<ArraySize> arrays;
    ExprPtr init;
    const sema::Symbol* symbol=nullptr;     // sema
};
struct FieldGroup { Qualifiers qualifiers; TypeSpec type; std::vector<Declarator> fields; };
struct StructDecl { std::string name; Loc loc; std::vector<FieldGroup> members; };

enum class ExprKind {
    IntLiteral, UintLiteral, FloatLiteral, BoolLiteral,
    Name,           // text
    Unary,          // text: + - ! ~ ++ --; operands[0]
    Postfix,        // text: ++ --; operands[0]
    Binary,         // text: operator; operands[0], operands[1]
    Assign,         // text: = += ...; operands[0] target, operands[1] value
    Ternary,        // operands: condition, true value, false value
    Call,           // text: function name; operands: arguments
    Constructor,    // type: constructed type, possibly an array type; operands: arguments
    Index,          // operands: base, index
    Field,          // text: member or swizzle; operands[0]
    Method,         // text: method name (length); operands[0] object, rest arguments
    Comma,          // operands[0], operands[1]
};
struct Expr {
    ExprKind kind; Loc loc;
    std::string text;               // literal spelling, name, operator, field or function name
    std::uint64_t intValue=0;       // Int/Uint literals: raw 32-bit pattern; Bool: 0 or 1
    double floatValue=0;
    std::vector<ExprPtr> operands;
    std::unique_ptr<TypeSpec> constructed; // Constructor only
    bool parenthesized=false;
    bool voidArgument=false;        // f(void)
    unsigned height=1;              // longest path to a leaf, bounded by the parser
    sema::Type type;                // sema: Error when the expression is invalid
    const sema::Symbol* symbol=nullptr;     // sema: Name
    const sema::Function* function=nullptr; // sema: Call
    bool constant=false;            // sema: a constant expression in the GLSL ES sense
    sema::ConstantPtr value;        // sema: folded value, when computable
    std::vector<int> swizzle;       // sema: Field on a vector
    int field=-1;                   // sema: Field on a struct or block
    Expr(ExprKind kind, Loc loc):kind(kind),loc(loc){}
    ~Expr();                        // iterative, so long operator chains cannot exhaust the stack
};

enum class StmtKind {
    Block,          // body; scoped=false for a function body or a loop body, which share the enclosing scope
    Declaration,    // decl
    Expression,     // expr
    Empty, If, Switch, Case, Default, For, While, DoWhile, Return, Break, Continue, Discard,
};
struct Stmt {
    StmtKind kind; Loc loc;
    std::vector<StmtPtr> body;      // Block statements; Switch body
    bool scoped=true;
    DeclPtr decl;                   // Declaration; condition declaration of For/While (bool b = x)
    ExprPtr expr;                   // Expression, If/While/DoWhile/For condition, Switch selector, Case label, Return value
    StmtPtr then, otherwise;        // If
    StmtPtr init;                   // For initialiser statement (declaration, expression or empty)
    ExprPtr step;                   // For iteration expression
    StmtPtr loop;                   // For/While/DoWhile body
    Stmt(StmtKind kind, Loc loc):kind(kind),loc(loc){}
};

struct Param {
    Qualifiers qualifiers; TypeSpec type; std::string name; Loc loc; std::vector<ArraySize> arrays;
    const sema::Symbol* symbol=nullptr;     // sema
};
struct Function {
    Qualifiers returnQualifiers; TypeSpec returnType;
    std::string name; Loc loc;
    std::vector<Param> params;
    bool voidParams=false;          // f(void)
    StmtPtr body;                   // null for a prototype
    const sema::Function* symbol=nullptr;   // sema
};
struct InterfaceBlock {
    Qualifiers qualifiers;
    std::string name; Loc loc;
    std::vector<FieldGroup> members;
    std::string instance; Loc instanceLoc;
    std::vector<ArraySize> instanceArrays;
};
enum class DeclKind {
    Variable,       // qualifiers type a=1, b[2]; no declarators for `struct S {...};`
    Function,
    Precision,      // precision highp float; qualifiers holds the precision
    Block,          // interface block
    Qualifier,      // layout(...) in;  invariant x, y;  (names may be empty)
};
struct Decl {
    DeclKind kind; Loc loc;
    Qualifiers qualifiers;
    TypeSpec type;
    std::vector<Declarator> declarators;
    std::unique_ptr<Function> function;
    std::unique_ptr<InterfaceBlock> block;
    std::vector<std::pair<std::string,Loc>> names; // Qualifier declarations
    Decl(DeclKind kind, Loc loc):kind(kind),loc(loc){}
};
struct TranslationUnit {
    int version=100;
    bool standardDerivatives=false;     // GLSL ES 1.00 with GL_OES_standard_derivatives enabled
    bool shaderTextureLod=false;        // GLSL ES 1.00 with GL_EXT_shader_texture_lod enabled
    std::vector<DeclPtr> decls;
    std::vector<Comment> comments;      // every comment of the source, for annotations
};

// GLSL spelling of an expression, for diagnostics.
std::string toGlsl(const Expr& e);
// S-expression rendering for tests and debugging.
std::string dump(const Expr& e);
std::string dump(const Stmt& s);
std::string dump(const Decl& d);
std::string dump(const TranslationUnit& unit);
}
