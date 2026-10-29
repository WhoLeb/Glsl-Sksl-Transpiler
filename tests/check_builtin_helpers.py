"""Compile-checks every GLSL built-in overload that the transpiler replaces with a helper.

For each overload that needs a generated helper (sinh, cosh, tanh, asinh, acosh, atanh,
trunc, round, roundEven, modf, fma, determinant, transpose, outerProduct, int abs/sign/min/
max/clamp, and bool-weight mix, over every vector width), the script writes a small GLSL ES
3.20 fragment shader that calls the built-in, runs glsl-to-sksl on it, and compiles the
emitted SkSL with Skia's skslc. A case fails when the transpiler refuses the shader or when
skslc rejects the output. The summary line reports the number of cases and failures; the
exit status is 1 when any case failed.

skslc needs Skia's SkSL module files (src/sksl/*.sksl, copied next to the executable as
sksl_*.sksl). The script runs skslc with the folder that contains it as the working
directory, so that folder must hold those module files.

Usage:
    python tests/check_builtin_helpers.py --transpiler build/glsl-to-sksl.exe --skslc PATH/skslc.exe [--work DIR]
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile


def value(t):
    """A GLSL expression of type t that depends on the uniform u."""
    if t == "float": return "u.x"
    if t == "int": return "int(u.x)"
    if t == "bool": return "u.x > 0."
    m = re.fullmatch(r"(|i|b)vec([234])", t)
    if m: return f"{t}({value({'': 'float', 'i': 'int', 'b': 'bool'}[m.group(1)])})"
    if t.startswith("mat"): return f"{t}(u.x)"
    raise ValueError(t)


def gen(prefix, n):
    """Type name for a scalar (n == 1) or vector of n components with the given prefix."""
    return {"float": "float", "int": "int", "bool": "bool"}[prefix] if n == 1 else {"float": "vec", "int": "ivec", "bool": "bvec"}[prefix] + str(n)


def build_cases():
    """Each case is (label, result type, parameter types[, function name])."""
    cases = []
    for n in (1, 2, 3, 4):
        f, i, b = gen("float", n), gen("int", n), gen("bool", n)
        for name in ("sinh", "cosh", "tanh", "asinh", "acosh", "atanh", "trunc", "round", "roundEven"):
            cases.append((f"{name}({f})", f, [f]))
        cases.append((f"modf({f}, out)", f, [f, "out " + f], "modf"))
        cases.append((f"fma({f})", f, [f, f, f], "fma"))
        for name in ("abs", "sign"):
            cases.append((f"{name}({i})", i, [i], name))
        for name in ("min", "max"):
            cases.append((f"{name}({i},{i})", i, [i, i], name))
            if n > 1: cases.append((f"{name}({i},int)", i, [i, "int"], name))
        cases.append((f"clamp({i},{i},{i})", i, [i, i, i], "clamp"))
        if n > 1: cases.append((f"clamp({i},int,int)", i, [i, "int", "int"], "clamp"))
        for t in (f, i, b):
            cases.append((f"mix({t},{t},{b})", t, [t, t, b], "mix"))
    for n in (2, 3, 4):
        m, v = f"mat{n}", f"vec{n}"
        cases.append((f"determinant({m})", "float", [m], "determinant"))
        cases.append((f"transpose({m})", m, [m], "transpose"))
        cases.append((f"outerProduct({v},{v})", m, [v, v], "outerProduct"))
    return cases


def check(transpiler, skslc, work):
    """Runs every case, prints each failure and the summary, and returns the failure count."""
    cases = build_cases()
    skslc_dir = os.path.dirname(os.path.abspath(skslc))
    rts_path = os.path.join(work, "h.rts")
    glsl_path = os.path.join(work, "h.glsl")
    failures = 0
    for case in cases:
        label, result, params = case[0], case[1], case[2]
        name = case[3] if len(case) > 3 else label.split("(")[0]
        decls, args = [], []
        for k, p in enumerate(params):
            if p.startswith("out "):
                decls.append(f"{p[4:]} o{k};"); args.append(f"o{k}")
            else:
                args.append(value(p))
        src = ("#version 320 es\nprecision highp float;\nuniform vec4 u;\nout vec4 color;\n"
               f"void main(){{ {' '.join(decls)} {result} r = {name}({', '.join(args)}); color = vec4(1.); }}\n")
        t = subprocess.run([transpiler, "-"], input=src, capture_output=True, text=True, encoding="utf-8")
        if t.returncode != 0:
            failures += 1
            print(f"TRANSPILE FAIL {label}: {t.stderr.strip().splitlines()[0] if t.stderr.strip() else ''}")
            continue
        with open(rts_path, "w", encoding="utf-8") as fh:
            fh.write(t.stdout)
        c = subprocess.run([os.path.abspath(skslc), rts_path, glsl_path], cwd=skslc_dir, capture_output=True, text=True, encoding="utf-8", errors="replace")
        if c.returncode != 0:
            failures += 1
            err = next((l for l in (c.stdout + c.stderr).splitlines() if l.startswith("error")), "?")
            print(f"SKSLC FAIL {label}: {err}")
    print(f"{len(cases)} built-in overloads checked, {failures} failures")
    return failures


def main(argv=None):
    ap = argparse.ArgumentParser(description="Compile-check the built-in overloads replaced by transpiler helpers.")
    ap.add_argument("--transpiler", required=True, metavar="PATH", help="path to glsl-to-sksl executable")
    ap.add_argument("--skslc", required=True, metavar="PATH", help="path to Skia's skslc executable; its folder must contain sksl_*.sksl")
    ap.add_argument("--work", metavar="DIR", help="scratch directory (default: a new temporary directory)")
    args = ap.parse_args(argv)
    if not os.path.isfile(args.transpiler):
        ap.error(f"--transpiler not found: {args.transpiler}")
    if not os.path.isfile(args.skslc):
        ap.error(f"--skslc not found: {args.skslc}")
    work = os.path.abspath(args.work) if args.work else tempfile.mkdtemp(prefix="builtin-helpers-")
    os.makedirs(work, exist_ok=True)
    failures = check(args.transpiler, args.skslc, work)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
