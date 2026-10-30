"""Downloads single-pass fragment shaders from public collections into a local cache for
check_online_shaders.py. The shaders keep their authors' licenses; the cache is for local
testing only and is not part of the repository.

Sources:
  shadereval  Shadertoy image shaders with permissive licenses (Hugging Face Vipitis/Shadereval-inputs)
  shadertoy   a sample of a 45000-shader Shadertoy dump (Hugging Face seanmemery/shader_dataset)
  bookofshaders  the GLSL ES 1.00 examples of The Book of Shaders (GitHub)

Run: python tests/fetch_online_shaders.py [--out build/online-shaders] [--limit 3000]
"""
import argparse
import hashlib
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROWS = 'https://datasets-server.huggingface.co/rows'
PAGE = 100  # most rows the rows API returns per request


def get(url, attempts=4):
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers={'User-Agent': 'glsl-sksl-transpiler-tests'})
            with urllib.request.urlopen(request, timeout=60) as response:
                return response.read()
        except (urllib.error.URLError, TimeoutError) as error:
            if attempt + 1 == attempts:
                raise
            print(f'  retry after {error}', file=sys.stderr)
            time.sleep(2 * (attempt + 1))


def rows(dataset, split, offset, length=PAGE):
    query = urllib.parse.urlencode({'dataset': dataset, 'config': 'default', 'split': split,
                                    'offset': offset, 'length': length})
    data = json.loads(get(f'{ROWS}?{query}'))
    return data['num_rows_total'], [r['row'] for r in data['rows']]


def single_pass(code):
    # Sound and cubemap passes are other pipelines; the image pass must define mainImage.
    return 'mainImage' in code and 'mainSound' not in code and 'mainCubemap' not in code


def fetch_shadereval(limit):
    total, first = rows('Vipitis/Shadereval-inputs', 'test', 0)
    found = []
    for offset in range(0, min(total, limit), PAGE):
        page = first if offset == 0 else rows('Vipitis/Shadereval-inputs', 'test', offset)[1]
        for r in page:
            if single_pass(r['image_code']):
                found.append({'id': r['id'], 'author': r['author'], 'license': r['license'],
                              'url': f"https://www.shadertoy.com/view/{r['id']}",
                              'format': 'shadertoy', 'code': r['image_code']})
    return found[:limit]


def fetch_shadertoy(limit):
    # Pages spread evenly over the dump, so the sample covers its whole range.
    total, _ = rows('seanmemery/shader_dataset', 'train', 0, 1)
    pages = max(1, -(-limit // PAGE))
    stride = max(PAGE, total // pages)
    found = []
    for offset in range(0, total, stride):
        if len(found) >= limit:
            break
        print(f'  shadertoy rows {offset}..{offset + PAGE} of {total}', file=sys.stderr)
        for i, r in enumerate(rows('seanmemery/shader_dataset', 'train', offset)[1]):
            if single_pass(r['code']):
                found.append({'id': f'st{offset + i:05d}', 'author': '', 'license': 'unknown (Shadertoy default: CC BY-NC-SA 3.0)',
                              'url': '', 'description': r['description'][:300],
                              'format': 'shadertoy', 'code': r['code']})
    return found[:limit]


def fetch_bookofshaders(limit):
    repo = 'patriciogonzalezvivo/thebookofshaders'
    tree = json.loads(get(f'https://api.github.com/repos/{repo}/git/trees/master?recursive=1'))
    paths = sorted(e['path'] for e in tree['tree'] if e['type'] == 'blob' and e['path'].endswith('.frag'))
    found = []
    for path in paths[:limit]:
        code = get(f'https://raw.githubusercontent.com/{repo}/master/{urllib.parse.quote(path)}').decode('utf-8', 'replace')
        if 'void main' not in code:
            continue
        found.append({'id': 'bos_' + path[:-5].replace('/', '_'), 'author': 'The Book of Shaders contributors',
                      'license': 'see repository', 'url': f'https://github.com/{repo}/blob/master/{path}',
                      'format': 'glsl', 'code': code})
    return found


SOURCES = {'shadereval': fetch_shadereval, 'shadertoy': fetch_shadertoy, 'bookofshaders': fetch_bookofshaders}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', default='build/online-shaders', help='cache folder (default build/online-shaders)')
    parser.add_argument('--limit', type=int, default=3000, help='most shaders taken from each source')
    parser.add_argument('--sources', default=','.join(SOURCES), help='comma-separated sources: ' + ', '.join(SOURCES))
    args = parser.parse_args()
    out = Path(args.out)
    names = args.sources.split(',')
    # Sources not fetched this time keep their entries.
    previous = out / 'index.json'
    index = [e for e in json.loads(previous.read_text(encoding='utf-8')) if e['source'] not in names] if previous.is_file() else []
    seen = set()
    for name in names:
        print(f'fetching {name}', file=sys.stderr)
        try:
            shaders = SOURCES[name](args.limit)
        except Exception as error:  # one unreachable source does not stop the others
            print(f'  {name} failed: {error}', file=sys.stderr)
            continue
        folder = out / name
        folder.mkdir(parents=True, exist_ok=True)
        kept = 0
        for shader in shaders:
            digest = hashlib.sha1(' '.join(shader['code'].split()).encode()).hexdigest()
            if digest in seen:
                continue
            seen.add(digest)
            file = folder / (shader['id'] + '.glsl')
            file.write_text(shader.pop('code'), encoding='utf-8')
            index.append(dict(shader, source=name, file=str(file.relative_to(out)).replace('\\', '/')))
            kept += 1
        print(f'  {kept} shaders', file=sys.stderr)
    (out / 'index.json').write_text(json.dumps(index, indent=1), encoding='utf-8')
    print(f'{len(index)} shaders in {out / "index.json"}')


if __name__ == '__main__':
    main()
