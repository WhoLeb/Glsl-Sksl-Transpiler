# GLSL to SkSL

Experimental dependency-free C++17 static library (plus a command-line tool) that converts GLSL ES fragment shaders to Skia runtime shaders. The goal is output that renders the same as the input, and a conversion for every valid shader: what SkSL lacks is replaced by a software implementation, reported as a warning that names any assumption the replacement makes. Constructs that have no replacement yet are refused with a diagnostic instead of being approximated.

The repository has three CMake parts:

- `CMakeLists.txt`: the library `glsl_sksl` (sources in `src/`, public header in `include/`), with its install and package rules.
- `cli/CMakeLists.txt`: the command-line tool `glsl-to-sksl` (`cli/main.cpp`).
- `tests/CMakeLists.txt`: the tests.

Build and test everything (the tool and the tests are on when this is the top-level project):

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build/glsl-to-sksl examples/solid.frag solid.sksl
```

## Using the library

As a Git submodule (or any copy of the repository), add it with `add_subdirectory` and link the library. Only the library is built: the tool and the tests are off when the repository is not the top-level project, and so are its install rules.

```cmake
add_subdirectory(external/glsl-sksl-transpiler)
target_link_libraries(my_app PRIVATE glsl_sksl::glsl_sksl)
```

The options `GLSL_SKSL_BUILD_TOOLS`, `GLSL_SKSL_BUILD_TESTS` and `GLSL_SKSL_INSTALL` turn the tool, the tests and the install rules on or off (each defaults to whether this is the top-level project). Alternatively, `cmake --install` the library and use `find_package(glsl_sksl)` with the same target. The tool also builds on its own against an installed library: `cmake -S cli -B build-cli -DCMAKE_PREFIX_PATH=<install prefix>`.

The library is static (`glsl_sksl.lib` / `libglsl_sksl.a`). Build it with the same compiler, configuration and MSVC runtime (`/MD` vs `/MT`) as the consuming project.

```cpp
#include "glsl_sksl/transpiler.hpp"
glsl_sksl::Result r = glsl_sksl::transpile(glslSource);
if (r.ok()) use(r.source);
for (const auto& d : r.diagnostics) log(glsl_sksl::formatDiagnostic(d));
auto byLine = r.diagnosticsByLine();   // std::map<GLSL line, diagnostics>
```

`transpile` never throws and keeps no global state, so it can run on several threads at once. A conversion ends in one of three ways:

- **Converted**: `state == Success` with no diagnostics.
- **Converted with replacements**: `state == Success` with warnings. Something SkSL lacks was replaced by a software implementation: `software.derivative` (dFdx, dFdy, fwidth), `software.builtin` (built-ins such as `sinh` or integer `min`, computed by generated functions), `software.loop` (a loop given a constant range, including `while` and `do-while`), `software.operator` (integer `%` and bit operations, computed by generated functions, and float division by a constant zero), `software.input` (the texture coordinate input), `software.texture` (samplers and lookups, as child shaders), `software.discard` (`discard`, as a transparent result), `software.missing-return` (a function that can end without returning a value returns zero), and the GLSL ES 1.00 built-in variables `software.output` (`gl_FragData[0]`), `software.front-facing`, `software.point-coord`, `software.depth-range` and `software.constant` (`gl_Max*` constants). Each warning points at the replacement in the SkSL.
- **Needs manual adjustment**: `state == Failure` with errors: `Unsupported` for valid GLSL that SkSL cannot run, `Invalid` for GLSL that breaks the GLSL ES rules. The SkSL is still generated, with those parts written as close to the GLSL as possible, and starts with one `#error GLSL line:column: message [code]` line per error. SkSL has no `#error`, so Skia rejects such source for certain: a refused construct never compiles into a shader that renders differently. Only a shader that does not parse (or has no usable `main`) produces no SkSL, since there is nothing to write.

Each `Diagnostic` carries the severity, a stable `code` (for example `unsupported.loop`), the message, a `suggestion`, the GLSL line and column with the text of the GLSL line, and the SkSL line and column with the text of the SkSL line (0 and empty when no SkSL corresponds). Every problem is reported in one pass (the parser stops after 25), sorted by GLSL position. `formatDiagnostic` renders:

```
11:5: error: Unsupported for loop: unknown iteration count: no @minmax on 'n' [unsupported.loop]
  GLSL 11 |     for (int j = 0; j < n; j++) s += 2.0;
          |     ^
  SkSL 47 |     for (int j = 0; j < n; j++) {
          |     ^
  suggestion: annotate it: 'uniform int n; // @minmax{0, 32} @init{0}'
```

Invalid GLSL shows `SkSL - | (no SkSL generated)` instead.

The CLI reads stdin when input is omitted or `-`, prints diagnostics in this format to stderr, writes whatever SkSL was produced (also with errors), and returns 1 for conversion errors or 2 for I/O/usage errors.

## The texture coordinate input

SkSL runtime shaders have no inputs: the only per-pixel value is the coordinate parameter of `main`. The shaders this transpiler is written for receive UV coordinates (0 to 1 across the drawn area) in their first input, for example `layout(location = 0) in highp vec4 uv_coords;` (GLSL ES 3.10 or later allows a location on a fragment input). That input (the one at location 0, or the only input; a `vec2`, `vec3` or `vec4`) is computed at the start of `main` from the coordinate and the `iResolution` uniform, with z = 0 and w = 1:

```sksl
uniform float2 iResolution;
float4 uv_coords;
half4 main(float2 coord) {
    uv_coords = float4(coord / iResolution, 0.0, 1.0);
```

The application sets `iResolution` to the width and height of the drawn area (`.xy` is used when it is a `vec3` or `vec4`; a shader without an `iResolution` uniform gets `uniform float2 iResolution;` in its SkSL, the type the target application uses). The conversion reports the warning `software.input`. Derivatives of the input are exact (one pixel apart, `dFdx(uv) = 1 / width`). Skia's coordinates have y growing downwards; draw with a flipped matrix (see below) when the UV of the original pipeline grows upwards. Other inputs are refused (`unsupported.input`).

The coordinate is in x and y. A shader that declares the input as a `vec3` or `vec4` but uses it where a `vec2` is needed (`uv_coords * 2.0 - vec2(1.0)`, which GLSL rejects) is converted with the input taken as a `vec2` holding x and y: when the analysis finds GLSL errors, it runs again with the input typed as a `vec2`, and that result is kept when it removes errors without adding any, with the warning `sema.input-vec2`. Shaders that are valid as written are not changed, and a shader that also uses `.z`, `.w` or the whole `vec4` keeps its errors.

## Textures

SkSL runtime shaders have no textures; they sample child shaders. A `uniform sampler2D t;` becomes `uniform shader t;`, and `texture(t, uv)` (`texture2D` in GLSL ES 1.00) becomes `float4(t.eval(uv * tSize))`; `textureProj` divides by the last component first. Both report the warning `software.texture`. GLSL samples with coordinates from 0 to 1 across the image, and a child shader with no local matrix takes texel coordinates, so the shader multiplies by the size of the texture, `uniform float2 tSize;` (see **Size** below). The application creates the child without a matrix:

```cpp
auto child = image->makeRawShader(tileX, tileY, sampling);
```

A raw shader returns the texels without color-space conversion or premultiplication, as GLSL does. Use the wrap and filter modes of the GL sampler, without mipmaps: a child shader's `eval` has no level of detail. Rendered with Skia's CPU backend, a shader that copies `texture(t, uv)` reproduces the image exactly. One difference remains at the exact boundary between two texels with nearest filtering, where Skia picks the lower texel and GL the upper one.

Every lookup function of GLSL ES 1.00 to 3.20 is replaced (`software.texture`), for `sampler2D`, `samplerCube`, `sampler3D` and `sampler2DArray`:

- **Level of detail**: child shaders have no mipmaps, so `textureLod`, `textureGrad`, a bias argument, and the GLSL ES 1.00 `texture2DLodEXT`/`texture2DGradEXT` family (`GL_EXT_shader_texture_lod`) sample the base level. That matches GL for a texture without mipmaps, which this contract requires; the warning says that the level was ignored.
- **Size**: every lookup except `texelFetch` of a 2D texture needs the size of the texture in texels. The SkSL then declares `uniform float2 tSize;` next to the child (named after the sampler), which the application sets; `Result::textures` gives its name. `texelFetch` reads the texel centre, where any filter returns the texel itself; `textureGather` reads the four texels of the linear footprint at their centres.
- **Cube maps**: `uniform samplerCube sky;` becomes six children `sky_px`, `sky_nx`, `sky_py`, `sky_ny`, `sky_pz`, `sky_nz` (+X, -X, +Y, -Y, +Z, -Z), each created like a 2D texture and clamped at its edges. The shader chooses the face and the coordinates in it with the GLSL ES table. With linear filtering, texels within half a texel of a face edge differ from GL, which filters across the edge.
- **3D and array textures**: one child holds the slices (or layers) stacked from top to bottom, slice k in rows k × height to (k + 1) × height − 1, created without a matrix; `tSize` holds width, height and depth (or the number of layers). The shader reads texel centres and filters and wraps itself, as the GL sampler states in an annotation on the sampler: `uniform sampler3D vol; // @filter{linear} @wrap{repeat}` (`nearest` or `linear`; `repeat`, `clamp` or `mirror`; linear and repeat by default). A layer index is rounded and clamped as in GL.
- **Arrays of samplers** become one child per element (`t_0`, `t_1`, ...); an index that is not constant selects among them.
- **Samplers as function parameters**: SkSL functions cannot take child shaders, so the function gets a copy for each sampler it is called with (`f_t_`, `f_t1`, ...; see below for the `_`); an element chosen at run time selects among the copies.

`tests/check_textures.py` renders these lookups with Skia's CPU backend and compares them with GL's rules computed independently. They match exactly, up to float rounding (1e-6 for software filtering).

Refused (`unsupported.*`): depth-comparison (`sampler2DShadow`, ...), integer (`isampler*`, `usampler*`), multisample, buffer and cube-array samplers, whose texels cannot come through a color child shader, and images.

The target application passes the SkSL through regular expressions of its own (`SkiaRuntimeEffect::GlslToSksl`): `child(...)` becomes `child.eval(...)` for each `uniform shader child;`, `texture(a, b)` becomes `a.eval(b)`, and an `f` after a decimal number is dropped. They also match at the end of a longer name, so the SkSL avoids what they would change. Functions, structs and generated names that end in `texture` or in the name of a child shader get a trailing `_`: `blurtexture` becomes `blurtexture_`, and the copy of `textureSK(sampler2D)` for `inputTexture` is `textureSK_inputTexture_`. The child keeps its name, which the application binds. Only a child whose name ends a built-in function (a sampler `s` and `abs(x)`) is renamed itself (`s_`). Renamed GLSL names appear in `renamedIdentifiers`.

## Discard

SkSL runtime shaders cannot discard. `discard` in `main` returns transparent black (`return half4(0.0);`), which leaves the destination unchanged under source-over blending (Skia's default, `SkBlendMode::kSrcOver`), as discard does; with other blend modes the pixel is not left alone. Outside `main`, `discard` sets a flag that `main` checks when it returns, and the rest of the shader runs without effect on the result. A pixel of the 2x2 quad that discards keeps running for the derivatives of its neighbours, like a GPU helper invocation. Reported as `software.discard`. A function that can then reach its end without a `return` gets `return 0;` there, the value GLSL leaves undefined (`software.missing-return` when a path really has no return).

## GLSL ES 1.00 built-in variables

`gl_FragData[0]` is the returned color, like `gl_FragColor` (GLSL ES 1.00 has `gl_MaxDrawBuffers = 1`). `gl_FrontFacing` is `true`: Skia draws have no back faces. `gl_PointCoord` treats the drawn area as the point sprite: `(x / width, 1 - y / height)` from the coordinate and `iResolution` (declared when the shader has none), with y measured as for `gl_FragCoord`. `gl_DepthRange` has its default values (near 0, far 1, diff 1). The device-dependent constants `gl_Max*` and `gl_MinProgramTexelOffset` take the minimum the specification of the shader's version allows. Each reports a `software.*` warning naming the assumption.

## Loops with a bound that is not constant

SkSL runtime shaders only run `for` loops whose iteration count is known when the shader is compiled (GLSL ES 1.00 Appendix A). A loop whose start or bound is not constant is rewritten when the range of that value is known: the loop runs over a constant range that covers it, and guards at the start of the body skip the iterations before the real start and leave at the real bound, so exactly the GLSL iterations run:

```glsl
uniform int radius; // @minmax{0, 32} @init{4}
for (int i = -radius; i <= radius; i++) s += f(i);
```
```sksl
uniform int radius;  // @minmax{0, 32} @init{4}
for (int i = (-32); i <= 32; i++) {
    if (i < -radius) { continue; }
    if (!(i <= radius)) { break; }
    s += f(i);
}
```

Ranges come from constants, the indices of enclosing loops, locals and parameters the loop does not change, `min`, `max`, `clamp`, `abs`, `floor`, `ceil`, `round`, `?:`, `int()`/`float()` and `+ - * /`. These are exact and need nothing from the shader: `i < min(n, 16)` and the triangular `for (int j = 0; j < i; j++)` convert as they are. A local ranges over every value assigned to it (its initializer and its `=` assignments, wherever they are), and a parameter over the arguments of every call as well, so a loop bounded by a parameter converts when the calls pass constants or values of known range: with `fbm(p, 5)` and `fbm(q, 3)`, `for (int i = 0; i < octaves; i++)` runs over `i < 5` and leaves at the real bound. A local changed in another way (`+=`, `++`, an `out` argument) has no known range. A uniform has no known range unless its declaration carries the annotation `// @minmax{MIN, MAX} @init{VALUE}` (after the uniform on the same line, or alone on the line before it; `@init` is optional and is repeated in the SkSL for the application). Every rewritten loop gets the warning `software.loop`. The annotation is an assumption the application must keep, and the warning of a loop that relies on it says so: with values outside the range the loop would run fewer iterations than in GLSL. Malformed annotations are ignored with the warning `annotation.invalid`.

A condition that joins the comparison of the index with other tests, as in `for (int i = 0; i < 16 && t < 2.0; i++)`, keeps the comparison and leaves the loop when the rest fails, tested at the start of the body (`if (!(t < 2.0)) break;`), so `i` stays a loop index (usable as an array index). When the comparison is not the first part, the parts before it must have no side effects; otherwise the loop is rewritten as below.

### While, do-while and other loops

SkSL runtime shaders have no `while` or `do-while` loops. These loops, and `for` loops outside the form above (no single declared index, a condition that does not compare the index, an index changed in the body, ...), become a `for` loop over a constant number of iterations that keeps the original condition, so it leaves exactly when the GLSL loop does; `continue` still reaches the condition (and the step of a `for` loop):

```glsl
int i = 0;
while (i < 10) { s += f(i); i++; }
```
```sksl
int i = 0;
for (int iteration = 0; iteration < 11; iteration++) {
    if (!(i < 10)) { break; }
    { s += f(i); i++; }
}
```

The number of iterations comes from one of:

- **A counter** (no assumption): the condition (or one part of a condition joined with `&&`, which the other parts do not change the counter in) compares a local `int` or `float` with a bound of known range (as above, `@minmax` uniforms included), and the counter starts at a value of known range (the `for` initializer, or its declaration right before the loop, unchanged until it) and moves towards the bound by a constant once in every iteration: in the `for` step, as `i++` in the condition, or in a statement of the body that comes before any `continue`. Exactly the GLSL iterations run (`software.loop`).
- **An annotation** `// @maxiter{N}` on the line of the loop or alone on the line before it, giving the most iterations the loop runs, for loops without a counter such as a ray march (`while (d > eps) { // @maxiter{128}`). The warning (`software.loop`) states the assumption: after N iterations the SkSL loop stops, so with more iterations in GLSL the results differ.

A loop with neither is refused (`unsupported.loop`) with a suggestion to annotate it.

Skia allows fewer than 100000 iterations in a loop, also with a constant range (`for (int i = 0; i < 200000; i++)`). Longer loops become two nested loops that count the iterations together, `iteration = outer * C + inner`, with the original condition and step inside; every `break` of the body sets a flag that leaves the outer loop too, and `continue` reaches the next iteration as before (`software.loop`). The original index is then an ordinary variable, which SkSL does not accept as an array index. Refused: loops over more than 99999 × 99999 iterations. Malformed or unused `@maxiter` annotations are reported as `annotation.invalid` and `annotation.unused`.

## Front end

`src/` contains a full GLSL ES front end:

- **Lexer** (`lexer.cpp`): line continuations, CR/LF/CRLF line ends, comments, and preprocessing numbers, with physical line/column positions for every token. Characters outside the GLSL character set (non-ASCII outside comments, a byte order mark, NUL, quotes, `$`, ...) are errors.
- **Preprocessor** (`preprocessor.cpp`): `#version` (100, 300 es, 310 es, 320 es; desktop GLSL is refused), object- and function-like macros with C rescanning rules, `#if/#ifdef/#ifndef/#elif/#else/#endif` with 32-bit integer arithmetic (undefined identifiers are errors, as GLSL ES requires), `#undef`, `#line`, `#error`, `#pragma`, `#extension`, and the predefined `__LINE__`, `__FILE__`, `__VERSION__`, `GL_ES` and `GL_FRAGMENT_PRECISION_HIGH`. Token pasting (`##`) is not supported. No extension is available in SkSL, so `require` fails and `enable` warns.
- **Classification** (`language.cpp`): version-specific keyword and reserved-word tables, literal validation (octal, hexadecimal, 32-bit range, `u` and `f` suffixes from 3.00, no `lf`), and the operators reserved in GLSL ES 1.00.
- **Parser** (`parser.cpp`, `ast.hpp`): recursive descent over the GLSL ES 3.20 grammar producing a syntax tree with positions: declarations, qualifiers and layouts, structs, interface blocks, arrays of every form, functions and prototypes, all statements and expressions with C precedence. Struct names are scoped type names. It recovers after an error and reports up to 25 independent problems.

- **Semantic pass** (`sema.cpp`, `types.cpp`, `builtins.cpp`): scoped symbols, a type for every expression, exact-match overload resolution against user functions and the built-in library of each version (GLSL ES has no implicit conversions), constructor and operator rules, l-values, constant expressions with folding (array sizes, case labels and constant indices are range-checked), qualifier, layout and default-precision rules, `main`, `switch` and `return` rules, missing definitions and recursion. Violations are reported as `Invalid`.
- **SkSL capability checks** (second half of `sema.cpp`): valid GLSL that SkSL runtime shaders cannot run faithfully is refused as `Unsupported` with a suggestion: loops whose number of iterations is unknown and not annotated (see "While, do-while and other loops"), non-constant indices, `uint`, inputs other than the texture coordinate, depth-comparison, integer and multisample samplers, bool and struct uniforms, outputs at a location other than 0, `gl_FragCoord.z`/`.w`, arrays of arrays, `default` before other labels, `continue` inside `switch`, and built-ins with no exact equivalent (`isnan`/`isinf`, bit casts and packing).

- **Built-in replacements** (`helpers.cpp`): GLSL ES built-ins that SkSL runtime shaders lack get generated SkSL functions with the result the GLSL ES specification defines: `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `trunc`, `round`, `roundEven`, `modf`, `fma`, `determinant`, `transpose`, `outerProduct`, integer `abs`/`sign`/`min`/`max`/`clamp`, and `mix` with a bool selector. Only the overloads a shader uses are emitted, and native overloads (float `abs`, `mix` with a float weight, ...) are left alone. Which overloads SkSL lacks was measured with skslc; `tests/check_builtin_helpers.py` repeats that check. Built-ins without an exact equivalent stay refused: `isnan`/`isinf`, bit casts, packing and `frexp`/`ldexp`. Non-square matrices do not exist in SkSL and are refused.

- **Emitter** (`emitter.cpp`): writes SkSL from the analysed syntax tree. GLSL types map to SkSL types, precision, layout, `invariant` and `precise` qualifiers are dropped, functions are ordered so callees come first (prototypes are not needed), struct definitions are moved to global scope, uniform blocks are flattened into plain uniforms, constant `%` and bit operations are replaced by their values and the others by generated functions (see below), constant `.length()` calls by the length, constructors drop the components GLSL ignores (`vec3(v4)` becomes `float3(v4.xyz)`), integer literals keep their 32-bit pattern, identifiers that SkSL reserves are renamed, and `out` parameters a function never writes are set to zero. `gl_FragColor` or the single `out vec4` becomes the returned color, and the GLSL `main` becomes the SkSL `main`.

  Layout qualifiers have no SkSL equivalent and are dropped, so an invalid one (a misspelled name, a location where GLSL ES does not allow it, a missing value) is reported as the warning `sema.layout` with a suggestion (`did you mean 'location'?`) instead of an error. `location` keeps its effect: it selects the output and the texture coordinate input.

  Variables, uniforms, functions and structs keep their GLSL names when SkSL permits them. The returned `renamedIdentifiers` vector contains pairs of original identifiers and emitted identifiers; use it when binding renamed ordinary uniforms. Samplers are listed in `textures` instead: for each sampler (each element of an array of samplers) its child shaders and its size uniform, the names to bind, since a generated name avoids every identifier of the shader (with a `uniform vec2 tSize;` of its own, the size uniform of `t` is `tSize_2`). Identifiers that SkSL reserves, including intrinsic names, are renamed (`float2` becomes `float2_`, `sk_x` becomes `_sk_x`). Names the emitter introduces are plain words (`coord`, `fragColor` for `gl_FragColor`, `fragCoord`, `quad`, `tmp1`, `x_L1` for the copy of `x` in another pixel of the quad, `glsl_sinh` for a built-in replacement), chosen so that they collide with no identifier of the shader: if the shader already uses `coord`, the parameter becomes `coord_2`.

  An array index must be built from constants and loop indices with operators. Two other indices are written in that form, with the same value: an index with a single possible value becomes that constant (`int k = 2; a[k]` becomes `a[2]`), and a local that is only initialized from loop indices and constants is replaced by its initializer (`int j = i * 2 + 1; a[j]` becomes `a[(i * 2 + 1)]`). Other indices are refused (`unsupported.dynamic-index`).

  SkSL has no array constructors and cannot initialize, assign, compare, select (`?:`) or sequence (`,`) arrays and structs containing arrays, nor return them from functions. The emitter computes such values before the statement that uses them, element by element, keeping GLSL's evaluation order: initializations and assignments become element assignments (through a temporary when the value reads the target), constructors elsewhere become temporary arrays, `?:` becomes `if`/`else`, `&&`/`||` and `?:` whose skipped operand would have side effects become `if`s, operands evaluated earlier are computed first when the order could change a result, comparisons compare the elements (`a == b` becomes `(a[0] == b[0] && a[1] == b[1])`), functions return such values through an `out` parameter, and `const` arrays become variables initialized at the start of `main` with their constant elements folded where read. Refused: array returns from functions that compute derivatives, an `inout` argument that a later part of the call changes, and these values in `for` headers.

  Built-ins that become generated functions (`tanh`, `transpose`, integer `max`, ...) are computed by the transpiler when their arguments are constant, because SkSL accepts no function call in a `const` initializer: `const float f = tanh(1.0);` becomes `const float f = 0.7615942;`. Results that are not finite stay calls. An `out` parameter the function never writes is set to zero at its start (GLSL leaves it undefined; Skia rejects the function otherwise), structs and arrays element by element.

  Skia stops parsing at 50 levels of nesting (each statement, parenthesis and operator counts, and each call, index or field counts two), and its GPU backend writes every operation in parentheses and parses the program again. GLSL compilers have no such limit. The emitter computes operands that would nest deeper than 20 levels into temporaries before the statement (the left part of a long `a + b + c + ...`, the inner calls of `min(min(min(...)))`), in GLSL's order of evaluation, and writes `else if` chains without a nested block per branch.

## Switch

SkSL requires `default` to be the last label of a `switch`. A `default` elsewhere moves to the end with a copy of the statements it runs: its own and those it falls into, up to the first `break`, `return`, `continue` or `discard`, in a block of their own so that their declarations stay apart. The statements also stay where they were for the labels that fall into them (and go when only the `default` reached them), and the old last section gets a `break` so that it cannot fall into the moved `default`. Every value runs the same statements as in GLSL (`software.switch`):

```glsl
switch (k) { case 1: A; default: B; case 2: C; break; case 3: D; }
```
```sksl
switch (k) { case 1: A; B; case 2: C; break; case 3: D; break; default: { B; C; break; } }
```

Refused: a first `default` whose statements declare a variable (the declaration would come before the first label).

## Integer remainder and bit operations

SkSL runtime shaders have no `%` or bit operators. `%`, `&`, `|`, `^`, `<<`, `>>` on `int` and `ivec` values (and their compound assignments) call generated functions, and `~a` becomes `-1 - a`; constant operations are folded. The functions work on the 32-bit two's complement value with the integer arithmetic SkSL has: the sign bit apart, the other 31 bits bit by bit through division by 2, so they never overflow and give the result GLSL defines, also for negative values and `INT_MIN` (`%` keeps the sign of the dividend, as GPUs do; GLSL leaves negative operands undefined). Compound assignments take the target as an `inout` parameter, so it is evaluated once. Reported as `software.operator`. In an array index, which Skia requires to be built from loop indices and constants with operators, `a % n` by a positive constant `n` is written `(a - n * (a / n))` instead, exact for `a >= 0`.

A float division by a constant with a zero component, such as `(a - a) / (a - a)` with `const float a`, is rejected by Skia, while GLSL leaves the result unspecified and GPUs give infinity or NaN. The divisor gets a zero known only at run time (`1.0 / ((f - f) + zero)`, with a global `float zero` set at the start of `main`), so the result is the IEEE value as on a GPU (`software.operator`). The functions have no early returns and divide only by constants: Skia's CPU backend also evaluates the lanes a branch has left, and an overflowing division there stops the process. `tests/check_integer_ops.py` compares every operator with Python's 32-bit results.

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

The `transpile` boundary is `noexcept`. If memory allocation fails, the result is Failure; diagnostics are best effort and can be empty when allocating the diagnostic also fails. This holds wherever the standard containers can be constructed without allocating. With MSVC's iterator debugging (`_ITERATOR_DEBUG_LEVEL` 1 or 2, the Debug default), the `noexcept` constructors of `std::string` and `std::vector` allocate a debug proxy, and the standard library itself terminates the program when that allocation fails, inside the library or in the caller; no code outside the standard library can prevent it. The resilience test therefore compiles its own copy of the library with `_ITERATOR_DEBUG_LEVEL=0`, so its allocation sweep checks the library's handling of allocation failures in every configuration.
