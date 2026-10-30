#pragma once
#include "glsl_sksl/transpiler.hpp"
#include <string>
#include <vector>
namespace glsl_sksl {
struct Loc { unsigned line=0, column=0; };
// A source comment, kept for annotations such as '// @minmax{0, 32}'.
struct Comment {
    Loc loc;                    // physical position of the opening '//' or '/*'
    unsigned endLine=0;         // physical line of the last character
    std::string text;           // without the comment delimiters
    bool ownLine=false;         // nothing but whitespace precedes it on its first line
};
struct DiagnosticSink {
    std::vector<Diagnostic> list;
    unsigned errors=0;
    // Per diagnostic (same index as `list`): text to look for in the SkSL generated for its GLSL
    // position, such as the name of a replacement; empty means the GLSL token at the position.
    std::vector<std::string> skslHints;
    void hint(std::string text) {
        skslHints.resize(list.size());
        if(!list.empty())skslHints.back()=std::move(text);
    }
    void error(Loc at, std::string code, std::string message, std::string suggestion={}) {
        list.push_back({at.line,at.column,std::move(message),Severity::Error,std::move(code),std::move(suggestion)});
        ++errors;
    }
    void warning(Loc at, std::string code, std::string message, std::string suggestion={}) {
        list.push_back({at.line,at.column,std::move(message),Severity::Warning,std::move(code),std::move(suggestion)});
    }
};
}
