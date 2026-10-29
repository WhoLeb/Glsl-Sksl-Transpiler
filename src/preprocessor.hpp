#pragma once
#include "lexer.hpp"
#include <string>
#include <vector>
namespace glsl_sksl {
struct ExtensionDirective { std::string name, behavior; Loc loc; };
struct PreprocessOutput {
    std::vector<Token> tokens;  // macro-expanded tokens of active groups, still unclassified
    int version=100;
    bool versionDeclared=false;
    std::vector<ExtensionDirective> extensions;
    std::vector<std::string> pragmas;
};
// Directives and macro expansion per GLSL ES section 3.4. Only the version-independent
// behaviour lives here; keyword and literal checks happen when tokens are classified.
PreprocessOutput preprocess(const LexOutput& lexed, DiagnosticSink& sink);
// Turns preprocessed tokens into parser tokens: keywords, checked literals, reserved words and
// operators for the shader's version. Appends an End token.
std::vector<Token> classify(PreprocessOutput& preprocessed, const LexOutput& lexed, DiagnosticSink& sink);
}
