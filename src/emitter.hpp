#pragma once
#include "ast.hpp"
#include "lexer.hpp"
#include "sema.hpp"
#include <string>
#include <vector>
// SkSL emitter for a translation unit that passed semantic analysis without errors. Problems found
// while lowering are reported to the sink; the returned source is only meaningful without errors.
//
// Derivatives (dFdx, dFdy, fwidth) are computed in software: each pixel also evaluates the code
// that leads to a derivative for the other three pixels of its 2x2 quad (the lanes), and takes the
// differences the way GPUs compute fine derivatives. gl_FragCoord.xy is the coordinate parameter
// of the SkSL main.
namespace glsl_sksl {
// The GLSL source of a statement or declaration: where it starts and the last line it covers.
struct SourceSpan { Loc start; unsigned endLine=0; };
// `origins` receives, per line of the returned SkSL, the span of the statement or declaration it
// was generated from (empty for generated code such as built-in replacements).
std::string emitSkSL(const ast::TranslationUnit& unit, const sema::Program& program, const std::vector<Token>& tokens, DiagnosticSink& sink,
    std::vector<SourceSpan>& origins);
}
