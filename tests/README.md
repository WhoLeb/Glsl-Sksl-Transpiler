# Fragment corpus

The 58 named GLSL ES 3.20 fragments exercise expressions, constructors,
swizzles, matrices, arrays, helpers, loops, branches, uniforms and modern
outputs, plus explicit unsupported pipeline cases. Regenerate using
`python tests/generate_corpus.py`.

Install `tests/requirements-validation.txt` into a local environment and run:

```
python tests/run_corpus.py --transpiler build/Debug/glsl-to-sksl.exe
```

The runner uses Skia RuntimeEffect.MakeForShader, checks a deliberately invalid
shader control, and preserves emitted source and stderr in build/corpus-results.
Pass `--glslang PATH` to validate source fixtures independently. Missing Skia
fails by default; `--allow-missing-skia` explicitly runs conversion checks only.
Compiler acceptance does not establish rendered semantic equivalence.

Preparation status: Python fixture generation was run. C++ compilation and the
full corpus require the C++ toolchain. The attempted pinned wheel installation
failed because package-index access was disabled by PIP_NO_INDEX; after enabling
it for the command, the configured proxy refused connections. The version pin
was verified through https://pypi.org/pypi/skia-python/138.0/json;
target compilation has not been performed.
# Runtime profile compatibility probes

The corpus includes uniform/helper-bounded `for`, dynamic start/step,
dependent nested loops, `while`/`do while`, dynamic indexing, switch fallthrough,
and integer bitwise/remainder probes. Constant-bound and bounded-loop-with-break
controls exercise nearby patterns. Uniforms contribute to the returned color.

`expect` describes translation; `source_valid` describes GLSL validity;
`target_expect` independently describes the default
`skia.RuntimeEffect.MakeForShader` profile. Dynamic probes currently predict
rejection under its ES2 restrictions. These predictions remain unconfirmed
until the pinned compiler runs; they are not claims about every SkSL profile.
Unexpected target acceptance fails the test so expectations can be corrected.
Missing Skia leaves all target predictions untested. Expected rejections require
compiler diagnostics, saved as `.target.stderr` artifacts.

Rationale: Skia loop-unroll analysis checks constant initializers and bounds:
https://skia.googlesource.com/skia.git/+/5f4f454b9662b83facd71dcbc6e8ac105660e938/src/sksl/analysis/SkSLGetLoopUnrollInfo.cpp


# Edge-case corpus (`edge_cases.py`)

~680 adversarial fragments registered from `generate_corpus.py`: output-variable
lowering, SkSL-reserved names, literals/conversions, operator and swizzle traps,
arrays/structs/scope/globals, control flow (loops, switch), overloads and
`out`-argument aliasing, builtins, qualifiers, headers/directives/lexing, stress
sizes, realistic shader shapes, GLSL ES 1.00 shaders, preprocessor conditionals
and macros, and software-derivative edge cases. Conventions: `Unsupported` =
valid GLSL ES 3.20 the transpiler refuses; `Invalid` = not valid GLSL, must be diagnosed;
`note` explains the trap or the decision a case forces; `target_expect: reject`
marks ES2-strict Skia limits (dynamic loops, `%`, bit ops, `switch`). Many
expectations are predictions to be corrected against the pinned compiler.

# Built-in helper check (`check_builtin_helpers.py`)

`check_builtin_helpers.py` covers each GLSL ES built-in overload that the transpiler
replaces with a generated helper (sinh through outerProduct, int abs/sign/min/max/clamp,
and bool-weight mix). It translates a small shader for each overload and compiles the
output with Skia's skslc, then exits with status 1 if any overload fails. Run it with
`python tests/check_builtin_helpers.py --transpiler build/glsl-to-sksl.exe --skslc PATH\skslc.exe`,
where skslc.exe must sit in the same folder as Skia's `sksl_*.sksl` module files, and
`--work DIR` optionally selects the scratch folder. The expected summary is
`94 built-in overloads checked, 0 failures`.

# Derivative check (`check_derivatives.py`)

`python tests/check_derivatives.py --transpiler build/glsl-to-sksl.exe` renders shaders that use dFdx, dFdy and fwidth with Skia's CPU backend and compares every pixel with GPU fine-derivative semantics computed from the rendered argument values. All comparisons must match exactly. Requires skia-python from `requirements-validation.txt`.

# Array check (`check_arrays.py`)

`python tests/check_arrays.py --transpiler build/glsl-to-sksl.exe` renders shaders that construct, assign, compare, select, sequence and return arrays (and structs containing arrays) with Skia's CPU backend and compares each result with the value GLSL defines, including cases where evaluation order or skipped operands would change the result. It also checks that generated names keep the shader's identifiers. Requires skia-python from `requirements-validation.txt`; the expected summary is `failures: 0`.

# Texture check (`check_textures.py`)

`build\.venv-validation\Scripts\python.exe tests\check_textures.py --transpiler build\glsl-to-sksl.exe` renders shaders that sample 2D, cube, 3D and array textures, use texture bias, LOD and gradients, texelFetch, textureSize, textureGather and textureOffset, sampler parameters and arrays of samplers, and the GLSL ES 1.00 texture families, with Skia's CPU backend. Each rendered result is compared with the value GL's sampling rules give, computed in numpy from the same texture data, and the maximum error is reported per check. Requires skia-python from `requirements-validation.txt` (run it with the validation venv Python); the expected summary is `failures: 0`.

# Integer operation check (`check_integer_ops.py`)

`build\.venv-validation\Scripts\python.exe tests\check_integer_ops.py --transpiler build\glsl-to-sksl.exe` renders integer `%`, `&`, `|`, `^`, `<<`, `>>`, their compound assignments, vector forms and `~` as translated by the generated software helpers, and compares each result with 32-bit two's complement GLSL results over edge and random operands. Remainders with negative operands or a zero divisor are skipped, and shift counts are reduced modulo 32, because GLSL leaves those cases undefined or implementation-defined. Requires skia-python from `requirements-validation.txt`; the expected summary is `failures: 0`.

# Loop check (`check_loops.py`)

`build\.venv-validation\Scripts\python.exe tests\check_loops.py --transpiler build\glsl-to-sksl.exe` renders `while`, `do-while` and `for` loops that the transpiler lowers to bounded iteration (counter proofs, `while(false)`, `@maxiter` annotations and `for` loops with a separate init, no increment or a step inside the body), including `continue` and conditions with side effects, and compares the rendered pixel with the GLSL result. Requires skia-python from `requirements-validation.txt`; the expected summary is `failures: 0`.
