"""Compiler-backed fragment corpus. Missing tools fail unless explicitly allowed."""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

def validate_manifest(root):
    cases=json.loads((root/'manifest.json').read_text())
    seen=set()
    for case in cases:
        if case['name'] in seen or case['expect'] not in ('accept','reject'):
            raise ValueError('Duplicate name or invalid expected outcome')
        seen.add(case['name'])
        if case.get('target_expect','accept') not in ('accept','reject'):
            raise ValueError('Invalid target expectation')
        path=(root/case['file']).resolve()
        if path.parent!=root.resolve() or not path.is_file():
            raise ValueError('Missing or unsafe fixture path')
        if case['expect']=='reject' and not case.get('diagnostic'):
            raise ValueError('Rejected case needs diagnostic')
    if not cases: raise ValueError('Empty corpus')
    return cases

def compile_target(skia, source, expected='accept'):
    diagnostic=''
    try:
        effect=skia.RuntimeEffect.MakeForShader(source)
    except RuntimeError as error:
        diagnostic=str(error)
        if not diagnostic.strip(): raise RuntimeError('Compiler failed without diagnostics')
        effect=None
    if expected=='reject':
        if effect: raise RuntimeError('Target unexpectedly accepted compatibility probe')
        if not diagnostic: raise RuntimeError('Target rejection lacked compiler diagnostics')
    elif not effect: raise RuntimeError('Target rejected shader: '+diagnostic)
    return diagnostic

def compiler_controls(skia):
    if not skia.RuntimeEffect.MakeForShader('half4 main(float2 p){return half4(1);}'):
        raise RuntimeError('Compiler valid-source control failed')
    try:
        result=skia.RuntimeEffect.MakeForShader('half4 main(float2 p){return unknown_identifier;}')
    except RuntimeError:
        result=None
    if result: raise RuntimeError('Compiler invalid-source control unexpectedly accepted')

def cli_checks(executable):
    with tempfile.TemporaryDirectory(prefix='shader cli ') as folder:
        root=Path(folder)
        source='#version 320 es\nprecision highp float;out vec4 color;void main(){color=vec4(1.);}'
        valid=root/'input shader.frag';valid.write_text(source)
        out=root/'output shader.sksl'
        cases=[([executable,'--help'],None,0),([executable,str(root/'missing.frag')],None,2),
               ([executable,'-'],source,0),([executable,'-'],'void main(){discard;}',1),
               ([executable,str(valid),str(out)],None,0),
               ([executable,str(valid),str(root/'missing'/'output.sksl')],None,2)]
        for command,stdin,expected in cases:
            run=subprocess.run(command,input=stdin,capture_output=True,text=True)
            if run.returncode!=expected:raise RuntimeError('CLI return code mismatch: '+str(command)+run.stderr)
        if not out.is_file() or not out.read_text():raise RuntimeError('CLI output file missing')

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--transpiler', required=True)
    p.add_argument('--glslang')
    p.add_argument('--allow-missing-skia', action='store_true')
    p.add_argument('--artifacts', default='build/corpus-results')
    args=p.parse_args()
    try:
        import skia
    except ImportError:
        if not args.allow_missing_skia:
            p.error('skia-python is required: install tests/requirements-validation.txt')
        skia=None
        print('SKIP target compilation: skia-python unavailable')
    if skia:
        compiler_controls(skia)
    root=Path(__file__).parent/'corpus'
    cli_checks(args.transpiler)
    artifacts=Path(args.artifacts);artifacts.mkdir(parents=True,exist_ok=True)
    failures=[]
    cases=validate_manifest(root)
    for case in cases:
        source=root/case['file']
        try:
            if args.glslang:
                valid=subprocess.run([args.glslang,'-S','frag',str(source)],capture_output=True,text=True)
                if bool(valid.returncode)==case.get('source_valid',True):
                    raise RuntimeError('Unexpected GLSL source validity: '+valid.stdout+valid.stderr)
            run=subprocess.run([args.transpiler,str(source)],capture_output=True,text=True)
            (artifacts/(case['name']+'.stderr')).write_text(run.stderr)
            if case['expect']=='reject':
                if run.returncode!=1 or run.stdout or case['diagnostic'] not in run.stderr:
                    raise RuntimeError('Expected diagnostic rejection: '+run.stderr)
            else:
                if run.returncode or not run.stdout: raise RuntimeError('Conversion failed: '+run.stderr)
                (artifacts/(case['name']+'.sksl')).write_text(run.stdout)
                if skia:
                    diagnostic=compile_target(skia,run.stdout,case.get('target_expect','accept'))
                    (artifacts/(case['name']+'.target.stderr')).write_text(diagnostic)
            print('PASS '+case['name'])
        except (RuntimeError,OSError) as error:
            failures.append(case['name']);print('FAIL '+case['name']+': '+str(error))
    print(f'{len(cases)-len(failures)}/{len(cases)} conversion cases passed; target compiler '+('enabled' if skia else 'SKIPPED'))
    return bool(failures)

if __name__=='__main__': sys.exit(main())
