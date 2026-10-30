"""Converts the shaders downloaded by fetch_online_shaders.py and sorts every problem into a report.

For each shader:
  1. Shadertoy image code is wrapped in Shadertoy's preamble (uniforms, iChannel samplers, a main
     that calls mainImage with gl_FragCoord), as a GLSL ES 3.00 fragment shader.
  2. glslangValidator, when given, decides whether the GLSL is valid; invalid shaders (for example
     ones that need Shadertoy's Common tab) are counted apart and are not transpiler problems.
  3. The transpiler converts it; a crash, a timeout or an error on valid GLSL is a problem.
  4. Skia compiles the SkSL; a rejection is a transpiler bug.
  5. With --render, the GPU's OpenGL driver draws the GLSL and Skia draws the SkSL on the same GPU
     (so both use the same sin, pow, ...), with the same uniforms and textures, and the images are
     compared. Shaders whose images differ are listed with a side-by-side picture.

Requires skia-python (and moderngl for --render) from requirements-validation.txt. Run:
  build\\.venv-validation\\Scripts\\python.exe tests\\check_online_shaders.py --transpiler build\\glsl-to-sksl.exe
      --glslang build\\glslang\\StandAlone\\glslang.exe --render
The summary goes to stdout and <out>/summary.md, every result to <out>/report.json, and the input,
SkSL, diagnostics and comparison picture of each problem shader to <out>/<category>/<id>/.
"""
import argparse
import collections
import concurrent.futures
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

SHADERTOY_HEADER = '''#version 300 es
precision highp float;
precision highp int;
precision mediump sampler3D;
#define HW_PERFORMANCE 1
uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform float iFrameRate;
uniform int iFrame;
uniform float iChannelTime[4];
uniform vec3 iChannelResolution[4];
uniform vec4 iMouse;
uniform vec4 iDate;
uniform float iSampleRate;
'''
SHADERTOY_FOOTER = '''
out vec4 shadertoyColor;
void main() {
    vec4 shadertoyResult = vec4(0.0);
    mainImage(shadertoyResult, gl_FragCoord.xy);
    shadertoyColor = vec4(shadertoyResult.xyz, 1.0);
}
'''
SAMPLER_TYPES = ('sampler2D', 'samplerCube', 'sampler3D')
WIDTH, HEIGHT = 80, 48
TIMES = (1.37, 7.21)
TEXTURE = 256  # texture size in texels, as Shadertoy's noise textures (3D: 16)
UNIFORM = re.compile(r'^\s*uniform\s+(?!shader\b)(\w+)\s+(\w+)\s*(?:\[(\d+)\])?\s*;', re.M)
CODE = re.compile(r'^\d+:\d+: (error|warning): (.*) \[([\w.-]+)\]$')


def shadertoy_source(code, samplers):
    lines = [f'uniform {samplers.get(i, "sampler2D")} iChannel{i};' for i in range(4)]
    header = SHADERTOY_HEADER + '\n'.join(lines) + '\n'
    return header + code + SHADERTOY_FOOTER, header.count('\n')


def glslang_check(glslang, source, work):
    path = Path(work) / 'shader.frag'
    path.write_text(source, encoding='utf-8')
    run = subprocess.run([glslang, str(path)], capture_output=True, text=True, timeout=60)
    log = '\n'.join(l for l in run.stdout.splitlines() if l.startswith(('ERROR', 'WARNING')))
    return run.returncode == 0, log


def choose_samplers(glslang, code, work):
    """Shadertoy binds a 2D, cube or 3D texture to each channel; keeps the types glslang accepts."""
    samplers = {}
    valid, log = glslang_check(glslang, shadertoy_source(code, samplers)[0], work)
    errors = log.count('ERROR')
    used = sorted({int(c) for c in re.findall(r'iChannel([0-3])', code)})
    for channel in used if not valid else []:
        for kind in SAMPLER_TYPES[1:]:
            trial = dict(samplers, **{channel: kind})
            ok, trial_log = glslang_check(glslang, shadertoy_source(code, trial)[0], work)
            if ok or trial_log.count('ERROR') < errors:
                samplers, valid, log, errors = trial, ok, trial_log, trial_log.count('ERROR')
                break
        if valid:
            break
    return samplers, valid, log


def convert(entry, root, transpiler, glslang):
    code = (root / entry['file']).read_text(encoding='utf-8', errors='replace')
    result = {'id': entry['id'], 'source': entry['source'], 'url': entry.get('url', ''), 'samplers': {}}
    with tempfile.TemporaryDirectory() as work:
        if entry['format'] == 'shadertoy':
            if glslang:
                samplers, valid, log = choose_samplers(glslang, code, work)
            else:
                samplers, valid, log = {}, None, ''
            source, offset = shadertoy_source(code, samplers)
            result['samplers'] = samplers
        else:
            source, offset = code, 0
            valid, log = glslang_check(glslang, source, work) if glslang else (None, '')
    result.update(glsl=source, valid=valid, glslang=log, line_offset=offset)
    try:
        run = subprocess.run([transpiler, '-'], input=source.encode(), capture_output=True, timeout=60)
    except subprocess.TimeoutExpired:
        result.update(category='timeout', sksl='', stderr='')
        return result
    stderr = run.stderr.decode('utf-8', 'replace')
    result.update(returncode=run.returncode, sksl=run.stdout.decode('utf-8', 'replace'), stderr=stderr)
    diagnostics = [m.groups() for m in map(CODE.match, stderr.splitlines()) if m]
    result['errors'] = sorted({c for s, _, c in diagnostics if s == 'error'})
    result['warnings'] = sorted({c for s, _, c in diagnostics if s == 'warning'})
    result['messages'] = [f'{s}: {m} [{c}]' for s, m, c in diagnostics if s == 'error'][:10]
    if run.returncode not in (0, 1):
        result['category'] = 'crash'
    elif run.returncode == 1:
        result['category'] = 'invalid-source' if valid is False else 'transpile-error'
    else:
        result['category'] = 'compile'  # decided by Skia next
    return result


class Renderer:
    """Draws GLSL with the OpenGL driver and SkSL with Skia's GPU backend on the same context."""

    def __init__(self, skia, gl=True):
        self.skia, self.gpu = skia, None
        rng = np.random.default_rng(7)
        self.tex2d = rng.integers(0, 256, (TEXTURE, TEXTURE, 4), dtype=np.uint8)
        self.cube = rng.integers(0, 256, (6, TEXTURE, TEXTURE, 4), dtype=np.uint8)
        self.tex3d = rng.integers(0, 256, (16, 16, 16, 4), dtype=np.uint8)
        if not gl:  # Skia's CPU backend only
            return
        import moderngl
        self.moderngl = moderngl
        self.ctx = moderngl.create_standalone_context(require=430)
        self.gpu = skia.GrDirectContext.MakeGL()
        self.fbo = self.ctx.framebuffer([self.ctx.texture((WIDTH, HEIGHT), 4, dtype='f4')])
        self.vbo = self.ctx.buffer(np.array([-1, -1, 3, -1, -1, 3], dtype='f4').tobytes())
        self.renderer = self.ctx.info['GL_RENDERER']

    def gl_textures(self, samplers):
        textures = []
        for i in range(4):
            kind = samplers.get(i, 'sampler2D')
            if kind == 'samplerCube':
                t = self.ctx.texture_cube((TEXTURE, TEXTURE), 4, self.cube.tobytes())
                t.repeat_x = t.repeat_y = False
            elif kind == 'sampler3D':
                t = self.ctx.texture3d((16, 16, 16), 4, self.tex3d.tobytes())
            else:
                t = self.ctx.texture((TEXTURE, TEXTURE), 4, self.tex2d.tobytes())
            t.filter = (self.moderngl.LINEAR, self.moderngl.LINEAR)
            textures.append(t)
        return textures

    @staticmethod
    def uniforms(time, samplers, shadertoy):
        size = lambda i: (16.0, 16.0, 16.0) if samplers.get(i) == 'sampler3D' else (float(TEXTURE), float(TEXTURE), 1.0)
        if not shadertoy:
            return {'u_resolution': (float(WIDTH), float(HEIGHT)), 'u_time': time, 'u_mouse': (0.0, 0.0),
                    'u_tex0Resolution': (float(TEXTURE), float(TEXTURE)),
                    'time': time, 'resolution': (float(WIDTH), float(HEIGHT)), 'mouse': (0.0, 0.0)}
        return {'iResolution': (float(WIDTH), float(HEIGHT), 1.0), 'iTime': time, 'iTimeDelta': 1 / 60,
                'iFrameRate': 60.0, 'iFrame': int(time * 60), 'iChannelTime': [time] * 4,
                'iChannelResolution': [size(i) for i in range(4)], 'iMouse': (0.0, 0.0, 0.0, 0.0),
                'iDate': (2026.0, 9.0, 30.0, 43200.0 + time), 'iSampleRate': 44100.0}

    def draw_gl(self, glsl, values, samplers):
        vertex = '#version 300 es\nin vec2 p;\nvoid main(){gl_Position=vec4(p,0.0,1.0);}'
        if not glsl.lstrip().startswith('#version'):
            vertex = '#version 100\nattribute vec2 p;\nvoid main(){gl_Position=vec4(p,0.0,1.0);}'
        # Some drivers compute mediump in 16 bits, where Shadertoy on a desktop and SkSL's float use
        # 32 (GLSL only gives a minimum precision), so the reference is drawn in highp.
        version, newline, rest = glsl.partition('\n') if glsl.lstrip().startswith('#version') else ('', '', glsl)
        highp = '#define lowp highp\n#define mediump highp\n'
        program = self.ctx.program(vertex_shader=vertex, fragment_shader=version + newline + highp + rest)
        try:
            textures = self.gl_textures(samplers)
            for name in program:
                member = program[name]
                if not isinstance(member, self.moderngl.Uniform):
                    continue
                base = name.split('[')[0]
                if base.startswith('iChannel') and base[8:].isdigit():
                    textures[int(base[8:])].use(int(base[8:]))
                    member.value = int(base[8:])
                elif base == 'u_tex0' or 'sampler' in str(getattr(member, 'gl_type', '')):
                    textures[0].use(0)
                    member.value = 0
                elif base in values:
                    value = values[base]
                    member.value = (value[:member.array_length] if member.array_length > 1 else value) \
                        if isinstance(value, list) else value
            vao = self.ctx.vertex_array(program, [(self.vbo, '2f', 'p')]) if 'p' in program else self.ctx.vertex_array(program, [])
            self.fbo.use()
            self.fbo.clear(0.0, 0.0, 0.0, 0.0)  # discarded pixels stay transparent, as in the SkSL
            self.ctx.viewport = (0, 0, WIDTH, HEIGHT)
            self.ctx.scissor = None
            self.ctx.disable(self.moderngl.BLEND | self.moderngl.DEPTH_TEST | self.moderngl.CULL_FACE)
            vao.render(self.moderngl.TRIANGLES, vertices=3)
            pixels = np.frombuffer(self.fbo.read(components=4, dtype='f4'), dtype=np.float32)
            vao.release()
            for t in textures:
                t.release()
        finally:
            program.release()
        return pixels.reshape(HEIGHT, WIDTH, 4)[::-1]  # GL rows go up

    def child(self, name, samplers):
        skia = self.skia
        m = re.match(r'(?:iChannel([0-3])|u_tex0)(?:_(px|nx|py|ny|pz|nz))?$', name)
        channel = int(m.group(1)) if m and m.group(1) else 0
        kind = samplers.get(channel, 'sampler2D')
        if kind == 'sampler3D':
            atlas = self.tex3d.reshape(16 * 16, 16, 4)  # slices stacked from top to bottom
            image = skia.Image.fromarray(np.ascontiguousarray(atlas), colorType=skia.kRGBA_8888_ColorType)
            return image.makeRawShader(skia.TileMode.kRepeat, skia.TileMode.kRepeat,
                                       skia.SamplingOptions(skia.FilterMode.kNearest),
                                       skia.Matrix.Scale(1 / 16, 1 / 256))
        data, tile = self.tex2d, skia.TileMode.kRepeat
        if m and m.group(2):
            data = self.cube[['px', 'nx', 'py', 'ny', 'pz', 'nz'].index(m.group(2))]
            tile = skia.TileMode.kClamp
        image = skia.Image.fromarray(np.ascontiguousarray(data), colorType=skia.kRGBA_8888_ColorType)
        return image.makeRawShader(tile, tile, skia.SamplingOptions(skia.FilterMode.kLinear),
                                   skia.Matrix.Scale(1 / TEXTURE, 1 / TEXTURE))

    @staticmethod
    def uniform_data(sksl, effect, values, samplers):
        """Runtime effect uniforms are packed in declaration order, 4-byte aligned, arrays unpadded."""
        declared = UNIFORM.findall(sksl)
        if [n for _, n, _ in declared] != [u.name for u in effect.uniforms()]:
            raise RuntimeError('uniform declarations do not match the effect: ' + str([u.name for u in effect.uniforms()]))
        data = bytearray()
        for kind, name, count in declared:
            base = kind.replace('half', 'float')
            size = {'float2x2': 4, 'float3x3': 9, 'float4x4': 16}.get(base) or int(base[-1] if base[-1].isdigit() else 1)
            n = size * int(count or 1)
            value = values.get(name)
            m = re.match(r'(iChannel[0-3]|u_tex0)Size$', name)
            if m:
                channel = int(m.group(1)[8:]) if m.group(1)[8:].isdigit() else 0
                value = [16.0] * 3 if samplers.get(channel) == 'sampler3D' else [float(TEXTURE)] * 2
            flat = []
            for v in value if isinstance(value, (list, tuple)) else ([] if value is None else [value]):
                flat.extend(v if isinstance(v, (list, tuple)) else (v,))
            flat = (flat + [0] * n)[:n]
            data += np.array(flat, dtype=np.int32 if base.startswith('int') else np.float32).tobytes()
        return bytes(data)

    def draw_skia(self, sksl, effect, values, samplers):
        skia = self.skia
        children = skia.VectorRuntimeEffectChildPtr(
            [skia.RuntimeEffectChildPtr(self.child(c.name, samplers)) for c in effect.children()])
        shader = effect.makeShader(skia.Data.MakeWithCopy(self.uniform_data(sksl, effect, values, samplers)),
                                   skia.SpanRuntimeEffectChildPtr(children))
        if self.gpu:
            self.gpu.resetContext()
            info = skia.ImageInfo.Make(WIDTH, HEIGHT, skia.kRGBA_F16_ColorType, skia.kPremul_AlphaType)
            surface = skia.Surface.MakeRenderTarget(self.gpu, skia.Budgeted.kNo, info)
        else:
            surface = skia.Surface.MakeRaster(skia.ImageInfo.Make(WIDTH, HEIGHT, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType))
        canvas = surface.getCanvas()
        canvas.translate(0, HEIGHT)
        canvas.scale(1, -1)  # gl_FragCoord.y grows upwards
        paint = skia.Paint()
        paint.setShader(shader)
        paint.setBlendMode(skia.BlendMode.kSrc)
        canvas.drawPaint(paint)
        if self.gpu:
            self.gpu.flushAndSubmit(skia.GrSyncCpu.kYes)
        read = skia.ImageInfo.Make(WIDTH, HEIGHT, skia.kRGBA_F32_ColorType, skia.kPremul_AlphaType)
        pixels = np.zeros((HEIGHT, WIDTH, 4), dtype=np.float32)
        surface.readPixels(read, pixels, WIDTH * 16)
        if self.gpu:
            self.gpu.resetContext()
        return pixels


def draw_cpu(sksl, values, samplers):
    """Draws with Skia's CPU backend in a child process: an integer division by a garbage value in a
    lane the CPU backend runs masked off can kill the process."""
    with tempfile.TemporaryDirectory() as work:
        job = Path(work) / 'job.json'
        job.write_text(json.dumps({'sksl': sksl, 'values': values, 'samplers': samplers}), encoding='utf-8')
        run = subprocess.run([sys.executable, __file__, '--cpu-render', str(job)], capture_output=True, timeout=300)
        if run.returncode != 0:
            raise RuntimeError(f'Skia CPU render failed (exit code {run.returncode}): '
                               + run.stderr.decode('utf-8', 'replace').strip()[-1000:])
        return np.load(Path(work) / 'pixels.npy')


def cpu_render_job(path):
    import skia
    job = json.loads(Path(path).read_text(encoding='utf-8'))
    samplers = {int(k): v for k, v in job['samplers'].items()}
    effect = skia.RuntimeEffect.MakeForShader(job['sksl'])
    pixels = Renderer(skia, gl=False).draw_skia(job['sksl'], effect, job['values'], samplers)
    np.save(Path(path).parent / 'pixels.npy', pixels)


def compare(a, b):
    a = np.clip(np.nan_to_num(a, nan=0.0), 0, 1)
    b = np.clip(np.nan_to_num(b, nan=0.0), 0, 1)
    diff = np.abs(a - b).max(axis=2)
    return float((diff > 0.05).mean()), float(diff.max()), diff


def same_on_average(a, b, block=8):
    """Hash noise such as fract(sin(x) * 43758.5) differs per pixel with the last bit of sin; the
    averages over blocks of pixels still agree when the conversion is right."""
    mean = lambda x: np.clip(np.nan_to_num(x, nan=0.0), 0, 1).reshape(HEIGHT // block, block, WIDTH // block, block, 4).mean(axis=(1, 3))
    return float(np.abs(mean(a) - mean(b)).max()) < 0.06


def save_picture(skia, path, gl, sk, diff):
    to8 = lambda x: (np.clip(np.nan_to_num(x, nan=0.0), 0, 1) * 255 + 0.5).astype(np.uint8)
    d = np.repeat(np.clip(diff * 4, 0, 1)[:, :, None], 3, axis=2)
    gap = np.full((HEIGHT, 2, 4), 255, np.uint8)
    rgba = lambda x: np.concatenate([to8(x[:, :, :3]), np.full((HEIGHT, WIDTH, 1), 255, np.uint8)], axis=2)
    row = np.concatenate([rgba(gl), gap, rgba(sk), gap, rgba(d)], axis=1)
    row = row.repeat(4, axis=0).repeat(4, axis=1)
    skia.Image.fromarray(np.ascontiguousarray(row), colorType=skia.kRGBA_8888_ColorType).save(str(path), skia.kPNG)


def check_target(skia, renderer, result, pictures):
    try:
        effect = skia.RuntimeEffect.MakeForShader(result['sksl'])
    except RuntimeError as error:
        result['category'] = 'skia-reject'
        result['skia'] = str(error).strip()
        return
    if not renderer:
        result['category'] = 'ok'
        return
    shadertoy = 'mainImage(shadertoyResult, gl_FragCoord.xy)' in result['glsl']
    worst = None
    for time in TIMES:
        values = renderer.uniforms(time, result['samplers'], shadertoy)
        try:
            gl = renderer.draw_gl(result['glsl'], values, result['samplers'])
        except Exception as error:
            result['category'] = 'reference-error'
            result['reference'] = str(error).strip()[:2000]
            return
        try:
            sk = renderer.draw_skia(result['sksl'], effect, values, result['samplers'])
        except Exception as error:
            result['category'] = 'skia-render-error'
            result['skia'] = str(error).strip()
            return
        bad, peak, diff = compare(gl, sk)
        if worst is None or bad > worst[0]:
            worst = (bad, peak, time, values, gl, sk, diff)
    bad, peak, time, values, gl, sk, diff = worst
    result.update(mismatch=bad, peak=peak, time=time)
    result['category'] = 'render-mismatch' if bad > 0.01 else 'ok'
    if result['category'] == 'ok':
        return
    # Second opinion from Skia's CPU backend: when it matches, the difference comes from Skia's GPU
    # backend or the driver compiling Skia's GLSL, not from the conversion.
    try:
        cpu = draw_cpu(result['sksl'], values, result['samplers'])
    except Exception as error:
        result['skia'] = str(error)
        result['category'] = 'skia-cpu-crash' if 'exit code' in str(error) else result['category']
        pictures[result['id']] = (gl, sk, diff)
        return
    cpu_bad, cpu_peak, cpu_diff = compare(gl, cpu)
    result.update(cpu_mismatch=cpu_bad)
    if cpu_bad <= 0.01:
        result['category'] = 'skia-gpu-only'
        pictures[result['id']] = (gl, sk, diff)
    else:
        result.update(mismatch=min(bad, cpu_bad), peak=cpu_peak if cpu_bad < bad else peak)
        pictures[result['id']] = (gl, cpu, cpu_diff) if cpu_bad < bad else (gl, sk, diff)
        if same_on_average(gl, sk):
            result['category'] = 'noise-differs'


def write_artifacts(skia, out, result, picture):
    folder = out / result['category'] / result['id']
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'input.frag').write_text(result['glsl'], encoding='utf-8')
    (folder / 'output.sksl').write_text(result.get('sksl', ''), encoding='utf-8')
    notes = [f"url: {result['url']}", f"samplers: {result['samplers']}",
             f"image code line N is line N + {result['line_offset']} of input.frag" if result['line_offset'] else '',
             '--- transpiler', result.get('stderr', ''), '--- glslang', result.get('glslang', ''),
             '--- skia', result.get('skia', ''), '--- reference', result.get('reference', '')]
    (folder / 'notes.txt').write_text('\n'.join(notes), encoding='utf-8')
    if picture:
        save_picture(skia, folder / 'gl-skia-diff.png', *picture)


def summary(results, renderer_name, out):
    by = collections.defaultdict(list)
    for r in results:
        by[r['category']].append(r)
    order = ['ok', 'crash', 'timeout', 'transpile-error', 'skia-reject', 'skia-render-error', 'skia-cpu-crash',
             'render-mismatch', 'noise-differs', 'skia-gpu-only', 'reference-error', 'invalid-source', 'compile']
    explain = {'ok': 'converted, Skia compiles it' + (' and it renders like the GLSL' if renderer_name else ''),
               'crash': 'the transpiler crashed (bug)', 'timeout': 'the transpiler ran over 60 s (bug)',
               'transpile-error': 'valid GLSL that the transpiler refused or misjudged',
               'skia-reject': 'converted without errors but Skia rejects the SkSL (bug)',
               'skia-render-error': 'Skia compiled the SkSL but could not draw it',
               'skia-cpu-crash': "Skia's GPU drawing differs from the GLSL and its CPU backend crashed on the SkSL",
               'render-mismatch': "renders differently from the GLSL with both Skia backends (over 1% of pixels off by more than 0.05)",
               'noise-differs': 'differs pixel by pixel but agrees on average over 8x8 blocks (hash noise; usually precision, not the conversion)',
               'skia-gpu-only': "Skia's CPU backend draws it like the GLSL, its GPU backend does not "
                                "(Skia's generated GLSL or the driver; see the log for 'Shader compilation error')",
               'reference-error': 'the OpenGL driver could not draw the GLSL (not compared)',
               'invalid-source': 'glslang rejects the GLSL and so does the transpiler (not counted)'}
    lines = [f'# Online shader check: {len(results)} shaders', '']
    if renderer_name:
        lines += [f'Reference renderer: {renderer_name}, {WIDTH}x{HEIGHT}, iTime {TIMES}', '']
    lines += ['| result | shaders | meaning |', '|---|---:|---|']
    lines += [f'| {c} | {len(by[c])} | {explain.get(c, "")} |' for c in order if by[c]]
    valid = [r for r in results if r['category'] != 'invalid-source']
    if valid:
        lines += ['', f"Converted correctly: {len(by['ok'])} of {len(valid)} shaders that glslang accepts "
                  f"({100 * len(by['ok']) / len(valid):.1f}%)."]
    errors = collections.defaultdict(list)
    for r in by['transpile-error'] + by['invalid-source']:
        for code in r['errors'] or ['(no code)']:
            errors[(r['category'], code)].append(r)
    for category in ('transpile-error', 'invalid-source'):
        codes = sorted(((len(v), c, v) for (cat, c), v in errors.items() if cat == category), reverse=True)
        if codes:
            lines += ['', f'## {category} by diagnostic code', '', '| code | shaders | example message | examples |', '|---|---:|---|---|']
            for n, code, rs in codes:
                example = next((m for m in rs[0]['messages'] if m.endswith(f'[{code}]')), '').replace('|', '\\|')[:160]
                lines.append(f"| {code} | {n} | {example} | {', '.join(r['id'] for r in rs[:4])} |")
    if by['skia-reject']:
        kinds = collections.Counter(re.sub(r'\d+', 'N', (r.get('skia') or '').splitlines()[0] if r.get('skia') else '')[:120]
                                    for r in by['skia-reject'])
        lines += ['', '## skia-reject by first Skia error', '', '| error | shaders |', '|---|---:|']
        lines += [f"| {k.replace('|', '/')} | {n} |" for k, n in kinds.most_common()]
    if by['render-mismatch']:
        lines += ['', '## render-mismatch (worst first)', '', '| shader | pixels off | largest difference | warnings |', '|---|---:|---:|---|']
        for r in sorted(by['render-mismatch'], key=lambda r: -r['mismatch'])[:60]:
            lines.append(f"| {r['id']} | {100 * r['mismatch']:.1f}% | {r['peak']:.2f} | {', '.join(r['warnings'])} |")
    text = '\n'.join(lines) + '\n'
    (out / 'summary.md').write_text(text, encoding='utf-8')
    return text


def main():
    if sys.argv[1:2] == ['--cpu-render']:  # child process of draw_cpu
        return cpu_render_job(sys.argv[2])
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--transpiler', required=True, help='path to the glsl-to-sksl executable')
    parser.add_argument('--glslang', help='path to glslangValidator, to tell valid GLSL from invalid')
    parser.add_argument('--shaders', default='build/online-shaders', help='folder written by fetch_online_shaders.py')
    parser.add_argument('--out', default='build/online-results', help='report folder (replaced on each run)')
    parser.add_argument('--render', action='store_true', help='compare GPU renderings of the GLSL and the SkSL')
    parser.add_argument('--only', help='comma-separated shader ids, or a substring of them, to check')
    parser.add_argument('--limit', type=int, help='check at most this many shaders')
    parser.add_argument('--jobs', type=int, default=8, help='parallel conversions')
    args = parser.parse_args()
    import skia

    args.transpiler = str(Path(args.transpiler).resolve())
    args.glslang = args.glslang and str(Path(args.glslang).resolve())
    root = Path(args.shaders)
    index = json.loads((root / 'index.json').read_text(encoding='utf-8'))
    if args.only:
        wanted = args.only.split(',')
        index = [e for e in index if e['id'] in wanted or any(w in e['id'] for w in wanted)]
    index = index[:args.limit] if args.limit else index
    out = Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    renderer = Renderer(skia) if args.render else None
    results = []
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        futures = [pool.submit(convert, e, root, args.transpiler, args.glslang) for e in index]
        for n, future in enumerate(futures, 1):
            result = future.result()
            pictures = {}
            if result['category'] == 'compile':
                check_target(skia, renderer, result, pictures)
            if result['category'] != 'ok' and result['category'] != 'invalid-source':
                write_artifacts(skia, out, result, pictures.get(result['id']))
            results.append(result)
            if n % 100 == 0 or n == len(futures):
                counts = collections.Counter(r['category'] for r in results)
                print(f'{n}/{len(futures)} ' + ' '.join(f'{k}={v}' for k, v in sorted(counts.items())), file=sys.stderr, flush=True)
    for r in results:
        r.pop('glsl', None)
        r.pop('sksl', None)
    (out / 'report.json').write_text(json.dumps(results, indent=1), encoding='utf-8')
    print(summary(results, renderer.renderer if renderer else '', out))


if __name__ == '__main__':
    main()
