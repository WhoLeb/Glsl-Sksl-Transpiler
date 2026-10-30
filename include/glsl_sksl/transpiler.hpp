#pragma once
#include <map>
#include <string>
#include <vector>
namespace glsl_sksl {
enum class Severity { Error, Warning };
// One message about the conversion. Positions are 1-based; 0 when there is no such position.
//  - Warning: the shader converts, but something was replaced by a software implementation (codes
//    "software.*": derivatives, built-ins SkSL lacks, loops given a constant range, the texture
//    coordinate input, textures as child shaders, discard, GLSL ES 1.00 built-in variables) or an
//    input problem was ignored. The SkSL
//    position points at the replacement.
//  - Error: "Unsupported ..." for valid GLSL that SkSL cannot run, which needs manual adjustment
//    (the SkSL position points at the construct, written as close to the GLSL as possible), or
//    "Invalid ..." for GLSL that is not valid GLSL ES (then there is no SkSL).
struct Diagnostic {
    unsigned line=0, column=0;   // position in the GLSL source
    std::string message;
    Severity severity=Severity::Error;
    std::string code;            // stable identifier such as "unsupported.loop" or "software.derivative"
    std::string suggestion;      // how to fix or work around the problem; may be empty
    unsigned skslLine=0, skslColumn=0;  // position in Result::source of the code generated for it
    std::string sourceLine;      // text of the GLSL line at `line`
    std::string skslSourceLine;  // text of the SkSL line at `skslLine`
};
struct Result {
    enum class State {
        Success,                 // `source` is a working Skia runtime shader; there may be warnings
        Failure,                 // there are errors; `source` is SkSL that needs manual adjustment
                                 // (Skia rejects it), or empty when the GLSL itself is invalid
    };
    State state=State::Failure;
    std::string source;          // the generated SkSL; with errors it starts with one '#error' line per error
    std::vector<Diagnostic> diagnostics;   // every error and warning, sorted by GLSL position
    bool ok() const;             // true when no diagnostic is an error (state == Success)
    std::map<unsigned, std::vector<Diagnostic>> diagnosticsByLine() const; // keyed by GLSL line
};
// Converts a GLSL ES fragment shader (1.00, 3.00, 3.10, 3.20) to a Skia runtime shader; see README.md.
// Never throws: internal failures are reported as diagnostics.
Result transpile(const std::string& source);
// Multi-line human-readable rendering: position, message, GLSL line, SkSL line and suggestion.
std::string formatDiagnostic(const Diagnostic& diagnostic);
}
