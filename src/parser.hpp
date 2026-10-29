#pragma once
#include "ast.hpp"
#include "preprocessor.hpp"
#include <memory>
#include <string>
#include <vector>
namespace glsl_sksl {
// Reports the first unmatched ( [ { ) ] } and returns false if there is one.
bool checkDelimiters(const std::vector<Token>& tokens, DiagnosticSink& sink);
// Recursive-descent parser for the GLSL ES 3.20 grammar (1.00-3.10 are subsets; words that are
// not keywords in older versions are already identifiers after classification).
std::unique_ptr<ast::TranslationUnit> parse(const std::vector<Token>& tokens, int version, DiagnosticSink& sink);

struct ParsedShader {
    int version=100;
    bool versionDeclared=false;
    std::vector<ExtensionDirective> extensions;
    std::vector<std::string> pragmas;
    std::vector<Token> tokens;                    // classified tokens ending with End
    std::unique_ptr<ast::TranslationUnit> unit;   // null when lexing or preprocessing failed
};
// Front end: lex, preprocess, classify, check delimiters and parse.
ParsedShader parseShader(const std::string& source, DiagnosticSink& sink);
}
