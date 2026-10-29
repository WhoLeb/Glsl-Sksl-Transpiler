# GLSL to SkSL

Experimental dependency-free C++17 static library (plus a command-line tool) that converts GLSL ES fragment shaders to Skia runtime shaders. The goal is output that renders the same as the input; shaders the transpiler cannot convert faithfully are refused with a diagnostic instead of being approximated.

Build and test:

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build/glsl-to-sksl examples/solid.frag solid.sksl
```

## Using the library

Add the project with `add_subdirectory(glsl-sksl-transpiler)` and link `glsl_sksl::glsl_sksl`, or `cmake --install` it and use `find_package(glsl_sksl)`. The library is static (`glsl_sksl.lib` / `libglsl_sksl.a`); the tests and the command-line tool are only built when it is the top-level project. Build it with the same compiler, configuration and MSVC runtime (`/MD` vs `/MT`) as the consuming project.

```cpp
#include "glsl_sksl/transpiler.hpp"
glsl_sksl::Result r = glsl_sksl::transpile(glslSource);
if (r.ok()) use(r.source);
for (const auto& d : r.diagnostics) log(glsl_sksl::formatDiagnostic(d));
auto byLine = r.diagnosticsByLine();   // std::map<GLSL line, diagnostics>
```

`transpile` never throws and keeps no global state, so it can run on several threads at once. Each `Diagnostic` carries the GLSL line and column, severity, a stable `code` (for example `syntax.expected-token`), the message, a `suggestion`, the GLSL source line and the SkSL line (0 when no SkSL was generated). Messages begin with `Invalid` when the input is not valid GLSL ES and `Unsupported` when it is valid but cannot be converted faithfully. Warnings do not prevent conversion. `formatDiagnostic` renders:

```
4:1: error: Invalid syntax: expected ';' after the expression, found '}' [syntax.expected-token]
  GLSL 4 | }
         | ^
  SkSL - | (no SkSL generated)
  suggestion: add ';' after ')'
```

The CLI reads stdin when input is omitted or `-`, prints diagnostics in this format, and returns 1 for conversion errors or 2 for I/O/usage errors.

## Front end

`src/` contains a full GLSL ES front end:

- **Lexer** (`lexer.cpp`): line continuations, CR/LF/CRLF line ends, comments, and preprocessing numbers, with physical line/column positions for every token. Characters outside the GLSL character set (non-ASCII outside comments, a byte order mark, NUL, quotes, `$`, ...) are errors.
- **Preprocessor** (`preprocessor.cpp`): `#version` (100, 300 es, 310 es, 320 es; desktop GLSL is refused), object- and function-like macros with C rescanning rules, `#if/#ifdef/#ifndef/#elif/#else/#endif` with 32-bit integer arithmetic (undefined identifiers are errors, as GLSL ES requires), `#undef`, `#line`, `#error`, `#pragma`, `#extension`, and the predefined `__LINE__`, `__FILE__`, `__VERSION__`, `GL_ES` and `GL_FRAGMENT_PRECISION_HIGH`. Token pasting (`##`) is not supported. No extension is available in SkSL, so `require` fails and `enable` warns.
- **Classification** (`language.cpp`): version-specific keyword and reserved-word tables, literal validation (octal, hexadecimal, 32-bit range, `u` and `f` suffixes from 3.00, no `lf`), and the operators reserved in GLSL ES 1.00.
- **Parser** (`parser.cpp`, `ast.hpp`): recursive descent over the GLSL ES 3.20 grammar producing a syntax tree with positions: declarations, qualifiers and layouts, structs, interface blocks, arrays of every form, functions and prototypes, all statements and expressions with C precedence. Struct names are scoped type names. It recovers after an error and reports up to 25 independent problems.

- **Semantic pass** (`sema.cpp`, `types.cpp`, `builtins.cpp`): scoped symbols, a type for every expression, exact-match overload resolution against user functions and the built-in library of each version (GLSL ES has no implicit conversions), constructor and operator rules, l-values, constant expressions with folding (array sizes, case labels and constant indices are range-checked), qualifier, layout and default-precision rules, `main`, `switch` and `return` rules, missing definitions and recursion. Violations are reported as `Invalid`.
- **SkSL capability checks** (second half of `sema.cpp`): valid GLSL that SkSL runtime shaders cannot run faithfully is refused as `Unsupported` with a suggestion: loops outside GLSL ES 1.00 Appendix A (dynamic bounds, `while`, a modified index), non-constant indices, non-constant `%` and bitwise operators, `uint`, varyings, samplers, `discard`, bool and struct uniforms, outputs at a location other than 0, `gl_FragCoord.z`/`.w`, arrays of arrays, `default` before other labels, `continue` inside `switch`, functions that can end without returning, and built-ins with no exact equivalent (`isnan`/`isinf`, bit casts and packing).

- **Built-in replacements** (`helpers.cpp`): GLSL ES built-ins that SkSL runtime shaders lack get generated SkSL functions with the result the GLSL ES specification defines: `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `trunc`, `round`, `roundEven`, `modf`, `fma`, `determinant`, `transpose`, `outerProduct`, integer `abs`/`sign`/`min`/`max`/`clamp`, and `mix` with a bool selector. Only the overloads a shader uses are emitted, and native overloads (float `abs`, `mix` with a float weight, ...) are left alone. Which overloads SkSL lacks was measured with skslc; `tests/check_builtin_helpers.py` repeats that check. Built-ins without an exact equivalent stay refused: `isnan`/`isinf`, bit casts, packing and `frexp`/`ldexp`. Non-square matrices do not exist in SkSL and are refused.

- **Emitter** (`emitter.cpp`): writes SkSL from the analysed syntax tree. GLSL types map to SkSL types, precision, layout, `invariant` and `precise` qualifiers are dropped, functions are ordered so callees come first (prototypes are not needed), struct definitions are moved to global scope, uniform blocks are flattened into plain uniforms, constant `%` and bit operations are replaced by their values, constant `.length()` calls by the length, constructors drop the components GLSL ignores (`vec3(v4)` becomes `float3(v4.xyz)`), integer literals keep their 32-bit pattern, identifiers that SkSL reserves are renamed, and `out` parameters a function never writes are set to zero. `gl_FragColor` or the single `out vec4` becomes the returned color, and the GLSL `main` becomes the SkSL `main`.

  Variables, uniforms, functions and structs keep their GLSL names. Only identifiers that SkSL reserves are renamed (`float2` becomes `float2_`, `sk_x` becomes `_sk_x`). Names the emitter introduces are plain words (`coord`, `fragColor` for `gl_FragColor`, `fragCoord`, `quad`, `tmp1`, `x_L1` for the copy of `x` in another pixel of the quad, `glsl_sinh` for a built-in replacement), chosen so that they collide with no identifier of the shader: if the shader already uses `coord`, the parameter becomes `coord_2`.

  SkSL has no array constructors and cannot initialize, assign, compare, select (`?:`) or sequence (`,`) arrays and structs containing arrays, nor return them from functions. The emitter computes such values before the statement that uses them, element by element, keeping GLSL's evaluation order: initializations and assignments become element assignments (through a temporary when the value reads the target), constructors elsewhere become temporary arrays, `?:` becomes `if`/`else`, `&&`/`||` and `?:` whose skipped operand would have side effects become `if`s, operands evaluated earlier are computed first when the order could change a result, comparisons compare the elements (`a == b` becomes `(a[0] == b[0] && a[1] == b[1])`), functions return such values through an `out` parameter, and `const` arrays become variables initialized at the start of `main` with their constant elements folded where read. Refused: array returns from functions that compute derivatives, an `inout` argument that a later part of the call changes, and these values in `for` headers.

## Coordinates and derivatives

`gl_FragCoord.xy` is the coordinate parameter of the SkSL `main`, that is, Skia's local coordinates (pixel centres at .5, like GLSL). GLSL window coordinates have their origin at the bottom left, so to reproduce an image rendered with OpenGL draw with a matrix that flips the y axis (`canvas.translate(0, height); canvas.scale(1, -1)`); then `gl_FragCoord` matches exactly. Only `.x` and `.y` exist: SkSL has no fragment depth or 1/w.

`dFdx`, `dFdy` and `fwidth` (GLSL ES 3.00, or 1.00 with `GL_OES_standard_derivatives`) are computed in software with the semantics of GPU fine derivatives. GPUs run pixels in 2x2 quads and take a derivative as the difference between the two pixels of the quad's row (`dFdx`) or column (`dFdy`). The emitter makes every pixel evaluate the code that leads to a derivative for all four pixels of its quad (four lanes; the quad is the one GPUs use, aligned to even coordinates) and takes the same differences, so derivatives of derivatives also behave as on a GPU. Code after the last derivative runs for the pixel itself only. Functions that compute derivatives get a variant that takes and returns all four lanes; functions that use per-pixel globals get one copy per lane. `tests/check_derivatives.py` renders shaders with Skia's CPU backend and checks that every derivative equals the quad differences of the rendered argument values exactly.

GLSL leaves derivatives undefined where the pixels of a quad take different paths. The emitter supports what has a defined or universally implemented result and refuses the rest with a suggestion:

- A derivative inside an `if` or `switch` whose condition depends on the pixel is computed before the condition when its argument does not change inside the branches (the value GPUs produce in practice); otherwise it is refused.
- A `return`, `break` or `continue` under a pixel-dependent condition before a derivative is refused (`unsupported.derivative-control`). Loops whose exit depends on the pixel are fine when they contain no derivative, and so are returns after the last derivative.
- A derivative whose argument has side effects, or that cannot be computed apart from the side effects of its statement, is refused (`unsupported.derivative`).

The cost is up to four evaluations of the code before the last derivative; lanes whose values are never used are dead code that optimizing compilers can remove.

Nesting is limited to 128 levels of statements, parentheses, call arguments and unary operators, and expression trees to 256 levels. Deeper code is refused as unsupported; SkSL itself rejects anything deeper than about 50. Within these limits the whole pipeline needs at most about 256 KB of stack in an optimised build and 384 KB in an unoptimised one.

A SkSL-to-GLSL line map is the next step; see [the plan](docs/PLAN.md). `tests/run_corpus.py` compiles every converted fixture with Skia (skia-python).

See [fragment test setup](tests/README.md) for the fixture corpus and real Skia compiler validation. Enable strict CTest integration with `-DGLSL_SKSL_COMPILER_TESTS=ON`.

Output must already satisfy Skia's premultiplied color contract; no automatic color-space or alpha conversion occurs.

No external dependencies are used. Future Git dependencies must be added as actual submodules, never copied nested repositories.
