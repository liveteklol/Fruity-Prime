"""Test-only fragment bodies through the actual launcher paths on both APIs.

Run sequentially in an idle workspace. Original source bytes and a production
binary are restored in finally, including after a failed diagnostic. No runtime
debug branch, vendor correction, or diagnostic uniform enters production.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/MphRead.Native/Shaders.cpp'
PATTERN = re.compile(r'(const std::string Shaders::BackdropFragmentShader = R"shader\()(.*?)(\)shader";)', re.S)
CELLS = '''    vec2 cells = vec2(clamp(view_width / 6.0, 1.0, 320.0), clamp(view_height / 6.0, 1.0, 320.0));
    vec2 uv = noisecoord * cells * 0.055;
'''
LEGACY = '''float legacy_hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}
'''


def run(command, log):
    with log.open('wb') as stream:
        subprocess.run(command, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--modes', nargs='+', choices=['coord', 'cell', 'legacy', 'lattice', 'value', 'final'],
                        default=['coord', 'cell', 'legacy', 'lattice', 'value', 'final'])
    args = parser.parse_args()
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    original = SOURCE.read_bytes()
    text = original.decode('utf-8')
    match = PATTERN.search(text)
    if not match:
        raise ValueError('Backdrop shader body missing')
    prefix = match[2].split('void main()')[0]
    # GLSL declarations are kept intact so the production ABI generator checks
    # exactly the same descriptor/constant transport for every test body.
    bodies = {
        'coord': '    gl_FragColor = vec4(noisecoord, 0.0, 1.0);\n',
        'cell': CELLS + '    gl_FragColor = vec4(fract(uv), 0.0, 1.0);\n',
        'legacy': CELLS + '    gl_FragColor = vec4(vec3(legacy_hash(floor(uv))), 1.0);\n',
        'lattice': CELLS + '    gl_FragColor = vec4(vec3(lattice_noise(floor(uv))), 1.0);\n',
        'value': CELLS + '    gl_FragColor = vec4(vec3(value_noise(uv)), 1.0);\n',
    }
    summary = {}
    # MSVC's wrapper recreates its compiler environment and uses existing cache.
    if os.name != 'nt':
        raise RuntimeError('This runner currently uses the Windows MSVC build wrapper.')
    build = ['cmd', '/c', str(ROOT / 'tools/build/build-cpp.bat'), 'msvc', 'Release']
    try:
        for mode in args.modes:
            print(f'[backdrop diagnostic] {mode}', flush=True)
            if mode == 'final':
                SOURCE.write_bytes(original)
            else:
                body = prefix + (LEGACY if mode == 'legacy' else '') + 'void main()\n{\n' + bodies[mode] + '}\n'
                replacement = match[1] + body + match[3]
                SOURCE.write_bytes((text[:match.start()] + replacement + text[match.end():]).encode('utf-8'))
            run(build, output / f'{mode}-build.log')
            captures = output / mode
            run([str(ROOT / 'tools/build/out/msvc-Release/FruityPrime.exe'), '-backdropparity', str(captures), '-noupdate']
                + ([] if mode == 'final' else ['-backdropobserve']), output / f'{mode}-run.log')
            with (captures / 'metrics.csv').open() as stream:
                rows = list(csv.DictReader(stream))
            worst = max(int(row['max_channel_difference']) for row in rows)
            mean = max(float(row['mean_channel_difference']) for row in rows)
            summary[mode] = dict(max_channel_difference=worst, max_mean_channel_difference=mean, captures=len(rows))
            (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
            print(summary[mode], flush=True)
            if mode != 'legacy' and (worst > 2 or mean > 0.1):
                raise RuntimeError(f'{mode} parity failed')
    finally:
        SOURCE.write_bytes(original)
        run(build, output / 'production-restore-build.log')


if __name__ == '__main__':
    main()
