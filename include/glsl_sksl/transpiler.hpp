#pragma once
#include <map>
#include <string>
#include <vector>
#include <utility>
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
// What the application provides for a GLSL sampler uniform, by the SkSL names it has to set; one
// per element of an array of samplers. The names may differ from the GLSL name (a generated size
// uniform avoids every identifier of the shader), so look them up here rather than spelling them.
struct Texture {
    std::string sampler;         // the GLSL sampler uniform
    int element=-1;              // the element of an array of samplers; -1 for a single sampler
    std::string type;            // "sampler2D", "samplerCube", "sampler3D" or "sampler2DArray"
    std::vector<std::string> children;  // its child shaders: one, or six faces for a cube map (+X, -X, +Y, -Y, +Z, -Z)
    std::string sizeUniform;     // uniform to set to the size in texels: float2 width, height (of a face for a cube
                                 // map), float3 width, height, depth or layers for 3D and array textures; empty
                                 // when the shader does not need the size
};
struct Result {
    enum class State {
        Success,                 // `source` is a working Skia runtime shader; there may be warnings
        Failure,                 // there are errors; `source` is SkSL that needs manual adjustment
                                 // (Skia rejects it), or empty when the GLSL itself is invalid
    };
    State state=State::Failure;
    std::vector<std::pair<std::string,std::string>> renamedIdentifiers; // GLSL identifiers renamed in SkSL, including uniforms
    std::vector<Texture> textures;         // the child shaders and size uniforms of each sampler, in declaration order
    std::string source;          // the generated SkSL; with errors it starts with one '#error' line per error
    std::vector<Diagnostic> diagnostics;   // every error and warning, sorted by GLSL position
    bool ok() const;             // true when no diagnostic is an error (state == Success)
    std::map<unsigned, std::vector<Diagnostic>> diagnosticsByLine() const; // keyed by GLSL line
};
// Converts a GLSL ES fragment shader (1.00, 3.00, 3.10, 3.20) to a Skia runtime shader; see README.md.
// Never throws: internal failures, memory exhaustion included, give state Failure with a
// diagnostic when there is memory for one. Exception: with MSVC's iterator debugging (the Debug
// default) the standard containers' noexcept constructors allocate, and the standard library
// terminates the program when that allocation fails.
Result transpile(const std::string& source) noexcept;
// Multi-line human-readable rendering: position, message, GLSL line, SkSL line and suggestion.
std::string formatDiagnostic(const Diagnostic& diagnostic);
}
