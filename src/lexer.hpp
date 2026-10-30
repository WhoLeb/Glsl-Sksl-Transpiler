#pragma once
#include "diagnostics.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace glsl_sksl {
enum class TokenKind { Identifier, Keyword, Number, IntLiteral, UintLiteral, FloatLiteral, Punct, Newline, Invalid, End };
struct Token {
    TokenKind kind=TokenKind::End;
    std::string text;           // spelling; literals keep their written form without the suffix after classification
    Loc loc;                    // physical position of the first character, or of the macro invocation
    bool spaceBefore=false;
    std::string invalidReason;  // Invalid only
    std::string macro;          // outermost macro whose expansion produced this token
    std::vector<std::string> hideSet; // macros that must not expand this token again
    std::uint64_t intValue=0;
    double floatValue=0;
};
struct LexOutput {
    std::vector<Token> tokens;  // ends with a Newline token; no End token
    std::vector<Loc> continuations; // positions of backslash-newline line splices
    std::vector<Comment> comments;
};
// Phase 1-3 of compilation: line splicing, comment removal and preprocessing-token formation.
// Characters that cannot form tokens become Invalid tokens, reported only if they survive preprocessing.
LexOutput lex(const std::string& source, DiagnosticSink& sink);
}
