# Implementation plan

Goal: SkSL output that renders the same as the GLSL input, within floating-point precision. Anything that cannot be converted faithfully is refused with a diagnostic giving the GLSL line, the SkSL line, the problem and a suggestion.

1. C++17 static library, CLI, CMake and regression tests. **Done.**
2. Front end: lexer, preprocessor, version-aware token classification and a GLSL ES 3.20 parser producing a positioned syntax tree, with structured diagnostics. **Done.**
3. Semantic pass: symbol tables, types for every expression, overload resolution, GLSL ES conversion rules (no implicit int to float), constant expressions, qualifier and scope rules, and checks that a loop or index fits SkSL's ES2 restrictions (dynamic loop bounds and similar are refused with a suggestion). **Done.**
4. AST emitter (`src/emitter.cpp`). **Done:** it replaced the token-level lowering; it folds constant `%` and bit operations, drops `invariant`/`precise`, substitutes constant `.length()`, flattens uniform blocks (row-major blocks with matrices are refused), trims constructor arguments, supports structs and `in`/`out`/`inout` parameters, renames identifiers SkSL reserves, and computes `dFdx`/`dFdy`/`fwidth` in software over 2x2 quad lanes (see the README). **Remaining:** a SkSL-line to GLSL-line map (`Diagnostic::skslLine`), and lowering of array constructors, which SkSL runtime shaders reject (initialize element by element; constant global arrays need initialization at the start of `main`).
5. Coordinate and sampling contracts. **Done:** `gl_FragCoord.xy` is the local coordinate (drawn with a y flip it equals GLSL window coordinates). **Remaining:** child-shader sampling for samplers; texture lookups would get lanes like the rest, so derivatives of sampled values work unchanged.
6. Validation: compile every emitted shader with Skia, and add numeric checks of generated helpers against reference implementations.
