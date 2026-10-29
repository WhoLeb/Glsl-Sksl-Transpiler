#pragma once
#include <map>
#include <string>
#include <vector>
namespace glsl_sksl {
enum class Severity { Error, Warning };
struct Diagnostic {
    unsigned line=0, column=0;   // 1-based position in the GLSL source; 0 when not tied to a position
    std::string message;         // starts with "Invalid" for non-conforming GLSL, "Unsupported" for valid GLSL that is refused
    Severity severity=Severity::Error;
    std::string code;            // stable identifier such as "syntax.expected-token"
    std::string suggestion;      // how to fix or work around the problem; may be empty
    unsigned skslLine=0;         // 1-based line in Result::source; 0 when no SkSL was emitted for it
    std::string sourceLine;      // text of the GLSL line at `line`
};
struct Result {
    std::string source;          // empty unless ok()
    std::vector<Diagnostic> diagnostics;
    bool ok() const;             // true when no diagnostic is an error
    std::map<unsigned, std::vector<Diagnostic>> diagnosticsByLine() const; // keyed by GLSL line
};
// Converts a GLSL ES fragment shader (1.00, 3.00, 3.10, 3.20) to a Skia runtime shader; see README.md.
// Never throws: internal failures are reported as diagnostics.
Result transpile(const std::string& source);
// Multi-line human-readable rendering: position, message, GLSL line, SkSL line and suggestion.
std::string formatDiagnostic(const Diagnostic& diagnostic);
}
