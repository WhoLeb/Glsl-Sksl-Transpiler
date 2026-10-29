#pragma once
#include "glsl_sksl/transpiler.hpp"
#include <string>
#include <vector>
namespace glsl_sksl {
struct Loc { unsigned line=0, column=0; };
struct DiagnosticSink {
    std::vector<Diagnostic> list;
    unsigned errors=0;
    void error(Loc at, std::string code, std::string message, std::string suggestion={}) {
        list.push_back({at.line,at.column,std::move(message),Severity::Error,std::move(code),std::move(suggestion)});
        ++errors;
    }
    void warning(Loc at, std::string code, std::string message, std::string suggestion={}) {
        list.push_back({at.line,at.column,std::move(message),Severity::Warning,std::move(code),std::move(suggestion)});
    }
};
}
