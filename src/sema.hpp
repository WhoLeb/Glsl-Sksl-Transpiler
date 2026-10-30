#pragma once
#include "ast.hpp"
#include "types.hpp"
#include <deque>
#include <memory>
#include <string>
#include <vector>
// Semantic analysis: resolves names, types every expression, folds constants and enforces the
// GLSL ES rules beyond the grammar ("Invalid ..."). A second pass then refuses what SkSL runtime
// shaders cannot express faithfully ("Unsupported ...") with a suggestion.
namespace glsl_sksl::sema {
enum class Storage { Global, Local, Parameter, Uniform, Input, Output, Buffer, BuiltIn };
enum class Direction { None, In, Out, InOut };

struct Symbol {
    std::string name; Type type; Loc loc;
    Storage storage=Storage::Local;
    Direction direction=Direction::None;  // parameters
    bool constQualified=false;
    bool constantExpression=false;        // usable in constant expressions (const with a constant initializer)
    bool readOnly=false;
    std::string readOnlyReason;
    ConstantPtr value;                    // folded value of a constant expression, when computable
    std::string unsupported;              // built-ins that SkSL cannot provide
    int location=-1;                      // layout(location=N)
    // Uniforms: '// @minmax{a, b} @init{v}' comment on the declaration's line or the line before.
    // The range lets loops bounded by the uniform run over a constant range (see loopIssues).
    std::string annotation;               // the annotation as written, repeated in the SkSL
    bool hasRange=false, hasInit=false;
    double minimum=0, maximum=0, initial=0;
    // Inputs: the texture coordinate input (the one at location 0, or the only input), which holds
    // UV coordinates from 0 to 1 across the drawn area. The SkSL computes it from the coordinate
    // parameter of main divided by the iResolution uniform (see the emitter).
    bool textureCoordinate=false;
    // Locals the transpiler introduces (the counter of a rewritten loop); the emitter names them.
    bool generated=false;
    // Samplers filtered in software (sampler3D, sampler2DArray): '// @filter{nearest|linear}
    // @wrap{repeat|clamp|mirror}' gives the GL sampler state, linear and repeat by default.
    bool nearest=false;
    std::string wrap="repeat";
};

struct Param { Type type; Direction direction=Direction::In; bool constQualified=false; std::string name; };
struct Function {
    std::string name; Type returnType; std::vector<Param> params;
    Loc loc;
    bool builtin=false;
    int version=100;                      // built-ins: first GLSL ES version that has it
    int lastVersion=320;                  // built-ins: last GLSL ES version that has it
    std::string unsupported;              // built-ins without a faithful SkSL equivalent
    const ast::Function* definition=nullptr;
    const ast::Function* declaration=nullptr;   // first prototype or the definition
    std::vector<const Function*> callees;
    bool called=false;
};
std::string signature(const Function& f);   // name(type, type)

struct Program {
    int version=100;
    std::deque<Symbol> symbols;
    std::deque<Function> functions;
    std::deque<StructDef> structs;
    const Function* main=nullptr;
    std::vector<const Symbol*> outputs, uniforms, inputs;
    bool writesFragColor=false, writesFragData=false;
};

// Built-in functions of every version, as an immutable shared table.
const std::vector<const Function*>& builtinOverloads(const std::string& name);
bool isBuiltinFunctionName(const std::string& name, int version);
// Texture, image, atomic and synchronisation built-ins of a version, which are refused as a group.
bool isUnsupportedBuiltinName(const std::string& name, int version);
// dFdx, dFdy and fwidth, which the emitter computes from neighbouring pixels.
bool isDerivative(const std::string& name);

std::unique_ptr<Program> analyze(ast::TranslationUnit& unit, DiagnosticSink& sink);
}
